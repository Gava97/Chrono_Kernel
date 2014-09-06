/* samsung_drm.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *
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

enum samsung_drm_output_type {
	SAMSUNG_DISPLAY_TYPE_NONE,
	SAMSUNG_DISPLAY_TYPE_LCD,	/* RGB or CPU Interface. */
	SAMSUNG_DISPLAY_TYPE_MIPI,	/* MIPI-DSI Interface. */
	SAMSUNG_DISPLAY_TYPE_HDMI,	/* HDMI Interface. */
	SAMSUNG_DISPLAY_TYPE_VENC,
};

struct samsung_video_timings {
	u16 x_res;
	u16 y_res;
	u16 hsw;
	u16 hfp;
	u16 hbp;
	u16 vsw;
	u16 vfp;
	u16 vbp;
	u32 framerate;
	u32 vclk;	/* Hz, calcurate from driver */
};

struct samsung_drm_manager_data {
	unsigned int display_type;
	unsigned int overlay_nr;
	unsigned int overlay_num;
	unsigned int bpp;
};

struct samsung_drm_fimd_pdata {
	struct samsung_drm_manager_data	manager_data;
	struct samsung_video_timings	timing;
	void				(*setup_gpio)(void);
	u32				vidcon0;
	u32				vidcon1;
};
