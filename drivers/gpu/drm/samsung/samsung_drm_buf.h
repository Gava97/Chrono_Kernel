/* samsung_drm_buf.h
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

#ifndef _SAMSUNG_DRM_BUF_H_
#define _SAMSUNG_DRM_BUF_H_

/**
 * samsung drm buffer entry structure.
 *
 * @paddr: physical address of allocated memory.
 * @vaddr: kernel virtual address of allocated memory.
 * @size: size of allocated memory.
 */
struct samsung_drm_buf_entry {
	unsigned int paddr;
	void __iomem *vaddr;
	unsigned int size;
};

/**
 * samsung drm buffer structure.
 *
 * @entry: pointer to samsung drm buffer entry object.
 * @flags: it means memory type to be alloated or cache attributes.
 * @handle: pointer to specific buffer object.
 * @id: unique id to specific buffer object.
 *
 * ps. this object would be transfered to user as kms_bo.handle so
 *	user can access to memory through kms_bo.handle.
 */
struct samsung_drm_gem_obj {
	struct drm_gem_object base;
	struct samsung_drm_buf_entry *entry;
	unsigned int flags;

	unsigned int handle;
	unsigned int id;
};

/* create new buffer object and memory region and add the object to list. */
struct samsung_drm_gem_obj *samsung_drm_buf_new(struct drm_device *dev,
		unsigned int size);

/* allocate physical memory and add its object to list. */
struct samsung_drm_gem_obj *samsung_drm_buf_create(struct drm_device *dev,
		unsigned int size);

/* remove allocated physical memory. */
int samsung_drm_buf_destroy(struct drm_device *dev,
		struct samsung_drm_gem_obj *in_obj);

/* find object added to list. */
struct samsung_drm_gem_obj *samsung_drm_buffer_find(struct drm_device *dev,
		struct samsung_drm_gem_obj *in_obj, unsigned int paddr);

#endif
