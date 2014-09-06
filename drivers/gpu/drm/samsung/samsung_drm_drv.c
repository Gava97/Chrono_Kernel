/* samsung_drm_drv.c
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
#include "drm.h"
#include "samsung_drm.h"

#include "samsung_drm_encoder.h"
#include "samsung_drm_connector.h"
#include "samsung_drm_crtc.h"
#include "samsung_drm_fbdev.h"
#include "samsung_drm_fb.h"
#include "samsung_drm_dispc.h"
#include "samsung_drm_gem.h"
#include "samsung_drm_buf.h"

#include <drm/samsung_drm.h>
#include <plat/samsung_drm.h>

#define DRIVER_NAME	"samsung-drm"
#define DRIVER_DESC	"Samsung SoC DRM"
#define DRIVER_DATE	"20110530"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static DEFINE_MUTEX(drv_mutex);
static LIST_HEAD(subdrv_list);

void samsung_drm_subdrv_register(struct samsung_drm_dispc *dispc)
{
	mutex_lock(&drv_mutex);
	list_add_tail(&dispc->list, &subdrv_list);
	mutex_unlock(&drv_mutex);
}
EXPORT_SYMBOL(samsung_drm_subdrv_register);

void samsung_drm_subdrv_unregister(struct samsung_drm_dispc *dispc)
{
	mutex_lock(&drv_mutex);
	list_del(&dispc->list);
	mutex_unlock(&drv_mutex);
}
EXPORT_SYMBOL(samsung_drm_subdrv_unregister);

static struct drm_mode_config_funcs samsung_drm_mode_config_funcs = {
	.fb_create = samsung_drm_fb_create,
};

static int samsung_drm_mode_init(struct drm_device *dev,
			  struct samsung_drm_dispc *dispc)
{
	struct samsung_drm_overlay *overlay;
	struct samsung_drm_manager *manager;
	struct samsung_drm_manager_data *manager_data;
	struct drm_encoder *encoder;
	int ret;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!dispc)
		return -EINVAL;

	manager_data = dispc->manager_data;

	overlay = kzalloc(sizeof(*overlay), GFP_KERNEL);
	manager = kzalloc(sizeof(*manager), GFP_KERNEL);

	if (!overlay || !manager) {
		DRM_ERROR("failed to allocate\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	overlay->win_num = manager_data->overlay_num;
	overlay->bpp = manager_data->bpp;
	overlay->ops = dispc->overlay_ops;
	overlay->dispc_dev = dispc->dev;

	manager->display_type = manager_data->display_type;
	manager->ops = dispc->manager_ops;
	manager->dispc_dev = dispc->dev;

	/* initialize encoder. */
	encoder = samsung_drm_encoder_create(dev, manager);
	if (!encoder) {
		DRM_ERROR("failed to create encoder\n");
		ret = -EFAULT;
		goto err_alloc;
	}

	/* initialize connector. */
	ret = samsung_drm_connector_create(dev, encoder);
	if (ret) {
		DRM_ERROR("failed to create connector\n");
		goto err_encoder;
	}

	/* initialize crtc. */
	ret = samsung_drm_crtc_create(dev, overlay, manager_data->overlay_nr);
	if (ret) {
		DRM_ERROR("failed to create crtc\n");
		goto err_connector;
	}

	DRM_DEBUG_KMS("completed mode initialization\n");

	return 0;

err_connector:
	/* TODO */
err_encoder:
	/* TODO */
err_alloc:
	kfree(overlay);
	kfree(manager);
	return ret;
}

static void samsung_drm_mode_cleanup(struct drm_device *dev,
			      struct samsung_drm_dispc *dispc)
{
	/* TODO */
}

static void samsung_drm_subdrv_probe(struct drm_device *dev)
{
	struct samsung_drm_dispc *dispc;
	int err;

	list_for_each_entry(dispc, &subdrv_list, list) {
		if (dispc->probe) {
			/* FIXME */
			err = dispc->probe(dev, dispc);
			if (err)
				continue;
		}

		err = samsung_drm_mode_init(dev, dispc);
		if (err) {
			if (dispc->remove)
				dispc->remove(dev);
		}
	}
}

static void samsung_drm_subdrv_remove(struct drm_device *dev)
{
	struct samsung_drm_dispc *dispc;

	list_for_each_entry(dispc, &subdrv_list, list) {
		samsung_drm_mode_cleanup(dev, dispc);

		if (dispc->remove)
			dispc->remove(dev);
	}
}

static int samsung_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct samsung_drm_private *private;
	int ret;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	private = kzalloc(sizeof(struct samsung_drm_private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("failed to allocate samsung_drm_private.\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&private->pageflip_event_list);
	dev->dev_private = (void *)private;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	/*
	 * It sets max width and height as default value(4096x4096).
	 * this value would be used to check for framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	dev->mode_config.funcs = &samsung_drm_mode_config_funcs;

	/* probe dispc drivers registered to drm */
	samsung_drm_subdrv_probe(dev);

	ret = samsung_drm_fbdev_init(dev);
	if (ret < 0) {
		DRM_ERROR("failed to initialize drm fbdev.\n");
		goto err_dispc_probe;
	}

	ret = drm_vblank_init(dev, private->num_crtc);
	if (ret)
		goto err_dispc_probe;

	return 0;

err_dispc_probe:
	samsung_drm_subdrv_remove(dev);
	kfree(private);

	return ret;
}

static int samsung_drm_unload(struct drm_device *dev)
{
	samsung_drm_subdrv_remove(dev);

	drm_vblank_cleanup(dev);
	kfree(dev->dev_private);

	return 0;
}

static int samsung_drm_open(struct drm_device *dev, struct drm_file *file_priv)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	return 0;
}

static void samsung_drm_lastclose(struct drm_device *dev)
{
	samsung_drm_fbdev_restore_mode(dev);

	/* TODO */
}

static int samsung_drm_master_create(struct drm_device *dev,
		struct drm_master *master)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	/* TODO. */
	master->driver_priv = NULL;

	return 0;
}

static int samsung_drm_master_set(struct drm_device *dev,
		struct drm_file *file_priv, bool from_open)
{
	struct drm_master *master = file_priv->master;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	master->lock.hw_lock = kzalloc(sizeof(struct drm_hw_lock), GFP_KERNEL);
	if (!master->lock.hw_lock) {
		DRM_DEBUG("failed to allocate drm_hw_lock.\n");
		return -ENOMEM;
	}

	return 0;
}

static struct vm_operations_struct samsung_drm_gem_vm_ops = {
	.fault = samsung_drm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_ioctl_desc samsung_ioctls[] = {
	DRM_IOCTL_DEF_DRV(SAMSUNG_GEM_CREATE, samsung_drm_gem_create_ioctl,
			DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(SAMSUNG_GEM_MAP_OFFSET,
			samsung_drm_gem_map_offset_ioctl, DRM_UNLOCKED),
};

static struct drm_driver samsung_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_BUS_PLATFORM |
					DRIVER_MODESET | DRIVER_GEM,
	.load			= samsung_drm_load,
	.unload			= samsung_drm_unload,
	.open			= samsung_drm_open,
	.firstopen		= NULL,
	.lastclose		= samsung_drm_lastclose,
	.preclose		= NULL,
	.postclose		= NULL,
	.get_vblank_counter	= drm_vblank_count,
	.master_create		= samsung_drm_master_create,
	.master_set		= samsung_drm_master_set,
	.gem_init_object	= samsung_drm_gem_init_object,
	.gem_free_object	= samsung_drm_gem_free_object,
	.gem_vm_ops		= &samsung_drm_gem_vm_ops,
	.dumb_create		= samsung_drm_gem_dumb_create,
	.dumb_map_offset	= samsung_drm_gem_dumb_map_offset,
	.dumb_destroy		= samsung_drm_gem_dumb_destroy,
	.ioctls			= samsung_ioctls,
	.fops = {
		.owner		= THIS_MODULE,
		.open		= drm_open,
		.mmap		= samsung_drm_gem_mmap,
		.poll		= drm_poll,
		.read		= drm_read,
		.unlocked_ioctl	= drm_ioctl,
		.release	= drm_release,
	},
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static int samsung_drm_platform_probe(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	samsung_drm_driver.num_ioctls = DRM_ARRAY_SIZE(samsung_ioctls);

	return drm_platform_init(&samsung_drm_driver, pdev);
}

static int samsung_drm_platform_remove(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	drm_platform_exit(&samsung_drm_driver, pdev);

	return 0;
}

static struct platform_driver samsung_drm_platform_driver = {
	.probe		= samsung_drm_platform_probe,
	.remove		= __devexit_p(samsung_drm_platform_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRIVER_NAME,
	},
};

static int __init samsung_drm_init(void)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	return platform_driver_register(&samsung_drm_platform_driver);
}

static void __exit samsung_drm_exit(void)
{
	platform_driver_unregister(&samsung_drm_platform_driver);
}

module_init(samsung_drm_init);
module_exit(samsung_drm_exit);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Driver");
MODULE_LICENSE("GPL");
