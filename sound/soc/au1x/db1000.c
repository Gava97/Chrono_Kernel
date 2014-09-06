/*
 * DB1000/DB1500/DB1100 ASoC audio fabric support code.
 *
 * (c) 2011 Manuel Lauss <manuel.lauss@googlemail.com>
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>

#include "psc.h"

/*-------------------------  AC97 PART  ---------------------------*/

static struct snd_soc_dai_link db1000_ac97_dai = {
	.name		= "AC97",
	.stream_name	= "AC97 HiFi",
	.codec_dai_name	= "ac97-hifi",
	.cpu_dai_name	= AC97C_DAINAME,
	.platform_name	= AC97C_DMANAME,
	.codec_name	= "ac97-codec",
};

static struct snd_soc_card db1000_ac97_machine = {
	.name		= "DB1000_AC97",
	.dai_link	= &db1000_ac97_dai,
	.num_links	= 1,
};

/*-------------------------  COMMON PART  ---------------------------*/

static struct platform_device *db1000_asoc97_dev;

static int __init db1000_audio_load(void)
{
	int ret, id;

	/* impostor check */
	id = BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI));

	ret = -ENOMEM;
	db1000_asoc97_dev = platform_device_alloc("soc-audio", 0);
	if (!db1000_asoc97_dev)
		goto out;

	platform_set_drvdata(db1000_asoc97_dev, &db1000_ac97_machine);
	ret = platform_device_add(db1000_asoc97_dev);

	if (ret) {
		platform_device_put(db1000_asoc97_dev);
		db1000_asoc97_dev = NULL;
	}
out:
	return ret;
}

static void __exit db1000_audio_unload(void)
{
	platform_device_unregister(db1000_asoc97_dev);
}

module_init(db1000_audio_load);
module_exit(db1000_audio_unload);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DB1000/DB1500/DB1100 ASoC audio support");
MODULE_AUTHOR("Manuel Lauss");
