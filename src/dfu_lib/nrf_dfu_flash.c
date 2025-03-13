/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

// #include <zephyr.h>
// #include <inttypes.h>
// #include <zephyr/types.h>
// #include <sys/byteorder.h>
// #include <storage/flash_map.h>
// #include <logging/log.h>
// #include "nrf_dfu_flash.h"
// #include <drivers/flash.h>
// #include <storage/stream_flash.h>
// #ifndef CONFIG_SECURE_BOOT
// #include <dfu/flash_img.h>
// #endif
// #include "nrf_dfu_settings.h"
#include <stdint.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/logging/log.h>

#include "nrf_dfu_flash.h"


LOG_MODULE_REGISTER(nrf_dfu_flash, CONFIG_TCP_LOG_LEVEL);


#define DFU_UNLOCKED					0


static atomic_t dfu_locked = ATOMIC_INIT(DFU_UNLOCKED);

const void *const dfu_flash_module;


bool dfu_lock(const void *module_id)
{
	return atomic_cas(&dfu_locked, DFU_UNLOCKED, (atomic_val_t)module_id);
}

void dfu_unlock(const void *module_id)
{
	bool success = atomic_cas(&dfu_locked, (atomic_val_t)module_id,
				  DFU_UNLOCKED);

	/* Module that have not locked dfu, should not try to unlock it. */
	__ASSERT_NO_MSG(success);
	ARG_UNUSED(success);
}

#if (CONFIG_HEAP_MEM_POOL_SIZE > 0)
	static struct flash_img_context *ctx = NULL;
#else
	static struct flash_img_context ctx_data;
#define ctx (&ctx_data)
#endif


int dfu_flash_start(uint32_t image_start)
{
	int rc = 0;

	if (!dfu_lock(dfu_flash_module)) {
		LOG_WRN("DFU already started by another module");
		return 0;
	}		
#if (CONFIG_HEAP_MEM_POOL_SIZE > 0)
	if (ctx == NULL) {
		ctx = k_malloc(sizeof(*ctx));
		if (ctx == NULL) {
			return -EFAULT;
		}
	}
#endif
	rc = flash_img_init(ctx);
	if (rc)
	{
#if (CONFIG_HEAP_MEM_POOL_SIZE > 0)
		k_free(ctx);
		ctx = NULL;
#endif		
		LOG_ERR("flash_img_init err %d", rc);		
	}
	else
	{
		ctx->stream.offset = PM_MCUBOOT_SECONDARY_ADDRESS + image_start;
	}

	boot_write_img_confirmed();
	
	rc = boot_erase_img_bank(IMAGE1_ID);
	if(rc)
	{
		LOG_ERR("erase Secondary slot failed!");
	}

	return rc;
}


void dfu_flash_finish(void)
{	
	int err = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
	if (err) {
		LOG_ERR("Cannot request the image upgrade (err:%d)", err);
	}

	dfu_unlock(dfu_flash_module);

#if (CONFIG_HEAP_MEM_POOL_SIZE > 0)	
	k_free(ctx);
	ctx = NULL;	
#endif
	LOG_INF("image trailer written");
}


int dfu_data_store(const void *src, size_t len, bool flush)
{
	int rc;
		/* Cast away const. */
	rc = flash_img_buffered_write(ctx, (void *)src, len, flush);

	return rc;
}


int dfu_page_erase(int off, size_t len)
{
	return 0;
}