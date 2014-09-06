/* samsung_drm_fb.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
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
#include "drm_crtc.h"
#include "drm_crtc_helper.h"

#include "samsung_drm_fb.h"
#include "samsung_drm_buf.h"
#include "samsung_drm_gem.h"

#include <plat/samsung_drm.h>

#define to_samsung_drm_framebuffer(x) container_of(x,\
		struct samsung_drm_framebuffer, drm_framebuffer)

struct samsung_drm_framebuffer {
	struct drm_framebuffer drm_framebuffer;
	struct drm_file *file_priv;
	struct samsung_drm_gem_obj *samsung_gem_obj;

	/* samsung gem object handle. */
	unsigned int gem_handle;
	/* unique id to buffer object. */
	unsigned int id;

	unsigned int fb_size;
	unsigned long paddr;
	void __iomem *vaddr;
};

static void samsung_drm_fb_destroy(struct drm_framebuffer *framebuffer)
{
	struct drm_device *dev = framebuffer->dev;
	struct samsung_drm_framebuffer *samsung_fb =
			to_samsung_drm_framebuffer(framebuffer);
	struct samsung_drm_gem_obj *samsung_gem_obj;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/**
	 * revert drm framebuffer to old one and remove drm framebuffer object.
	 * - this callback would be called when uer application is released.
	 *	drm_release() -> drm_fb_release() -> fb->func->destroy()
	 */

	/**
	 * release drm_framebuffer from idr table and
	 * call crtc->funcs->set_config() callback
	 * to change current framebuffer to old one.
	 */
	drm_framebuffer_cleanup(framebuffer);

	/**
	 * find buffer object registered.
	 *
	 * if samsung_fb->gem_handle is 0, then this means
	 * that the memory region for drm framebuffer was allocated
	 * without using gem interface.
	 */
	samsung_gem_obj = find_samsung_drm_gem_object(samsung_fb->file_priv,
			dev, samsung_fb->gem_handle);
	if (!samsung_gem_obj) {
		DRM_DEBUG_KMS("this gem object has already been released.\n");

		if (samsung_fb->samsung_gem_obj && !samsung_fb->gem_handle) {
			samsung_gem_obj = samsung_fb->samsung_gem_obj;
			DRM_DEBUG_KMS("so release buffer without using gem.\n");
		} else
			goto out;
	}

	ret = drm_gem_handle_delete(samsung_fb->file_priv,
			samsung_fb->gem_handle);
	if (ret < 0) {
		DRM_ERROR("failed to delete drm_gem_handle.\n");
		goto out;
	}

	/* release framebuffer memory region. */
	ret = samsung_drm_buf_destroy(dev, samsung_gem_obj);
	if (ret < 0)
		DRM_DEBUG_KMS("failed to release this buffer.\n");

out:
	kfree(samsung_fb);
}

static int samsung_drm_fb_create_handle(struct drm_framebuffer *fb,
		struct drm_file *file_priv, unsigned int *handle)
{
	struct samsung_drm_framebuffer *samsung_fb =
			to_samsung_drm_framebuffer(fb);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/**
	 * set buffer handle of this framebuffer to *handle.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_GETFB command.
	 */

	if (!samsung_fb->gem_handle) {
		DRM_ERROR("can't get id to buffer object.\n");
		return -EINVAL;
	}

	*handle = samsung_fb->gem_handle;

	DRM_DEBUG_KMS("got buffer object id(%d)\n", *handle);

	return 0;
}

static int samsung_drm_fb_dirty(struct drm_framebuffer *framebuffer,
		     struct drm_file *file_priv, unsigned flags,
		     unsigned color, struct drm_clip_rect *clips,
		     unsigned num_clips)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/**
	 * update framebuffer and its hardware.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_DIRTYFB command.
	 *
	 * ps. Userspace can notify the driver via this callback
	 * that an area of the framebuffer has been changed then should
	 * be flushed to the display hardware.
	 */

	return 0;
}

static struct drm_framebuffer_funcs samsung_drm_fb_funcs = {
	.destroy = samsung_drm_fb_destroy,
	.create_handle = samsung_drm_fb_create_handle,
	.dirty = samsung_drm_fb_dirty,
};

struct drm_framebuffer *samsung_drm_fb_create(struct drm_device *dev,
		struct drm_file *file_priv, struct drm_mode_fb_cmd *mode_cmd)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/**
	 * create new drm framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_ADDFB command.
	 */

	return samsung_drm_fb_init(file_priv, dev, mode_cmd);
}

struct drm_framebuffer *samsung_drm_fb_init(struct drm_file *file_priv,
		struct drm_device *dev, struct drm_mode_fb_cmd *mode_cmd)
{
	struct samsung_drm_framebuffer *samsung_fb;
	struct drm_framebuffer *fb;
	struct samsung_drm_gem_obj *samsung_gem_obj;
	unsigned int size, gem_handle = 0;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mode_cmd->pitch = max(mode_cmd->pitch, mode_cmd->width *
			(mode_cmd->bpp >> 3));

	DRM_LOG_KMS("drm fb create(%dx%d)\n", mode_cmd->width,
			mode_cmd->height);

	samsung_fb = kzalloc(sizeof(*samsung_fb), GFP_KERNEL);
	if (!samsung_fb) {
		DRM_ERROR("failed to allocate samsung drm framebuffer.\n");
		return NULL;
	}

	fb = &samsung_fb->drm_framebuffer;
	ret = drm_framebuffer_init(dev, fb, &samsung_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer.\n");
		goto fail;
	}

	DRM_LOG_KMS("create: fb id: %d\n", fb->base.id);

	size = mode_cmd->pitch * mode_cmd->height;

	/**
	 * if mode_cmd->handle is NULL,
	 * it allocates framebuffer memory internally.
	 * else using allocator defined.
	 *
	 * ps. mode_cmd->handle could be pointer to a buffer allocated
	 *	by user application using KMS library.
	 */
	if (!mode_cmd->handle) {
		/**
		 * allocate framebuffer memory.
		 * - allocated memory address would be set to vaddr
		 *	and paddr of samsung_drm_framebuffer object.
		 */
		samsung_gem_obj = samsung_drm_buf_create(dev, size);
		if (!samsung_gem_obj)
			return ERR_PTR(-ENOMEM);
	} else {
		/* find buffer object registered. */
		samsung_gem_obj = find_samsung_drm_gem_object(file_priv, dev,
				mode_cmd->handle);
		if (!samsung_gem_obj)
			return ERR_PTR(-EINVAL);

		gem_handle = mode_cmd->handle;
	}

	samsung_fb->file_priv = file_priv;
	samsung_fb->samsung_gem_obj = samsung_gem_obj;
	samsung_fb->gem_handle = gem_handle;
	samsung_fb->id = samsung_gem_obj->id;
	samsung_fb->fb_size = size;
	samsung_fb->vaddr = samsung_gem_obj->entry->vaddr;
	samsung_fb->paddr = samsung_gem_obj->entry->paddr;

	DRM_DEBUG_KMS("handle = 0x%x, id = %d\n",
			samsung_gem_obj->handle, samsung_gem_obj->id);
	DRM_DEBUG_KMS("fb: size = 0x%x, vaddr = 0x%x, paddr = 0x%x\n",
		samsung_fb->fb_size, (unsigned int)samsung_fb->vaddr,
		(unsigned int)samsung_fb->paddr);

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	return fb;

fail:
	kfree(samsung_fb);

	return ERR_PTR(ret);
}

int samsung_drm_fb_update_buf_off(struct drm_framebuffer *fb,
		unsigned int x, unsigned int y,
		struct samsung_drm_buffer_info *buffer_info)
{
	unsigned int bpp = fb->bits_per_pixel >> 3;
	unsigned long offset;
	struct samsung_drm_framebuffer *samsung_fb;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_fb = to_samsung_drm_framebuffer(fb);

	offset = (x * bpp) + (y * fb->pitch);

	DRM_DEBUG_KMS("offset(0x%x) = (x(%d) * bpp(%d) + (y(%d) * pitch(%d)\n",
			(unsigned int)offset, x, bpp, y, fb->pitch);

	buffer_info->vaddr = samsung_fb->vaddr + offset;
	buffer_info->paddr = samsung_fb->paddr + offset;

	DRM_DEBUG_KMS("updated vaddr = 0x%x, paddr = 0x%x\n",
			(unsigned int)buffer_info->vaddr,
			(unsigned int)buffer_info->paddr);

	return 0;
}
