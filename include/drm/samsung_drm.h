/* samsung_drm.h
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

#ifndef _SAMSUNG_DRM_H_
#define _SAMSUNG_DRM_H_

#include "drm.h"

struct samsung_drm_overlay;

/**
 * Samsung drm overlay ops structure.
 *
 * @mode_set: copy drm overlay info to hw specific overlay info.
 * @commit: set hw specific overlay into to hw.
 */
struct samsung_drm_overlay_ops {
	void (*mode_set)(struct device *dispc_dev,
			struct samsung_drm_overlay *overlay);
	void (*commit)(struct device *dispc_dev, unsigned int win);
	void (*disable)(struct device *dispc_dev, unsigned int win);
};

/**
 * Samsung drm common overlay structure.
 *
 * @win_num: window number.
 * @offset_x: offset to x position.
 * @offset_y: offset to y position.
 * @pos_x: x position.
 * @pos_y: y position.
 * @width: window width.
 * @height: window height.
 * @bpp: bit per pixel.
 * @paddr: physical memory address to this overlay.
 * @vaddr: virtual memory addresss to this overlay.
 * @buf_off: start offset of framebuffer to be displayed.
 * @end_buf_off: end offset of framebuffer to be displayed.
 * @buf_offsize: this value has result from
 *			(framebuffer width - display width) * bpp.
 * @line_size: line size to this overlay memory in bytes.
 * @default_win: a window to be enabled.
 * @color_key: color key on or off.
 * @index_color: if using color key feature then this value would be used
 *			as index color.
 * @local_path: in case of lcd type, local path mode on or off.
 * @transparency: transparency on or off.
 * @activated: activated or not.
 * @dispc_dev: pointer to device object for dispc device driver.
 * @ops: pointer to samsung_drm_overlay_ops.
 *
 * this structure is common to Samsung SoC and would be copied
 * to hardware specific overlay info.
 */
struct samsung_drm_overlay {
	unsigned int win_num;
	unsigned int offset_x;
	unsigned int offset_y;
	unsigned int width;
	unsigned int height;
	unsigned int bpp;
	unsigned int paddr;
	void __iomem *vaddr;
	unsigned int buf_off;
	unsigned int end_buf_off;
	unsigned int buf_offsize;
	unsigned int line_size;

	bool default_win;
	bool color_key;
	unsigned int index_color;
	bool local_path;
	bool transparency;
	bool activated;

	struct device *dispc_dev;
	struct samsung_drm_overlay_ops *ops;
};

/**
 * Samsung drm common display information structure.
 *
 * @display_type: SAMSUNG_DRM_LCD/HDMI or TVOUT.
 * @edid: Extended display identification data support or not.
 *		if true, get_edid() would be called by get_modes()
 *		of connector helper to get edid tables.
 * @id: mean unique display id.
 * @default_display: display to be enabled at booting time.
 * @activated: activated or not.
 * @connected: indicate whether display device of this display type is
 *		connected or not.
 */
struct samsung_drm_display_info {
	unsigned int id;
	unsigned int type;
	bool edid;
	bool default_display;
	bool activated;
	bool connected;
};

/**
 * Samsung DRM Display Structure.
 *	- this structure is common to analog tv, digital tv and lcd panel.
 *
 * @dev: pointer to specific device object.
 * @is_connected: check for that display is connected or not.
 * @get_edid: get edid modes from display driver.
 * @get_timing: get timing object from display driver.
 * @set_timing: convert mode to timing and then set it to display driver.
 *		this callback doesn't apply it to hw.
 * @check_timing: check if timing is valid or not.
 * @commit_timing: apply timing value to hw.
 * @power_on: display device on or off.
 */
struct samsung_drm_display {
	struct device *dev;

	bool (*is_connected)(void);
	int (*get_edid)(struct samsung_drm_display *display, u8 *edid, int len);
	void *(*get_timing)(void);
	int (*set_timing)(struct drm_display_mode *mode);
	int (*check_timing)(struct samsung_drm_display *display,
			void *timing);
	void (*commit_timing)(void);
	int (*power_on)(struct drm_device *dev, int mode);
};

/**
 * Samsung drm manager ops
 *
 * @get_display: get an pointer of samsung_drm_display object.
 * @mode_set: convert drm_display_mode to hw specific display mode and
 *			would be called by encoder->mode_set().
 * @commit: set current hw specific display mode to hw.
 */
struct samsung_drm_manager_ops {
	struct samsung_drm_display *
		(*get_display)(struct device *dispc_dev);
	void (*mode_set)(struct device *dispc_dev, void *mode);
	void (*commit)(struct device *dispc_dev);
};

/**
 * Samsung drm common manager structure.
 *
 * @name:
 * @id:
 * @default_manager: default manager used at booting time.
 * @dispc_dev: pointer to device object for dispc device driver.
 * @display_type: SAMSUNG_DRM_LCD/HDMI or TVOUT.
 * @overlay: pointer to samsung_drm_overlay object registered.
 * @display: pointer to samsung_drm_display_info object registered.
 * @ops: ops pointer to samsung drm common framebuffer.
 *	 ops of fimd or hdmi driver should be set to this ones.
 */
struct samsung_drm_manager {
	const char *name;
	unsigned int id;
	bool default_manager;
	struct device *dispc_dev;
	unsigned int display_type;
	struct samsung_drm_display_info *display_info;
	struct samsung_drm_overlay *default_overlay;

	struct samsung_drm_manager_ops *ops;
};

/**
 * Samsung drm private structure.
 *
 * @default_dispc
 * @fbdev
 * @num_crtc: probed display driver count.
 *	this variable would be used to get possible crtc.
 */
struct samsung_drm_private {
	struct samsung_drm_dispc *default_dispc;
	struct samsung_drm_fbdev *fbdev;

	unsigned int num_crtc;

	/* FIXME */
	/* for pageflip */
	struct list_head pageflip_event_list;
	bool pageflip_event;

	/* add some structures. */
};

/**
 * User-desired buffer creation information structure.
 *
 * @usr_addr: an address allocated by user process and this address
 *	would be mmapped to physical region by fault handler.
 * @size: requested size for the object.
 *	- this size value would be page-aligned internally.
 * @flags: user request for setting memory type or cache attributes.
 * @handle: returned handle for the object.
 */
struct drm_samsung_gem_create {
	unsigned int usr_addr;
	unsigned int size;
	unsigned int flags;

	unsigned int handle;
};

/**
 * A structure for getting buffer offset.
 *
 * @handle: a pointer to gem object created.
 * @offset: relatived offset value of the memory region allocated.
 *	- this value should be set by user.
 * @size: mmaped memory size.
 *	- this value should be set by user.
 *		if size is 0, overall size of the buffer would be used.
 */
struct drm_samsung_gem_map_off {
	unsigned int handle;
	uint64_t offset;
	unsigned int size;
};

#define DRM_SAMSUNG_GEM_CREATE		0x00
#define DRM_SAMSUNG_GEM_MAP_OFFSET	0x01

#define DRM_IOCTL_SAMSUNG_GEM_CREATE		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_SAMSUNG_GEM_CREATE, struct drm_samsung_gem_create)

#define DRM_IOCTL_SAMSUNG_GEM_MAP_OFFSET	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_SAMSUNG_GEM_MAP_OFFSET, struct drm_samsung_gem_map_off)

#endif
