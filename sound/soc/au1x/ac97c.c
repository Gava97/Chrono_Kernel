/*
 * Au1000/Au1500/Au1100 AC97C controller driver for ASoC
 *
 * (c) 2011 Manuel Lauss <manuel.lauss@googlemail.com>
 *
 * based on the old ALSA driver by Charles Eidsness.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#include "psc.h"

/*#define AC_DEBUG*/

#define MSG(x...)	printk(KERN_ERR "ac97c: " x)
#ifdef AC_DEBUG
#define DBG(x...)	MSG(x)
#else
#define DBG(x...)	do {} while (0)
#endif

/* register offsets and bits */
#define AC97_CONFIG	0x00
#define AC97_STATUS	0x04
#define AC97_DATA	0x08
#define AC97_CMDRESP	0x0c
#define AC97_ENABLE	0x10

#define CFG_RC(x)	(((x) & 0x3ff) << 13)
#define CFG_XS(x)	(((x) & 0x3ff) << 3)
#define CFG_SG		(1 << 2)	/* sync gate */
#define CFG_SN		(1 << 1)	/* sync control */
#define CFG_RS		(1 << 0)	/* acrst# control */
#define STAT_XU		(1 << 11)	/* tx underflow */
#define STAT_XO		(1 << 10)	/* tx overflow */
#define STAT_RU		(1 << 9)	/* rx underflow */
#define STAT_RO		(1 << 8)	/* rx overflow */
#define STAT_RD		(1 << 7)	/* codec ready */
#define STAT_CP		(1 << 6)	/* command pending */
#define STAT_TE		(1 << 4)	/* tx fifo empty */
#define STAT_TF		(1 << 3)	/* tx fifo full */
#define STAT_RE		(1 << 1)	/* rx fifo empty */
#define STAT_RF		(1 << 0)	/* rx fifo full */
#define CMD_SET_DATA(x)	(((x) & 0xffff) << 16)
#define CMD_GET_DATA(x)	((x) & 0xffff)
#define CMD_READ	(1 << 7)
#define CMD_WRITE	(0 << 7)
#define CMD_IDX(x)	((x) & 0x7f)
#define EN_D		(1 << 1)	/* DISable bit */
#define EN_CE		(1 << 0)	/* clock enable bit */

/* how often to retry failed codec register reads/writes */
#define AC97_RW_RETRIES	5

#define AC97_DIR	\
	(SND_SOC_DAIDIR_PLAYBACK | SND_SOC_DAIDIR_CAPTURE)

#define AC97_RATES	\
	SNDRV_PCM_RATE_8000_44100

#define AC97_FMTS	\
	SNDRV_PCM_FMTBIT_S16_LE

struct ac97c_ctx {
	void __iomem *mmio;

	unsigned long cfg;

	struct mutex lock;	/* codec access lock */

	struct platform_device *dmapd;
};

/* instance data. There can be only one, MacLeod!!!!, fortunately there IS only
 * once AC97C on early Alchemy chips.
 */
static struct ac97c_ctx *ac97c_workdata;


#define ac97_to_ctx(x)		ac97c_workdata

static inline unsigned long RD(struct ac97c_ctx *ctx, int reg)
{
	return __raw_readl(ctx->mmio + reg);
}

static inline void WR(struct ac97c_ctx *ctx, int reg, unsigned long v)
{
	__raw_writel(v, ctx->mmio + reg);
	wmb();
}

static unsigned short au1xac97c_ac97_read(struct snd_ac97 *ac97,
					  unsigned short r)
{
	struct ac97c_ctx *ctx = ac97_to_ctx(ac97);
	unsigned int tmo, retry;
	unsigned long data;

	data = ~0;
	retry = AC97_RW_RETRIES;
	do {
		mutex_lock(&ctx->lock);

		tmo = 5;
		while ((RD(ctx, AC97_STATUS) & STAT_CP) && tmo--)
			udelay(21);	/* wait an ac97 frame time */
		if (!tmo) {
			DBG("ac97rd timeout #1\n");
			goto next;
		}

		WR(ctx, AC97_CMDRESP, CMD_IDX(r) | CMD_READ);

		/* stupid errata: data is only valid for 21us, so
		 * poll, forrest, poll...
		 */
		tmo = 0x10000;
		while ((RD(ctx, AC97_STATUS) & STAT_CP) && tmo--)
			asm volatile ("nop");
		data = RD(ctx, AC97_CMDRESP);

		if (!tmo)
			DBG("ac97rd timeout #2\n");

next:
		mutex_unlock(&ctx->lock);
	} while (--retry && !tmo);

	DBG("AC97RD %04x %04lx %d\n", r, data, retry);

	return retry ? data & 0xffff : 0xffff;
}

static void au1xac97c_ac97_write(struct snd_ac97 *ac97, unsigned short r,
				 unsigned short v)
{
	struct ac97c_ctx *ctx = ac97_to_ctx(ac97);
	unsigned int tmo, retry;

	retry = AC97_RW_RETRIES;
	do {
		mutex_lock(&ctx->lock);

		for (tmo = 5; (RD(ctx, AC97_STATUS) & STAT_CP) && tmo; tmo--)
			udelay(21);	/* wait an ac97 frame time */
		if (!tmo) {
			DBG("ac97wr timeout #1\n");
			goto next;
		}

		WR(ctx, AC97_CMDRESP, CMD_WRITE | CMD_IDX(r) | CMD_SET_DATA(v));

		for (tmo = 10; (RD(ctx, AC97_STATUS) & STAT_CP) && tmo; tmo--)
			udelay(21);	/* wait an ac97 frame time */
		if (!tmo)
			DBG("ac97wr timeout #2\n");
next:
		mutex_unlock(&ctx->lock);
	} while (--retry && !tmo);

	DBG("AC97WR %04x %04x %d\n", r, v, retry);
}

static void au1xac97c_ac97_warm_reset(struct snd_ac97 *ac97)
{
	struct ac97c_ctx *ctx = ac97_to_ctx(ac97);

	DBG("entering WARM_RESET\n");

	WR(ctx, AC97_CONFIG, ctx->cfg | CFG_SG | CFG_SN);
	msleep(20);
	WR(ctx, AC97_CONFIG, ctx->cfg | CFG_SG);
	WR(ctx, AC97_CONFIG, ctx->cfg);

	DBG("leaving WARM_RESET\n");
}

static void au1xac97c_ac97_cold_reset(struct snd_ac97 *ac97)
{
	struct ac97c_ctx *ctx = ac97_to_ctx(ac97);
	int i;

	DBG("entering COLD_RESET\n");

	WR(ctx, AC97_CONFIG, ctx->cfg | CFG_RS);
	msleep(500);
	WR(ctx, AC97_CONFIG, ctx->cfg);

	/* wait for codec ready */
	i = 1000;
	while (((RD(ctx, AC97_STATUS) & STAT_RD) == 0) && --i)
		msleep(20);
	if (!i)
		printk(KERN_ERR "ac97c: codec not ready\n");

	DBG("leaving COLD_RESET\n");
}

/* AC97 controller operations */
struct snd_ac97_bus_ops soc_ac97_ops = {
	.read		= au1xac97c_ac97_read,
	.write		= au1xac97c_ac97_write,
	.reset		= au1xac97c_ac97_cold_reset,
	.warm_reset	= au1xac97c_ac97_warm_reset,
};
EXPORT_SYMBOL_GPL(soc_ac97_ops);

static int au1xac97c_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	return 0;
}

static int au1xac97c_trigger(struct snd_pcm_substream *substream,
			     int cmd, struct snd_soc_dai *dai)
{
	return 0;
}

static int au1xac97c_dai_probe(struct snd_soc_dai *dai)
{
	return ac97c_workdata ? 0 : -ENODEV;
}

static struct snd_soc_dai_ops au1xac97c_dai_ops = {
	.trigger	= au1xac97c_trigger,
	.hw_params	= au1xac97c_hw_params,
};

static struct snd_soc_dai_driver au1xac97c_dai_driver = {
	.name			= AC97C_DAINAME,
	.ac97_control		= 1,
	.probe			= au1xac97c_dai_probe,
	.playback = {
		.rates		= AC97_RATES,
		.formats	= AC97_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.capture = {
		.rates		= AC97_RATES,
		.formats	= AC97_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.ops = &au1xac97c_dai_ops,
};

static int __devinit au1xac97c_drvprobe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;
	struct ac97c_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		ret = -ENODEV;
		goto out0;
	}

	ret = -EBUSY;
	if (!request_mem_region(r->start, resource_size(r), pdev->name))
		goto out0;

	ctx->mmio = ioremap_nocache(r->start, resource_size(r));
	if (!ctx->mmio)
		goto out1;

	/* switch it on */
	WR(ctx, AC97_ENABLE, EN_D | EN_CE);
	WR(ctx, AC97_ENABLE, EN_CE);

	ctx->cfg = CFG_RC(3) | CFG_XS(3);
	WR(ctx, AC97_CONFIG, ctx->cfg);

	platform_set_drvdata(pdev, ctx);

	ret = snd_soc_register_dai(&pdev->dev, &au1xac97c_dai_driver);
	if (ret)
		goto out1;

	ctx->dmapd = alchemy_pcm_add(pdev, 0);	/* 0 == AC97 */
	if (ctx->dmapd) {
		ac97c_workdata = ctx;
		return 0;
	}

	snd_soc_unregister_dai(&pdev->dev);
out1:
	release_mem_region(r->start, resource_size(r));
out0:
	kfree(ctx);
	return ret;
}

static int __devexit au1xac97c_drvremove(struct platform_device *pdev)
{
	struct ac97c_ctx *ctx = platform_get_drvdata(pdev);
	struct resource *r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (ctx->dmapd)
		alchemy_pcm_destroy(ctx->dmapd);

	snd_soc_unregister_dai(&pdev->dev);

	WR(ctx, AC97_ENABLE, EN_D);	/* clock off, disable */

	iounmap(ctx->mmio);
	release_mem_region(r->start, resource_size(r));
	kfree(ctx);

	ac97c_workdata = NULL;	/* MDEV */

	return 0;
}

#ifdef CONFIG_PM
static int au1xac97c_drvsuspend(struct device *dev)
{
	struct ac97c_ctx *ctx = dev_get_drvdata(dev);

	WR(ctx, AC97_ENABLE, EN_D);	/* clock off, disable */

	return 0;
}

static int au1xac97c_drvresume(struct device *dev)
{
	struct ac97c_ctx *ctx = dev_get_drvdata(dev);

	WR(ctx, AC97_ENABLE, EN_D | EN_CE);
	WR(ctx, AC97_ENABLE, EN_CE);
	WR(ctx, AC97_CONFIG, ctx->cfg);

	return 0;
}

static const struct dev_pm_ops au1xpscac97_pmops = {
	.suspend	= au1xac97c_drvsuspend,
	.resume		= au1xac97c_drvresume,
};

#define AU1XPSCAC97_PMOPS (&au1xpscac97_pmops)

#else

#define AU1XPSCAC97_PMOPS NULL

#endif

static struct platform_driver au1xac97c_driver = {
	.driver	= {
		.name	= "alchemy-ac97c",
		.owner	= THIS_MODULE,
		.pm	= AU1XPSCAC97_PMOPS,
	},
	.probe		= au1xac97c_drvprobe,
	.remove		= __devexit_p(au1xac97c_drvremove),
};

static int __init au1xac97c_load(void)
{
	ac97c_workdata = NULL;
	return platform_driver_register(&au1xac97c_driver);
}

static void __exit au1xac97c_unload(void)
{
	platform_driver_unregister(&au1xac97c_driver);
}

module_init(au1xac97c_load);
module_exit(au1xac97c_unload);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Au1000/1500/1100 AC97C ALSA ASoC audio driver");
MODULE_AUTHOR("Manuel Lauss");
