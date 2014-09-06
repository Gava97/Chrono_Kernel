/* samsung_drm_connector.c
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
#include <plat/samsung_drm.h>

#include "samsung_drm_common.h"

#define MAX_EDID 256
#define to_samsung_connector(x)	container_of(x, struct samsung_drm_connector,\
		drm_connector);

struct samsung_drm_connector {
	struct drm_connector		drm_connector;
	struct samsung_drm_display	*display;
};

/* convert samsung_video_timings to drm_display_mode. */
static inline void convert_to_display_mode(struct samsung_video_timings *timing,
		struct drm_display_mode *mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	mode->clock = timing->vclk / 1000;

	mode->hdisplay = timing->x_res;
	mode->hsync_start = mode->hdisplay + timing->hfp;
	mode->hsync_end = mode->hsync_start + timing->hsw;
	mode->htotal = mode->hsync_end + timing->hbp;

	mode->vdisplay = timing->y_res;
	mode->vsync_start = mode->vdisplay + timing->vfp;
	mode->vsync_end = mode->vsync_start + timing->vsw;
	mode->vtotal = mode->vsync_end + timing->vbp;
}

/* convert drm_display_mode to samsung_video_timings. */
static inline void convert_to_video_timing(struct drm_display_mode *mode,
		struct samsung_video_timings *timing)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	timing->vclk = mode->clock * 1000;

	timing->x_res = mode->hdisplay;
	timing->hfp = mode->hsync_start - mode->hdisplay;
	timing->hsw = mode->hsync_end - mode->hsync_start;
	timing->hbp = mode->htotal - mode->hsync_end;

	timing->y_res = mode->vdisplay;
	timing->vfp = mode->vsync_start - mode->vdisplay;
	timing->vsw = mode->vsync_end - mode->vsync_start;
	timing->vbp = mode->vtotal - mode->vsync_end;
}

/* get detection status of display device. */
static enum drm_connector_status
	samsung_drm_connector_detect(struct drm_connector *connector,
			bool force)
{
	struct drm_encoder *encoder;
	struct samsung_drm_connector *samsung_connector;
	struct samsung_drm_display *display;
	struct samsung_drm_manager *manager;
	unsigned int ret = connector_status_unknown;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_connector = to_samsung_connector(connector);
	display = samsung_connector->display;

	/* get drm_encoder object connected to this drm_connector. */
	encoder = samsung_drm_get_attached_encoder(connector);
	if (!encoder) {
		DRM_ERROR("encoder connected to connector is null.\n");
		return ret;
	}

	manager = samsung_drm_get_manager(encoder);
	if (!manager) {
		DRM_ERROR("manager of encoder is null.\n");
		return ret;
	}

	if (display->is_connected) {
		if (display->is_connected() == true)
			ret = connector_status_connected;
		else
			ret = connector_status_disconnected;
	}

	return ret;
}

static struct drm_connector_funcs samsung_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = samsung_drm_connector_detect,
};

static int samsung_drm_connector_get_modes(struct drm_connector *connector)
{
	struct samsung_drm_connector *samsung_connector;
	struct samsung_drm_display *display;
	unsigned int count = 0;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_connector = to_samsung_connector(connector);
	display = samsung_connector->display;

	/* DRM_INFO("%s", display_dev->name); */

	/* if edid, get edid modes from display device and update it and then
	 * add its data.
	 */
	if (display->get_edid) {
		void *edid = kzalloc(MAX_EDID, GFP_KERNEL);
		if (!edid) {
			DRM_ERROR("failed to allocate edid.\n");
			goto fail;
		}

		display->get_edid(display, edid, MAX_EDID);

		drm_mode_connector_update_edid_property(connector, edid);
		count = drm_add_edid_modes(connector, edid);

		kfree(connector->display_info.raw_edid);
		connector->display_info.raw_edid = edid;
	} else {
		struct drm_display_mode *mode = drm_mode_create(connector->dev);
		struct samsung_video_timings *timing;

		if (display->get_timing)
			timing = (struct samsung_video_timings *)
						display->get_timing();
		else {
			DRM_ERROR("get_timing is null.\n");
			goto fail;
		}

		convert_to_display_mode(timing, mode);

		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);

		count = 1;
	}

fail:
	return count;
}

static int samsung_drm_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	struct samsung_drm_connector *samsung_connector;
	struct samsung_drm_display *display;
	struct samsung_video_timings timing;
	int ret = MODE_BAD;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_connector = to_samsung_connector(connector);
	display = samsung_connector->display;

	convert_to_video_timing(mode, &timing);

	if (display && display->check_timing)
		if (display->check_timing(display, (void *)&timing))
			ret = MODE_OK;

	return ret;
}

struct drm_encoder *samsung_drm_best_encoder(struct drm_connector *connector)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);
	return samsung_drm_get_attached_encoder(connector);
}

static struct drm_connector_helper_funcs samsung_connector_helper_funcs = {
	.get_modes = samsung_drm_connector_get_modes,
	.mode_valid = samsung_drm_connector_mode_valid,
	.best_encoder = samsung_drm_best_encoder,
};

static int get_connector_type(struct samsung_drm_manager *manager)
{
	int type = -EINVAL;

	switch (manager->display_type) {
	case SAMSUNG_DISPLAY_TYPE_HDMI:
		type = DRM_MODE_CONNECTOR_HDMIA;
		break;
	case SAMSUNG_DISPLAY_TYPE_MIPI:
		type = DRM_MODE_CONNECTOR_DVID;
		break;
	default:
		type = DRM_MODE_CONNECTOR_Unknown;
		break;
	}

	return type;
}

int samsung_drm_connector_create(struct drm_device *dev,
				 struct drm_encoder *encoder)
{
	struct samsung_drm_connector *samsung_connector;
	struct samsung_drm_manager *manager = samsung_drm_get_manager(encoder);
	struct samsung_drm_display *display;
	struct drm_connector *connector;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_connector = kzalloc(sizeof(*samsung_connector),
			GFP_KERNEL);
	if (!samsung_connector) {
		DRM_ERROR("failed to allocate connector.\n");
		return -ENOMEM;
	}

	/**
	 * get display device driver obejct according to display type.
	 * we can control display device through this object.
	 */
	if (!manager->ops->get_display) {
		DRM_ERROR("get_display is null.\n");
		return -EFAULT;
	}

	display = manager->ops->get_display(manager->dispc_dev);
	if (!display) {
		DRM_ERROR("failed to get display device.\n");
		return -EFAULT;
	}

	samsung_connector->display = display;
	connector = &samsung_connector->drm_connector;

	ret = get_connector_type(manager);
	if (ret < 0) {
		DRM_ERROR("wrong display type.\n");
		goto out;
	}

	drm_connector_init(dev, connector,
			 &samsung_connector_funcs, ret);
	drm_connector_helper_add(connector,
			&samsung_connector_helper_funcs);

	ret = drm_sysfs_connector_add(connector);
	if (ret < 0)
		goto out;

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		DRM_ERROR("failed to attach a connector to a encoder.\n");
		goto out;
	}

	DRM_DEBUG_KMS("connector has been created.\n");

out:
	return ret;
}

struct drm_encoder *
	samsung_drm_get_attached_encoder(struct drm_connector *connector)
{
	int i;
	struct samsung_drm_connector *samsung_connector;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_connector = to_samsung_connector(connector);

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		struct drm_mode_object *obj;

		if (connector->encoder_ids[i] == 0) {
			DRM_ERROR("there is no drm_encoder registered.\n");
			return NULL;
		}

		obj = drm_mode_object_find(connector->dev,
				connector->encoder_ids[i],
				DRM_MODE_OBJECT_ENCODER);

		if (!obj) {
			DRM_ERROR("drm_mode_object of encoder_ids is null.\n");
			return NULL;
		}

		return obj_to_encoder(obj);
	}

	return NULL;
}

struct samsung_drm_display *
	get_display_from_connector(struct drm_connector *connector) {

	struct samsung_drm_connector *samsung_connector;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_connector = to_samsung_connector(connector);

	return samsung_connector->display;
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Connector Driver");
MODULE_LICENSE("GPL");
