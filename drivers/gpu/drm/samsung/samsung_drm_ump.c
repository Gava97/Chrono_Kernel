/* samsung_drm_ump.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"

#include <plat/samsung_drm.h>

#include "samsung_drm_buf.h"
#include "ump_kernel_interface_ref_drv.h"

static unsigned int samsung_drm_ump_get_handle(unsigned int id)
{
	return (unsigned int)ump_dd_handle_get((ump_secure_id)id);
}

static int samsung_drm_ump_add_buffer(unsigned int paddr, unsigned int size,
		unsigned int *handle, unsigned int *id)
{
	ump_dd_physical_block ump_mem_desc;

	ump_mem_desc.addr = paddr;
	ump_mem_desc.size = size;

	*handle = (unsigned int)
		ump_dd_handle_create_from_phys_blocks(&ump_mem_desc, 1);
	if (!(*handle)) {
		DRM_ERROR("failed to create handle for physical block.\n");
		return -EINVAL;
	}

	*id = ump_dd_secure_id_get((ump_dd_handle)*handle);

	DRM_DEBUG_KMS("ump handle : 0x%x, secure id = %d\n", *handle, *id);

	return 0;
}

static unsigned int samsung_drm_ump_get_phy(unsigned int handle)
{
	int ret = 1;
	ump_dd_physical_block block;

	ret = ump_dd_phys_block_get((ump_dd_handle)handle, 0, &block);
	if (ret) {
		DRM_ERROR("failed to get physical block from ump.\n");
		return 0;
	}

	return block.addr;
}

static void samsung_drm_ump_release_buffer(unsigned int handle)
{
	ump_dd_reference_release((ump_dd_handle)handle);
}

static struct samsung_drm_buf_callback ump_callback = {
	.get_handle = samsung_drm_ump_get_handle,
	.add_buffer = samsung_drm_ump_add_buffer,
	.get_phy = samsung_drm_ump_get_phy,
	.release_buffer = samsung_drm_ump_release_buffer,
};

static int samsung_drm_ump_init(void)
{
	printk(KERN_INFO "%s.\n", __func__);

	samsung_drm_buf_register(&ump_callback);

	return 0;
}

static void samsung_drm_ump_exit(void)
{
}

subsys_initcall(samsung_drm_ump_init);
module_exit(samsung_drm_ump_exit);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM UMP Backend Module");
MODULE_LICENSE("GPL");
