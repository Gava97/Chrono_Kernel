/*
 * pinmux driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#define DRIVER_NAME "pinmux-sirf"

#define SIRFSOC_NUM_PADS    622
#define SIRFSOC_GPIO_PAD_EN(g) ((g)*0x100 + 0x84)
#define SIRFSOC_RSC_PIN_MUX 0x4

/*
 * pad list for the pinmux subsystem
 * refer to CS-131858-DC-6A.xls
 */
const struct pinctrl_pin_desc __refdata sirfsoc_pads[] = {
	PINCTRL_PIN(4, "pwm0"),
	PINCTRL_PIN(5, "pwm1"),
	PINCTRL_PIN(6, "pwm2"),
	PINCTRL_PIN(7, "pwm3"),
	PINCTRL_PIN(8, "warm_rst_b"),
	PINCTRL_PIN(9, "odo_0"),
	PINCTRL_PIN(10, "odo_1"),
	PINCTRL_PIN(11, "dr_dir"),
	PINCTRL_PIN(13, "scl_1"),
	PINCTRL_PIN(15, "sda_1"),
	PINCTRL_PIN(16, "x_ldd[16]"),
	PINCTRL_PIN(17, "x_ldd[17]"),
	PINCTRL_PIN(18, "x_ldd[18]"),
	PINCTRL_PIN(19, "x_ldd[19]"),
	PINCTRL_PIN(20, "x_ldd[20]"),
	PINCTRL_PIN(21, "x_ldd[21]"),
	PINCTRL_PIN(22, "x_ldd[22]"),
	PINCTRL_PIN(23, "x_ldd[23], lcdrom_frdy"),
	PINCTRL_PIN(24, "gps_sgn"),
	PINCTRL_PIN(25, "gps_mag"),
	PINCTRL_PIN(26, "gps_clk"),
	PINCTRL_PIN(27,	"sd_cd_b_1"),
	PINCTRL_PIN(28, "sd_vcc_on_1"),
	PINCTRL_PIN(29, "sd_wp_b_1"),
	PINCTRL_PIN(30, "sd_clk_3"),
	PINCTRL_PIN(31, "sd_cmd_3"),

	PINCTRL_PIN(32, "x_sd_dat_3[0]"),
	PINCTRL_PIN(33, "x_sd_dat_3[1]"),
	PINCTRL_PIN(34, "x_sd_dat_3[2]"),
	PINCTRL_PIN(35, "x_sd_dat_3[3]"),
	PINCTRL_PIN(36, "x_sd_clk_4"),
	PINCTRL_PIN(37, "x_sd_cmd_4"),
	PINCTRL_PIN(38, "x_sd_dat_4[0]"),
	PINCTRL_PIN(39, "x_sd_dat_4[1]"),
	PINCTRL_PIN(40, "x_sd_dat_4[2]"),
	PINCTRL_PIN(41, "x_sd_dat_4[3]"),
	PINCTRL_PIN(42, "x_cko_1"),
	PINCTRL_PIN(43, "x_ac97_bit_clk"),
	PINCTRL_PIN(44, "x_ac97_dout"),
	PINCTRL_PIN(45, "x_ac97_din"),
	PINCTRL_PIN(46, "x_ac97_sync"),
	PINCTRL_PIN(47, "x_txd_1"),
	PINCTRL_PIN(48, "x_txd_2"),
	PINCTRL_PIN(49, "x_rxd_1"),
	PINCTRL_PIN(50, "x_rxd_2"),
	PINCTRL_PIN(51, "x_usclk_0"),
	PINCTRL_PIN(52, "x_utxd_0"),
	PINCTRL_PIN(53, "x_urxd_0"),
	PINCTRL_PIN(54, "x_utfs_0"),
	PINCTRL_PIN(55, "x_urfs_0"),
	PINCTRL_PIN(56, "x_usclk_1"),
	PINCTRL_PIN(57, "x_utxd_1"),
	PINCTRL_PIN(58, "x_urxd_1"),
	PINCTRL_PIN(59, "x_utfs_1"),
	PINCTRL_PIN(60, "x_urfs_1"),
	PINCTRL_PIN(61, "x_usclk_2"),
	PINCTRL_PIN(62, "x_utxd_2"),
	PINCTRL_PIN(63, "x_urxd_2"),

	PINCTRL_PIN(64, "x_utfs_2"),
	PINCTRL_PIN(65, "x_urfs_2"),
	PINCTRL_PIN(66, "x_df_we_b"),
	PINCTRL_PIN(67, "x_df_re_b"),
	PINCTRL_PIN(68, "x_txd_0"),
	PINCTRL_PIN(69, "x_rxd_0"),
	PINCTRL_PIN(78, "x_cko_0"),
	PINCTRL_PIN(79, "x_vip_pxd[7]"),
	PINCTRL_PIN(80, "x_vip_pxd[6]"),
	PINCTRL_PIN(81, "x_vip_pxd[5]"),
	PINCTRL_PIN(82, "x_vip_pxd[4]"),
	PINCTRL_PIN(83, "x_vip_pxd[3]"),
	PINCTRL_PIN(84, "x_vip_pxd[2]"),
	PINCTRL_PIN(85, "x_vip_pxd[1]"),
	PINCTRL_PIN(86, "x_vip_pxd[0]"),
	PINCTRL_PIN(87, "x_vip_vsync"),
	PINCTRL_PIN(88, "x_vip_hsync"),
	PINCTRL_PIN(89, "x_vip_pxclk"),
	PINCTRL_PIN(90, "x_sda_0"),
	PINCTRL_PIN(91, "x_scl_0"),
	PINCTRL_PIN(92, "x_df_ry_by"),
	PINCTRL_PIN(93, "x_df_cs_b[1]"),
	PINCTRL_PIN(94, "x_df_cs_b[0]"),
	PINCTRL_PIN(95, "x_l_pclk"),

	PINCTRL_PIN(96, "x_l_lck"),
	PINCTRL_PIN(97, "x_l_fck"),
	PINCTRL_PIN(98, "x_l_de"),
	PINCTRL_PIN(99, "x_ldd[0]"),
	PINCTRL_PIN(100, "x_ldd[1]"),
	PINCTRL_PIN(101, "x_ldd[2]"),
	PINCTRL_PIN(102, "x_ldd[3]"),
	PINCTRL_PIN(103, "x_ldd[4]"),
	PINCTRL_PIN(104, "x_ldd[5]"),
	PINCTRL_PIN(105, "x_ldd[6]"),
	PINCTRL_PIN(106, "x_ldd[7]"),
	PINCTRL_PIN(107, "x_ldd[8]"),
	PINCTRL_PIN(108, "x_ldd[9]"),
	PINCTRL_PIN(109, "x_ldd[10]"),
	PINCTRL_PIN(110, "x_ldd[11]"),
	PINCTRL_PIN(111, "x_ldd[12]"),
	PINCTRL_PIN(112, "x_ldd[13]"),
	PINCTRL_PIN(113, "x_ldd[14]"),
	PINCTRL_PIN(114, "x_ldd[15]"),
};

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct sirfsoc_pmx {
	struct device *dev;
	struct pinctrl_dev *pmx;
	void __iomem *gpio_virtbase;
	void __iomem *rsc_virtbase;
};

/* SIRFSOC_GPIO_PAD_EN set */
struct sirfsoc_muxmask {
	unsigned long group;
	unsigned long mask;
};

struct sirfsoc_padmux {
	unsigned long muxmask_counts;
	const struct sirfsoc_muxmask *muxmask;
	/* RSC_PIN_MUX set */
	unsigned long funcmask;
	unsigned long funcval;
};

/**
 * struct sirfsoc_pinmux_func - describes a SIRFSOC pinmux function
 * @name: the name of this specific function
 * @pins: an array of discrete physical pins used in this mapping, taken
 *	from the global pin enumeration space
 * @num_pins: the number of pins in this mapping array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @padmux: registers set for required pad mux
 */
struct sirfsoc_pinmux_func {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
	const struct sirfsoc_padmux *padmux;
};

static const struct sirfsoc_muxmask lcd_16bits_sirfsoc_muxmask[] = {
	{
		.group = 3,
		.mask = 0x7FFFF,
	}, {
		.group = 2,
		.mask = 1 << 31,
	},
};

static const struct sirfsoc_padmux lcd_16bits_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcd_16bits_sirfsoc_muxmask),
	.muxmask = lcd_16bits_sirfsoc_muxmask,
	.funcmask = 1 << 4,
	.funcval = 0 << 4,
};

static const unsigned lcd_16bits_pins[] = { 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114 };

static const struct sirfsoc_muxmask lcd_18bits_muxmask[] = {
	{
		.group = 3,
		.mask = 0x7FFFF,
	}, {
		.group = 2,
		.mask = 1 << 31,
	}, {
		.group = 0,
		.mask = (1 << 16) | (1 << 17),
	},
};

static const struct sirfsoc_padmux lcd_18bits_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcd_18bits_muxmask),
	.muxmask = lcd_18bits_muxmask,
	.funcmask = 1 << 4,
	.funcval = 0 << 4,
};

static const unsigned lcd_18bits_pins[] = { 16, 17, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114};

static const struct sirfsoc_muxmask lcd_24bits_muxmask[] = {
	{
		.group = 3,
		.mask = 0x7FFFF,
	}, {
		.group = 2,
		.mask = 1 << 31,
	}, {
		.group = 0,
		.mask = 0xFF0000,
	},
};

static const struct sirfsoc_padmux lcd_24bits_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcd_24bits_muxmask),
	.muxmask = lcd_24bits_muxmask,
	.funcmask = 1 << 4,
	.funcval = 0 << 4,
};

static const unsigned lcd_24bits_pins[] = { 16, 17, 18, 19, 20, 21, 22, 23, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114 };

static const struct sirfsoc_muxmask lcdrom_muxmask[] = {
	{
		.group = 3,
		.mask = 0x7FFFF,
	}, {
		.group = 2,
		.mask = 1 << 31,
	}, {
		.group = 0,
		.mask = 1 << 23,
	},
};

static const struct sirfsoc_padmux lcdrom_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcdrom_muxmask),
	.muxmask = lcdrom_muxmask,
	.funcmask = 1 << 4,
	.funcval = 1 << 4,
};

static const unsigned lcdrom_pins[] = { 23, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114 };

static const struct sirfsoc_muxmask uart1_muxmask[] = {
	{
		.group = 1,
		.mask = (1 << 15) | (1 << 17),
	},
};

static const struct sirfsoc_padmux uart1_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart1_muxmask),
	.muxmask = uart1_muxmask,
};

static const unsigned uart1_pins[] = { 47, 49 };

static const struct sirfsoc_muxmask uart2_muxmask[] = {
	{
		.group = 1,
		.mask = (1 << 16) | (1 << 18) | (1 << 24) | (1 << 27),
	},
};

static const struct sirfsoc_padmux uart2_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart2_muxmask),
	.muxmask = uart2_muxmask,
	.funcmask = 1 << 10,
	.funcval = 1 << 10,
};

static const unsigned uart2_pins[] = { 48, 50, 56, 59 };

static const struct sirfsoc_muxmask uart2_nostreamctrl_muxmask[] = {
	{
		.group = 1,
		.mask = (1 << 16) | (1 << 18),
	},
};

static const struct sirfsoc_padmux uart2_nostreamctrl_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart2_nostreamctrl_muxmask),
	.muxmask = uart2_nostreamctrl_muxmask,
};

static const unsigned uart2_nostreamctrl_pins[] = { 48, 50 };

static const struct sirfsoc_pinmux_func sirfsoc_pinmux_funcs[] = {
	{
		.name = "lcd_16bits",
		.pins = lcd_16bits_pins,
		.num_pins = ARRAY_SIZE(lcd_16bits_pins),
		.padmux = &lcd_16bits_padmux,
	}, {
		.name = "lcd_18bits",
		.pins = lcd_18bits_pins,
		.num_pins = ARRAY_SIZE(lcd_18bits_pins),
		.padmux = &lcd_18bits_padmux,
	}, {
		.name = "lcd_24bits",
		.pins = lcd_24bits_pins,
		.num_pins = ARRAY_SIZE(lcd_24bits_pins),
		.padmux = &lcd_24bits_padmux,
	}, {
		.name = "lcdrom",
		.pins = lcdrom_pins,
		.num_pins = ARRAY_SIZE(lcdrom_pins),
		.padmux = &lcdrom_padmux,
	}, {
		.name = "uart1",
			.pins = uart1_pins,
			.num_pins = ARRAY_SIZE(uart1_pins),
			.padmux = &lcdrom_padmux,
	}, {
		.name = "uart2",
			.pins = uart2_pins,
			.num_pins = ARRAY_SIZE(uart2_pins),
			.padmux = &uart2_padmux,
	}, {
		.name = "uart2_nostreamctrl",
			.pins = uart2_nostreamctrl_pins,
			.num_pins = ARRAY_SIZE(uart2_nostreamctrl_pins),
			.padmux = &uart2_nostreamctrl_padmux,
	},
};

static void sirfsoc_pinmux_endisable(struct sirfsoc_pmx *upmx, unsigned selector,
	bool enable)
{
	int i;
	const struct sirfsoc_padmux *mux = sirfsoc_pinmux_funcs[selector].padmux;
	const struct sirfsoc_muxmask *mask = mux->muxmask;

	for (i = 0; i < mux->muxmask_counts; i++) {
		u32 muxval;
		muxval = readl(upmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
		if (enable)
			muxval = muxval & ~mask[i].mask;
		else
			muxval = muxval | mask[i].mask;
		writel(muxval, upmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
	}

	if (mux->funcmask && enable) {
		u32 func_en_val;
		func_en_val =
			readl(upmx->rsc_virtbase + SIRFSOC_RSC_PIN_MUX);
		func_en_val =
			(func_en_val & (~(mux->funcmask))) | (mux->
				funcval);
		writel(func_en_val, upmx->rsc_virtbase + SIRFSOC_RSC_PIN_MUX);
	}
}

static int sirfsoc_pinmux_enable(struct pinctrl_dev *pmxdev, unsigned selector)
{
	struct sirfsoc_pmx *upmx;

	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return -EINVAL;
	upmx = pctldev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(upmx, selector, true);

	return 0;
}

static void sirfsoc_pinmux_disable(struct pinctrl_dev *pmxdev, unsigned selector)
{
	struct sirfsoc_pmx *upmx;

	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return;
	upmx = pctldev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(upmx, selector, false);
}

static int sirfsoc_pinmux_list(struct pinctrl_dev *pmxdev, unsigned selector)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return -EINVAL;
	return 0;
}

static const char *sirfsoc_pinmux_get_fname(struct pinctrl_dev *pmxdev,
	unsigned selector)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return NULL;
	return sirfsoc_pinmux_funcs[selector].name;
}

static int sirfsoc_pinmux_get_pins(struct pinctrl_dev *pmxdev, unsigned selector,
	unsigned ** const pins, unsigned * const num_pins)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return -EINVAL;
	*pins = (unsigned *) sirfsoc_pinmux_funcs[selector].pins;
	*num_pins = sirfsoc_pinmux_funcs[selector].num_pins;
	return 0;
}

static void sirfsoc_dbg_show(struct pinctrl_dev *pmxdev, struct seq_file *s,
	unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static int sirfsoc_pinmux_request_gpio(struct pinctrl_dev *pmxdev, unsigned offset)
{
	struct sirfsoc_pmx *upmx;

	int group = offset / 32;

	u32 muxval;

	upmx = pctldev_get_drvdata(pmxdev);

	muxval = readl(upmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(group));
	muxval = muxval | (1 << (offset % 32));
	writel(muxval, upmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(group));

	return 0;
}

static struct pinmux_ops sirfsoc_pinmux_ops = {
	.list_functions = sirfsoc_pinmux_list,
	.get_function_name = sirfsoc_pinmux_get_fname,
	.get_function_pins = sirfsoc_pinmux_get_pins,
	.enable = sirfsoc_pinmux_enable,
	.disable = sirfsoc_pinmux_disable,
	.dbg_show = sirfsoc_dbg_show,
	.gpio_request_enable = sirfsoc_pinmux_request_gpio,
};

static struct pinctrl_desc sirfsoc_pinmux_desc = {
	.name = DRIVER_NAME,
	.pins = sirfsoc_pads,
	.npins = ARRAY_SIZE(sirfsoc_pads),
	.maxpin = SIRFSOC_NUM_PADS - 1,
	.pmxops = &sirfsoc_pinmux_ops,
	.owner = THIS_MODULE,
};

static void __iomem *sirfsoc_rsc_of_iomap(void)
{
	const struct of_device_id rsc_ids[]  = {
		{ .compatible = "sirf,prima2-rsc" },
		{}
	};
	struct device_node *np;

	np = of_find_matching_node(NULL, rsc_ids);
	if (!np)
		panic("unable to find compatible rsc node in dtb\n");

	return of_iomap(np, 0);
}

static int __devinit sirfsoc_pinmux_probe(struct platform_device *pdev)
{
	int ret;
	struct sirfsoc_pmx *upmx;
	struct device_node *np = pdev->dev.of_node;

	/* Create state holders etc for this driver */
	upmx = kzalloc(sizeof(struct sirfsoc_pmx), GFP_KERNEL);
	if (!upmx)
		return -ENOMEM;

	upmx->dev = &pdev->dev;

	platform_set_drvdata(pdev, upmx);

	upmx->gpio_virtbase = of_iomap(np, 0);
	if (!upmx->gpio_virtbase) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "can't map gpio registers\n");
		goto out_no_gpio_remap;
	}

	upmx->rsc_virtbase = sirfsoc_rsc_of_iomap();
	if (!upmx->rsc_virtbase) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "can't map rsc registers\n");
		goto out_no_rsc_remap;
	}

	/* Now register the pin controller and all pins it handles */
	upmx->pmx = pinctrl_register(&sirfsoc_pinmux_desc, &pdev->dev, upmx);
	if (IS_ERR(upmx->pmx)) {
		dev_err(&pdev->dev, "could not register SIRFSOC pinmux driver\n");
		ret = PTR_ERR(upmx->pmx);
		goto out_no_pmx;
	}

	dev_info(&pdev->dev, "initialized SIRFSOC pinmux driver\n");

	return 0;

out_no_pmx:
	iounmap(upmx->rsc_virtbase);
out_no_rsc_remap:
	iounmap(upmx->gpio_virtbase);
out_no_gpio_remap:
	platform_set_drvdata(pdev, NULL);
	kfree(upmx);
	return ret;
}

static const struct of_device_id pinmux_ids[]  = {
	{ .compatible = "sirf,prima2-gpio-pinmux" },
	{}
};

static struct platform_driver sirfsoc_pinmux_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = pinmux_ids,
	},
	.probe = sirfsoc_pinmux_probe,
};

static int __init sirfsoc_pinmux_init(void)
{
	return platform_driver_register(&sirfsoc_pinmux_driver);
}
arch_initcall(sirfsoc_pinmux_init);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>, "
	"Barry Song <baohua.song@csr.com>");
MODULE_DESCRIPTION("SIRFSOC pin control driver");
MODULE_LICENSE("GPL");
