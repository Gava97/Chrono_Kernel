/* samsung_drm_encoder.c
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

#include "samsung_drm_dispc.h"
#include "samsung_drm_common.h"

#include <drm/samsung_drm.h>

#define to_samsung_encoder(x)	container_of(x, struct samsung_drm_encoder,\
		drm_encoder)

struct samsung_drm_encoder {
	struct drm_encoder		drm_encoder;
	struct samsung_drm_manager	*mgr;
};

static void samsung_drm_encoder_destroy(struct drm_encoder *encoder)
{
	struct samsung_drm_encoder *samsung_encoder =
			to_samsung_encoder(encoder);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	drm_encoder_cleanup(encoder);
	kfree(samsung_encoder);
}

static struct drm_encoder_funcs samsung_encoder_funcs = {
	.destroy = samsung_drm_encoder_destroy,
};

static void samsung_drm_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct samsung_drm_manager *mgr;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mgr = samsung_drm_get_manager(encoder);
	if (!mgr) {
		DRM_ERROR("manager is NULL.\n");
		return;
	}

	DRM_INFO("%s: encoder dpms: %d\n", mgr->name, mode);

	list_for_each_entry(connector, &dev->mode_config.connector_list,
			head) {
		if (connector->encoder == encoder) {
			struct samsung_drm_display *display;

			display = get_display_from_connector(connector);

			if (display && display->power_on)
				display->power_on(dev, mode);
		}
	}
}

static bool samsung_drm_encoder_mode_fixup(struct drm_encoder *encoder,
			   struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	return true;
}

static void samsung_drm_encoder_mode_set(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct samsung_drm_manager *mgr;
	struct samsung_drm_manager_ops *manager_ops;

	mode = adjusted_mode;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mgr = samsung_drm_get_manager(encoder);
	if (!mgr) {
		DRM_ERROR("manager is NULL.\n");
		return;
	}

	DRM_INFO("%s: encoder set mode: %dx%d\n", mgr->name,
			mode->hdisplay, mode->vdisplay);

	manager_ops = mgr->ops;
	if (!manager_ops) {
		DRM_ERROR("ops of mgr is null.\n");
		return;
	}

	list_for_each_entry(connector, &dev->mode_config.connector_list,
			head) {
		if (connector->encoder == encoder)
			if (manager_ops && manager_ops->mode_set)
				manager_ops->mode_set(mgr->dispc_dev, mode);
	}
}

static void samsung_drm_encoder_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */
}

static void samsung_drm_encoder_commit(struct drm_encoder *encoder)
{
	struct samsung_drm_manager *mgr;
	struct samsung_drm_manager_ops *manager_ops;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mgr = samsung_drm_get_manager(encoder);
	if (!mgr) {
		DRM_ERROR("manager is NULL.\n");
		return;
	}

	DRM_INFO("%s: encoder commit\n", mgr->name);

	manager_ops = mgr->ops;
	if (!manager_ops) {
		DRM_ERROR("ops of mgr is null.\n");
		return;
	}

	if (manager_ops && manager_ops->commit)
		manager_ops->commit(mgr->dispc_dev);
}

static struct drm_crtc *
	samsung_drm_encoder_get_crtc(struct drm_encoder *encoder)
{
	/* FIXME!!! */

	return encoder->crtc;
}

static struct drm_encoder_helper_funcs samsung_encoder_helper_funcs = {
	.dpms = samsung_drm_encoder_dpms,
	.mode_fixup = samsung_drm_encoder_mode_fixup,
	.mode_set = samsung_drm_encoder_mode_set,
	.prepare = samsung_drm_encoder_prepare,
	.commit = samsung_drm_encoder_commit,
	.get_crtc = samsung_drm_encoder_get_crtc,
};

/**
 * initialize encoder. (drm and samsung SoC specific encoder)
 *
 * @dev: object of struct drm_device
 */
struct drm_encoder *samsung_drm_encoder_create(struct drm_device *dev,
					       struct samsung_drm_manager *mgr)
{
	struct samsung_drm_private *private = dev->dev_private;
	struct drm_encoder *encoder;
	struct samsung_drm_encoder *samsung_encoder;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_encoder = kzalloc(sizeof(*samsung_encoder), GFP_KERNEL);
	if (!samsung_encoder) {
		DRM_ERROR("failed to allocate encoder.\n");
		return NULL;
	}

	samsung_encoder->mgr = mgr;
	encoder = &samsung_encoder->drm_encoder;

	BUG_ON(!private->num_crtc);

	encoder->possible_crtcs = 0x1 << (private->num_crtc - 1);

	DRM_DEBUG_KMS("num_crtc = %d, possible_crtcs = 0x%x\n",
			private->num_crtc, encoder->possible_crtcs);

	/* add to encoder list. */
	drm_encoder_init(dev, encoder, &samsung_encoder_funcs,
			DRM_MODE_ENCODER_TMDS);

	/* set encoder helper callbacks. */
	drm_encoder_helper_add(encoder, &samsung_encoder_helper_funcs);

	DRM_DEBUG_KMS("encoder has been created.\n");

	return encoder;
}

struct samsung_drm_manager *samsung_drm_get_manager(struct drm_encoder *encoder)
{
	struct samsung_drm_encoder *samsung_encoder;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_encoder = container_of(encoder, struct samsung_drm_encoder,
			drm_encoder);
	if (!samsung_encoder) {
		DRM_ERROR("samsung_encoder is null.\n");
		return NULL;
	}

	return samsung_encoder->mgr;
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Encoder Driver");
MODULE_LICENSE("GPL");
