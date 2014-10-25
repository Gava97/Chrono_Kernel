/*
 * drivers/cpufreq/cpufreq_limits_on_suspend.c
 * 
 * Copyright (c) 2014, Shilin Victor <chrono.monochrome@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#ifdef MODULE
#include <linux/kallsyms.h>
#ifdef CONFIG_TOUCHSCREEN_ZINITIX_BT404
static bool (*gm_bt404_is_suspend)(void);
#define bt404_is_suspend (*gm_bt404_is_suspend)
#endif /* CONFIG_TOUCHSCREEN_ZINITIX_BT404 */
#else 
#ifdef CONFIG_TOUCHSCREEN_ZINITIX_BT404
extern bool bt404_is_suspend(void);
#endif /* CONFIG_TOUCHSCREEN_ZINITIX_BT404 */
#endif /* MODULE */

#define VERBOSE_DEBUG 0

static bool cpu_freq_limits = true;
static unsigned int screenoff_min_cpufreq = 46000;
static unsigned int screenoff_max_cpufreq = 276000;

static unsigned int screenon_min_cpufreq = 276000;
static unsigned int screenon_max_cpufreq = 1200000; 

static int cpufreq_callback(struct notifier_block *nfb,
		unsigned long event, void *data)
{
	if (cpu_freq_limits) {
		struct cpufreq_policy *policy = data;
		int new_min = 0, new_max = 0;
#ifdef CONFIG_TOUCHSCREEN_ZINITIX_BT404
		bool is_suspend = bt404_is_suspend();
#endif

		if (event != CPUFREQ_ADJUST)
			return 0;
		
		new_min = is_suspend ? screenoff_min_cpufreq : screenon_min_cpufreq;
		new_max = is_suspend ? screenoff_max_cpufreq : screenon_max_cpufreq;
		
		if (new_min > new_max) 
			new_max = new_min;

		policy->min = new_min;
		policy->max = new_max;
	}
	return 0;
}

static struct notifier_block cpufreq_notifier_block = 
{
	.notifier_call = cpufreq_callback,
};

static ssize_t cpufreq_limits_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) 
{
	sprintf(buf, "status: %s\n"
		      "min(screen off) = %d KHz\n"
		      "max(screen off) = %d KHz\n"
		      "min(screen on) = %d KHz\n"
		      "max(screen on) = %d KHz\n",
		      cpu_freq_limits ? "on" : "off",
		      screenoff_min_cpufreq, 
		      screenoff_max_cpufreq, 
		      screenon_min_cpufreq,
		      screenon_max_cpufreq
 	      );

	return strlen(buf);
}

static ssize_t cpufreq_limits_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
  	
	if (!strncmp(&buf[0], "screenon_min=", 13)) {
		if (!sscanf(&buf[13], "%d", &screenon_min_cpufreq))
			goto invalid_input;
	}

	if (!strncmp(&buf[0], "screenon_max=", 13)) {
		if (!sscanf(&buf[13], "%d", &screenon_max_cpufreq))
			goto invalid_input;
	}
  
	if (!strncmp(buf, "on", 2)) {
		cpu_freq_limits = true;
		return count;
	}

	if (!strncmp(buf, "off", 3)) {
		cpu_freq_limits = false;
		return count;
	}
	
	if (!strncmp(&buf[0], "min=", 4)) {
		if (!sscanf(&buf[4], "%d", &screenoff_min_cpufreq))
			goto invalid_input;
	}

	if (!strncmp(&buf[0], "max=", 4)) {
		if (!sscanf(&buf[4], "%d", &screenoff_max_cpufreq))
			goto invalid_input;
	}

	return count;

invalid_input:
	pr_err("[cpufreq_limits] invalid input");
	return -EINVAL;
}

static struct kobj_attribute cpufreq_limits_interface = __ATTR(cpufreq_limits_on_suspend, 0644,
									   cpufreq_limits_show,
									   cpufreq_limits_store);

static struct attribute *cpufreq_attrs[] = {
	&cpufreq_limits_interface.attr,
	NULL,
};

static struct attribute_group cpufreq_interface_group = {
	.attrs = cpufreq_attrs,
};

static struct kobject *cpufreq_kobject;

static int cpufreq_limits_driver_init(void)
{
	int ret;
	struct cpufreq_policy *data = cpufreq_cpu_get(0);
	
#ifdef MODULE	
	gm_bt404_is_suspend = (bool (*)(void))kallsyms_lookup_name("bt404_is_suspend");
#endif
	
	if (!screenoff_min_cpufreq)
		screenoff_min_cpufreq = data->min;
	if (!screenoff_max_cpufreq)
		screenoff_max_cpufreq = data->max;

	pr_err("[cpufreq_limits] initialized module with min %d and max %d MHz limits",
					 screenoff_min_cpufreq / 1000,  screenoff_max_cpufreq / 1000
	);
	
	cpufreq_kobject = kobject_create_and_add("cpufreq", kernel_kobj);
	if (!cpufreq_kobject) {
		pr_err("[cpufreq_limits] Failed to create kobject interface\n");
	}

	ret = sysfs_create_group(cpufreq_kobject, &cpufreq_interface_group);
	if (ret) {
		kobject_put(cpufreq_kobject);
	}
	
	cpufreq_register_notifier(&cpufreq_notifier_block, CPUFREQ_POLICY_NOTIFIER);
	
	return ret;
}

static void cpufreq_limits_driver_exit(void)
{
	sysfs_remove_group(cpufreq_kobject, &cpufreq_interface_group);
}
module_init(cpufreq_limits_driver_init);
module_exit(cpufreq_limits_driver_exit);

MODULE_AUTHOR("Shilin Victor <chrono.monochrome@gmail.com>");
MODULE_DESCRIPTION("CPUfreq limits on suspend");
MODULE_LICENSE("GPL");
