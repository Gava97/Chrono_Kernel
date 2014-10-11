/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __DEVICES_COMMON_H
#define __DEVICES_COMMON_H

#include <linux/amba/serial.h>

extern struct amba_device *
dbx500_add_amba_device(struct device *parent, const char *name, resource_size_t base,
		       int irq, void *pdata, unsigned int periphid);

extern struct platform_device *
dbx500_add_platform_device_4k1irq(const char *name, int id,
				  resource_size_t base,
				  int irq, void *pdata);

extern struct platform_device *
dbx500_add_platform_device_noirq(const char *name, int id,
				  resource_size_t base, void *pdata);

struct stm_msp_controller;

static inline struct amba_device *
dbx500_add_msp_spi(struct device *parent, const char *name,
		   resource_size_t base, int irq,
		   struct spi_master_cntlr *pdata)
{
	return dbx500_add_amba_device(parent, name, base, irq,
				      pdata, 0);
}

struct pl022_ssp_controller;

static inline struct amba_device *
dbx500_add_spi(struct device *parent, const char *name, resource_size_t base,
	       int irq, struct spi_master_cntlr *pdata,
	       u32 periphid)
{
		return dbx500_add_amba_device(parent, name, base, irq,
				      pdata, periphid);
}

struct mmci_platform_data;

static inline struct amba_device *
dbx500_add_sdi(struct device *parent, const char *name, resource_size_t base,
	       int irq, struct mmci_platform_data *pdata, u32 periphid)
{
	return dbx500_add_amba_device(parent, name, base, irq,
				      pdata, periphid);
}

struct amba_pl011_data;

static inline struct amba_device *
dbx500_add_uart(struct device *parent, const char *name, resource_size_t base,
		int irq, struct amba_pl011_data *pdata)
{
	return dbx500_add_amba_device(parent, name, base, irq, pdata, 0);
}

struct nmk_i2c_controller;

static inline struct platform_device *
dbx500_add_i2c(struct device *parent, int id, resource_size_t base, int irq,
	       struct nmk_i2c_controller *pdata)
{
	return dbx500_add_platform_device_4k1irq("nmk-i2c", id, base, irq,
						 pdata);
}

struct msp_i2s_platform_data;

static inline struct platform_device *
dbx500_add_msp_i2s(struct device *parent, int id, resource_size_t base, int irq,
		   struct msp_i2s_platform_data *pdata)
{
	return dbx500_add_platform_device_4k1irq("ux500-msp-i2s", id, base, irq,
						 pdata);
}

static inline struct amba_device *
dbx500_add_rtc(struct device *parent, resource_size_t base, int irq)
{
	return dbx500_add_amba_device(parent, "rtc-pl031", base, irq, NULL, 0);
}

struct cryp_platform_data;

static inline struct platform_device *
dbx500_add_cryp1(struct device *parent, int id, resource_size_t base, int irq,
		  struct cryp_platform_data *pdata)
{
	return dbx500_add_platform_device_4k1irq("cryp1", id, base, irq,
						 pdata);
}

struct hash_platform_data;

static inline struct platform_device *
dbx500_add_hash1(struct device *parent, int id, resource_size_t base,
		struct hash_platform_data *pdata)
{
	return dbx500_add_platform_device_noirq("hash1", id, base, pdata);
}

struct nmk_gpio_platform_data;

void dbx500_add_gpios(struct device *parent, resource_size_t *base, int num,
		      int irq, struct nmk_gpio_platform_data *pdata);

#endif
