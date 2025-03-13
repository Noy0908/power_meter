/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __NRF_DFU_FLASH_H_
#define __NRF_DFU_FLASH_H_

// #include <zephyr/types.h>
#define FLASH_PAGE_SIZE					0x1000
#define IMAGE0_ID						PM_MCUBOOT_PRIMARY_ID
#define IMAGE0_ADDRESS					PM_MCUBOOT_PRIMARY_ADDRESS
#define IMAGE1_ID						PM_MCUBOOT_SECONDARY_ID
#define IMAGE1_ADDRESS					PM_MCUBOOT_SECONDARY_ADDRESS


extern bool dfu_lock(const void *module_id);

extern void dfu_unlock(const void *module_id);

extern int dfu_data_store(const void *src, size_t len, bool flush);

extern int dfu_page_erase(int off, size_t len);

extern int dfu_flash_start(uint32_t image_start);

extern void dfu_flash_finish(void);

#endif /* _NRF_DFU_FLASH_H_ */
