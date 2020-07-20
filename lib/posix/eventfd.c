/*
 * Copyright (c) 2020 Tobias Svehagen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <posix/sys/eventfd.h>
#include <net/socket.h>

struct eventfd {
	struct k_sem read_sem;
	struct k_sem write_sem;
	struct k_mutex cnt_mtx;
	eventfd_t cnt;
	int flags;
};

K_MUTEX_DEFINE(eventfd_mtx);
static struct eventfd efds[CONFIG_EVENTFD_MAX];

static int eventfd_poll_prepare(struct eventfd *efd,
				struct zsock_pollfd *pfd,
				struct k_poll_event **pev,
				struct k_poll_event *pev_end)
{
	ARG_UNUSED(efd);

	if (pfd->events & ZSOCK_POLLIN) {
		if (*pev == pev_end) {
			errno = ENOMEM;
			return -1;
		}

		(*pev)->obj = &efd->read_sem;
		(*pev)->type = K_POLL_TYPE_SEM_AVAILABLE;
		(*pev)->mode = K_POLL_MODE_NOTIFY_ONLY;
		(*pev)->state = K_POLL_STATE_NOT_READY;
		(*pev)++;
	}

	return 0;
}

static int eventfd_poll_update(struct eventfd *efd,
			       struct zsock_pollfd *pfd,
			       struct k_poll_event **pev)
{
	ARG_UNUSED(efd);

	if (pfd->events & ZSOCK_POLLOUT) {
		pfd->revents |= ZSOCK_POLLOUT;
	}

	if (pfd->events & ZSOCK_POLLIN) {
		if ((*pev)->state != K_POLL_STATE_NOT_READY) {
			pfd->revents |= ZSOCK_POLLIN;
		}
		(*pev)++;
	}

	return 0;
}

static ssize_t eventfd_read_op(void *obj, void *buf, size_t sz)
{
	struct eventfd *efd = obj;
	bool nonblock = efd->flags & EFD_NONBLOCK;
	int result = 0;
	bool givesem = false;
	eventfd_t count;
	bool repeat;

	if (sz < sizeof(eventfd_t)) {
		errno = EINVAL;
		return -1;
	}

	/* Reset the read_sem unconditionally. */
	k_sem_take(&efd->read_sem, K_NO_WAIT);

	do {
		repeat = false;
		k_mutex_lock(&efd->cnt_mtx, K_FOREVER);
		do {
			if (nonblock && efd->cnt == 0) {
				result = EAGAIN;
				break;
			}
			else if (efd->cnt == 0) {
				repeat = true;
				break;
			}
			count = (efd->flags & EFD_SEMAPHORE) ? 1 : efd->cnt;
			efd->cnt -= count;
			if (efd->cnt != 0) {
				k_sem_give(&efd->read_sem);
			}
		} while (0);
		k_mutex_unlock(&efd->cnt_mtx);
		if (repeat) {
			k_sem_take(&efd->read_sem, K_FOREVER);
			continue;
		}
	} while (repeat);

	/* Give the semaphores outside of the mutex so we don't reschedule more than we have to. */
	if (result == 0) {
		k_sem_give(&efd->write_sem);
	}


	if (result != 0) {
		errno = result;
		return -1;
	}

#if defined(__GNUC__)
/*
 * GCC cannot determine, if count variable is initialized or not during
 * assignment with buf. However since we know it is in fact initialized,
 * we can ignore this warning.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

	*(eventfd_t *)buf = count;

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	return sizeof(eventfd_t);
}


static ssize_t eventfd_write_op(void *obj, const void *buf, size_t sz)
{
	struct eventfd *efd = obj;
	bool nonblock = efd->flags & EFD_NONBLOCK;
	int result = 0;
	eventfd_t count;
	bool overflow;
	bool repeat;

	if (sz < sizeof(eventfd_t)) {
		errno = EINVAL;
		return -1;
	}

	count = *((eventfd_t *)buf);

	if (count == UINT64_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (count == 0) {
		return sizeof(eventfd_t);
	}

	do {
		repeat = false;
		k_mutex_lock(&efd->cnt_mtx, K_FOREVER);
		do {
			overflow = UINT64_MAX - count <= efd->cnt;
			if (nonblock && overflow) {
				result = EAGAIN;
				break;
			}
			else if (overflow) {
				repeat = true;
				break;
			}
			efd->cnt += count;
		} while (0);
		k_mutex_unlock(&efd->cnt_mtx);
		if (repeat) {
			k_sem_take(&efd->write_sem, K_FOREVER);
			continue;
		}
		k_sem_give(&efd->read_sem);
	} while (repeat);

	if (result != 0) {
		errno = result;
		return -1;
	}

	return sizeof(eventfd_t);
}

static int eventfd_ioctl_op(void *obj, unsigned int request, va_list args)
{
	struct eventfd *efd = (struct eventfd *)obj;

	switch (request) {
	case F_GETFL:
		return efd->flags & EFD_FLAGS_SET;

	case F_SETFL: {
		int flags;

		flags = va_arg(args, int);

		if (flags & ~EFD_FLAGS_SET) {
			errno = EINVAL;
			return -1;
		}

		efd->flags = flags;

		return 0;
	}

	case ZFD_IOCTL_CLOSE:
		efd->flags = 0;
		return 0;

	case ZFD_IOCTL_POLL_PREPARE: {
		struct zsock_pollfd *pfd;
		struct k_poll_event **pev;
		struct k_poll_event *pev_end;

		pfd = va_arg(args, struct zsock_pollfd *);
		pev = va_arg(args, struct k_poll_event **);
		pev_end = va_arg(args, struct k_poll_event *);

		return eventfd_poll_prepare(obj, pfd, pev, pev_end);
	}

	case ZFD_IOCTL_POLL_UPDATE: {
		struct zsock_pollfd *pfd;
		struct k_poll_event **pev;

		pfd = va_arg(args, struct zsock_pollfd *);
		pev = va_arg(args, struct k_poll_event **);

		return eventfd_poll_update(obj, pfd, pev);
	}

	default:
		errno = EOPNOTSUPP;
		return -1;
	}
}

static const struct fd_op_vtable eventfd_fd_vtable = {
	.read = eventfd_read_op,
	.write = eventfd_write_op,
	.ioctl = eventfd_ioctl_op,
};

int eventfd(unsigned int initval, int flags)
{
	struct eventfd *efd = NULL;
	int fd = -1;
	int seminit;
	int i;

	if (flags & ~EFD_FLAGS_SET) {
		errno = EINVAL;
		return -1;
	}

	k_mutex_lock(&eventfd_mtx, K_FOREVER);

	for (i = 0; i < ARRAY_SIZE(efds); ++i) {
		if (!(efds[i].flags & EFD_IN_USE)) {
			efd = &efds[i];
			break;
		}
	}

	if (efd == NULL) {
		errno = ENOMEM;
		goto exit_mtx;
	}

	fd = z_reserve_fd();
	if (fd < 0) {
		goto exit_mtx;
	}

	efd->flags = EFD_IN_USE | flags;
	efd->cnt = initval;
	seminit = efd->cnt != 0;

	k_sem_init(&efd->read_sem, seminit, 1);
	k_sem_init(&efd->write_sem, 0, 1);
	k_mutex_init(&efd->cnt_mtx);

	z_finalize_fd(fd, efd, &eventfd_fd_vtable);

exit_mtx:
	k_mutex_unlock(&eventfd_mtx);
	return fd;
}
