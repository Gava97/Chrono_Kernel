/* samsung_drm_dispc.h
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

#ifndef _SAMSUNG_DRM_DISPC_H_
#define _SAMSUNG_DRM_DISPC_H_

struct samsung_drm_dispc {
	const char *name;
	struct device *dev;
	struct list_head list;

	/* driver ops */
	int (*probe)(struct drm_device *dev, struct samsung_drm_dispc *dispc);
	int (*remove)(struct drm_device *dev);

	struct samsung_drm_manager_ops *manager_ops;
	struct samsung_drm_overlay_ops *overlay_ops;

	void *manager_data;
};

void samsung_drm_subdrv_register(struct samsung_drm_dispc *drm_dispc);
void samsung_drm_subdrv_unregister(struct samsung_drm_dispc *drm_dispc);

#endif
