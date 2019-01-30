/*
 * kernel/power/wakeup_reason.c
 *
 * Logs the reasons which caused the kernel to resume from
 * the suspend mode.
 *
 * Copyright (C) 2014 Google, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/wakeup_reason.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/suspend.h>


#define MAX_WAKEUP_REASON_IRQS 32
static int irq_list[MAX_WAKEUP_REASON_IRQS];
static int irqcount;
static bool suspend_abort;
static char abort_reason[MAX_SUSPEND_ABORT_LEN];
static struct kobject *wakeup_reason;
static DEFINE_SPINLOCK(resume_reason_lock);

static ktime_t last_monotime; /* monotonic time before last suspend */
static ktime_t curr_monotime; /* monotonic time after last suspend */
static ktime_t last_stime; /* monotonic boottime offset before last suspend */
static ktime_t curr_stime; /* monotonic boottime offset after last suspend */
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/msm_drm_notify.h>
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
extern u64 	alarm_count;
extern u64	wakeup_source_count_rtc;
extern u64	wakeup_source_count_wifi;
extern u64	wakeup_source_count_modem;
extern u64  wakeup_source_count_kpdpwr;
extern u64  wakeup_source_count_adsp;
extern u64  wakeup_source_count_dsps;



static ssize_t ap_resume_reason_stastics_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	int buf_offset = 0;

	buf_offset += sprintf(buf + buf_offset, "wcnss_wlan");
	buf_offset += sprintf(buf + buf_offset,  "%s",":");
	buf_offset += sprintf(buf + buf_offset,  "%lld \n",wakeup_source_count_wifi);
	printk(KERN_WARNING "%s wakeup %lld times\n","wcnss_wlan",wakeup_source_count_wifi);

	buf_offset += sprintf(buf + buf_offset, "modem");
	buf_offset += sprintf(buf + buf_offset,  "%s",":");
	buf_offset += sprintf(buf + buf_offset,  "%lld \n",wakeup_source_count_modem);
	printk(KERN_WARNING "%s wakeup %lld times\n","qcom,smd-modem",wakeup_source_count_modem);

	buf_offset += sprintf(buf + buf_offset, "qpnp_rtc_alarm");
	buf_offset += sprintf(buf + buf_offset,  "%s",":");
	buf_offset += sprintf(buf + buf_offset,  "%lld \n",wakeup_source_count_rtc);
	printk(KERN_WARNING "%s wakeup %lld times\n","qpnp_rtc_alarm",wakeup_source_count_rtc);
	
	buf_offset += sprintf(buf + buf_offset, "power_key");
	buf_offset += sprintf(buf + buf_offset,  "%s",":");
	buf_offset += sprintf(buf + buf_offset,  "%lld \n",wakeup_source_count_kpdpwr);
	printk(KERN_WARNING "%s wakeup %lld times\n","power_key",wakeup_source_count_kpdpwr);

    buf_offset += sprintf(buf + buf_offset, "adsp");
	buf_offset += sprintf(buf + buf_offset,  "%s",":");
	buf_offset += sprintf(buf + buf_offset,  "%lld \n",wakeup_source_count_adsp);
	printk(KERN_WARNING "%s wakeup %lld times\n","adsp",wakeup_source_count_adsp);

    buf_offset += sprintf(buf + buf_offset, "dsps");
	buf_offset += sprintf(buf + buf_offset,  "%s",":");
	buf_offset += sprintf(buf + buf_offset,  "%lld \n",wakeup_source_count_dsps);
	printk(KERN_WARNING "%s wakeup %lld times\n","dsps",wakeup_source_count_dsps);
	return buf_offset;
}
#endif

static ssize_t last_resume_reason_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	int irq_no, buf_offset = 0;
	struct irq_desc *desc;
	spin_lock(&resume_reason_lock);
	if (suspend_abort) {
		buf_offset = sprintf(buf, "Abort: %s", abort_reason);
	} else {
		for (irq_no = 0; irq_no < irqcount; irq_no++) {
			desc = irq_to_desc(irq_list[irq_no]);
			if (desc && desc->action && desc->action->name)
				buf_offset += sprintf(buf + buf_offset, "%d %s\n",
						irq_list[irq_no], desc->action->name);
			else
				buf_offset += sprintf(buf + buf_offset, "%d\n",
						irq_list[irq_no]);
		}
	}
	spin_unlock(&resume_reason_lock);
	return buf_offset;
}

static ssize_t last_suspend_time_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct timespec sleep_time;
	struct timespec total_time;
	struct timespec suspend_resume_time;

	/*
	 * total_time is calculated from monotonic bootoffsets because
	 * unlike CLOCK_MONOTONIC it include the time spent in suspend state.
	 */
	total_time = ktime_to_timespec(ktime_sub(curr_stime, last_stime));

	/*
	 * suspend_resume_time is calculated as monotonic (CLOCK_MONOTONIC)
	 * time interval before entering suspend and post suspend.
	 */
	suspend_resume_time = ktime_to_timespec(ktime_sub(curr_monotime, last_monotime));

	/* sleep_time = total_time - suspend_resume_time */
	sleep_time = timespec_sub(total_time, suspend_resume_time);

	/* Export suspend_resume_time and sleep_time in pair here. */
	return sprintf(buf, "%lu.%09lu %lu.%09lu\n",
				suspend_resume_time.tv_sec, suspend_resume_time.tv_nsec,
				sleep_time.tv_sec, sleep_time.tv_nsec);
}

static struct kobj_attribute resume_reason = __ATTR_RO(last_resume_reason);
static struct kobj_attribute suspend_time = __ATTR_RO(last_suspend_time);
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
static struct kobj_attribute ap_resume_reason_stastics = __ATTR_RO(ap_resume_reason_stastics);
#endif /* VENDOR_EDIT */

static struct attribute *attrs[] = {
	&resume_reason.attr,
	&suspend_time.attr,
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
    &ap_resume_reason_stastics.attr,
#endif /* VENDOR_EDIT */

	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

/*
 * logs all the wake up reasons to the kernel
 * stores the irqs to expose them to the userspace via sysfs
 */
void log_wakeup_reason(int irq)
{
	struct irq_desc *desc;
	desc = irq_to_desc(irq);
	if (desc && desc->action && desc->action->name)
		printk(KERN_INFO "Resume caused by IRQ %d, %s\n", irq,
				desc->action->name);
	else
		printk(KERN_INFO "Resume caused by IRQ %d\n", irq);

	spin_lock(&resume_reason_lock);
	if (irqcount == MAX_WAKEUP_REASON_IRQS) {
		spin_unlock(&resume_reason_lock);
		printk(KERN_WARNING "Resume caused by more than %d IRQs\n",
				MAX_WAKEUP_REASON_IRQS);
		return;
	}

	irq_list[irqcount++] = irq;
	spin_unlock(&resume_reason_lock);
}

int check_wakeup_reason(int irq)
{
	int irq_no;
	int ret = false;

	spin_lock(&resume_reason_lock);
	for (irq_no = 0; irq_no < irqcount; irq_no++)
		if (irq_list[irq_no] == irq) {
			ret = true;
			break;
	}
	spin_unlock(&resume_reason_lock);
	return ret;
}

void log_suspend_abort_reason(const char *fmt, ...)
{
	va_list args;

	spin_lock(&resume_reason_lock);

	//Suspend abort reason has already been logged.
	if (suspend_abort) {
		spin_unlock(&resume_reason_lock);
		return;
	}

	suspend_abort = true;
	va_start(args, fmt);
	vsnprintf(abort_reason, MAX_SUSPEND_ABORT_LEN, fmt, args);
	va_end(args);
	spin_unlock(&resume_reason_lock);
}

/* Detects a suspend and clears all the previous wake up reasons*/
static int wakeup_reason_pm_event(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		spin_lock(&resume_reason_lock);
		irqcount = 0;
		suspend_abort = false;
		spin_unlock(&resume_reason_lock);
		/* monotonic time since boot */
		last_monotime = ktime_get();
		/* monotonic time since boot including the time spent in suspend */
		last_stime = ktime_get_boottime();
		break;
	case PM_POST_SUSPEND:
		/* monotonic time since boot */
		curr_monotime = ktime_get();
		/* monotonic time since boot including the time spent in suspend */
		curr_stime = ktime_get_boottime();
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wakeup_reason_pm_notifier_block = {
	.notifier_call = wakeup_reason_pm_event,
};
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
static void wakeup_reason_count_clear(void)
{
    printk(KERN_INFO  "ENTER %s\n", __func__);
    alarm_count = 0;
	wakeup_source_count_rtc = 0;
	wakeup_source_count_wifi = 0;
	wakeup_source_count_modem = 0;
	wakeup_source_count_kpdpwr = 0;
    wakeup_source_count_adsp = 0;
    wakeup_source_count_dsps = 0;
}

static void wakeup_reason_count_out(void)
{
	printk(KERN_INFO   "%s wakeup %lld times\n","wcnss_wlan",wakeup_source_count_wifi);
	printk(KERN_INFO   "%s wakeup %lld times\n","qcom,smd-modem",wakeup_source_count_modem);
	printk(KERN_INFO   "%s wakeup %lld times\n","qpnp_rtc_alarm",wakeup_source_count_rtc);
	printk(KERN_INFO   "%s wakeup %lld times\n","power_key",wakeup_source_count_kpdpwr);
    printk(KERN_INFO   "%s wakeup %lld times\n","adsp",wakeup_source_count_adsp);
    printk(KERN_INFO   "%s wakeup %lld times\n","dsps",wakeup_source_count_dsps);
	printk(KERN_INFO  "ENTER %s\n", __func__);
}

static int wakeup_src_fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

#ifdef CONFIG_DRM_MSM
        if (evdata && evdata->data && event == MSM_DRM_EVENT_BLANK)
#else
        if (evdata && evdata->data && event == FB_EVENT_BLANK)
#endif
        {
            blank = evdata->data;

		#ifdef CONFIG_DRM_MSM
            if (*blank == MSM_DRM_BLANK_UNBLANK)
		#else
            if (*blank == FB_BLANK_UNBLANK)
		#endif
            {
                wakeup_reason_count_out();
            }
        }
#ifdef CONFIG_DRM_MSM
        else if (evdata && evdata->data && event == MSM_DRM_EARLY_EVENT_BLANK)
#else
        else if (evdata && evdata->data && event == FB_EARLY_EVENT_BLANK)
#endif
        {
                blank = evdata->data;

			#ifdef CONFIG_DRM_MSM
                if (*blank == MSM_DRM_BLANK_POWERDOWN)
			#else
                if (*blank == FB_BLANK_POWERDOWN)
			#endif
                {
                    wakeup_reason_count_clear();
                    printk(KERN_INFO  "%s, POWERDOWN\n", __func__);
                }
        }

	return 0;
}
static struct notifier_block wakeup_src_fb_notif = {
	.notifier_call = wakeup_src_fb_notifier_callback,
};

#endif /* VENDOR_EDIT */

/* Initializes the sysfs parameter
 * registers the pm_event notifier
 */
int __init wakeup_reason_init(void)
{
	int retval;

	retval = register_pm_notifier(&wakeup_reason_pm_notifier_block);
	if (retval)
		printk(KERN_WARNING "[%s] failed to register PM notifier %d\n",
				__func__, retval);

	wakeup_reason = kobject_create_and_add("wakeup_reasons", kernel_kobj);
	if (!wakeup_reason) {
		printk(KERN_WARNING "[%s] failed to create a sysfs kobject\n",
				__func__);
		return 1;
	}
	retval = sysfs_create_group(wakeup_reason, &attr_group);
	if (retval) {
		kobject_put(wakeup_reason);
		printk(KERN_WARNING "[%s] failed to create a sysfs group %d\n",
				__func__, retval);
	}

    #ifdef VENDOR_EDIT
    //PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
    #ifdef CONFIG_DRM_MSM
	msm_drm_register_client(&wakeup_src_fb_notif);
    #else
	fb_register_client(&wakeup_src_fb_notif);
    #endif
    #endif /* VENDOR_EDIT */
	return 0;
}

late_initcall(wakeup_reason_init);
