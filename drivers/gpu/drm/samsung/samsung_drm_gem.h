/* samsung_drm_gem.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authoer: Inki Dae <inki.dae@samsung.com>
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

#ifndef _SAMSUNG_DRM_GEM_H_
#define _SAMSUNG_DRM_GEM_H_

#define to_samsung_gem_obj(x)	container_of(x,\
			struct samsung_drm_gem_obj, base)

/* create a new mm object and get a handle to it. */
int samsung_drm_gem_create_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file);

/* get buffer offset to map to user space. */
int samsung_drm_gem_map_offset_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file);

/* unmap a buffer from user space. */
int samsung_drm_gem_munmap_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file);

/* initialize gem object. */
int samsung_drm_gem_init_object(struct drm_gem_object *obj);

/* free gem object. */
void samsung_drm_gem_free_object(struct drm_gem_object *gem_obj);

struct samsung_drm_gem_obj *
		find_samsung_drm_gem_object(struct drm_file *file_priv,
			struct drm_device *dev, unsigned int handle);

/* create memory region for drm framebuffer. */
int samsung_drm_gem_dumb_create(struct drm_file *file_priv,
		struct drm_device *dev, struct drm_mode_create_dumb *args);

/* map memory region for drm framebuffer to user space. */
int samsung_drm_gem_dumb_map_offset(struct drm_file *file_priv,
		struct drm_device *dev, uint32_t handle, uint64_t *offset);

/* page fault handler and mmap fault address(virtual) to physical memory. */
int samsung_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

/* set vm_flags and we can change vm attribute to other here. */
int samsung_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/* destroy memory region allocated. */
int samsung_drm_gem_dumb_destroy(struct drm_file *file_priv,
		struct drm_device *dev, unsigned int handle);

#endif
