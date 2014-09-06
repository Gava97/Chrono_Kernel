/*
 * Au1000/Au1500/Au1100 I2S controller driver for ASoC
 *
 * (c) 2011 Manuel Lauss <manuel.lauss@googlemail.com>
 *
 * Note: clock supplied to the I2S controller must be 256x samplerate.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/mach-au1x00/au1000.h>

#include "psc.h"

#define I2S_DATA	0x00
#define I2S_CONFIG	0x04
#define I2S_ENABLE	0x08

#define CFG_XU		(1 << 25)	/* tx underflow */
#define CFG_XO		(1 << 24)
#define CFG_RU		(1 << 23)
#define CFG_RO		(1 << 22)
#define CFG_TR		(1 << 21)
#define CFG_TE		(1 << 20)
#define CFG_TF		(1 << 19)
#define CFG_RR		(1 << 18)
#define CFG_RF		(1 << 17)
#define CFG_ICK		(1 << 12)	/* clock invert */
#define CFG_PD		(1 << 11)	/* set to make I2SDIO INPUT */
#define CFG_LB		(1 << 10)	/* loopback */
#define CFG_IC		(1 << 9)	/* word select invert */
#define CFG_FM_I2S	(0 << 7)	/* I2S format */
#define CFG_FM_LJ	(1 << 7)	/* left-justified */
#define CFG_FM_RJ	(2 << 7)	/* right-justified */
#define CFG_FM_MASK	(3 << 7)
#define CFG_TN		(1 << 6)	/* tx fifo en */
#define CFG_RN		(1 << 5)	/* rx fifo en */
#define CFG_SZ_8	(0x08)
#define CFG_SZ_16	(0x10)
#define CFG_SZ_18	(0x12)
#define CFG_SZ_20	(0x14)
#define CFG_SZ_24	(0x18)
#define CFG_SZ_MASK	(0x1f)
#define EN_D		(1 << 1)	/* DISable */
#define EN_CE		(1 << 0)	/* clock enable */

/* supported I2S DAI hardware formats */
#define AU1XI2SC_DAIFMT \
	(SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_LEFT_J |	\
	 SND_SOC_DAIFMT_NB_NF)

/* supported I2S direction */
#define AU1XI2SC_DIR \
	(SND_SOC_DAIDIR_PLAYBACK | SND_SOC_DAIDIR_CAPTURE)

#define AU1XI2SC_RATES \
	SNDRV_PCM_RATE_8000_192000

#define AU1XI2SC_FMTS \
	SNDRV_PCM_FMTBIT_S16_LE

struct i2sc_ctx {
	void __iomem *mmio;
	unsigned long cfg, rate;
	struct platform_device *dmapd;
};

static inline unsigned long RD(struct i2sc_ctx *ctx, int reg)
{
	return __raw_readl(ctx->mmio + reg);
}

static inline void WR(struct i2sc_ctx *ctx, int reg, unsigned long v)
{
	__raw_writel(v, ctx->mmio + reg);
	wmb();
}

static int au1xi2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct i2sc_ctx *ctx = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long c;
	int ret;

	ret = -EINVAL;
	c = ctx->cfg;

	c &= ~CFG_FM_MASK;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		c |= CFG_FM_I2S;	/* enable I2S mode */
		break;
	case SND_SOC_DAIFMT_MSB:
		c |= CFG_FM_RJ;
		break;
	case SND_SOC_DAIFMT_LSB:
		c |= CFG_FM_LJ;
		break;
	default:
		goto out;
	}

	c &= ~(CFG_IC | CFG_ICK);		/* IB-IF */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		c |= CFG_IC | CFG_ICK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		c |= CFG_IC;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		c |= CFG_ICK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		goto out;
	}

	/* I2S controller only supports master */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:	/* CODEC slave */
		break;
	default:
		goto out;
	}

	ret = 0;
	ctx->cfg = c;
out:
	return ret;
}

static int au1xi2s_trigger(struct snd_pcm_substream *substream,
			   int cmd, struct snd_soc_dai *dai)
{
	struct i2sc_ctx *ctx = snd_soc_dai_get_drvdata(dai);
	int stype = SUBSTREAM_TYPE(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		ctx->cfg |= (stype == PCM_TX) ? CFG_TN : CFG_RN;
		WR(ctx, I2S_CONFIG, ctx->cfg);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ctx->cfg &= ~((stype == PCM_TX) ? CFG_TN : CFG_RN);
		WR(ctx, I2S_CONFIG, ctx->cfg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned long msbits_to_reg(int msbits)
{
	switch (msbits) {
	case 8:  return CFG_SZ_8;
	case 16: return CFG_SZ_16;
	case 18: return CFG_SZ_18;
	case 20: return CFG_SZ_20;
	case 24: return CFG_SZ_24;
	}
	return 0;
}

static int au1xi2s_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct i2sc_ctx *ctx = snd_soc_dai_get_drvdata(dai);
	unsigned long stat, v;

	v = msbits_to_reg(params->msbits);
	/* check if the PSC is already streaming data */
	stat = RD(ctx, I2S_CONFIG);
	if (stat & (CFG_TN | CFG_RN)) {
		/* reject parameters not currently set up in hardware */
		if ((ctx->rate != params_rate(params)) ||
		    ((stat & CFG_SZ_MASK) != v))
			return -EINVAL;
	} else {
		/* set sample bitdepth */
		ctx->cfg &= ~CFG_SZ_MASK;
		if (v)
			ctx->cfg |= v;
		else
			return -EINVAL;
		/* remember current rate for other stream */
		ctx->rate = params_rate(params);
	}
	return 0;
}

static struct snd_soc_dai_ops au1xi2s_dai_ops = {
	.trigger	= au1xi2s_trigger,
	.hw_params	= au1xi2s_hw_params,
	.set_fmt	= au1xi2s_set_fmt,
};

static struct snd_soc_dai_driver au1xi2s_dai_driver = {
	.playback = {
		.rates		= AU1XI2SC_RATES,
		.formats	= AU1XI2SC_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.capture = {
		.rates		= AU1XI2SC_RATES,
		.formats	= AU1XI2SC_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.ops = &au1xi2s_dai_ops,
};

static int __devinit au1xi2s_drvprobe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;
	struct i2sc_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

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
	WR(ctx, I2S_ENABLE, EN_D | EN_CE);
	WR(ctx, I2S_ENABLE, EN_CE);

	ctx->cfg = CFG_FM_I2S | CFG_SZ_16;
	WR(ctx, I2S_CONFIG, ctx->cfg);

	platform_set_drvdata(pdev, ctx);

	ret = snd_soc_register_dai(&pdev->dev, &au1xi2s_dai_driver);
	if (ret)
		goto out1;

	ctx->dmapd = alchemy_pcm_add(pdev, 1);	/* 1 == I2S */
	if (ctx->dmapd)
		return 0;

	snd_soc_unregister_dai(&pdev->dev);
out1:
	release_mem_region(r->start, resource_size(r));
out0:
	kfree(ctx);
	return ret;
}

static int __devexit au1xi2s_drvremove(struct platform_device *pdev)
{
	struct i2sc_ctx *ctx = platform_get_drvdata(pdev);
	struct resource *r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (ctx->dmapd)
		alchemy_pcm_destroy(ctx->dmapd);

	snd_soc_unregister_dai(&pdev->dev);

	WR(ctx, I2S_ENABLE, EN_D);	/* clock off, disable */

	iounmap(ctx->mmio);
	release_mem_region(r->start, resource_size(r));
	kfree(ctx);

	return 0;
}

#ifdef CONFIG_PM
static int au1xi2s_drvsuspend(struct device *dev)
{
	struct i2sc_ctx *ctx = dev_get_drvdata(dev);

	WR(ctx, I2S_ENABLE, EN_D);	/* clock off, disable */

	return 0;
}

static int au1xi2s_drvresume(struct device *dev)
{
	struct i2sc_ctx *ctx = dev_get_drvdata(dev);

	WR(ctx, I2S_ENABLE, EN_D | EN_CE);
	WR(ctx, I2S_ENABLE, EN_CE);
	WR(ctx, I2S_CONFIG, ctx->cfg);

	return 0;
}

static const struct dev_pm_ops au1xpscac97_pmops = {
	.suspend	= au1xi2s_drvsuspend,
	.resume		= au1xi2s_drvresume,
};

#define AU1XPSCAC97_PMOPS (&au1xpscac97_pmops)

#else

#define AU1XPSCAC97_PMOPS NULL

#endif

static struct platform_driver au1xi2s_driver = {
	.driver	= {
		.name	= "alchemy-ac97c",
		.owner	= THIS_MODULE,
		.pm	= AU1XPSCAC97_PMOPS,
	},
	.probe		= au1xi2s_drvprobe,
	.remove		= __devexit_p(au1xi2s_drvremove),
};

static int __init au1xi2s_load(void)
{
	return platform_driver_register(&au1xi2s_driver);
}

static void __exit au1xi2s_unload(void)
{
	platform_driver_unregister(&au1xi2s_driver);
}

module_init(au1xi2s_load);
module_exit(au1xi2s_unload);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Au1000/1500/1100 AC97C ALSA ASoC audio driver");
MODULE_AUTHOR("Manuel Lauss");
