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

#include <linux/earlysuspend.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/kobject.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/sysfs.h>

#include <linux/mfd/dbx500-prcmu.h>

static bool module_is_loaded = false; //FIXME: move code that uses module_is_loaded to init function
static bool cpu_freq_limits = false;
static bool is_suspend = false;

static unsigned int screenoff_min_cpufreq = 100000;
static unsigned int screenoff_max_cpufreq = 400000;

static unsigned int screenon_min_cpufreq = 0; // screenon_min_cpufreq and screenon_max_cpufreq uses system values
static unsigned int screenon_max_cpufreq = 0;

static unsigned int restore_screenon_min_cpufreq = 0;// these values will be gotten from system at load
static unsigned int restore_screenon_max_cpufreq = 0; 

unsigned int input_boost_freq = 400000;
EXPORT_SYMBOL(input_boost_freq);
unsigned int input_boost_ms = 40;
EXPORT_SYMBOL(input_boost_ms);
u64 last_input_time;
EXPORT_SYMBOL(last_input_time);

#define MIN_INPUT_INTERVAL (150 * USEC_PER_MSEC)

static void requirements_add_thread(struct work_struct *requirements_add_work)
{
	if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
			"codina_lcd_dpi", 50)) {
		pr_info("pcrm_qos_add APE failed\n");
	}
}
static DECLARE_WORK(requirements_add_work, requirements_add_thread);

static void requirements_remove_thread(struct work_struct *requirements_remove_work)
{
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, "codina_lcd_dpi");
}
static DECLARE_WORK(requirements_remove_work, requirements_remove_thread);

static void cpufreq_limits_update(bool is_suspend_) {
	int new_min, new_max;
	
	is_suspend = is_suspend_;
	
	if (cpu_freq_limits) {
		
		/*
		 * since we don't have different cpufreq settings for different CPU cores,
		 * we'll update cpufreq limits only for first CPU core.
		 */
		struct cpufreq_policy *policy = cpufreq_cpu_get(0); 

		new_min = is_suspend_ ? screenoff_min_cpufreq : screenon_min_cpufreq;
		new_max = is_suspend_ ? screenoff_max_cpufreq : screenon_max_cpufreq;
		if (new_min)
			policy->min = new_min;
		else 
			pr_err("[cpufreq] new_min == 0\n");
		if (new_max)
			policy->max = new_max;
		else
			pr_err("[cpufreq] new_max == 0\n");
		pr_err("[cpufreq_limits] new cpufreqs are %d - %d kHz\n", policy->min, policy->max);
		
		if (restore_screenon_max_cpufreq < screenon_max_cpufreq)
			restore_screenon_max_cpufreq = screenon_max_cpufreq;
		if (restore_screenon_min_cpufreq < screenon_min_cpufreq)
			restore_screenon_min_cpufreq = screenon_min_cpufreq;
	}
}  

static struct work_struct early_suspend_work;
static struct work_struct late_resume_work;

static void early_suspend_fn(struct early_suspend *handler)
{
	schedule_work(&early_suspend_work);
	schedule_work(&requirements_remove_work);
}

static void late_resume_fn(struct early_suspend *handler)
{
	schedule_work(&requirements_add_work);
	schedule_work(&late_resume_work);
}

static void early_suspend_work_fn(struct work_struct *work)
{
	cpufreq_limits_update(true);
}

static void late_resume_work_fn(struct work_struct *work)
{
	cpufreq_limits_update(false);
}

static struct early_suspend driver_early_suspend = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = early_suspend_fn,
	.resume = late_resume_fn,
};

static int cpufreq_callback(struct notifier_block *nfb,
		unsigned long event, void *data)
{
  
	if (event != CPUFREQ_ADJUST)
		return 0;
	
	if (cpu_freq_limits) {
	  
		struct cpufreq_policy *policy = data;
		/* FIXME: would be better move that code to init function ?   */
		if (!module_is_loaded) {
		
			/*
			if (policy)
				policy->min = 200000;
			*/
	
			if (!screenoff_min_cpufreq)
				  screenoff_min_cpufreq = policy->min;
			if (!screenoff_max_cpufreq)
				  screenoff_max_cpufreq = policy->max;
			module_is_loaded = true;
		}
		/*------------------------------------------------------------*/
	  
		if (!is_suspend) { 
			if (screenon_max_cpufreq != policy->max)
				screenon_max_cpufreq = policy->max;
	
			if  (screenon_min_cpufreq != policy->min)
				screenon_min_cpufreq = policy->min;

			if  (policy->min == screenoff_min_cpufreq) { 
				if (restore_screenon_min_cpufreq) {
					policy->min = restore_screenon_min_cpufreq;
					pr_err("[cpufreq_limits] min cpufreq restored -> %d kHz\n", policy->min);
				}
			}
			
			if  (policy->max == screenoff_max_cpufreq) { 
				if (restore_screenon_max_cpufreq) {
					policy->max = restore_screenon_max_cpufreq;
					pr_err("[cpufreq_limits] max cpufreq restored -> %d kHz\n", policy->max);
				}
			}
		}	
	}

	return 0;
}

static struct notifier_block cpufreq_notifier_block = 
{
	.notifier_call = cpufreq_callback,
};

static ssize_t cpufreq_limits_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) 
{
	sprintf(buf,  "status: %s\n"
		      "Screen off settings:\n"
		      "min = %d kHz\n"
		      "max = %d kHz\n"
		      "Screen on settings:\n"
		      "min = %d kHz\n"
		      "max = %d kHz\n",
		      cpu_freq_limits ? "on" : "off",
		      screenoff_min_cpufreq,
		      screenoff_max_cpufreq,
		      screenon_min_cpufreq,  
		      screenon_max_cpufreq
	);

	return strlen(buf);
}

static ssize_t cpufreq_input_boost_freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) 
{
	sprintf(buf, "%d", input_boost_freq);
	return strlen(buf);
}

static ssize_t cpufreq_input_boost_freq_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret;
	
	ret = sscanf(&buf[0], "%d", &input_boost_freq);

	if (!ret)
		return -EINVAL;
	
	return count;
}

static struct kobj_attribute cpufreq_input_boost_freq_interface = __ATTR(input_boost_freq, 0644,
									   cpufreq_input_boost_freq_show,
									   cpufreq_input_boost_freq_store);

static ssize_t cpufreq_input_boost_ms_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) 
{
	sprintf(buf, "%d", input_boost_ms);
	return strlen(buf);
}

static ssize_t cpufreq_input_boost_ms_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret;
  
	ret = sscanf(&buf[0], "%d", &input_boost_ms);

	if (!ret)
		return -EINVAL;
	
	return count;
}

static struct kobj_attribute cpufreq_input_boost_ms_interface = __ATTR(input_boost_ms, 0644,
									   cpufreq_input_boost_ms_show,
									   cpufreq_input_boost_ms_store);

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
	&cpufreq_input_boost_freq_interface.attr,
	&cpufreq_input_boost_ms_interface.attr,
	NULL,
};

static struct attribute_group cpufreq_interface_group = {
	.attrs = cpufreq_attrs,
};

static struct kobject *cpufreq_kobject;

static void cpufreq_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	u64 now;
	
	if (!input_boost_freq)
		return;

	now = ktime_to_us(ktime_get());
	if (now - last_input_time < MIN_INPUT_INTERVAL)
		return;

	last_input_time = ktime_to_us(ktime_get());
}

static int cpufreq_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void cpufreq_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id cpufreq_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler cpufreq_input_handler = {
	.event          = cpufreq_input_event,
	.connect        = cpufreq_input_connect,
	.disconnect     = cpufreq_input_disconnect,
	.name           = "cpufreq_input_boost",
	.id_table       = cpufreq_ids,
};

static int cpufreq_limits_driver_init(void)
{
	int ret;

	pr_err("[cpufreq_limits] initialized module with min %d and max %d MHz limits",
					 screenoff_min_cpufreq / 1000,  screenoff_max_cpufreq / 1000
	);
	
	INIT_WORK(&early_suspend_work, early_suspend_work_fn);
	INIT_WORK(&late_resume_work, late_resume_work_fn);
	
	register_early_suspend(&driver_early_suspend);
	
	//when screen is on, APE_OPP 25 sometimes messes it up
	if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
				"codina_lcd_dpi", 50)) {
			pr_info("pcrm_qos_add APE failed\n");
	}

	cpufreq_kobject = kobject_create_and_add("cpufreq", kernel_kobj);
	if (!cpufreq_kobject) {
		pr_err("[cpufreq] Failed to create kobject interface\n");
	}

	ret = sysfs_create_group(cpufreq_kobject, &cpufreq_interface_group);
	if (ret) {
		kobject_put(cpufreq_kobject);
	}
	
	ret = input_register_handler(&cpufreq_input_handler);
	if (ret)
		pr_err("Cannot register cpufreq input handler.\n");
	
	cpufreq_register_notifier(&cpufreq_notifier_block, CPUFREQ_POLICY_NOTIFIER);
	
	return ret;
}

static void cpufreq_limits_driver_exit(void)
{
	unregister_early_suspend(&driver_early_suspend);
}

module_init(cpufreq_limits_driver_init);
module_exit(cpufreq_limits_driver_exit);

MODULE_AUTHOR("Shilin Victor <chrono.monochrome@gmail.com>");
MODULE_DESCRIPTION("CPUfreq limits on suspend");
MODULE_LICENSE("GPL");
