/* samsung_drm_buf.c
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

static DEFINE_MUTEX(samsung_drm_buf_lock);

static int lowlevel_buffer_allocate(struct drm_device *dev,
		struct samsung_drm_buf_entry *entry)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	entry->vaddr = dma_alloc_writecombine(dev->dev, entry->size,
			(dma_addr_t *)&entry->paddr, GFP_KERNEL);
	if (!entry->paddr) {
		DRM_ERROR("failed to allocate buffer.\n");
		return -ENOMEM;
	}

	DRM_DEBUG_KMS("allocated : vaddr(0x%x), paddr(0x%x), size(0x%x)\n",
			(unsigned int)entry->vaddr, entry->paddr, entry->size);

	return 0;
}

static void lowlevel_buffer_deallocate(struct drm_device *dev,
		struct samsung_drm_buf_entry *entry)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	dma_free_writecombine(dev->dev, entry->size, entry->vaddr,
			entry->paddr);

	DRM_DEBUG_KMS("deallocated : vaddr(0x%x), paddr(0x%x), size(0x%x)\n",
			(unsigned int)entry->vaddr, entry->paddr, entry->size);
}

static void  samsung_drm_buf_del(struct drm_device *dev,
		struct samsung_drm_gem_obj *obj)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	lowlevel_buffer_deallocate(dev, obj->entry);

	kfree(obj->entry);

	kfree(obj);
}

struct samsung_drm_gem_obj *samsung_drm_buf_new(struct drm_device *dev,
		unsigned int size)
{
	struct samsung_drm_gem_obj *obj;
	struct samsung_drm_buf_entry *entry;
	int ret;

	DRM_DEBUG_KMS("%s.\n", __FILE__);
	DRM_DEBUG_KMS("desired size = 0x%x\n", size);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		DRM_ERROR("failed to allocate samsung_drm_gem_obj.\n");
		return NULL;
	}

	/* use only one memory plane yet. */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		DRM_ERROR("failed to allocate samsung_drm_buf_entry.\n");
		return NULL;
	}

	entry->size = size;

	/* allocate memory region and set it to vaddr and paddr. */
	ret = lowlevel_buffer_allocate(dev, entry);
	if (ret < 0)
		return NULL;

	obj->entry = entry;

	return obj;
}

struct samsung_drm_gem_obj *samsung_drm_buf_create(struct drm_device *dev,
		unsigned int size)
{
	struct samsung_drm_gem_obj *obj;

	DRM_DEBUG_KMS("%s.\n", __FILE__);

	obj = samsung_drm_buf_new(dev, size);
	if (!obj)
		return NULL;

	DRM_DEBUG_KMS("buffer id : 0x%x\n", obj->id);

	return obj;
}

int samsung_drm_buf_destroy(struct drm_device *dev,
		struct samsung_drm_gem_obj *in_obj)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	samsung_drm_buf_del(dev, in_obj);

	return 0;
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Buffer Management Module");
MODULE_LICENSE("GPL");
