/* samsung_drm_crtc.h
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

#ifndef _SAMSUNG_DRM_CRTC_H_
#define _SAMSUNG_DRM_CRTC_H_

/*
 * @fb_x: horizontal position from framebuffer base
 * @fb_y: vertical position from framebuffer base
 * @base_x: horizontal position from screen base
 * @base_y: vertical position from screen base
 * @crtc_w: width of crtc
 * @crtc_h: height of crtc
 */
struct samsung_drm_crtc_pos {
	unsigned int fb_x;
	unsigned int fb_y;
	unsigned int base_x;
	unsigned int base_y;
	unsigned int crtc_w;
	unsigned int crtc_h;
};

int samsung_drm_crtc_create(struct drm_device *dev,
			    struct samsung_drm_overlay *overlay,
			    unsigned int overlay_nr);

void samsung_drm_crtc_destroy(struct drm_crtc *crtc);
#endif
