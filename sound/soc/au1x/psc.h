/*
 * Alchemy ALSA ASoC audio support.
 *
 * (c) 2007-2011 MSC Vertriebsges.m.b.H.,
 *	Manuel Lauss <manuel.lauss@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _AU1X_PCM_H
#define _AU1X_PCM_H

#define PCM_TX	0
#define PCM_RX	1

#define SUBSTREAM_TYPE(substream) \
	((substream)->stream == SNDRV_PCM_STREAM_PLAYBACK ? PCM_TX : PCM_RX)


/* AC97C/I2SC DMA helpers */
extern  struct platform_device *alchemy_pcm_add(struct platform_device *pdev,
						int type);
extern void alchemy_pcm_destroy(struct platform_device *dmapd);

/* Au1000 AC97C/I2SC DAI names. Required to get at correct DMA instance */
#define AC97C_DAINAME	"alchemy-ac97c"
#define I2SC_DAINAME	"alchemy-i2sc"
#define AC97C_DMANAME	"alchemy-pcm-ac97"
#define I2SC_DMANAME	"alchemy-pcm-i2s"


/* PSC/DBDMA helpers */
extern struct platform_device *au1xpsc_pcm_add(struct platform_device *pdev);
extern void au1xpsc_pcm_destroy(struct platform_device *dmapd);

struct au1xpsc_audio_data {
	void __iomem *mmio;

	unsigned long cfg;
	unsigned long rate;

	struct snd_soc_dai_driver dai_drv;

	unsigned long pm[2];
	struct mutex lock;
	struct platform_device *dmapd;
};

/* easy access macros */
#define PSC_CTRL(x)	((unsigned long)((x)->mmio) + PSC_CTRL_OFFSET)
#define PSC_SEL(x)	((unsigned long)((x)->mmio) + PSC_SEL_OFFSET)
#define I2S_STAT(x)	((unsigned long)((x)->mmio) + PSC_I2SSTAT_OFFSET)
#define I2S_CFG(x)	((unsigned long)((x)->mmio) + PSC_I2SCFG_OFFSET)
#define I2S_PCR(x)	((unsigned long)((x)->mmio) + PSC_I2SPCR_OFFSET)
#define AC97_CFG(x)	((unsigned long)((x)->mmio) + PSC_AC97CFG_OFFSET)
#define AC97_CDC(x)	((unsigned long)((x)->mmio) + PSC_AC97CDC_OFFSET)
#define AC97_EVNT(x)	((unsigned long)((x)->mmio) + PSC_AC97EVNT_OFFSET)
#define AC97_PCR(x)	((unsigned long)((x)->mmio) + PSC_AC97PCR_OFFSET)
#define AC97_RST(x)	((unsigned long)((x)->mmio) + PSC_AC97RST_OFFSET)
#define AC97_STAT(x)	((unsigned long)((x)->mmio) + PSC_AC97STAT_OFFSET)

#endif
