/*
 * Copyright (c) 2011 Peter Korsgaard <jacmet@xxxxxxxxxx>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/platform_device.h>

#define TRNG_CR		0x00
#define TRNG_ISR	0x1c
#define TRNG_ODATA	0x50

#define TRNG_KEY	0x524e4700 /* RNG */

struct atmel_trng {
	struct clk *clk;
	void __iomem *base;
};

static int atmel_trng_read(struct hwrng *rng, void *buf, size_t max,
			   bool wait)
{
	struct atmel_trng *trng = (struct atmel_trng *)rng->priv;
	u32 *data = buf;

	/* data ready? */
	if (readl(trng->base + TRNG_ODATA) & 1) {
		*data = readl(trng->base + TRNG_ODATA);
		return 4;
	} else
		return 0;
}

static struct hwrng atmel_trng = {
	.name	= "atmel-trng",
	.read	= atmel_trng_read,
};

static int atmel_trng_probe(struct platform_device *pdev)
{
	struct atmel_trng *trng;
	struct resource *res;
	int ret;

	if (atmel_trng.priv) {
		dev_err(&pdev->dev, "multiple instances not supported\n");
		return -EBUSY;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), pdev->name))
		return -EBUSY;

	trng->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!trng->base)
		return -EBUSY;

	trng->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(trng->clk))
		return PTR_ERR(trng->clk);

	ret = clk_enable(trng->clk);
	if (ret)
		goto err_enable;

	writel(TRNG_KEY | 1, trng->base + TRNG_CR);

	atmel_trng.priv = (unsigned long)trng;

	ret = hwrng_register(&atmel_trng);
	if (ret)
		goto err_register;

	platform_set_drvdata(pdev, trng);

	return 0;

err_register:
	atmel_trng.priv = 0;
	clk_disable(trng->clk);
err_enable:
	clk_put(trng->clk);

	return ret;
}

static int __devexit atmel_trng_remove(struct platform_device *pdev)
{
	struct atmel_trng *trng = platform_get_drvdata(pdev);

	hwrng_unregister(&atmel_trng);

	writel(TRNG_KEY, trng->base + TRNG_CR);
	clk_disable(trng->clk);
	clk_put(trng->clk);

	atmel_trng.priv = 0;

	return 0;
}

#ifdef CONFIG_PM
static int atmel_trng_suspend(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);

	clk_disable(trng->clk);

	return 0;
}

static int atmel_trng_resume(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);

	return clk_enable(trng->clk);
}

static const struct dev_pm_ops atmel_trng_pm_ops = {
	.suspend	= atmel_trng_suspend,
	.resume		= atmel_trng_resume,
};
#endif /* CONFIG_PM */

static struct platform_driver atmel_trng_driver = {
	.probe		= atmel_trng_probe,
	.remove		= __devexit_p(atmel_trng_remove),
	.driver		= {
		.name	= "atmel-trng",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &atmel_trng_pm_ops,
#endif /* CONFIG_PM */
	},
};

static int __init atmel_trng_init(void)
{
	return platform_driver_register(&atmel_trng_driver);
}
module_init(atmel_trng_init);

static void __exit atmel_trng_exit(void)
{
	platform_driver_unregister(&atmel_trng_driver);
}
module_exit(atmel_trng_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Korsgaard <jacmet@xxxxxxxxxx>");
MODULE_DESCRIPTION("Atmel true random number generator driver");
