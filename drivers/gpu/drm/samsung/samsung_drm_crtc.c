/* samsung_drm_crtc.c
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
#include "drm_crtc_helper.h"

#include <drm/samsung_drm.h>

#include "samsung_drm_common.h"
#include "samsung_drm_fb.h"
#include "samsung_drm_dispc.h"
#include "samsung_drm_crtc.h"

#define to_samsung_crtc(x)	container_of(x, struct samsung_drm_crtc,\
		drm_crtc)

struct samsung_drm_crtc {
	struct drm_crtc drm_crtc;
	struct samsung_drm_overlay *overlay;
};

static int samsung_drm_overlay_update(struct samsung_drm_overlay *overlay,
				      struct drm_framebuffer *fb,
				      struct drm_display_mode *mode,
				      struct samsung_drm_crtc_pos *pos)
{
	struct samsung_drm_buffer_info buffer_info;
	unsigned int bpp;
	unsigned int actual_w = pos->crtc_w;
	unsigned int actual_h = pos->crtc_h;
	unsigned int hw_w;
	unsigned int hw_h;
	int ret;

	/* update buffer address of framebuffer. */
	ret = samsung_drm_fb_update_buf_off(fb, pos->fb_x, pos->fb_y,
			&buffer_info);
	if (ret < 0) {
		DRM_ERROR("failed to update framebuffer offset\n");
		return ret;
	}

	/* set start position of framebuffer memory to be displayed. */
	overlay->paddr = buffer_info.paddr;
	overlay->vaddr = buffer_info.vaddr;

	hw_w = mode->hdisplay - pos->base_x;
	hw_h = mode->vdisplay - pos->base_y;

	if (actual_w > hw_w)
		actual_w = hw_w;
	if (actual_h > hw_h)
		actual_h = hw_h;

	overlay->offset_x = pos->base_x;
	overlay->offset_y = pos->base_y;
	overlay->width = actual_w;
	overlay->height = actual_h;

	DRM_DEBUG_KMS("overlay : offset_x/y(%d,%d), width/height(%d,%d)",
			overlay->offset_x, overlay->offset_y,
			overlay->width, overlay->height);

	bpp = (overlay->bpp >> 3);

	overlay->buf_offsize = (fb->width - actual_w) * bpp;
	overlay->line_size = actual_w * bpp;
	overlay->end_buf_off = fb->width * actual_h * bpp;

	return 0;
}

static int samsung_drm_crtc_update(struct drm_crtc *crtc)
{
	struct samsung_drm_crtc *samsung_crtc;
	struct samsung_drm_overlay *overlay;
	struct samsung_drm_crtc_pos pos;
	struct drm_display_mode *mode = &crtc->mode;
	struct drm_framebuffer *fb = crtc->fb;

	if (!mode || !fb)
		return -EINVAL;

	samsung_crtc = to_samsung_crtc(crtc);
	overlay = samsung_crtc->overlay;

	memset(&pos, 0, sizeof(struct samsung_drm_crtc_pos));
	pos.fb_x = crtc->x;
	pos.fb_y = crtc->y;
	pos.crtc_w = fb->width - crtc->x;
	pos.crtc_h = fb->height - crtc->y;

	return samsung_drm_overlay_update(overlay, crtc->fb, mode, &pos);
}

/* CRTC helper functions */
static void samsung_drm_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	/* TODO */
	DRM_DEBUG_KMS("%s\n", __FILE__);
}

static void samsung_drm_crtc_prepare(struct drm_crtc *crtc)
{
	/* TODO */
	DRM_DEBUG_KMS("%s\n", __FILE__);
}

static void samsung_drm_crtc_commit(struct drm_crtc *crtc)
{
	struct samsung_drm_crtc *samsung_crtc = to_samsung_crtc(crtc);
	struct samsung_drm_overlay *overlay = samsung_crtc->overlay;
	struct samsung_drm_overlay_ops *overlay_ops = overlay->ops;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	overlay_ops->commit(overlay->dispc_dev, overlay->win_num);
}

static bool samsung_drm_crtc_mode_fixup(struct drm_crtc *crtc,
			   struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode)
{
	/* TODO */
	return true;
}

/* change mode and update overlay. */
static int samsung_drm_crtc_mode_set(struct drm_crtc *crtc,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode, int x, int y,
			struct drm_framebuffer *old_fb)
{
	struct samsung_drm_crtc *samsung_crtc = to_samsung_crtc(crtc);
	struct samsung_drm_overlay *overlay = samsung_crtc->overlay;
	struct samsung_drm_overlay_ops *overlay_ops = overlay->ops;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mode = adjusted_mode;

	ret = samsung_drm_crtc_update(crtc);
	if (ret < 0)
		return ret;

	overlay_ops->mode_set(overlay->dispc_dev, overlay);

	return ret;
}

static int samsung_drm_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			     struct drm_framebuffer *old_fb)
{
	struct samsung_drm_crtc *samsung_crtc = to_samsung_crtc(crtc);
	struct samsung_drm_overlay *overlay = samsung_crtc->overlay;
	struct samsung_drm_overlay_ops *overlay_ops = overlay->ops;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	ret = samsung_drm_crtc_update(crtc);
	if (ret < 0)
		return ret;

	overlay_ops->mode_set(overlay->dispc_dev, overlay);
	overlay_ops->commit(overlay->dispc_dev, overlay->win_num);

	return ret;
}

static void samsung_drm_crtc_load_lut(struct drm_crtc *crtc)
{
	/* TODO */
	DRM_DEBUG_KMS("%s\n", __FILE__);
}

static struct drm_crtc_helper_funcs samsung_crtc_helper_funcs = {
	.dpms = samsung_drm_crtc_dpms,
	.prepare = samsung_drm_crtc_prepare,
	.commit = samsung_drm_crtc_commit,
	.mode_fixup = samsung_drm_crtc_mode_fixup,
	.mode_set = samsung_drm_crtc_mode_set,
	.mode_set_base = samsung_drm_crtc_mode_set_base,
	.load_lut = samsung_drm_crtc_load_lut,
};

/* CRTC functions */
static int samsung_drm_crtc_page_flip(struct drm_crtc *crtc,
				      struct drm_framebuffer *fb,
				      struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct samsung_drm_private *dev_priv = dev->dev_private;
	struct samsung_drm_crtc *samsung_crtc = to_samsung_crtc(crtc);
	struct samsung_drm_overlay *overlay = samsung_crtc->overlay;
	struct samsung_drm_overlay_ops *overlay_ops = overlay->ops;
	struct drm_framebuffer *old_fb = crtc->fb;
	int ret;

	if (event && !dev_priv->pageflip_event) {
		list_add_tail(&event->base.link,
				&dev_priv->pageflip_event_list);
		/* FIXME: CRTC */
		ret = drm_vblank_get(dev, 0);
		if (ret) {
			DRM_DEBUG("failed to acquire vblank counter\n");
			return ret;
		}
		dev_priv->pageflip_event = true;
	}

	crtc->fb = fb;

	ret = samsung_drm_crtc_update(crtc);
	if (ret < 0) {
		crtc->fb = old_fb;
		if (event && dev_priv->pageflip_event) {
			/* FIXME: CRTC */
			drm_vblank_put(dev, 0);
			dev_priv->pageflip_event = false;
		}
		return ret;
	}

	overlay_ops->mode_set(overlay->dispc_dev, overlay);
	overlay_ops->commit(overlay->dispc_dev, overlay->win_num);

	return 0;
}

static struct drm_crtc_funcs samsung_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.page_flip = samsung_drm_crtc_page_flip,
};

int samsung_drm_crtc_create(struct drm_device *dev,
			    struct samsung_drm_overlay *overlay,
			    unsigned int overlay_nr)
{
	struct samsung_drm_crtc *samsung_crtc;
	struct drm_crtc *crtc;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!overlay || !overlay_nr)
		return -EINVAL;

	samsung_crtc = kzalloc(sizeof(*samsung_crtc), GFP_KERNEL);
	if (!samsung_crtc) {
		DRM_ERROR("failed to allocate samsung crtc\n");
		return -ENOMEM;
	}

	samsung_crtc->overlay = overlay;
	crtc = &samsung_crtc->drm_crtc;

	drm_crtc_init(dev, crtc, &samsung_crtc_funcs);
	drm_crtc_helper_add(crtc, &samsung_crtc_helper_funcs);

	/* TODO: multi overlay */

	return 0;
}

void samsung_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct samsung_drm_crtc *samsung_crtc = to_samsung_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(samsung_crtc);
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM CRTC Driver");
MODULE_LICENSE("GPL");
