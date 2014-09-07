/*
 * Driver for the U300 pinmux
 *
 * Based on the original U300 padmux functions
 * Copyright (C) 2009 ST-Ericsson AB
 * Author: Martin Persson <martin.persson at stericsson.com>
 *
 * The DB3350 design and control registers are oriented around pads rather than
 * pins, so we enumerate the pads we can mux rather than actual pins. The pads
 * are connected to different pins in different packaging types, so it would
 * be confusing.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pinmux.h>

#include "pinmux-u300.h"

#define DRIVER_NAME "pinmux-u300"

/*
 * The DB3350 has 467 pads, I have enumerated the pads clockwise around the
 * edges of the silicon, finger by finger. LTCORNER upper left is pad 0.
 * Data taken from the PadRing chart, arranged like this:
 *
 *   0 ..... 104
 * 466        105
 *   .        .
 *   .        .
 * 358        224
 *  357 .... 225
 */
#define U300_NUM_PADS 467

struct u300_pin_desc {
	unsigned number;
	char *name;
};

#define U300_PIN(a, b) { .number = a, .name = b }

const struct u300_pin_desc u300_pins[] = {
	U300_PIN(134, "UART0 RTS"),
	U300_PIN(135, "UART0 CTS"),
	U300_PIN(136, "UART0 TX"),
	U300_PIN(137, "UART0 RX"),
	U300_PIN(166, "MMC DATA DIR LS"),
	U300_PIN(167, "MMC DATA 3"),
	U300_PIN(168, "MMC DATA 2"),
	U300_PIN(169, "MMC DATA 1"),
	U300_PIN(170, "MMC DATA 0"),
	U300_PIN(171, "MMC CMD DIR LS"),
	U300_PIN(176, "MMC CMD"),
	U300_PIN(177, "MMC CLK"),
	U300_PIN(420, "SPI CLK"),
	U300_PIN(421, "SPI DO"),
	U300_PIN(422, "SPI DI"),
	U300_PIN(423, "SPI CS0"),
	U300_PIN(424, "SPI CS1"),
	U300_PIN(425, "SPI CS2"),
};

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct u300_pmx {
	struct device *dev;
	struct pinmux_dev *pmx;
	u32 phybase;
	u32 physize;
	void __iomem *virtbase;
};

/**
 * u300_pmx_registers - the array of registers read/written for each pinmux
 * shunt setting
 */
const u32 u300_pmx_registers[] = {
	U300_SYSCON_PMC1LR,
	U300_SYSCON_PMC1HR,
	U300_SYSCON_PMC2R,
	U300_SYSCON_PMC3R,
	U300_SYSCON_PMC4R,
};

/**
 * struct pmx_onmask - mask bits to enable/disable padmux
 * @mask: mask bits to disable
 * @val: mask bits to enable
 *
 * onmask lazy dog:
 * onmask = {
 *   {"PMC1LR" mask, "PMC1LR" value},
 *   {"PMC1HR" mask, "PMC1HR" value},
 *   {"PMC2R"  mask, "PMC2R"  value},
 *   {"PMC3R"  mask, "PMC3R"  value},
 *   {"PMC4R"  mask, "PMC4R"  value}
 * }
 */
struct u300_pmx_mask {
	u16 mask;
	u16 bits;
};

/**
 * struct u300_pmx_func - describes an U300 pinmux function
 * @name: the name of this specific function
 * @pins: an array of discrete physical pins used in this mapping, taken
 *	from the global pin enumeration space
 * @num_pins: the number of pins in this mapping array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @onmask: bits to set to enable this muxing
 */
struct u300_pmx_func {
	char *name;
	const unsigned int *pins;
	const unsigned num_pins;
	const struct u300_pmx_mask *mask;
};

static const unsigned mmc0_pins[] = { 166, 167, 168, 169, 170, 171, 176, 177 };
static const unsigned spi0_pins[] = { 420, 421, 422, 423, 424, 425 };

static const struct u300_pmx_mask mmc0_mask[] = {
	{U300_SYSCON_PMC1LR_MMCSD_MASK,
	 U300_SYSCON_PMC1LR_MMCSD_MMCSD},
	{0, 0},
	{0, 0},
	{0, 0},
	{U300_SYSCON_PMC4R_APP_MISC_12_MASK,
	 U300_SYSCON_PMC4R_APP_MISC_12_APP_GPIO}
};

static const struct u300_pmx_mask spi0_mask[] = {
	{0, 0},
	{U300_SYSCON_PMC1HR_APP_SPI_2_MASK |
	 U300_SYSCON_PMC1HR_APP_SPI_CS_1_MASK |
	 U300_SYSCON_PMC1HR_APP_SPI_CS_2_MASK,
	 U300_SYSCON_PMC1HR_APP_SPI_2_SPI |
	 U300_SYSCON_PMC1HR_APP_SPI_CS_1_SPI |
	 U300_SYSCON_PMC1HR_APP_SPI_CS_2_SPI},
	{0, 0},
	{0, 0},
	{0, 0}
};

static const struct u300_pmx_func u300_pmx_funcs[] = {
	{
		.name = "mmc0",
		.pins = mmc0_pins,
		.num_pins = ARRAY_SIZE(mmc0_pins),
		.mask = mmc0_mask,
	},
	{
		.name = "spi0",
		.pins = spi0_pins,
		.num_pins = ARRAY_SIZE(spi0_pins),
		.mask = spi0_mask,
	},
};

static void u300_pmx_endisable(struct u300_pmx *upmx, unsigned selector,
			       bool enable)
{
	u16 regval, val, mask;
	int i;

	for (i = 0; i < ARRAY_SIZE(u300_pmx_registers); i++) {
		if (enable)
			val = u300_pmx_funcs[selector].mask->bits;
		else
			val = 0;

		mask = u300_pmx_funcs[selector].mask->mask;
		if (mask != 0) {
			regval = readw(upmx->virtbase + u300_pmx_registers[i]);
			regval &= ~mask;
			regval |= val;
			writew(regval, upmx->virtbase + u300_pmx_registers[i]);
		}
	}
}

static int u300_pmx_enable(struct pinmux_dev *pmxdev, unsigned selector)
{
	struct u300_pmx *upmx;

	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return -EINVAL;
	upmx = pmxdev_get_drvdata(pmxdev);
	u300_pmx_endisable(upmx, selector, true);

	return 0;
}

static void u300_pmx_disable(struct pinmux_dev *pmxdev, unsigned selector)
{
	struct u300_pmx *upmx;

	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return;
	upmx = pmxdev_get_drvdata(pmxdev);
	u300_pmx_endisable(upmx, selector, false);
}

static int u300_pmx_list(struct pinmux_dev *pmxdev, unsigned selector)
{
	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return -EINVAL;
	return 0;
}

static const char *u300_pmx_get_fname(struct pinmux_dev *pmxdev,
				      unsigned selector)
{
	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return NULL;
	return u300_pmx_funcs[selector].name;
}

static int u300_pmx_get_pins(struct pinmux_dev *pmxdev, unsigned selector,
			     unsigned ** const pins, unsigned * const num_pins)
{
	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return -EINVAL;
	*pins = (unsigned *) u300_pmx_funcs[selector].pins;
	*num_pins = u300_pmx_funcs[selector].num_pins;
	return 0;
}

static void u300_dbg_show(struct pinmux_dev *pmxdev, struct seq_file *s,
		   unsigned offset)
{
	int i;

	seq_printf(s, " " DRIVER_NAME);
	seq_printf(s, " :");
	for (i = 0; i < ARRAY_SIZE(u300_pins); i++) {
		if (u300_pins[i].number == offset) {
			seq_printf(s, " %s", u300_pins[i].name);
			break;
		}
	}
	if (i == ARRAY_SIZE(u300_pins))
		seq_printf(s, " (unknown function)");
}

static struct pinmux_ops u300_pmx_ops = {
	.list_functions = u300_pmx_list,
	.get_function_name = u300_pmx_get_fname,
	.get_function_pins = u300_pmx_get_pins,
	.enable = u300_pmx_enable,
	.disable = u300_pmx_disable,
	.dbg_show = u300_dbg_show,
};

static struct pinmux_desc u300_pmx_desc = {
	.name = DRIVER_NAME,
	.ops = &u300_pmx_ops,
	.owner = THIS_MODULE,
	.base = 0,
	.npins = U300_NUM_PADS,
};

static int __init u300_pmx_probe(struct platform_device *pdev)
{
	int ret;
	struct u300_pmx *upmx;
	struct resource *res;

	upmx = kzalloc(sizeof(struct u300_pmx), GFP_KERNEL);
	if (!upmx)
		return -ENOMEM;

	upmx->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOENT;
		goto out_no_resource;
	}
	upmx->phybase = res->start;
	upmx->physize = resource_size(res);

	if (request_mem_region(upmx->phybase, upmx->physize,
			       DRIVER_NAME) == NULL) {
		ret = -EBUSY;
		goto out_no_memregion;
	}

	upmx->virtbase = ioremap(upmx->phybase, upmx->physize);
	if (!upmx->virtbase) {
		ret = -ENOMEM;
		goto out_no_remap;
	}

	upmx->pmx = pinmux_register(&u300_pmx_desc, &pdev->dev, upmx);
	if (IS_ERR(upmx->pmx)) {
		ret = PTR_ERR(upmx->pmx);
		goto out_no_pmx;
	}
	platform_set_drvdata(pdev, upmx);

	dev_info(&pdev->dev, "initialized U300 pinmux driver\n");

	return 0;

out_no_pmx:
	iounmap(upmx->virtbase);
out_no_remap:
	platform_set_drvdata(pdev, NULL);
out_no_memregion:
	release_mem_region(upmx->phybase, SZ_4K);
out_no_resource:
	kfree(upmx);
	return ret;
}

static int __exit u300_pmx_remove(struct platform_device *pdev)
{
	struct u300_pmx *upmx = platform_get_drvdata(pdev);

	if (upmx) {
		pinmux_unregister(upmx->pmx);
		iounmap(upmx->virtbase);
		release_mem_region(upmx->phybase, upmx->physize);
		platform_set_drvdata(pdev, NULL);
		kfree(upmx);
	}

	return 0;
}

static struct platform_driver u300_pmx_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.remove = __exit_p(u300_pmx_remove),
};

static int __init u300_pmx_init(void)
{
	return platform_driver_probe(&u300_pmx_driver, u300_pmx_probe);
}
arch_initcall(u300_pmx_init);

static void __exit u300_pmx_exit(void)
{
	platform_driver_unregister(&u300_pmx_driver);
}
module_exit(u300_pmx_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij at linaro.org>");
MODULE_DESCRIPTION("U300 Padmux driver");
MODULE_LICENSE("GPL v2");
