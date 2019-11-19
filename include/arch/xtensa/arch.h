/*
 * Copyright (c) 2016 Cadence Design Systems, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Xtensa specific kernel interface header
 * This header contains the Xtensa specific kernel interface.  It is included
 * by the generic kernel interface header (include/arch/cpu.h)
 */

#ifndef ZEPHYR_INCLUDE_ARCH_XTENSA_ARCH_H_
#define ZEPHYR_INCLUDE_ARCH_XTENSA_ARCH_H_

#include <irq.h>

#include <devicetree.h>
#if !defined(_ASMLANGUAGE) && !defined(__ASSEMBLER__)
#include <zephyr/types.h>
#include <toolchain.h>
#include <arch/common/sys_io.h>
#include <arch/common/ffs.h>
#include <sw_isr_table.h>
#include <arch/xtensa/thread.h>
#include <arch/xtensa/irq.h>
#include <xtensa/config/core.h>
#include <arch/common/addr_types.h>

#define STACK_ALIGN 16

/* Xtensa GPRs are often designated by two different names */
#define sys_define_gpr_with_alias(name1, name2) union { u32_t name1, name2; }

#include <arch/xtensa/exc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* internal routine documented in C file, needed by IRQ_CONNECT() macro */
extern void z_irq_priority_set(u32_t irq, u32_t prio, u32_t flags);

#define ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
({ \
	Z_ISR_DECLARE(irq_p, flags_p, isr_p, isr_param_p); \
	irq_p; \
})

/* Spurious interrupt handler. Throws an error if called */
extern void z_irq_spurious(void *unused);

#define XTENSA_ERR_NORET

extern u32_t z_timer_cycle_get_32(void);

static inline u32_t arch_k_cycle_get_32(void)
{
	return z_timer_cycle_get_32();
}

static ALWAYS_INLINE void arch_nop(void)
{
	__asm__ volatile("nop");
}

#ifdef __cplusplus
}
#endif

#endif /* !defined(_ASMLANGUAGE) && !defined(__ASSEMBLER__)  */

#endif /* ZEPHYR_INCLUDE_ARCH_XTENSA_ARCH_H_ */
