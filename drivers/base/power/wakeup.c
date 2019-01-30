/*
 * drivers/base/power/wakeup.c - System wakeup events framework
 *
 * Copyright (c) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/pm_wakeirq.h>
#include <linux/types.h>
#include <trace/events/power.h>
#ifdef VENDOR_EDIT
//Yanzhen.Feng@PSW.AD.OppoDebug.702252, 2016/06/21, Add for Sync App and Kernel time
#include <linux/rtc.h>
#endif /* VENDOR_EDIT */
#include <linux/irq.h>
#include <linux/irqdesc.h>

#include "power.h"
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
#define WAKEUP_SOURCE_MODEM 101
#define WAKEUP_SOURCE_MODEM_IPA	106
#define WAKEUP_SOURCE_KPDPWR 44
#define WAKEUP_SOURCE_WIFI_1ST 109
#define WAKEUP_SOURCE_WIFI_2ND 115
#define WAKEUP_SOURCE_WIFI_3RD 117
#define WAKEUP_SOURCE_WIFI_4TH 120
u64	wakeup_source_count_modem = 0;
u64 wakeup_source_count_kpdpwr = 0;
u64 wakeup_source_count_wifi = 0;
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/07 add for wakelock profiler
#include <linux/kobject.h>
#include <linux/sysfs.h>
#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif
#endif /* VENDOR_EDIT */

/*
 * If set, the suspend/hibernate code will abort transitions to a sleep state
 * if wakeup events are registered during or immediately before the transition.
 */
bool events_check_enabled __read_mostly;

/* First wakeup IRQ seen by the kernel in the last cycle. */
unsigned int pm_wakeup_irq __read_mostly;

/* If set and the system is suspending, terminate the suspend. */
static bool pm_abort_suspend __read_mostly;

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Power.1104067, 2016/05/12, Add for modem wake up source
extern u16 modem_wakeup_source;
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
//liuhd@PSW.CN.WiFi.Hardware.1202765,2017/12/10,add for the irq of wlan when system wakeuped by wlan
#define WLAN_WAKEUP_IRQ_NUMBER	111
#endif //VENDOR_EDIT

/*
 * Combined counters of registered wakeup events and wakeup events in progress.
 * They need to be modified together atomically, so it's better to use one
 * atomic variable to hold them both.
 */
static atomic_t combined_event_count = ATOMIC_INIT(0);
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/07 add for kernel wakelock time statistics
static atomic_t ws_all_release_flag = ATOMIC_INIT(1);
static ktime_t ws_start_node;
static ktime_t ws_end_node;
static ktime_t ws_hold_all_time;
static ktime_t reset_time;
static spinlock_t statistics_lock;
#endif /* VENDOR_EDIT */

#define IN_PROGRESS_BITS	(sizeof(int) * 4)
#define MAX_IN_PROGRESS		((1 << IN_PROGRESS_BITS) - 1)

static void split_counters(unsigned int *cnt, unsigned int *inpr)
{
	unsigned int comb = atomic_read(&combined_event_count);

	*cnt = (comb >> IN_PROGRESS_BITS);
	*inpr = comb & MAX_IN_PROGRESS;
}

/* A preserved old value of the events counter. */
static unsigned int saved_count;

static DEFINE_SPINLOCK(events_lock);

static void pm_wakeup_timer_fn(unsigned long data);

static LIST_HEAD(wakeup_sources);

static DECLARE_WAIT_QUEUE_HEAD(wakeup_count_wait_queue);

DEFINE_STATIC_SRCU(wakeup_srcu);

static struct wakeup_source deleted_ws = {
	.name = "deleted",
	.lock =  __SPIN_LOCK_UNLOCKED(deleted_ws.lock),
};

/**
 * wakeup_source_prepare - Prepare a new wakeup source for initialization.
 * @ws: Wakeup source to prepare.
 * @name: Pointer to the name of the new wakeup source.
 *
 * Callers must ensure that the @name string won't be freed when @ws is still in
 * use.
 */
void wakeup_source_prepare(struct wakeup_source *ws, const char *name)
{
	if (ws) {
		memset(ws, 0, sizeof(*ws));
		ws->name = name;
	}
}
EXPORT_SYMBOL_GPL(wakeup_source_prepare);

/**
 * wakeup_source_create - Create a struct wakeup_source object.
 * @name: Name of the new wakeup source.
 */
struct wakeup_source *wakeup_source_create(const char *name)
{
	struct wakeup_source *ws;

	ws = kmalloc(sizeof(*ws), GFP_KERNEL);
	if (!ws)
		return NULL;

	wakeup_source_prepare(ws, name ? kstrdup_const(name, GFP_KERNEL) : NULL);
	return ws;
}
EXPORT_SYMBOL_GPL(wakeup_source_create);

/**
 * wakeup_source_drop - Prepare a struct wakeup_source object for destruction.
 * @ws: Wakeup source to prepare for destruction.
 *
 * Callers must ensure that __pm_stay_awake() or __pm_wakeup_event() will never
 * be run in parallel with this function for the same wakeup source object.
 */
void wakeup_source_drop(struct wakeup_source *ws)
{
	if (!ws)
		return;

	del_timer_sync(&ws->timer);
	__pm_relax(ws);
}
EXPORT_SYMBOL_GPL(wakeup_source_drop);

/*
 * Record wakeup_source statistics being deleted into a dummy wakeup_source.
 */
static void wakeup_source_record(struct wakeup_source *ws)
{
	unsigned long flags;

	spin_lock_irqsave(&deleted_ws.lock, flags);

	if (ws->event_count) {
		deleted_ws.total_time =
			ktime_add(deleted_ws.total_time, ws->total_time);
		deleted_ws.prevent_sleep_time =
			ktime_add(deleted_ws.prevent_sleep_time,
				  ws->prevent_sleep_time);
		deleted_ws.max_time =
			ktime_compare(deleted_ws.max_time, ws->max_time) > 0 ?
				deleted_ws.max_time : ws->max_time;
		deleted_ws.event_count += ws->event_count;
		deleted_ws.active_count += ws->active_count;
		deleted_ws.relax_count += ws->relax_count;
		deleted_ws.expire_count += ws->expire_count;
		deleted_ws.wakeup_count += ws->wakeup_count;
	}

	spin_unlock_irqrestore(&deleted_ws.lock, flags);
}

/**
 * wakeup_source_destroy - Destroy a struct wakeup_source object.
 * @ws: Wakeup source to destroy.
 *
 * Use only for wakeup source objects created with wakeup_source_create().
 */
void wakeup_source_destroy(struct wakeup_source *ws)
{
	if (!ws)
		return;

	wakeup_source_drop(ws);
	wakeup_source_record(ws);
	kfree_const(ws->name);
	kfree(ws);
}
EXPORT_SYMBOL_GPL(wakeup_source_destroy);

/**
 * wakeup_source_add - Add given object to the list of wakeup sources.
 * @ws: Wakeup source object to add to the list.
 */
void wakeup_source_add(struct wakeup_source *ws)
{
	unsigned long flags;

	if (WARN_ON(!ws))
		return;

	spin_lock_init(&ws->lock);
	setup_timer(&ws->timer, pm_wakeup_timer_fn, (unsigned long)ws);
	ws->active = false;
	ws->last_time = ktime_get();

	spin_lock_irqsave(&events_lock, flags);
	list_add_rcu(&ws->entry, &wakeup_sources);
	spin_unlock_irqrestore(&events_lock, flags);
}
EXPORT_SYMBOL_GPL(wakeup_source_add);

/**
 * wakeup_source_remove - Remove given object from the wakeup sources list.
 * @ws: Wakeup source object to remove from the list.
 */
void wakeup_source_remove(struct wakeup_source *ws)
{
	unsigned long flags;

	if (WARN_ON(!ws))
		return;

	spin_lock_irqsave(&events_lock, flags);
	list_del_rcu(&ws->entry);
	spin_unlock_irqrestore(&events_lock, flags);
	synchronize_srcu(&wakeup_srcu);
}
EXPORT_SYMBOL_GPL(wakeup_source_remove);

/**
 * wakeup_source_register - Create wakeup source and add it to the list.
 * @name: Name of the wakeup source to register.
 */
struct wakeup_source *wakeup_source_register(const char *name)
{
	struct wakeup_source *ws;

	ws = wakeup_source_create(name);
	if (ws)
		wakeup_source_add(ws);

	return ws;
}
EXPORT_SYMBOL_GPL(wakeup_source_register);

/**
 * wakeup_source_unregister - Remove wakeup source from the list and remove it.
 * @ws: Wakeup source object to unregister.
 */
void wakeup_source_unregister(struct wakeup_source *ws)
{
	if (ws) {
		wakeup_source_remove(ws);
		wakeup_source_destroy(ws);
	}
}
EXPORT_SYMBOL_GPL(wakeup_source_unregister);

/**
 * device_wakeup_attach - Attach a wakeup source object to a device object.
 * @dev: Device to handle.
 * @ws: Wakeup source object to attach to @dev.
 *
 * This causes @dev to be treated as a wakeup device.
 */
static int device_wakeup_attach(struct device *dev, struct wakeup_source *ws)
{
	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		spin_unlock_irq(&dev->power.lock);
		return -EEXIST;
	}
	dev->power.wakeup = ws;
	if (dev->power.wakeirq)
		device_wakeup_attach_irq(dev, dev->power.wakeirq);
	spin_unlock_irq(&dev->power.lock);
	return 0;
}

/**
 * device_wakeup_enable - Enable given device to be a wakeup source.
 * @dev: Device to handle.
 *
 * Create a wakeup source object, register it and attach it to @dev.
 */
int device_wakeup_enable(struct device *dev)
{
	struct wakeup_source *ws;
	int ret;

	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	ws = wakeup_source_register(dev_name(dev));
	if (!ws)
		return -ENOMEM;

	ret = device_wakeup_attach(dev, ws);
	if (ret)
		wakeup_source_unregister(ws);

	return ret;
}
EXPORT_SYMBOL_GPL(device_wakeup_enable);

/**
 * device_wakeup_attach_irq - Attach a wakeirq to a wakeup source
 * @dev: Device to handle
 * @wakeirq: Device specific wakeirq entry
 *
 * Attach a device wakeirq to the wakeup source so the device
 * wake IRQ can be configured automatically for suspend and
 * resume.
 *
 * Call under the device's power.lock lock.
 */
int device_wakeup_attach_irq(struct device *dev,
			     struct wake_irq *wakeirq)
{
	struct wakeup_source *ws;

	ws = dev->power.wakeup;
	if (!ws) {
		dev_err(dev, "forgot to call call device_init_wakeup?\n");
		return -EINVAL;
	}

	if (ws->wakeirq)
		return -EEXIST;

	ws->wakeirq = wakeirq;
	return 0;
}

/**
 * device_wakeup_detach_irq - Detach a wakeirq from a wakeup source
 * @dev: Device to handle
 *
 * Removes a device wakeirq from the wakeup source.
 *
 * Call under the device's power.lock lock.
 */
void device_wakeup_detach_irq(struct device *dev)
{
	struct wakeup_source *ws;

	ws = dev->power.wakeup;
	if (ws)
		ws->wakeirq = NULL;
}

/**
 * device_wakeup_arm_wake_irqs(void)
 *
 * Itereates over the list of device wakeirqs to arm them.
 */
void device_wakeup_arm_wake_irqs(void)
{
	struct wakeup_source *ws;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry)
		dev_pm_arm_wake_irq(ws->wakeirq);
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

/**
 * device_wakeup_disarm_wake_irqs(void)
 *
 * Itereates over the list of device wakeirqs to disarm them.
 */
void device_wakeup_disarm_wake_irqs(void)
{
	struct wakeup_source *ws;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry)
		dev_pm_disarm_wake_irq(ws->wakeirq);
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

/**
 * device_wakeup_detach - Detach a device's wakeup source object from it.
 * @dev: Device to detach the wakeup source object from.
 *
 * After it returns, @dev will not be treated as a wakeup device any more.
 */
static struct wakeup_source *device_wakeup_detach(struct device *dev)
{
	struct wakeup_source *ws;

	spin_lock_irq(&dev->power.lock);
	ws = dev->power.wakeup;
	dev->power.wakeup = NULL;
	spin_unlock_irq(&dev->power.lock);
	return ws;
}

/**
 * device_wakeup_disable - Do not regard a device as a wakeup source any more.
 * @dev: Device to handle.
 *
 * Detach the @dev's wakeup source object from it, unregister this wakeup source
 * object and destroy it.
 */
int device_wakeup_disable(struct device *dev)
{
	struct wakeup_source *ws;

	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	ws = device_wakeup_detach(dev);
	wakeup_source_unregister(ws);
	return 0;
}
EXPORT_SYMBOL_GPL(device_wakeup_disable);

/**
 * device_set_wakeup_capable - Set/reset device wakeup capability flag.
 * @dev: Device to handle.
 * @capable: Whether or not @dev is capable of waking up the system from sleep.
 *
 * If @capable is set, set the @dev's power.can_wakeup flag and add its
 * wakeup-related attributes to sysfs.  Otherwise, unset the @dev's
 * power.can_wakeup flag and remove its wakeup-related attributes from sysfs.
 *
 * This function may sleep and it can't be called from any context where
 * sleeping is not allowed.
 */
void device_set_wakeup_capable(struct device *dev, bool capable)
{
	if (!!dev->power.can_wakeup == !!capable)
		return;

	if (device_is_registered(dev) && !list_empty(&dev->power.entry)) {
		if (capable) {
			if (wakeup_sysfs_add(dev))
				return;
		} else {
			wakeup_sysfs_remove(dev);
		}
	}
	dev->power.can_wakeup = capable;
}
EXPORT_SYMBOL_GPL(device_set_wakeup_capable);

/**
 * device_init_wakeup - Device wakeup initialization.
 * @dev: Device to handle.
 * @enable: Whether or not to enable @dev as a wakeup device.
 *
 * By default, most devices should leave wakeup disabled.  The exceptions are
 * devices that everyone expects to be wakeup sources: keyboards, power buttons,
 * possibly network interfaces, etc.  Also, devices that don't generate their
 * own wakeup requests but merely forward requests from one bus to another
 * (like PCI bridges) should have wakeup enabled by default.
 */
int device_init_wakeup(struct device *dev, bool enable)
{
	int ret = 0;

	if (!dev)
		return -EINVAL;

	if (enable) {
		device_set_wakeup_capable(dev, true);
		ret = device_wakeup_enable(dev);
	} else {
		if (dev->power.can_wakeup)
			device_wakeup_disable(dev);

		device_set_wakeup_capable(dev, false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(device_init_wakeup);

/**
 * device_set_wakeup_enable - Enable or disable a device to wake up the system.
 * @dev: Device to handle.
 */
int device_set_wakeup_enable(struct device *dev, bool enable)
{
	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	return enable ? device_wakeup_enable(dev) : device_wakeup_disable(dev);
}
EXPORT_SYMBOL_GPL(device_set_wakeup_enable);

/**
 * wakeup_source_not_registered - validate the given wakeup source.
 * @ws: Wakeup source to be validated.
 */
static bool wakeup_source_not_registered(struct wakeup_source *ws)
{
	/*
	 * Use timer struct to check if the given source is initialized
	 * by wakeup_source_add.
	 */
	return ws->timer.function != pm_wakeup_timer_fn ||
		   ws->timer.data != (unsigned long)ws;
}

/*
 * The functions below use the observation that each wakeup event starts a
 * period in which the system should not be suspended.  The moment this period
 * will end depends on how the wakeup event is going to be processed after being
 * detected and all of the possible cases can be divided into two distinct
 * groups.
 *
 * First, a wakeup event may be detected by the same functional unit that will
 * carry out the entire processing of it and possibly will pass it to user space
 * for further processing.  In that case the functional unit that has detected
 * the event may later "close" the "no suspend" period associated with it
 * directly as soon as it has been dealt with.  The pair of pm_stay_awake() and
 * pm_relax(), balanced with each other, is supposed to be used in such
 * situations.
 *
 * Second, a wakeup event may be detected by one functional unit and processed
 * by another one.  In that case the unit that has detected it cannot really
 * "close" the "no suspend" period associated with it, unless it knows in
 * advance what's going to happen to the event during processing.  This
 * knowledge, however, may not be available to it, so it can simply specify time
 * to wait before the system can be suspended and pass it as the second
 * argument of pm_wakeup_event().
 *
 * It is valid to call pm_relax() after pm_wakeup_event(), in which case the
 * "no suspend" period will be ended either by the pm_relax(), or by the timer
 * function executed when the timer expires, whichever comes first.
 */

/**
 * wakup_source_activate - Mark given wakeup source as active.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and, if @ws has just been activated, notify the PM
 * core of the event by incrementing the counter of of wakeup events being
 * processed.
 */
static void wakeup_source_activate(struct wakeup_source *ws)
{
	unsigned int cec;

	if (WARN_ONCE(wakeup_source_not_registered(ws),
			"unregistered wakeup source\n"))
		return;

	#ifdef VENDOR_EDIT
	//PengNan@BSP.Power.Basic 2018/06/07 add for kernel wakelock time statistics
	if(atomic_read(&ws_all_release_flag)) {
		atomic_set(&ws_all_release_flag, 0);
		spin_lock(&statistics_lock);
		ws_start_node = ktime_get();
		spin_unlock(&statistics_lock);
	}
	#endif /* VENDOR_EDIT */
	/*
	 * active wakeup source should bring the system
	 * out of PM_SUSPEND_FREEZE state
	 */
	freeze_wake();

	ws->active = true;
	ws->active_count++;
	ws->last_time = ktime_get();
	if (ws->autosleep_enabled)
		ws->start_prevent_time = ws->last_time;

	/* Increment the counter of events in progress. */
	cec = atomic_inc_return(&combined_event_count);

	trace_wakeup_source_activate(ws->name, cec);
}

/**
 * wakeup_source_report_event - Report wakeup event using the given source.
 * @ws: Wakeup source to report the event for.
 */
static void wakeup_source_report_event(struct wakeup_source *ws)
{
	ws->event_count++;
	/* This is racy, but the counter is approximate anyway. */
	if (events_check_enabled)
		ws->wakeup_count++;

	if (!ws->active)
		wakeup_source_activate(ws);
}

/**
 * __pm_stay_awake - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * It is safe to call this function from interrupt context.
 */
void __pm_stay_awake(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);

	wakeup_source_report_event(ws);
	del_timer(&ws->timer);
	ws->timer_expires = 0;

	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_stay_awake);

/**
 * pm_stay_awake - Notify the PM core that a wakeup event is being processed.
 * @dev: Device the wakeup event is related to.
 *
 * Notify the PM core of a wakeup event (signaled by @dev) by calling
 * __pm_stay_awake for the @dev's wakeup source object.
 *
 * Call this function after detecting of a wakeup event if pm_relax() is going
 * to be called directly after processing the event (and possibly passing it to
 * user space for further processing).
 */
void pm_stay_awake(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_stay_awake(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_stay_awake);

#ifdef CONFIG_PM_AUTOSLEEP
static void update_prevent_sleep_time(struct wakeup_source *ws, ktime_t now)
{
	ktime_t delta = ktime_sub(now, ws->start_prevent_time);
	ws->prevent_sleep_time = ktime_add(ws->prevent_sleep_time, delta);
}
#else
static inline void update_prevent_sleep_time(struct wakeup_source *ws,
					     ktime_t now) {}
#endif

/**
 * wakup_source_deactivate - Mark given wakeup source as inactive.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and notify the PM core that the wakeup source has
 * become inactive by decrementing the counter of wakeup events being processed
 * and incrementing the counter of registered wakeup events.
 */
static void wakeup_source_deactivate(struct wakeup_source *ws)
{
	unsigned int cnt, inpr, cec;
	ktime_t duration;
	ktime_t now;

	ws->relax_count++;
	/*
	 * __pm_relax() may be called directly or from a timer function.
	 * If it is called directly right after the timer function has been
	 * started, but before the timer function calls __pm_relax(), it is
	 * possible that __pm_stay_awake() will be called in the meantime and
	 * will set ws->active.  Then, ws->active may be cleared immediately
	 * by the __pm_relax() called from the timer function, but in such a
	 * case ws->relax_count will be different from ws->active_count.
	 */
	if (ws->relax_count != ws->active_count) {
		ws->relax_count--;
		return;
	}

	ws->active = false;

	now = ktime_get();
	duration = ktime_sub(now, ws->last_time);
	ws->total_time = ktime_add(ws->total_time, duration);
	if (ktime_to_ns(duration) > ktime_to_ns(ws->max_time))
		ws->max_time = duration;

	ws->last_time = now;
	del_timer(&ws->timer);
	ws->timer_expires = 0;

	if (ws->autosleep_enabled)
		update_prevent_sleep_time(ws, now);

	/*
	 * Increment the counter of registered wakeup events and decrement the
	 * couter of wakeup events in progress simultaneously.
	 */
	cec = atomic_add_return(MAX_IN_PROGRESS, &combined_event_count);
	trace_wakeup_source_deactivate(ws->name, cec);

	split_counters(&cnt, &inpr);
	if (!inpr && waitqueue_active(&wakeup_count_wait_queue)) {
		#ifdef VENDOR_EDIT
		//PengNan@BSP.Power.Basic 2018/06/07 add for kernel wakelock time statistics
		ktime_t ws_hold_delta = ktime_set(0, 0);
		atomic_set(&ws_all_release_flag, 1);
		spin_lock(&statistics_lock);
		ws_end_node = ktime_get();
		ws_hold_delta = ktime_sub(ws_end_node, ws_start_node);
		ws_hold_all_time = ktime_add(ws_hold_all_time, ws_hold_delta);
		spin_unlock(&statistics_lock);
		#endif /* VENDOR_EDIT */
		wake_up(&wakeup_count_wait_queue);
	}
}

/**
 * __pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * Call this function for wakeup events whose processing started with calling
 * __pm_stay_awake().
 *
 * It is safe to call it from interrupt context.
 */
void __pm_relax(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);
	if (ws->active)
		wakeup_source_deactivate(ws);
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_relax);

/**
 * pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @dev: Device that signaled the event.
 *
 * Execute __pm_relax() for the @dev's wakeup source object.
 */
void pm_relax(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_relax(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_relax);

/**
 * pm_wakeup_timer_fn - Delayed finalization of a wakeup event.
 * @data: Address of the wakeup source object associated with the event source.
 *
 * Call wakeup_source_deactivate() for the wakeup source whose address is stored
 * in @data if it is currently active and its timer has not been canceled and
 * the expiration time of the timer is not in future.
 */
static void pm_wakeup_timer_fn(unsigned long data)
{
	struct wakeup_source *ws = (struct wakeup_source *)data;
	unsigned long flags;

	spin_lock_irqsave(&ws->lock, flags);

	if (ws->active && ws->timer_expires
	    && time_after_eq(jiffies, ws->timer_expires)) {
		wakeup_source_deactivate(ws);
		ws->expire_count++;
	}

	spin_unlock_irqrestore(&ws->lock, flags);
}

/**
 * __pm_wakeup_event - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the event source.
 * @msec: Anticipated event processing time (in milliseconds).
 *
 * Notify the PM core of a wakeup event whose source is @ws that will take
 * approximately @msec milliseconds to be processed by the kernel.  If @ws is
 * not active, activate it.  If @msec is nonzero, set up the @ws' timer to
 * execute pm_wakeup_timer_fn() in future.
 *
 * It is safe to call this function from interrupt context.
 */
void __pm_wakeup_event(struct wakeup_source *ws, unsigned int msec)
{
	unsigned long flags;
	unsigned long expires;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);

	wakeup_source_report_event(ws);

	if (!msec) {
		wakeup_source_deactivate(ws);
		goto unlock;
	}

	expires = jiffies + msecs_to_jiffies(msec);
	if (!expires)
		expires = 1;

	if (!ws->timer_expires || time_after(expires, ws->timer_expires)) {
		mod_timer(&ws->timer, expires);
		ws->timer_expires = expires;
	}

 unlock:
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_wakeup_event);


/**
 * pm_wakeup_event - Notify the PM core of a wakeup event.
 * @dev: Device the wakeup event is related to.
 * @msec: Anticipated event processing time (in milliseconds).
 *
 * Call __pm_wakeup_event() for the @dev's wakeup source object.
 */
void pm_wakeup_event(struct device *dev, unsigned int msec)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_wakeup_event(dev->power.wakeup, msec);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_wakeup_event);

void pm_get_active_wakeup_sources(char *pending_wakeup_source, size_t max)
{
	struct wakeup_source *ws, *last_active_ws = NULL;
	int len = 0;
	bool active = false;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		if (ws->active && len < max) {
			if (!active)
				len += scnprintf(pending_wakeup_source, max,
						"Pending Wakeup Sources: ");
			len += scnprintf(pending_wakeup_source + len, max - len,
				"%s ", ws->name);
			active = true;
		} else if (!active &&
			   (!last_active_ws ||
			    ktime_to_ns(ws->last_time) >
			    ktime_to_ns(last_active_ws->last_time))) {
			last_active_ws = ws;
		}
	}
	if (!active && last_active_ws) {
		scnprintf(pending_wakeup_source, max,
				"Last active Wakeup Source: %s",
				last_active_ws->name);
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}
EXPORT_SYMBOL_GPL(pm_get_active_wakeup_sources);

void pm_print_active_wakeup_sources(void)
{
	struct wakeup_source *ws;
	int srcuidx, active = 0;
	struct wakeup_source *last_activity_ws = NULL;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		if (ws->active) {
			pr_info("active wakeup source: %s\n", ws->name);
			active = 1;
		} else if (!active &&
			   (!last_activity_ws ||
			    ktime_to_ns(ws->last_time) >
			    ktime_to_ns(last_activity_ws->last_time))) {
			last_activity_ws = ws;
		}
	}

    if (!active && last_activity_ws)
		pr_info("last active wakeup source: %s\n",
			last_activity_ws->name);

	srcu_read_unlock(&wakeup_srcu, srcuidx);
}
EXPORT_SYMBOL_GPL(pm_print_active_wakeup_sources);

/**
 * pm_wakeup_pending - Check if power transition in progress should be aborted.
 *
 * Compare the current number of registered wakeup events with its preserved
 * value from the past and return true if new wakeup events have been registered
 * since the old value was stored.  Also return true if the current number of
 * wakeup events being processed is different from zero.
 */
bool pm_wakeup_pending(void)
{
	unsigned long flags;
	bool ret = false;

	spin_lock_irqsave(&events_lock, flags);
	if (events_check_enabled) {
		unsigned int cnt, inpr;

		split_counters(&cnt, &inpr);
		ret = (cnt != saved_count || inpr > 0);
		events_check_enabled = !ret;
	}
	spin_unlock_irqrestore(&events_lock, flags);

#ifndef VENDOR_EDIT
/*yixue.ge@bsp.drv modify for maybe pm_abort_suspend happend here*/
	if (ret) {
#else
	if (ret || pm_abort_suspend) {
#endif
		pr_info("PM: Wakeup pending, aborting suspend\n");
		pm_print_active_wakeup_sources();
	}

	return ret || pm_abort_suspend;
}

void pm_system_wakeup(void)
{
	pm_abort_suspend = true;
	freeze_wake();
}
EXPORT_SYMBOL_GPL(pm_system_wakeup);

void pm_wakeup_clear(void)
{
	pm_abort_suspend = false;
	pm_wakeup_irq = 0;
}

void pm_system_irq_wakeup(unsigned int irq_number)
{
	struct irq_desc *desc;
	const char *name = "null";

	if (pm_wakeup_irq == 0) {
		if (msm_show_resume_irq_mask) {
			desc = irq_to_desc(irq_number);
			if (desc == NULL)
				name = "stray irq";
			else if (desc->action && desc->action->name)
				name = desc->action->name;

			pr_warn("%s: %d triggered %s\n", __func__,
					irq_number, name);
            #ifdef VENDOR_EDIT
            //PengNan@BSP.Power.Basic, 2018/03/26, add for analysis power coumption.
            if((irq_number >= WAKEUP_SOURCE_WIFI_1ST && irq_number <= WAKEUP_SOURCE_WIFI_2ND) || 
                (irq_number >= WAKEUP_SOURCE_WIFI_3RD && irq_number <= WAKEUP_SOURCE_WIFI_4TH)) {
                wakeup_source_count_wifi++;
            }
            if(irq_number == WAKEUP_SOURCE_MODEM) {
                wakeup_source_count_modem++;
            }
            if(irq_number == wakeup_source_count_kpdpwr) {
                wakeup_source_count_kpdpwr++;
            }
            #endif /*VENDOR_EDIT*/
            #ifdef VENDOR_EDIT
            //liuhd@PSW.CN.WiFi.Hardware.1202765,2017/12/10,add for the irq of wlan when system wakeuped by wlan
           if (irq_number == WLAN_WAKEUP_IRQ_NUMBER){
               pr_warn("triggered by wlan side\n");
               //modem_wakeup_source = 0;
               //schedule_work(&wakeup_reason_work);
            }
            #endif //VENDOR_EDIT

		}
		pm_wakeup_irq = irq_number;
		pm_system_wakeup();
	}
}

/**
 * pm_get_wakeup_count - Read the number of registered wakeup events.
 * @count: Address to store the value at.
 * @block: Whether or not to block.
 *
 * Store the number of registered wakeup events at the address in @count.  If
 * @block is set, block until the current number of wakeup events being
 * processed is zero.
 *
 * Return 'false' if the current number of wakeup events being processed is
 * nonzero.  Otherwise return 'true'.
 */
bool pm_get_wakeup_count(unsigned int *count, bool block)
{
	unsigned int cnt, inpr;

	if (block) {
		DEFINE_WAIT(wait);

		for (;;) {
			prepare_to_wait(&wakeup_count_wait_queue, &wait,
					TASK_INTERRUPTIBLE);
			split_counters(&cnt, &inpr);
			if (inpr == 0 || signal_pending(current))
				break;

			schedule();
		}
		finish_wait(&wakeup_count_wait_queue, &wait);
	}

	split_counters(&cnt, &inpr);
	*count = cnt;
	return !inpr;
}

/**
 * pm_save_wakeup_count - Save the current number of registered wakeup events.
 * @count: Value to compare with the current number of registered wakeup events.
 *
 * If @count is equal to the current number of registered wakeup events and the
 * current number of wakeup events being processed is zero, store @count as the
 * old number of registered wakeup events for pm_check_wakeup_events(), enable
 * wakeup events detection and return 'true'.  Otherwise disable wakeup events
 * detection and return 'false'.
 */
bool pm_save_wakeup_count(unsigned int count)
{
	unsigned int cnt, inpr;
	unsigned long flags;

	events_check_enabled = false;
	spin_lock_irqsave(&events_lock, flags);
	split_counters(&cnt, &inpr);
	if (cnt == count && inpr == 0) {
		saved_count = count;
		events_check_enabled = true;
	}
	spin_unlock_irqrestore(&events_lock, flags);
	return events_check_enabled;
}

#ifdef CONFIG_PM_AUTOSLEEP
/**
 * pm_wakep_autosleep_enabled - Modify autosleep_enabled for all wakeup sources.
 * @enabled: Whether to set or to clear the autosleep_enabled flags.
 */
void pm_wakep_autosleep_enabled(bool set)
{
	struct wakeup_source *ws;
	ktime_t now = ktime_get();
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		spin_lock_irq(&ws->lock);
		if (ws->autosleep_enabled != set) {
			ws->autosleep_enabled = set;
			if (ws->active) {
				if (set)
					ws->start_prevent_time = now;
				else
					update_prevent_sleep_time(ws, now);
			}
		}
		spin_unlock_irq(&ws->lock);
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}
#endif /* CONFIG_PM_AUTOSLEEP */

static struct dentry *wakeup_sources_stats_dentry;

/**
 * print_wakeup_source_stats - Print wakeup source statistics information.
 * @m: seq_file to print the statistics into.
 * @ws: Wakeup source object to print the statistics for.
 */
static int print_wakeup_source_stats(struct seq_file *m,
				     struct wakeup_source *ws)
{
	unsigned long flags;
	ktime_t total_time;
	ktime_t max_time;
	unsigned long active_count;
	ktime_t active_time;
	ktime_t prevent_sleep_time;

	spin_lock_irqsave(&ws->lock, flags);

	total_time = ws->total_time;
	max_time = ws->max_time;
	prevent_sleep_time = ws->prevent_sleep_time;
	active_count = ws->active_count;
	if (ws->active) {
		ktime_t now = ktime_get();

		active_time = ktime_sub(now, ws->last_time);
		total_time = ktime_add(total_time, active_time);
		if (active_time.tv64 > max_time.tv64)
			max_time = active_time;

		if (ws->autosleep_enabled)
			prevent_sleep_time = ktime_add(prevent_sleep_time,
				ktime_sub(now, ws->start_prevent_time));
	} else {
		active_time = ktime_set(0, 0);
	}

	seq_printf(m, "%-32s\t%lu\t\t%lu\t\t%lu\t\t%lu\t\t%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\n",
		   ws->name, active_count, ws->event_count,
		   ws->wakeup_count, ws->expire_count,
		   ktime_to_ms(active_time), ktime_to_ms(total_time),
		   ktime_to_ms(max_time), ktime_to_ms(ws->last_time),
		   ktime_to_ms(prevent_sleep_time));

	spin_unlock_irqrestore(&ws->lock, flags);

	return 0;
}

/**
 * wakeup_sources_stats_show - Print wakeup sources statistics information.
 * @m: seq_file to print the statistics into.
 */
static int wakeup_sources_stats_show(struct seq_file *m, void *unused)
{
	struct wakeup_source *ws;
	int srcuidx;

	seq_puts(m, "name\t\t\t\t\tactive_count\tevent_count\twakeup_count\t"
		"expire_count\tactive_since\ttotal_time\tmax_time\t"
		"last_change\tprevent_suspend_time\n");

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry)
		print_wakeup_source_stats(m, ws);
	srcu_read_unlock(&wakeup_srcu, srcuidx);

	print_wakeup_source_stats(m, &deleted_ws);

	return 0;
}

static int wakeup_sources_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, wakeup_sources_stats_show, NULL);
}

#ifdef VENDOR_EDIT
//Yanzhen.Feng@PSW.AD.OppoDebug.702252, 2015/08/14, Add for Sync App and Kernel time
static ssize_t watchdog_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	s32 value;
	struct timespec ts;
	struct rtc_time tm;

	if (count == sizeof(s32)) {
		if (copy_from_user(&value, buf, sizeof(s32)))
			return -EFAULT;
	} else if (count <= 11) { /* ASCII perhaps? */
		char ascii_value[11];
		unsigned long int ulval;
		int ret;

		if (copy_from_user(ascii_value, buf, count))
			return -EFAULT;

		if (count > 10) {
			if (ascii_value[10] == '\n')
				ascii_value[10] = '\0';
			else
				return -EINVAL;
		} else {
			ascii_value[count] = '\0';
		}
		ret = kstrtoul(ascii_value, 16, &ulval);
		if (ret) {
			pr_debug("%s, 0x%lx, 0x%x\n", ascii_value, ulval, ret);
			return -EINVAL;
		}
		value = (s32)lower_32_bits(ulval);
	} else {
		return -EINVAL;
	}

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
	pr_warn("!@WatchDog_%d; %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		value, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);

	return count;
}
#endif /* VENDOR_EDIT */

static const struct file_operations wakeup_sources_stats_fops = {
	.owner = THIS_MODULE,
	.open = wakeup_sources_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
#ifdef VENDOR_EDIT
//Yanzhen.Feng@PSW.AD.OppoDebug.702252, 2016/06/21, Add for Sync App and Kernel time
	.write          = watchdog_write,
#endif /* VENDOR_EDIT */
};

static int __init wakeup_sources_debugfs_init(void)
{
#ifndef VENDOR_EDIT
//Yanzhen.Feng@PSW.AD.OppoDebug.702252, 2016/06/21,  Modify for Sync App and Kernel time
	wakeup_sources_stats_dentry = debugfs_create_file("wakeup_sources",
			S_IRUGO, NULL, NULL, &wakeup_sources_stats_fops);
#else /* VENDOR_EDIT */
	wakeup_sources_stats_dentry = debugfs_create_file("wakeup_sources",
			S_IRUGO| S_IWUGO, NULL, NULL, &wakeup_sources_stats_fops);
#endif /* VENDOR_EDIT */
	return 0;
}

#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/07 add for wakelock profiler
ktime_t active_max_reset_time;
static ssize_t active_max_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int srcuidx;
	int max_ws_rate;
	ktime_t cur_ws_total;
	ktime_t max_ws_time;
	ktime_t wall_delta;
	char max_ws_name[40];
	int buf_offset = 0;
	unsigned long flags;
	struct wakeup_source *ws;

	max_ws_time = ktime_set(0, 0);
	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		spin_lock_irqsave(&ws->lock, flags);
		cur_ws_total = ws->total_time;
		if(ws->active) {
			ktime_t active_time;
			ktime_t now = ktime_get();
			active_time = ktime_sub(now, ws->last_time);
			cur_ws_total = ktime_add(ws->total_time, active_time);
		}
		if(ktime_compare(cur_ws_total, ws->total_time_backup) >= 0) {
			if(ktime_compare(ktime_sub(cur_ws_total, ws->total_time_backup), max_ws_time) > 0) {
				strncpy(max_ws_name, ws->name, sizeof(max_ws_name)-1);
				max_ws_time = ktime_sub(cur_ws_total, ws->total_time_backup);
			}
		}
		spin_unlock_irqrestore(&ws->lock, flags);
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);

	wall_delta = ktime_sub(ktime_get(), active_max_reset_time);
	max_ws_rate = ktime_compare(wall_delta, max_ws_time) >= 0 ? ktime_to_ms(max_ws_time)*100/ktime_to_ms(wall_delta) : 0;

	buf_offset += sprintf(buf + buf_offset, "Name\tTime(mS)\tRate(%%)\n");
	buf_offset += sprintf(buf + buf_offset, "%s\t%lld\t%d\n", max_ws_name, ktime_to_ms(max_ws_time), max_ws_rate);
	return buf_offset;
}

static void active_max_reset(void)
{
	int srcuidx;
	unsigned long flags;
	ktime_t total_time;
	struct wakeup_source *ws;

	printk("%s\n", __func__);
	active_max_reset_time = ktime_get();
	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		spin_lock_irqsave(&ws->lock, flags);
		total_time = ws->total_time;
		if(ws->active) {
			ktime_t active_time;
			ktime_t now = ktime_get();
			active_time = ktime_sub(now, ws->last_time);
			total_time = ktime_add(ws->total_time, active_time);
		}
		ws->total_time_backup = total_time;
		spin_unlock_irqrestore(&ws->lock, flags);
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

static ssize_t active_max_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	char reset_string[]="reset";

	if(strlen(reset_string) != (count-1))
		return count;

	if (strncmp(buf, reset_string, strlen(reset_string)) != 0)
		return count;

	active_max_reset();
	return count;
}

static inline bool ws_all_release(void)
{
	unsigned int cnt, inpr;
	split_counters(&cnt, &inpr);
	if(!inpr && waitqueue_active(&wakeup_count_wait_queue)) {
		return true;
	} else {
		return false;
	}
}

static void kernel_time_reset(void)
{
	ktime_t newest_hold_time;

	printk("%s\n", __func__);
	if(!ws_all_release()) {
		ktime_t offset_hold_time;
		ktime_t now = ktime_get();
		spin_lock(&statistics_lock);
		offset_hold_time = ktime_sub(now, ws_start_node);
		newest_hold_time = ktime_add(ws_hold_all_time, offset_hold_time);
		spin_unlock(&statistics_lock);
	}
	else {
		spin_lock(&statistics_lock);
		newest_hold_time = ws_hold_all_time;
		spin_unlock(&statistics_lock);
	}

	reset_time = newest_hold_time;
}

static ssize_t kernel_time_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	char reset_string[]="reset";

	if(strlen(reset_string) != (count-1))
		return count;

	if (strncmp(buf, reset_string, strlen(reset_string)) != 0)
		return count;

	kernel_time_reset();
	return count;
}

static ssize_t kernel_time_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int buf_offset = 0;
	ktime_t newest_hold_time;

	if(!ws_all_release()) {
		ktime_t offset_hold_time;
		ktime_t now = ktime_get();
		spin_lock(&statistics_lock);
		offset_hold_time = ktime_sub(now, ws_start_node);
		newest_hold_time = ktime_add(ws_hold_all_time, offset_hold_time);
	}
	else {
		spin_lock(&statistics_lock);
		newest_hold_time = ws_hold_all_time;
	}
	newest_hold_time = ktime_sub(newest_hold_time, reset_time);
	spin_unlock(&statistics_lock);
	buf_offset += sprintf(buf + buf_offset, "%lld\n", ktime_to_ms(newest_hold_time));
	return buf_offset;
}

static int ws_fb_notify_callback(struct notifier_block *nb, unsigned long val, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
#ifdef CONFIG_DRM_MSM
	if(val != MSM_DRM_EARLY_EVENT_BLANK && val != MSM_DRM_EVENT_BLANK)
#else
    if (val != FB_EVENT_BLANK && val != FB_EARLY_EVENT_BLANK)
#endif
	return 0;
    if(evdata && evdata->data) {
        blank = evdata->data;
        printk(KERN_INFO  "%s, val=%ld, blank=%d\n", __func__,val,*blank);
        #ifdef CONFIG_DRM_MSM
        if (*blank == MSM_DRM_BLANK_POWERDOWN) { //suspend
             if (val == MSM_DRM_EARLY_EVENT_BLANK) {    //early event
        #else
        if (*blank == FB_BLANK_POWERDOWN) { //suspend
            if (val == FB_EARLY_EVENT_BLANK) {    //early event
        #endif
                kernel_time_reset();
			    active_max_reset();
                printk(KERN_INFO  "%s, POWERDOWN\n", __func__);
             }
        }
        #ifdef CONFIG_DRM_MSM
        else if (*blank == MSM_DRM_BLANK_UNBLANK) { //resume
             if (val == MSM_DRM_EVENT_BLANK) {    //event
        #else
        else if (*blank == FB_BLANK_UNBLANK) { //resume
            if (val == FB_EVENT_BLANK) {    //event
        #endif
            printk(KERN_INFO  "%s, UNBLANK\n", __func__);
            }
        }
        
    }
	return NOTIFY_OK;
}

static struct notifier_block ws_fb_notify_block = {
	.notifier_call =  ws_fb_notify_callback,
};

static struct kobj_attribute active_max = __ATTR_RW(active_max);
static struct kobj_attribute kernel_time = __ATTR_RW(kernel_time);

static struct attribute *attrs[] = {
	&active_max.attr,
	&kernel_time.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *wakelock_profiler;

static int __init wakelock_profiler_init(void)
{
	int retval;

	spin_lock_init(&statistics_lock);
	active_max_reset_time = ktime_set(0, 0);
	wakelock_profiler = kobject_create_and_add("wakelock_profiler", kernel_kobj);
	if (!wakelock_profiler) {
		printk(KERN_WARNING "[%s] failed to create a sysfs kobject\n",
				__func__);
		return 1;
	}
	retval = sysfs_create_group(wakelock_profiler, &attr_group);
	if (retval) {
		kobject_put(wakelock_profiler);
		printk(KERN_WARNING "[%s] failed to create a sysfs group %d\n",
				__func__, retval);
	}
#if defined(CONFIG_DRM_MSM)
	retval = msm_drm_register_client(&ws_fb_notify_block);
	if (retval) {
		printk("%s error: register notifier failed,drm!\n", __func__);
	}
#elif defined(CONFIG_FB)
    retval = fb_register_client(&ws_fb_notify_block);
	if (retval) {
		printk("%s error: register notifier failed!\n", __func__);
	}
#endif

	return 0;
}
#endif /* VENDOR_EDIT */

postcore_initcall(wakeup_sources_debugfs_init);
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/07 add for wakelock profiler
postcore_initcall(wakelock_profiler_init);
#endif /* VENDOR_EDIT */
