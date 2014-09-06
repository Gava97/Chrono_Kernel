/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include "drmP.h"

#include "samsung_drm_dispc.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

/* FIXME */
#include <drm/samsung_drm.h>
#include <plat/samsung_drm.h>
#include <plat/regs-fb-v4.h>

/* irq_flags bits */
#define FIMD_VSYNC_IRQ_EN	0

#define VIDOSD_A(win)	(VIDOSD_BASE + 0x00 + (win) * 16)
#define VIDOSD_B(win)	(VIDOSD_BASE + 0x04 + (win) * 16)
#define VIDOSD_C(win)	(VIDOSD_BASE + 0x08 + (win) * 16)
#define VIDOSD_D(win)	(VIDOSD_BASE + 0x0C + (win) * 16)

#define VIDWx_BUF_START(win, buf)	(VIDW_BUF_START(buf) + (win) * 8)
#define VIDWx_BUF_END(win, buf)		(VIDW_BUF_END(buf) + (win) * 8)
#define VIDWx_BUF_SIZE(win, buf)	(VIDW_BUF_SIZE(buf) + (win) * 4)

#define WINDOWS_NR	5

#define get_fimd_dev(drm_dev)	(((struct samsung_drm_private *)	\
				drm_dev->dev_private)->default_dispc->dev)
#define get_fimd_data(dev)	platform_get_drvdata(to_platform_device(dev))

struct fimd_win_data {
	unsigned int		win_num;
	unsigned int		offset_x;
	unsigned int		offset_y;
	unsigned int		width;
	unsigned int		height;
	unsigned int		bpp;
	unsigned int		paddr;
	void __iomem		*vaddr;
	unsigned int		end_buf_off;
	unsigned int		buf_offsize;
	unsigned int		line_size;	/* bytes */

	/* TODO */
};

struct fimd_data {
	struct clk			*bus_clk;
	struct resource			*regs_res;
	void __iomem			*regs;
	unsigned int			clkdiv;

	/* FIXME */
	struct samsung_drm_manager_data	manager_data;
	struct fimd_win_data		win_data[WINDOWS_NR];
	u32				vidcon0;
	u32				vidcon1;
	unsigned long			irq_flags;

	/* TODO */
};

static struct samsung_video_timings fimd_timing;

static bool fimd_display_is_connected(void)
{
	/* FIXME. */
	return true;
}

static void *fimd_get_timing(void)
{
#if 0
	struct platform_device *pdev = to_platform_device(dev);
	struct fimd_data *data = platform_get_drvdata(pdev);
#endif

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	return &fimd_timing;
}

static int fimd_set_timing(struct drm_display_mode *mode)
{
	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	/* TODO */
	return 0;
}

static void fimd_commit_timing(void)
{
	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	/* TODO */
}

static int fimd_check_timing(struct samsung_drm_display *display, void *timing)
{
	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	/* TODO: Indeed need this function?  */
	return 1;
}

static int fimd_display_power_on(struct drm_device *drm_dev, int mode)
{
	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);

	return 0;
}

static struct samsung_drm_display fimd_display = {
	.is_connected = fimd_display_is_connected,
	.get_timing = fimd_get_timing,
	.set_timing = fimd_set_timing,
	.commit_timing = fimd_commit_timing,
	.check_timing = fimd_check_timing,
	.power_on = fimd_display_power_on,
};

static struct samsung_drm_display *fimd_get_display(struct device *dev)
{
#if 0
	struct platform_device *pdev = to_platform_device(dev);
	struct fimd_data *data = platform_get_drvdata(pdev);
#endif

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	/* TODO */
	return &fimd_display;
}

static void fimd_mode_set(struct device *dev, void *mode)
{
	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	/* TODO */
}

static void fimd_commit(struct device *dev)
{
	struct fimd_data *data = get_fimd_data(dev);
	void __iomem *regs = data->regs;
	u32 val;

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);

	/* vidcon0 */
	val = data->vidcon0;
	val &= ~(VIDCON0_CLKVAL_F_MASK | VIDCON0_CLKDIR);

	if (data->clkdiv > 1)
		val |= VIDCON0_CLKVAL_F(data->clkdiv - 1) | VIDCON0_CLKDIR;
	else
		val &= ~VIDCON0_CLKDIR;	/* 1:1 clock */

	val |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(val, regs + VIDCON0);

	/* vidcon1 */
	writel(data->vidcon1, regs + VIDCON1);

	/* vidtcon0 */
	val = VIDTCON0_VBPD(fimd_timing.vfp - 1) |
	       VIDTCON0_VFPD(fimd_timing.vbp - 1) |
	       VIDTCON0_VSPW(fimd_timing.vsw - 1);
	writel(val, regs + VIDTCON0);

	/* vidtcon1 */
	val = VIDTCON1_HBPD(fimd_timing.hfp - 1) |
	       VIDTCON1_HFPD(fimd_timing.hbp - 1) |
	       VIDTCON1_HSPW(fimd_timing.hsw - 1);
	writel(val, regs + VIDTCON1);

	/* vidtcon2 */
	val = VIDTCON2_LINEVAL(fimd_timing.y_res - 1) |
	       VIDTCON2_HOZVAL(fimd_timing.x_res - 1);
	writel(val, regs + VIDTCON2);

	/* TODO */
}

static struct samsung_drm_manager_ops fimd_manager_ops = {
	.get_display = fimd_get_display,
	.mode_set = fimd_mode_set,
	.commit = fimd_commit,
};

static void fimd_win_mode_set(struct device *dev,
			      struct samsung_drm_overlay *overlay)
{
	struct fimd_data *data = get_fimd_data(dev);
	struct fimd_win_data *win_data;

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);

	if (!overlay) {
		dev_err(dev, "overlay is NULL\n");
		return;
	}

	win_data = &data->win_data[overlay->win_num];

	win_data->win_num = overlay->win_num;
	win_data->bpp = overlay->bpp;
	win_data->offset_x = overlay->offset_x;
	win_data->offset_y = overlay->offset_y;
	win_data->width = overlay->width;
	win_data->height = overlay->height;
	win_data->paddr = overlay->paddr;
	win_data->vaddr = overlay->vaddr;
	win_data->end_buf_off = overlay->end_buf_off;
	win_data->buf_offsize = overlay->buf_offsize;
	win_data->line_size = overlay->line_size;

	/* TODO */
}

static void fimd_win_commit(struct device *dev, unsigned int win)
{
	struct fimd_data *data = get_fimd_data(dev);
	void __iomem *regs = data->regs;
	struct fimd_win_data *win_data;
	u32 val;

	printk(KERN_DEBUG "[%d] %s, win: %d\n", __LINE__, __func__, win);

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &data->win_data[win];

	/* protect windows */
	val = readl(regs + SHADOWCON);
	val |= SHADOWCON_WINx_PROTECT(win);
	writel(val, regs + SHADOWCON);

	/* buffer start address */
	val = win_data->paddr;
	writel(val, regs + VIDWx_BUF_START(win, 0));

	/* buffer end address */
	val = win_data->paddr + win_data->end_buf_off;
	writel(val, regs + VIDWx_BUF_END(win, 0));

	/* FIXME: buffer size */
	val = VIDW_BUF_SIZE_OFFSET(win_data->buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH(win_data->line_size);
	writel(val, regs + VIDWx_BUF_SIZE(win, 0));

	/* OSD position */
	val = VIDOSDxA_TOPLEFT_X(win_data->offset_x) |
		VIDOSDxA_TOPLEFT_Y(win_data->offset_y);
	writel(val, regs + VIDOSD_A(win));

	val = VIDOSDxB_BOTRIGHT_X(win_data->offset_x + win_data->width - 1) |
		VIDOSDxB_BOTRIGHT_Y(win_data->offset_y + win_data->height - 1);
	writel(val, regs + VIDOSD_B(win));

	/* OSD alpha */

	/* OSD size */
	if (win != 3 && win != 4) {
		u32 offset = VIDOSD_D(win);
		if (win == 0)
			offset = VIDOSD_C(win);
		val = win_data->width * win_data->height;
		writel(val, regs + offset);
	}

	/* wincon */
	/* FIXME */
	val = WINCONx_ENWIN;
	val |= WINCON0_BPPMODE_24BPP_888;
	val |= WINCONx_WSWP;
	val |= WINCONx_BURSTLEN_16WORD;
	writel(val, regs + WINCON(win));

	/* colour key */

	/* Enable DMA channel and unprotect windows */
	val = readl(regs + SHADOWCON);
	val |= SHADOWCON_CHx_ENABLE(win);
	val &= ~SHADOWCON_WINx_PROTECT(win);
	writel(val, regs + SHADOWCON);

	/* TODO */
}

static void fimd_win_disable(struct device *dev, unsigned int win)
{
	struct fimd_data *data = get_fimd_data(dev);
	void __iomem *regs = data->regs;
	/* FIXME: only use win 0 */
	struct fimd_win_data *win_data;
	u32 val;

	printk(KERN_DEBUG "[%d] %s, win: %d\n", __LINE__, __func__, win);

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &data->win_data[win];

	/* protect windows */
	val = readl(regs + SHADOWCON);
	val |= SHADOWCON_WINx_PROTECT(win);
	writel(val, regs + SHADOWCON);

	/* wincon */
	val = readl(regs + WINCON(win));
	val &= ~WINCONx_ENWIN;
	writel(val, regs + WINCON(win));

	/* unprotect windows */
	val = readl(regs + SHADOWCON);
	val &= ~SHADOWCON_CHx_ENABLE(win);
	val &= ~SHADOWCON_WINx_PROTECT(win);
	writel(val, regs + SHADOWCON);

	/* TODO */
}

static struct samsung_drm_overlay_ops fimd_overlay_ops = {
	.mode_set = fimd_win_mode_set,
	.commit = fimd_win_commit,
	.disable = fimd_win_disable,
};

/* for pageflip event */
static void fimd_finish_pageflip(struct drm_device *drm_dev, int crtc)
{
	struct samsung_drm_private *dev_priv = drm_dev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	struct timeval now;
	unsigned long flags;

	if (!dev_priv->pageflip_event)
		return;

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	spin_lock_irqsave(&drm_dev->event_lock, flags);

	list_for_each_entry_safe(e, t, &dev_priv->pageflip_event_list,
			base.link) {
		do_gettimeofday(&now);
		e->event.sequence = 0;
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;

		list_move_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}

	drm_vblank_put(drm_dev, crtc);
	dev_priv->pageflip_event = false;

	spin_unlock_irqrestore(&drm_dev->event_lock, flags);
}

static irqreturn_t fimd_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *drm_dev = (struct drm_device *)arg;
	struct device *dev = get_fimd_dev(drm_dev);
	struct fimd_data *data = get_fimd_data(dev);
	void __iomem *regs = data->regs;
	u32 val;

	val = readl(regs + VIDINTCON1);

	if (val & VIDINTCON1_INT_FRAME)
		/* VSYNC interrupt */
		writel(VIDINTCON1_INT_FRAME, regs + VIDINTCON1);

	/* FIXME: Handle to all crtc */
	drm_handle_vblank(drm_dev, 0);
	fimd_finish_pageflip(drm_dev, 0);

	return IRQ_HANDLED;
}

static void fimd_irq_preinstall(struct drm_device *drm_dev)
{
}

static int fimd_irq_postinstall(struct drm_device *drm_dev)
{
	return 0;
}

static void fimd_irq_uninstall(struct drm_device *drm_dev)
{
}

static int fimd_enable_vblank(struct drm_device *drm_dev, int crtc)
{
	struct device *dev = get_fimd_dev(drm_dev);
	struct fimd_data *data = get_fimd_data(dev);
	void __iomem *regs = data->regs;
	u32 val;

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);

	if (!test_and_set_bit(FIMD_VSYNC_IRQ_EN, &data->irq_flags)) {
		val = readl(regs + VIDINTCON0);

		val |= VIDINTCON0_INT_ENABLE;
		val |= VIDINTCON0_INT_FRAME;

		val &= ~VIDINTCON0_FRAMESEL0_MASK;
		val |= VIDINTCON0_FRAMESEL0_VSYNC;
		val &= ~VIDINTCON0_FRAMESEL1_MASK;
		val |= VIDINTCON0_FRAMESEL1_NONE;

		writel(val, regs + VIDINTCON0);
	}

	return 0;
}

static void fimd_disable_vblank(struct drm_device *drm_dev, int crtc)
{
	struct device *dev = get_fimd_dev(drm_dev);
	struct fimd_data *data = get_fimd_data(dev);
	void __iomem *regs = data->regs;
	u32 val;

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);

	if (test_and_clear_bit(FIMD_VSYNC_IRQ_EN, &data->irq_flags)) {
		val = readl(regs + VIDINTCON0);

		val &= ~VIDINTCON0_INT_FRAME;
		val &= ~VIDINTCON0_INT_ENABLE;

		writel(val, regs + VIDINTCON0);
	}
}

static int fimd_dispc_probe(struct drm_device *drm_dev,
			    struct samsung_drm_dispc *dispc)
{
	struct samsung_drm_private *drm_private = drm_dev->dev_private;
	struct drm_driver *drm_driver = drm_dev->driver;

	drm_private->num_crtc++;
	drm_private->default_dispc = dispc;

	drm_driver->irq_handler = fimd_irq_handler;
	drm_driver->irq_preinstall = fimd_irq_preinstall;
	drm_driver->irq_postinstall = fimd_irq_postinstall;
	drm_driver->irq_uninstall = fimd_irq_uninstall;
	drm_driver->enable_vblank = fimd_enable_vblank;
	drm_driver->disable_vblank = fimd_disable_vblank;

	/* TODO */

	return drm_irq_install(drm_dev);
}

static int fimd_dispc_remove(struct drm_device *dev)
{
	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);
	/* TODO */
	return 0;
}

static struct samsung_drm_dispc fimd_dispc = {
	.name = "samsung_drm_fimd",
	.probe = fimd_dispc_probe,
	.remove = fimd_dispc_remove,
	.manager_ops = &fimd_manager_ops,
	.overlay_ops = &fimd_overlay_ops,
};

static int fimd_calc_clkdiv(struct fimd_data *data,
			    struct samsung_video_timings *timing)
{
	unsigned long clk = clk_get_rate(data->bus_clk);
	u32 retrace;
	u32 clkdiv;
	u32 best_framerate = 0;
	u32 framerate;

	retrace = timing->hfp + timing->hsw + timing->hbp + timing->x_res;
	retrace *= timing->vfp + timing->vsw + timing->vbp + timing->y_res;

	/* default framerate is 60Hz */
	if (!timing->framerate)
		timing->framerate = 60;

	clk /= retrace;

	for (clkdiv = 1; clkdiv < 0x100; clkdiv++) {
		int tmp;

		/* get best framerate */
		framerate = clk / clkdiv;
		tmp = timing->framerate - framerate;
		if (tmp < 0) {
			best_framerate = framerate;
			continue;
		} else {
			if (!best_framerate)
				best_framerate = framerate;
			else if (tmp < (best_framerate - framerate))
				best_framerate = framerate ;
			break;
		}
	}

	return clkdiv;
}

static void fimd_clear_win(struct fimd_data *data, int win)
{
	void __iomem *regs = data->regs;
	u32 val;

	writel(0, regs + WINCON(win));
	writel(0, regs + VIDOSD_A(win));
	writel(0, regs + VIDOSD_B(win));
	writel(0, regs + VIDOSD_C(win));

	if (win == 1 || win == 2)
		writel(0, regs + VIDOSD_D(win));

	val = readl(regs + SHADOWCON);
	val &= ~SHADOWCON_WINx_PROTECT(win);
	writel(val, regs + SHADOWCON);
}

static int __devinit fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_data *data;
	struct samsung_drm_fimd_pdata *pdata;
	struct resource *res;
	int win;
	int ret;

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(dev, "no platform data specified\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof(struct fimd_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->bus_clk = clk_get(dev, "lcd");
	if (IS_ERR(data->bus_clk)) {
		dev_err(dev, "failed to get bus clock\n");
		ret = PTR_ERR(data->bus_clk);
		goto err_data;
	}

	clk_enable(data->bus_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to find registers\n");
		ret = -ENOENT;
		goto err_clk;
	}

	data->regs_res = request_mem_region(res->start, resource_size(res),
					   dev_name(dev));
	if (!data->regs_res) {
		dev_err(dev, "failed to claim register region\n");
		ret = -ENOENT;
		goto err_clk;
	}

	data->regs = ioremap(res->start, resource_size(res));
	if (!data->regs) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_req_region;
	}

	if (pdata->setup_gpio)
		pdata->setup_gpio();

	/* clear win registers */
	for (win = 0; win < WINDOWS_NR; win++)
		fimd_clear_win(data, win);

	data->clkdiv = fimd_calc_clkdiv(data, &pdata->timing);
	data->vidcon0 = pdata->vidcon0;
	data->vidcon1 = pdata->vidcon1;

	platform_set_drvdata(pdev, data);

	memcpy(&fimd_timing, &pdata->timing,
			sizeof(struct samsung_video_timings));
	memcpy(&data->manager_data, &pdata->manager_data,
			sizeof(struct samsung_drm_manager_data));
	fimd_timing.vclk = clk_get_rate(data->bus_clk) / data->clkdiv;

	fimd_dispc.dev = dev;
	fimd_dispc.manager_data = &data->manager_data;
	samsung_drm_subdrv_register(&fimd_dispc);

	/* TODO */
	return 0;

err_req_region:
	release_resource(data->regs_res);
	kfree(data->regs_res);

err_clk:
	clk_disable(data->bus_clk);
	clk_put(data->bus_clk);

err_data:
	kfree(data);
	return ret;
}

static int __devexit fimd_remove(struct platform_device *pdev)
{
	struct fimd_data *data = platform_get_drvdata(pdev);

	printk(KERN_DEBUG "[%d] %s\n", __LINE__, __func__);

	iounmap(data->regs);

	clk_disable(data->bus_clk);
	clk_put(data->bus_clk);

	release_resource(data->regs_res);

	kfree(data);

	/* TODO */
	return 0;
}

static struct platform_driver fimd_driver = {
	.probe		= fimd_probe,
	.remove		= __devexit_p(fimd_remove),
	.driver		= {
		.name	= "samsung_drm_fimd",
		.owner	= THIS_MODULE,
	},
};

static int __init fimd_init(void)
{
	return platform_driver_register(&fimd_driver);
}

static void __exit fimd_exit(void)
{
	platform_driver_unregister(&fimd_driver);
}

subsys_initcall(fimd_init);
module_exit(fimd_exit);

MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_DESCRIPTION("Samsung DRM FIMD Driver");
MODULE_LICENSE("GPL");
