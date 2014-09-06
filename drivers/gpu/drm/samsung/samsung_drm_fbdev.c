/* samsung_drm_fbdev.c
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
#include "drm_fb_helper.h"
#include "drm_crtc_helper.h"
#include "samsung_drm_fb.h"

#include <drm/samsung_drm.h>

#define to_samsung_fbdev_by_helper(x) container_of(x, struct samsung_drm_fbdev,\
		drm_fb_helper)

struct samsung_drm_fbdev {
	struct drm_fb_helper drm_fb_helper;
	struct drm_framebuffer *fb;
};

static inline unsigned int chan_to_field(unsigned int chan,
		struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;

	return chan << bf->offset;
}

static int samsung_drm_fbdev_cursor(struct fb_info *info,
		struct fb_cursor *cursor)
{
	return 0;
}

static int samsung_drm_fbdev_setcolreg(unsigned regno, unsigned red,
		unsigned green, unsigned blue, unsigned transp,
		struct fb_info *info)
{
	unsigned int val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val = chan_to_field(red, &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue, &info->var.blue);

			pal[regno] = val;
		}
		break;
	default:
		return 1;
	}

	return 0;
}

/**
 * define linux framebuffer callbacks.
 * - this callback would be used at booting time.
 */
static struct fb_ops samsung_drm_fb_ops = {
	.owner = THIS_MODULE,

	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,

	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_cursor = samsung_drm_fbdev_cursor,
	.fb_setcolreg = samsung_drm_fbdev_setcolreg,
	.fb_blank = drm_fb_helper_blank,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_setcmap = drm_fb_helper_setcmap,
};

/* update fb_info. */
static int samsung_drm_fbdev_update(struct drm_fb_helper *helper,
		struct drm_framebuffer *fb)
{
	struct fb_info *fbi = helper->fbdev;
	struct drm_device *dev = helper->dev;
	struct samsung_drm_fbdev *samsung_fb =
		to_samsung_fbdev_by_helper(helper);
	struct samsung_drm_buffer_info buffer_info;
	unsigned int size = fb->width * fb->height * (fb->bits_per_pixel >> 3);
	int ret = -1;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_fb->fb = fb;

	drm_fb_helper_fill_fix(fbi, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb->width, fb->height);

	ret = samsung_drm_fb_update_buf_off(fb, fbi->var.xoffset,
			fbi->var.yoffset, &buffer_info);
	if (ret < 0) {
		DRM_ERROR("failed to update framebuffer offset.\n");
		return -EINVAL;
	}

	dev->mode_config.fb_base = buffer_info.paddr;

	fbi->screen_base = buffer_info.vaddr;
	fbi->screen_size = size;
	fbi->fix.smem_start = buffer_info.paddr;
	fbi->fix.smem_len = size;

	return 0;
}

static int samsung_drm_fbdev_create(struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct samsung_drm_fbdev *samsung_fbdev =
			to_samsung_fbdev_by_helper(helper);
	struct drm_device *dev = helper->dev;
	struct fb_info *fbi;
	struct drm_mode_fb_cmd mode_cmd = {0};
	struct platform_device *pdev = dev->platformdev;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	DRM_LOG_KMS("surface width(%d), height(%d) and bpp(%d\n",
			sizes->surface_width, sizes->surface_height,
			sizes->surface_bpp);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.bpp = sizes->surface_bpp;
	mode_cmd.depth = sizes->surface_depth;

	mutex_lock(&dev->struct_mutex);

	fbi = framebuffer_alloc(0, &pdev->dev);
	if (!fbi) {
		DRM_ERROR("failed to allocate fb info.\n");
		ret = -ENOMEM;
		goto fail;
	}

	samsung_fbdev->fb = samsung_drm_fb_init(NULL, dev, &mode_cmd);
	if (!samsung_fbdev->fb) {
		DRM_ERROR("failed to allocate fb.\n");
		ret = -ENOMEM;
		goto fail;
	}

	helper->fb = samsung_fbdev->fb;
	helper->fbdev = fbi;

	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &samsung_drm_fb_ops;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("failed to allocate cmap.\n");
		ret = -ENOMEM;
		goto fail;
	}

	/* update fb. */
	samsung_drm_fbdev_update(helper, helper->fb);

	mutex_unlock(&dev->struct_mutex);
	return 0;
fail:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int samsung_drm_fbdev_probe(struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	int ret = -1;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!helper->fb) {
		ret = samsung_drm_fbdev_create(helper, sizes);
		if (ret < 0) {
			DRM_ERROR("failed to create fbdev.\n");
			return -ENOMEM;
		}

		ret = 1;
	}

	return ret;
}

static struct drm_fb_helper_funcs samsung_drm_fb_helper_funcs = {
	.gamma_set = NULL,
	.gamma_get = NULL,
	.fb_probe = samsung_drm_fbdev_probe,
};

/* initialize drm fbdev helper. */
int samsung_drm_fbdev_init(struct drm_device *dev)
{
	struct samsung_drm_fbdev *fbdev;
	struct samsung_drm_private *private = dev->dev_private;
	struct drm_fb_helper *helper;
	unsigned int num_crtc = 0;
	int ret = -1;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		DRM_ERROR("failed to allocate drm fbdev.\n");
		return -ENOMEM;
	}

	private->fbdev = fbdev;

	helper = &fbdev->drm_fb_helper;
	helper->funcs = &samsung_drm_fb_helper_funcs;

	/* get crtc count. */
	num_crtc = dev->mode_config.num_crtc;

	ret = drm_fb_helper_init(dev, helper, num_crtc, 4);
	if (ret < 0) {
		DRM_ERROR("failed to initialize drm fb helper.\n");
		goto fail;
	}

	/**
	 * all the drm connector objects registered to connector_list
	 * at previous process would be registered to
	 * drm_fb_helper->connector_info[n].
	 */
	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret < 0) {
		DRM_ERROR("failed to register drm_fb_helper_connector.\n");
		goto fail;

	}

	/**
	 * all the hardware configurations would be completed by this function
	 * but if drm_fb_helper->funcs->fb_probe callback returns more then 1.
	 * drm framework would draw on linux framebuffer and then when
	 * register_framebuffer() is called, drm_fb_helper_set_par would be
	 * called by fb_set_par callback.(refer to fb_ops definitions above)
	 *
	 * ps. fb_info object is created by fb_probe callback.
	 */
	ret = drm_fb_helper_initial_config(helper, 32);
	if (ret < 0) {
		DRM_ERROR("failed to set up hw configuration.\n");
		goto fail;
	}

	return ret;
fail:
	kfree(fbdev);

	return ret;
}

void samsung_drm_fbdev_restore_mode(struct drm_device *dev)
{
	struct samsung_drm_private *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	drm_fb_helper_restore_fbdev_mode(&dev_priv->fbdev->drm_fb_helper);
}
