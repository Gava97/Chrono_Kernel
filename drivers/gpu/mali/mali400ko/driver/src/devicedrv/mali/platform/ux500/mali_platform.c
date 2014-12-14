/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Mali related Ux500 platform initialization
 *
 * Author: Marta Lofstedt <marta.lofstedt@stericsson.com> for ST-Ericsson.
 * Author: Huang Ji (cocafe@xda) <cocafehj@gmail.com>
 * 
 * License terms: GNU General Public License (GPL) version 2.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for ST-Ericsson's Ux500 platforms
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/kobject.h>
#include <linux/jiffies.h>

#if CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#include <mach/prcmu.h>
#else
#include <linux/mfd/dbx500-prcmu.h>
#endif

#define MALI_HIGH_TO_LOW_LEVEL_UTILIZATION_LIMIT 150
#define MALI_LOW_TO_HIGH_LEVEL_UTILIZATION_LIMIT 200

#define MALI_UX500_VERSION		"2.0.2"

#define MALI_MAX_UTILIZATION		256

#define PRCMU_SGACLK			0x0014
#define PRCMU_PLLSOC0			0x0080

#define PRCMU_SGACLK_INIT		0x00000021
#define PRCMU_PLLSOC0_INIT		0x01030128

#define AB8500_VAPE_SEL1 		0x0E
#define AB8500_VAPE_SEL2	 	0x0F
#define AB8500_VAPE_STEP_UV		12500
#define AB8500_VAPE_MIN_UV		700000
#define AB8500_VAPE_MAX_UV		1362500

#define MALI_CLOCK_DEFLO		2
#define MALI_CLOCK_DEFHISPEED1		5
#define MALI_CLOCK_DEFHISPEED2		9
#define MALI_HISPEED1_UTILIZATION_LIMIT 140
#define MALI_HISPEED2_UTILIZATION_LIMIT 245

struct mali_dvfs_data
{
	u32 	freq;
	u32 	clkpll;
	u8 	vape_raw;
};

static struct mali_dvfs_data mali_dvfs[] = {
	{128000, 0x01030114, 0x26},
	{192000, 0x0103011e, 0x26},
	{256000, 0x01030128, 0x26},
	{320000, 0x01030132, 0x28},
	{384000, 0x0103013c, 0x28},
	{403200, 0x0103013f, 0x28},
	{448000, 0x01030146, 0x2c},
	{512000, 0x01030150, 0x2c},
	{576000, 0x0103015a, 0x36},
	{601600, 0x0103015e, 0x39},
	{640000, 0x01030164, 0x3f},
	{672000, 0x01030169, 0x3f},
	{704000, 0x0103016e, 0x3f},
	{716800, 0x01030170, 0x3f},
};

int mali_utilization_high_to_low = MALI_HIGH_TO_LOW_LEVEL_UTILIZATION_LIMIT;
int mali_utilization_low_to_high = MALI_LOW_TO_HIGH_LEVEL_UTILIZATION_LIMIT;

static int hispeed1_threshold = MALI_HISPEED1_UTILIZATION_LIMIT;
static int hispeed2_threshold = MALI_HISPEED2_UTILIZATION_LIMIT;

static bool force_cpufreq_to_max = true;
static int force_cpufreq_to_max_threshold = MALI_MAX_UTILIZATION - 5;

static bool is_running;
static bool is_initialized;

static u32 mali_last_utilization;
module_param(mali_last_utilization, uint, 0444);

static struct regulator *regulator;
static struct clk *clk_sga;
static struct work_struct mali_utilization_work;
static struct workqueue_struct *mali_utilization_workqueue;

#if CONFIG_HAS_WAKELOCK
static struct wake_lock wakelock;
#endif

static u32 boost_low 		= MALI_CLOCK_DEFLO;
static u32 boost_cur		= 0;
static u32 boost_hispeed1	= MALI_CLOCK_DEFHISPEED1;
static u32 boost_hispeed2	= MALI_CLOCK_DEFHISPEED2;

static u32 boost_stat[17];
static u32 boost_stat_opp50	= 0;
static u32 boost_stat_total	= 0;

//mutex to protect above variables
static DEFINE_MUTEX(mali_boost_lock);

extern void set_min_cpufreq(int);
extern int get_min_cpufreq(void);
extern int get_max_cpufreq(void);

static bool min_cpufreq_forced_to_max = false;
static int prev_min_cpufreq;


bool get_cpufreq_forced_state(void) {
	return min_cpufreq_forced_to_max;
}

void set_cpufreq_forced_state(bool state) {
	min_cpufreq_forced_to_max = state;
}

int get_prev_cpufreq(void) {
	return prev_min_cpufreq;
}

static int vape_voltage(u8 raw)
{
	if (raw <= 0x35) {
		return (AB8500_VAPE_MIN_UV + (raw * AB8500_VAPE_STEP_UV));
	} else {
		return AB8500_VAPE_MAX_UV;
	}
}

static int pllsoc0_freq(u32 raw)
{
	int multiple = raw & 0x000000FF;
	int divider = (raw & 0x00FF0000) >> 16;
	int half = (raw & 0x01000000) >> 24;
	int pll;

	pll = (multiple * 38400);
	pll /= divider;

	if (half) {
		pll /= 2;
	}

	return pll;
}

static int sgaclk_freq(void)
{
	u32 soc0pll = prcmu_read(PRCMU_PLLSOC0);
	u32 sgaclk = prcmu_read(PRCMU_SGACLK);
	int div;

	if (!(sgaclk & BIT(5)))
		return 0;

	div = (sgaclk & 0xf);
	if (!div) div = 1;
	
	return (pllsoc0_freq(soc0pll) / div);
}

static int mali_freq_hispeed(int idx)
{
	u8 vape;
	u32 pll;
	
	if (idx > boost_hispeed2 || idx < boost_low) {
		return -1;
	}
	
	if (boost_cur >= idx || boost_cur == boost_hispeed2)
		return 1;
	
	boost_cur = idx;

	vape = mali_dvfs[boost_cur].vape_raw;
	pll = mali_dvfs[boost_cur].clkpll;
	
	prcmu_abb_write(AB8500_REGU_CTRL2, AB8500_VAPE_SEL1, &vape, 1);
	prcmu_write(PRCMU_PLLSOC0, pll);
	
	return 0; 
}

static int mali_freq_up(void)
{
	u8 vape;
	u32 pll;
	
	if (boost_cur < boost_hispeed2) {
		if (boost_cur == boost_low)
			boost_cur = boost_hispeed1;
		else
			boost_cur = boost_hispeed2;

		vape = mali_dvfs[boost_cur].vape_raw;
		pll = mali_dvfs[boost_cur].clkpll;

		prcmu_abb_write(AB8500_REGU_CTRL2, AB8500_VAPE_SEL1, &vape, 1);
		prcmu_write(PRCMU_PLLSOC0, pll);
		return 1; 
	} else {
		return 0; // reached table index high
	}
}

static int mali_freq_down(void)
{
	u8 vape;
	u32 pll;
	
	if (min_cpufreq_forced_to_max) {
		set_min_cpufreq(prev_min_cpufreq); // unlock cpufreq on every gpu_freq_down
		min_cpufreq_forced_to_max = false;
	}

	if (boost_cur > boost_low) {
		if (boost_cur == boost_hispeed2)
			boost_cur = boost_hispeed1;
		else
			boost_cur = boost_low;
		
		vape = mali_dvfs[boost_cur].vape_raw;
		pll = mali_dvfs[boost_cur].clkpll;

		prcmu_write(PRCMU_PLLSOC0, pll);
		prcmu_abb_write(AB8500_REGU_CTRL2, AB8500_VAPE_SEL1, &vape, 1);
		
		return 1;
	} else {	  
		return 0; // reached table index low -> lower qos requirements
	}
}

static void mali_clock_apply(u32 idx)
{
	u8 vape;
	u32 pll;

	vape = mali_dvfs[idx].vape_raw;
	pll = mali_dvfs[idx].clkpll;

	prcmu_abb_write(AB8500_REGU_CTRL2, 
		AB8500_VAPE_SEL1, 
		&vape, 
		1);
	prcmu_write(PRCMU_PLLSOC0, pll);
}

static void mali_boost_init(void)
{
	if (sgaclk_freq() != mali_dvfs[boost_low].freq) {
		mali_clock_apply(boost_low);
	}

	pr_info("[Mali] Booster: %u kHz - %u kHz\n", 
			mali_dvfs[boost_low].freq, 
			mali_dvfs[boost_hispeed2].freq);
	
	boost_cur = boost_low;
}

static _mali_osk_errcode_t mali_platform_powerdown(void)
{
	if (is_running) {

#if CONFIG_HAS_WAKELOCK
		wake_unlock(&wakelock);
#endif
		clk_disable(clk_sga);
		if (regulator) {
			int ret = regulator_disable(regulator);
			if (ret < 0) {
				MALI_DEBUG_PRINT(2, ("%s: Failed to disable regulator %s\n", __func__, "v-mali"));
				is_running = false;
				MALI_ERROR(_MALI_OSK_ERR_FAULT);
			}
		}
		is_running = false;
	}
	
	prcmu_qos_update_requirement(PRCMU_QOS_DDR_OPP, "mali", PRCMU_QOS_DEFAULT_VALUE);
	prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "mali", PRCMU_QOS_DEFAULT_VALUE);
	
	if (min_cpufreq_forced_to_max) {
		set_min_cpufreq(prev_min_cpufreq);
		min_cpufreq_forced_to_max = false;
	}
	
	MALI_DEBUG_PRINT(4, ("mali_platform_powerdown is_running: %u\n", is_running));
	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_platform_powerup(void)
{
	if (!is_running) {
		int ret = regulator_enable(regulator);
		if (ret < 0) {
			MALI_DEBUG_PRINT(2, ("%s: Failed to enable regulator %s\n", __func__, "v-mali"));
			goto error;
		}

		ret = clk_enable(clk_sga);
		if (ret < 0) {
			regulator_disable(regulator);
			MALI_DEBUG_PRINT(2, ("%s: Failed to enable clock %s\n", __func__, "mali"));
			goto error;
		}

#if CONFIG_HAS_WAKELOCK
		wake_lock(&wakelock);
#endif
		is_running = true;
	}
	MALI_DEBUG_PRINT(4, ("mali_platform_powerup is_running:%u\n", is_running));
	MALI_SUCCESS;
error:
	MALI_DEBUG_PRINT(1, ("Failed to power up.\n"));
	MALI_ERROR(_MALI_OSK_ERR_FAULT);
}

/* Original code behavior
 * Rationale behind the values for: (switching between APE_50_OPP and APE_100_OPP)
 * MALI_HIGH_LEVEL_UTILIZATION_LIMIT and MALI_LOW_LEVEL_UTILIZATION_LIMIT
 * When operating at half clock frequency a faster clock is requested when
 * reaching 75% utilization. When operating at full clock frequency a slower
 * clock is requested when reaching 25% utilization. There is a margin of 25%
 * at the high range of the slow clock to avoid complete saturation of the
 * hardware and there is some overlap to avoid an oscillating situation where
 * the clock goes back and forth from high to low.
 *
 * Utilization on full speed clock
 * 0               64             128             192              255
 * |---------------|---------------|---------------|---------------|
 *                 XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
 *                 |       ^
 *                 V       |
 * XXXXXXXXXXXXXXXXXXXXXXXXX
 * 0       64     128     192      255
 * |-------|-------|-------|-------|
 * Utilization on half speed clock
*/

/* New behavior:
 * Depending on utilization we make one step up/down in the table until reached limits
 * set by mali_threshold_freq_down/mali_threshold_freq_up
 * We start at OPP50 and rise to OPP100 first before frequency is increased
 * We go back to OPP50 after we reached lowest frequency
 * We immediately bump gpu freq to hispeed/hispeed2 if utilization exceeds hispeed1_threshold/hispeed2_threshold
 */

void mali_utilization_function(struct work_struct *ptr)
{
	/*By default, platform start with 50% APE OPP and 25% DDR OPP*/
	static u32 has_requested_low = 1;

	MALI_DEBUG_PRINT(5, ("MALI GPU utilization: %u\n", mali_last_utilization));

	mutex_lock(&mali_boost_lock);
	
	if (boost_cur == boost_low) {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "mali", PRCMU_QOS_DEFAULT_VALUE);
		prcmu_qos_update_requirement(PRCMU_QOS_DDR_OPP, "mali", PRCMU_QOS_DEFAULT_VALUE);
	} else  {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "mali", PRCMU_QOS_MAX_VALUE);
		if (boost_cur == boost_hispeed2)
			prcmu_qos_update_requirement(PRCMU_QOS_DDR_OPP, "mali", PRCMU_QOS_MAX_VALUE);
	}

	if (mali_last_utilization > hispeed1_threshold) {
		if (has_requested_low) { // switch to OPP100 before increasing frequency
			MALI_DEBUG_PRINT(5, ("MALI GPU utilization: %u SIGNAL_HIGH\n", mali_last_utilization));
			has_requested_low = 0;
		} else {
			if (mali_last_utilization > hispeed2_threshold)
				mali_freq_hispeed(boost_hispeed2);
			else if (boost_cur < boost_hispeed1)
				mali_freq_hispeed(boost_hispeed1);
		}
	}

	if (mali_last_utilization > mali_utilization_low_to_high) {
		if (has_requested_low) {
			MALI_DEBUG_PRINT(5, ("MALI GPU utilization: %u SIGNAL_HIGH\n", mali_last_utilization));
			has_requested_low = 0;
		} else {
			if (!mali_freq_up()) {
				if (force_cpufreq_to_max && !min_cpufreq_forced_to_max) {
					if (mali_last_utilization >= force_cpufreq_to_max_threshold) {
						prev_min_cpufreq = get_min_cpufreq();
						set_min_cpufreq(get_max_cpufreq()); // force max cpufreq if reached max gpu freq
						min_cpufreq_forced_to_max = true;
					}
				}
			}
		}
	} else if (mali_last_utilization < mali_utilization_high_to_low) {
			if (!mali_freq_down()) { // switch to opp50 if at lowest frequency
				if (!has_requested_low) {
					MALI_DEBUG_PRINT(5, ("MALI GPU utilization: %u SIGNAL_LOW\n", mali_last_utilization));
					has_requested_low = 1;
			} 
		}
	}
	// update stats, count time in opp50 seperately
	if (has_requested_low) {
		boost_stat_opp50++;
	} else {	
		boost_stat[boost_cur]++;
	}
	boost_stat_total++;
	mutex_unlock(&mali_boost_lock);
}

#define ATTR_RO(_name)	\
	static struct kobj_attribute _name##_interface = __ATTR(_name, 0444, _name##_show, NULL);

#define ATTR_WO(_name)	\
	static struct kobj_attribute _name##_interface = __ATTR(_name, 0220, NULL, _name##_store);

#define ATTR_RW(_name)	\
	static struct kobj_attribute _name##_interface = __ATTR(_name, 0644, _name##_show, _name##_store);

static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "DB8500 GPU OC Driver (%s), cocafe, 1n4148, ChronoMonochrome\n", MALI_UX500_VERSION);
}
ATTR_RO(version);

static ssize_t mali_gpu_clock_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d kHz\n", sgaclk_freq());
}

static ssize_t mali_gpu_clock_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	int i;

	if (sscanf(buf, "idx=%u", &val)) {
		if (val >= ARRAY_SIZE(mali_dvfs))
			return -EINVAL;

		mali_clock_apply(val);

		return count;
	}

	if (sscanf(buf, "%u", &val)) {
		for (i = 0; i < ARRAY_SIZE(mali_dvfs); i++) {
			if (mali_dvfs[i].freq == val) {
				mali_clock_apply(i);

				break;
			}
		}

		return count;
	}

	return -EINVAL;
}

ATTR_RW(mali_gpu_clock);

static ssize_t mali_gpu_vape_50_opp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	u8 value;

	prcmu_abb_read(AB8500_REGU_CTRL2,
			AB8500_VAPE_SEL2,
			&value,
			1);

	return sprintf(buf, "%u uV - 0x%x\n", vape_voltage(value), value);
}

static ssize_t mali_gpu_vape_50_opp_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	u8 vape;

	if (sscanf(buf, "%x", &val)) {
		vape = val;
		prcmu_abb_write(AB8500_REGU_CTRL2,
			AB8500_VAPE_SEL2,
			&vape,
			1);
		return count;
	}

	return -EINVAL;
}

ATTR_RW(mali_gpu_vape_50_opp);

static ssize_t mali_gpu_fullspeed_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	/*
	 * Check APE OPP status, on OPP50, clock is half.
	 */
	return sprintf(buf, "%s\n", (prcmu_get_ape_opp() == APE_100_OPP) ? "1" : "0");
}

static ssize_t mali_gpu_fullspeed_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (sscanf(buf, "%d", &val)) {
		if (val)
			prcmu_set_ape_opp(APE_100_OPP);
		else
			prcmu_set_ape_opp(APE_50_OPP);

		return count;
	}

	return -EINVAL;
}

ATTR_RW(mali_gpu_fullspeed);

static ssize_t mali_gpu_load_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d (%d%%)\n", mali_last_utilization, mali_last_utilization * 100 / 256);
}
ATTR_RO(mali_gpu_load);

static ssize_t mali_gpu_vape_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	u8 value;
	bool opp50;

	/*
	 * cocafe:
	 * Display Vape Seletion 1 only, 
	 * In APE 50OPP, Vape uses SEL2. 
	 * And the clocks are half.
	 */
	opp50 = (prcmu_get_ape_opp() != APE_100_OPP);
	prcmu_abb_read(AB8500_REGU_CTRL2, 
			opp50 ? AB8500_VAPE_SEL2 : AB8500_VAPE_SEL1,
			&value, 
			1);

	return sprintf(buf, "%u uV - 0x%x (OPP:%d)\n", vape_voltage(value), value, opp50 ? 50 : 100);
}
ATTR_RO(mali_gpu_vape);

static ssize_t mali_threshold_freq_up_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mali_utilization_low_to_high);
}

static ssize_t mali_threshold_freq_up_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (sscanf(buf, "%d", &val)) {

		if (val < 0)
			  mali_utilization_low_to_high = 0;
		else if (val > MALI_MAX_UTILIZATION)
			  mali_utilization_low_to_high = MALI_MAX_UTILIZATION;
		else
			  mali_utilization_low_to_high = val;

		return count;
	}

	return -EINVAL;
}
ATTR_RW(mali_threshold_freq_up);

static ssize_t mali_threshold_freq_down_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mali_utilization_high_to_low);
}

static ssize_t mali_threshold_freq_down_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (sscanf(buf, "%d", &val)) {

		if (val < 0)
			mali_utilization_high_to_low = 0;
		else if (val > MALI_MAX_UTILIZATION)
			mali_utilization_high_to_low = MALI_MAX_UTILIZATION;
		else
			mali_utilization_high_to_low = val;

		return count;
	}

	return -EINVAL;
}
ATTR_RW(mali_threshold_freq_down);

static ssize_t mali_force_cpufreq_to_max_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	sprintf(buf, "%sstatus: %s\n", buf, 
			force_cpufreq_to_max ? "on" : "off");
	sprintf(buf, "%sthreshold=%d\n", buf, 
			force_cpufreq_to_max_threshold);
	
	return strlen(buf);
}

static ssize_t mali_force_cpufreq_to_max_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (!strncmp(buf, "on", 2)) {
		force_cpufreq_to_max = true;
		return count;
	}

	if (!strncmp(buf, "off", 3)) {
		force_cpufreq_to_max = false;
		return count;
	}
		
	if (sscanf(buf, "threshold=%d", &val)) {
		force_cpufreq_to_max_threshold = val;
		return count;
	}

	return -EINVAL;
}
ATTR_RW(mali_force_cpufreq_to_max);

static ssize_t mali_boost_low_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	sprintf(buf, "%sLOWthreshold: %u\n", buf, mali_utilization_high_to_low);
	sprintf(buf, "%sDVFS idx: %u\n", buf, boost_low);
	sprintf(buf, "%sfrequency: %u kHz\n", buf, mali_dvfs[boost_low].freq);
	sprintf(buf, "%sVape: %u uV\n", buf, vape_voltage(mali_dvfs[boost_low].vape_raw));

	return strlen(buf);
}

static ssize_t mali_boost_low_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	int i;

	if (sscanf(buf, "idx=%u", &val)) {
		if (val >= ARRAY_SIZE(mali_dvfs))
			return -EINVAL;

		boost_low = val;
		mali_clock_apply(boost_low);

		return count;
	}

	if (sscanf(buf, "%u", &val)) {
		for (i = 0; i < ARRAY_SIZE(mali_dvfs); i++) {
			if (mali_dvfs[i].freq == val) {
				boost_low = i;
				mali_clock_apply(boost_low);

				break;
			}
		}

		return count;
	}

	return -EINVAL;
}
ATTR_RW(mali_boost_low);

static ssize_t mali_boost_hispeed_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	sprintf(buf, "%sHIthreshold: %u\n", buf, hispeed1_threshold);
	sprintf(buf, "%sDVFS idx: %u\n", buf, boost_hispeed1);
	sprintf(buf, "%sfrequency: %u kHz\n", buf, mali_dvfs[boost_hispeed1].freq);
	sprintf(buf, "%sVape: %u uV\n", buf, vape_voltage(mali_dvfs[boost_hispeed1].vape_raw));

	return strlen(buf);
}

static ssize_t mali_boost_hispeed_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	int i;

	if (sscanf(buf, "idx=%u", &val)) {
		if (val >= ARRAY_SIZE(mali_dvfs))
			return -EINVAL;

		boost_hispeed1 = val;

		return count;
	}

	if (sscanf(buf, "threshold=%u", &val)) {
		hispeed1_threshold = val;

		return count;
	}

	if (sscanf(buf, "%u", &val)) {
		for (i = 0; i < ARRAY_SIZE(mali_dvfs); i++) {
			if (mali_dvfs[i].freq == val) {
				boost_hispeed1 = i;

				break;
			}
		}

		return count;
	}

	return -EINVAL;
}
ATTR_RW(mali_boost_hispeed);

static ssize_t mali_boost_hispeed2_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	sprintf(buf, "%sHIthreshold: %u\n", buf, hispeed2_threshold);
	sprintf(buf, "%sDVFS idx: %u\n", buf, boost_hispeed2);
	sprintf(buf, "%sfrequency: %u kHz\n", buf, mali_dvfs[boost_hispeed2].freq);
	sprintf(buf, "%sVape: %u uV\n", buf, vape_voltage(mali_dvfs[boost_hispeed2].vape_raw));

	return strlen(buf);
}

static ssize_t mali_boost_hispeed2_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	int i;

	if (sscanf(buf, "idx=%u", &val)) {
		if (val >= ARRAY_SIZE(mali_dvfs))
			return -EINVAL;

		boost_hispeed2 = val;

		return count;
	}

	if (sscanf(buf, "threshold=%u", &val)) {
		hispeed2_threshold = val;

		return count;
	}

	if (sscanf(buf, "%u", &val)) {
		for (i = 0; i < ARRAY_SIZE(mali_dvfs); i++) {
			if (mali_dvfs[i].freq == val) {
				boost_hispeed2 = i;

				break;
			}
		}

		return count;
	}

	return -EINVAL;
}
ATTR_RW(mali_boost_hispeed2);

static ssize_t mali_debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	sprintf(buf, "%sboost_cur = %d\n", buf, boost_cur);
	sprintf(buf, "%spll = %#010x\n", buf, prcmu_read(PRCMU_PLLSOC0));
	sprintf(buf, "%sape_opp = %s\n", buf, 
		(prcmu_get_ape_opp() == APE_100_OPP) ? "100" : "50");
	sprintf(buf, "%sddr_opp = %s\n", buf, 
		(prcmu_get_ddr_opp() == DDR_100_OPP) ? "100" :
		((prcmu_get_ddr_opp() == DDR_50_OPP) ? "50" : "25"));

	return strlen(buf);
}
ATTR_RO(mali_debug);

static ssize_t mali_dvfs_config_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i;

	sprintf(buf, "idx freq   rawfreq clkpll     Vape\n");

	for (i = 0; i < ARRAY_SIZE(mali_dvfs); i++) {
		sprintf(buf, "%s%3u%7u%7u  %#010x%8u %#04x\n", 
			buf, 
			i, 
			mali_dvfs[i].freq, 
			pllsoc0_freq(mali_dvfs[i].clkpll), 
			mali_dvfs[i].clkpll, 
			vape_voltage(mali_dvfs[i].vape_raw), 
			mali_dvfs[i].vape_raw);
	}

	return strlen(buf);
}

static ssize_t mali_dvfs_config_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int idx, val;

	if (sscanf(buf, "%u pll=%x", &idx, &val) == 2) {
		mali_dvfs[idx].clkpll = val;

		return count;
	}

	if (sscanf(buf, "%u vape=%x", &idx, &val) == 2) {
		mali_dvfs[idx].vape_raw = val;

		return count;
	}

	return -EINVAL;
}
ATTR_RW(mali_dvfs_config);

static ssize_t mali_available_frequencies_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mali_dvfs); i++) {
		sprintf(buf, "%s%6u\n", buf, mali_dvfs[i].freq);
	}
	return strlen(buf);
}
ATTR_RO(mali_available_frequencies);

static ssize_t mali_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i;
	if (boost_stat_total == 0) boost_stat_total = 1; // prevent div by 0
	sprintf(buf, "%s OPP50 %10u %3u%%\n", buf, boost_stat_opp50, boost_stat_opp50*100/boost_stat_total);
	for (i = 0; i < ARRAY_SIZE(mali_dvfs); i++) {
		if (boost_stat[i]) {
			sprintf(buf, "%s%6u %10u %3u%%\n", buf, mali_dvfs[i].freq, boost_stat[i], boost_stat[i]*100/boost_stat_total);
		}
	}
	return strlen(buf);
}
ATTR_RO(mali_stats);

static struct attribute *mali_attrs[] = {
	&version_interface.attr, 
	&mali_gpu_clock_interface.attr, 
	&mali_gpu_fullspeed_interface.attr, 
	&mali_gpu_load_interface.attr, 
	&mali_gpu_vape_interface.attr, 
	&mali_gpu_vape_50_opp_interface.attr,
	&mali_boost_low_interface.attr, 
	&mali_boost_hispeed_interface.attr, 
	&mali_boost_hispeed2_interface.attr, 
	&mali_threshold_freq_down_interface.attr,
	&mali_threshold_freq_up_interface.attr,
	&mali_force_cpufreq_to_max_interface.attr,
	&mali_dvfs_config_interface.attr, 
	&mali_available_frequencies_interface.attr,
	&mali_debug_interface.attr,
	&mali_stats_interface.attr, 
	NULL,
};

static struct attribute_group mali_interface_group = {
	 /* .name  = "governor", */ /* Not using subfolder now */
	.attrs = mali_attrs,
};

static struct kobject *mali_kobject;

_mali_osk_errcode_t mali_platform_init()
{
	int ret;

	is_running = false;
	mali_last_utilization = 0;

	if (!is_initialized) {

		prcmu_write(PRCMU_PLLSOC0, PRCMU_PLLSOC0_INIT);
		prcmu_write(PRCMU_SGACLK,  PRCMU_SGACLK_INIT);
		mali_boost_init();

		mali_utilization_workqueue = create_singlethread_workqueue("mali_utilization_workqueue");
		if (NULL == mali_utilization_workqueue) {
			MALI_DEBUG_PRINT(2, ("%s: Failed to setup workqueue %s\n", __func__, "mali_utilization_workqueue"));
			goto error;
		}

		INIT_WORK(&mali_utilization_work, mali_utilization_function);

		regulator = regulator_get(NULL, "v-mali");
		if (IS_ERR(regulator)) {
			MALI_DEBUG_PRINT(2, ("%s: Failed to get regulator %s\n", __func__, "v-mali"));
			goto error;
		}

		clk_sga = clk_get_sys("mali", NULL);
		if (IS_ERR(clk_sga)) {
			regulator_put(regulator);
			MALI_DEBUG_PRINT(2, ("%s: Failed to get clock %s\n", __func__, "mali"));
			goto error;
		}

#if CONFIG_HAS_WAKELOCK
		wake_lock_init(&wakelock, WAKE_LOCK_SUSPEND, "mali_wakelock");
#endif

		mali_kobject = kobject_create_and_add("mali", kernel_kobj);
		if (!mali_kobject) {
			pr_err("[Mali] Failed to create kobject interface\n");
		}

		ret = sysfs_create_group(mali_kobject, &mali_interface_group);
		if (ret) {
			kobject_put(mali_kobject);
		}

		prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP, "mali", PRCMU_QOS_DEFAULT_VALUE);
		prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP, "mali", PRCMU_QOS_DEFAULT_VALUE);

		pr_info("[Mali] DB8500 GPU OC Initialized (%s)\n", MALI_UX500_VERSION);

		is_initialized = true;
	}

	MALI_SUCCESS;
error:
	MALI_DEBUG_PRINT(1, ("SGA initialization failed.\n"));
	MALI_ERROR(_MALI_OSK_ERR_FAULT);
}

_mali_osk_errcode_t mali_platform_deinit()
{
	destroy_workqueue(mali_utilization_workqueue);
	regulator_put(regulator);
	clk_put(clk_sga);

#if CONFIG_HAS_WAKELOCK
	wake_lock_destroy(&wakelock);
#endif
	kobject_put(mali_kobject);
	is_running = false;
	mali_last_utilization = 0;
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, "mali");
	prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP, "mali");
	is_initialized = false;
	MALI_DEBUG_PRINT(2, ("SGA terminated.\n"));
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	if (MALI_POWER_MODE_ON == power_mode)
		return mali_platform_powerup();

	/*We currently don't make any distinction between MALI_POWER_MODE_LIGHT_SLEEP and MALI_POWER_MODE_DEEP_SLEEP*/
	return mali_platform_powerdown();
}

void mali_gpu_utilization_handler(u32 utilization)
{
	mali_last_utilization = utilization;
	/*
	* We should not cancel the potentially not yet run old work
	* in favor of a new work.
	* Since the utilization value will change,
	* the mali_utilization_function will evaluate based on
	* what is the utilization now and not on what it was
	* when it was scheduled.
	*/
	queue_work(mali_utilization_workqueue, &mali_utilization_work);
}

void set_mali_parent_power_domain(void *dev)
{
	MALI_DEBUG_PRINT(2, ("This function should not be called since we are not using run time pm\n"));
}
