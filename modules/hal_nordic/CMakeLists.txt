# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_NRF_802154_SOURCE_HAL_NORDIC ${ZEPHYR_CURRENT_MODULE_DIR}/drivers/nrf_802154/zephyr nrf_802154)
add_subdirectory_ifdef(CONFIG_HAS_NRFX nrfx)
