/*
 * drivers/base/power/main.c - Where the driver meets power management.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 *
 * This file is released under the GPLv2
 *
 *
 * The driver model core calls device_pm_add() when a device is registered.
 * This will initialize the embedded device_pm_info object in the device
 * and add it to the list of power-controlled devices. sysfs entries for
 * controlling device power management will also be added.
 *
 * A separate list is used for keeping track of power info, because the power
 * domain dependencies may differ from the ancestral dependencies that the
 * subsystem list maintains.
 */

#include <linux/device.h>
#include <linux/kallsyms.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/resume-trace.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/async.h>
#include <linux/suspend.h>
#include <linux/cpuidle.h>
#include <linux/timer.h>
#include <linux/wakeup_reason.h>
#ifdef CONFIG_PM_WAKEUP_TIMES
#include <linux/math64.h>
#include <linux/wait.h>
#endif
#include <linux/aee.h>

#include "../base.h"
#include "power.h"

/* SUSPEND_RESUME_WAKELOCK_LOG-00+[ */
#include <linux/module.h>
#include <linux/moduleparam.h>

/* When enabled, we show driver resume time */
enum {
	DEBUG_ALL_DRIVER = 1U << 0, /* when enable this, print all */
	DEBUG_BY_DRIVER_NAME = 1U << 1,
	DEBUG_BY_FUNCTION_NAME = 1U << 2,
	DEBUG_BY_RESUME_TIME = 1U << 3,
	DEBUG_TOP_TEN = 1U << 4,
};

/* the file node will be /sys/module/main/parameters/debug_mask */

// %%TBTA: maybe need to set to 0 at user build
static uint __read_mostly debug_mask = (DEBUG_ALL_DRIVER | DEBUG_TOP_TEN);

module_param(debug_mask, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_mask, "mask for debugging resume time");
/* SUSPEND_RESUME_WAKELOCK_LOG-00+] */

// #define LOG

#define HIB_DPM_DEBUG 0
#define _TAG_HIB_M "HIB/DPM"
#if (HIB_DPM_DEBUG)
#undef hib_log
#define hib_log(fmt, ...)   pr_warn("[%s][%s]" fmt, _TAG_HIB_M, __func__, ##__VA_ARGS__);
#else
#define hib_log(fmt, ...)
#endif
#undef hib_warn
#define hib_warn(fmt, ...)  pr_warn("[%s][%s]" fmt, _TAG_HIB_M, __func__, ##__VA_ARGS__);

typedef int (*pm_callback_t)(struct device *);

/*
 * The entries in the dpm_list list are in a depth first order, simply
 * because children are guaranteed to be discovered after parents, and
 * are inserted at the back of the list on discovery.
 *
 * Since device_pm_add() may be called with a device lock held,
 * we must never try to acquire a device lock while holding
 * dpm_list_mutex.
 */

LIST_HEAD(dpm_list);
static LIST_HEAD(dpm_prepared_list);
static LIST_HEAD(dpm_suspended_list);
static LIST_HEAD(dpm_late_early_list);
static LIST_HEAD(dpm_noirq_list);

struct suspend_stats suspend_stats;
#ifdef CONFIG_PM_WAKEUP_TIMES
struct suspend_stats_queue suspend_stats_queue;
static ktime_t suspend_start_time;
static ktime_t resume_start_time;
#endif
static DEFINE_MUTEX(dpm_list_mtx);
static pm_message_t pm_transition;

struct dpm_watchdog {
	struct device		*dev;
	struct task_struct	*tsk;
	struct timer_list	timer;
};

static int async_error;

/**
 * device_pm_sleep_init - Initialize system suspend-related device fields.
 * @dev: Device object being initialized.
 */
void device_pm_sleep_init(struct device *dev)
{
	dev->power.is_prepared = false;
	dev->power.is_suspended = false;
	init_completion(&dev->power.completion);
	complete_all(&dev->power.completion);
	dev->power.wakeup = NULL;
	INIT_LIST_HEAD(&dev->power.entry);
}

/**
 * device_pm_lock - Lock the list of active devices used by the PM core.
 */
void device_pm_lock(void)
{
	mutex_lock(&dpm_list_mtx);
}

/**
 * device_pm_unlock - Unlock the list of active devices used by the PM core.
 */
void device_pm_unlock(void)
{
	mutex_unlock(&dpm_list_mtx);
}

/**
 * device_pm_add - Add a device to the PM core's list of active devices.
 * @dev: Device to add to the list.
 */
void device_pm_add(struct device *dev)
{
	pr_debug("PM: Adding info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus", dev_name(dev));
	mutex_lock(&dpm_list_mtx);
	if (dev->parent && dev->parent->power.is_prepared)
		dev_warn(dev, "parent %s should not be sleeping\n",
			dev_name(dev->parent));
	list_add_tail(&dev->power.entry, &dpm_list);
	mutex_unlock(&dpm_list_mtx);
}

/**
 * device_pm_remove - Remove a device from the PM core's list of active devices.
 * @dev: Device to be removed from the list.
 */
void device_pm_remove(struct device *dev)
{
	pr_debug("PM: Removing info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus", dev_name(dev));
	complete_all(&dev->power.completion);
	mutex_lock(&dpm_list_mtx);
	list_del_init(&dev->power.entry);
	mutex_unlock(&dpm_list_mtx);
	device_wakeup_disable(dev);
	pm_runtime_remove(dev);
}

/**
 * device_pm_move_before - Move device in the PM core's list of active devices.
 * @deva: Device to move in dpm_list.
 * @devb: Device @deva should come before.
 */
void device_pm_move_before(struct device *deva, struct device *devb)
{
	pr_debug("PM: Moving %s:%s before %s:%s\n",
		 deva->bus ? deva->bus->name : "No Bus", dev_name(deva),
		 devb->bus ? devb->bus->name : "No Bus", dev_name(devb));
	/* Delete deva from dpm_list and reinsert before devb. */
	list_move_tail(&deva->power.entry, &devb->power.entry);
}

/**
 * device_pm_move_after - Move device in the PM core's list of active devices.
 * @deva: Device to move in dpm_list.
 * @devb: Device @deva should come after.
 */
void device_pm_move_after(struct device *deva, struct device *devb)
{
	pr_debug("PM: Moving %s:%s after %s:%s\n",
		 deva->bus ? deva->bus->name : "No Bus", dev_name(deva),
		 devb->bus ? devb->bus->name : "No Bus", dev_name(devb));
	/* Delete deva from dpm_list and reinsert after devb. */
	list_move(&deva->power.entry, &devb->power.entry);
}

/**
 * device_pm_move_last - Move device to end of the PM core's list of devices.
 * @dev: Device to move in dpm_list.
 */
void device_pm_move_last(struct device *dev)
{
	pr_debug("PM: Moving %s:%s to end of list\n",
		 dev->bus ? dev->bus->name : "No Bus", dev_name(dev));
	list_move_tail(&dev->power.entry, &dpm_list);
}

static ktime_t initcall_debug_start(struct device *dev)
{
	ktime_t calltime = ktime_set(0, 0);

	if (pm_print_times_enabled) {
		pr_info("calling  %s+ @ %i, parent: %s\n",
			dev_name(dev), task_pid_nr(current),
			dev->parent ? dev_name(dev->parent) : "none");
		calltime = ktime_get();
	}

	return calltime;
}

static void initcall_debug_report(struct device *dev, ktime_t calltime,
				  int error)
{
	ktime_t delta, rettime;

	if (pm_print_times_enabled) {
		rettime = ktime_get();
		delta = ktime_sub(rettime, calltime);
		pr_info("call %s+ returned %d after %Ld usecs\n", dev_name(dev),
			error, (unsigned long long)ktime_to_ns(delta) >> 10);
	}
}

/**
 * dpm_wait - Wait for a PM operation to complete.
 * @dev: Device to wait for.
 * @async: If unset, wait only if the device's power.async_suspend flag is set.
 */
static void dpm_wait(struct device *dev, bool async)
{
	if (!dev)
		return;

	if (async || (pm_async_enabled && dev->power.async_suspend))
		wait_for_completion(&dev->power.completion);
}

static int dpm_wait_fn(struct device *dev, void *async_ptr)
{
	dpm_wait(dev, *((bool *)async_ptr));
	return 0;
}

static void dpm_wait_for_children(struct device *dev, bool async)
{
       device_for_each_child(dev, &async, dpm_wait_fn);
}

/**
 * pm_op - Return the PM operation appropriate for given PM event.
 * @ops: PM operations to choose from.
 * @state: PM transition of the system being carried out.
 */
static pm_callback_t pm_op(const struct dev_pm_ops *ops, pm_message_t state)
{
	switch (state.event) {
#ifdef CONFIG_SUSPEND
	case PM_EVENT_SUSPEND:
		return ops->suspend;
	case PM_EVENT_RESUME:
		return ops->resume;
#endif /* CONFIG_SUSPEND */
#ifdef CONFIG_HIBERNATE_CALLBACKS
	case PM_EVENT_FREEZE:
	case PM_EVENT_QUIESCE:
		return ops->freeze;
	case PM_EVENT_HIBERNATE:
		return ops->poweroff;
	case PM_EVENT_THAW:
	case PM_EVENT_RECOVER:
		return ops->thaw;
		break;
	case PM_EVENT_RESTORE:
		return ops->restore;
#endif /* CONFIG_HIBERNATE_CALLBACKS */
	}

	return NULL;
}

/**
 * pm_late_early_op - Return the PM operation appropriate for given PM event.
 * @ops: PM operations to choose from.
 * @state: PM transition of the system being carried out.
 *
 * Runtime PM is disabled for @dev while this function is being executed.
 */
static pm_callback_t pm_late_early_op(const struct dev_pm_ops *ops,
				      pm_message_t state)
{
	switch (state.event) {
#ifdef CONFIG_SUSPEND
	case PM_EVENT_SUSPEND:
		return ops->suspend_late;
	case PM_EVENT_RESUME:
		return ops->resume_early;
#endif /* CONFIG_SUSPEND */
#ifdef CONFIG_HIBERNATE_CALLBACKS
	case PM_EVENT_FREEZE:
	case PM_EVENT_QUIESCE:
		return ops->freeze_late;
	case PM_EVENT_HIBERNATE:
		return ops->poweroff_late;
	case PM_EVENT_THAW:
	case PM_EVENT_RECOVER:
		return ops->thaw_early;
	case PM_EVENT_RESTORE:
		return ops->restore_early;
#endif /* CONFIG_HIBERNATE_CALLBACKS */
	}

	return NULL;
}

/**
 * pm_noirq_op - Return the PM operation appropriate for given PM event.
 * @ops: PM operations to choose from.
 * @state: PM transition of the system being carried out.
 *
 * The driver of @dev will not receive interrupts while this function is being
 * executed.
 */
static pm_callback_t pm_noirq_op(const struct dev_pm_ops *ops, pm_message_t state)
{
	switch (state.event) {
#ifdef CONFIG_SUSPEND
	case PM_EVENT_SUSPEND:
		return ops->suspend_noirq;
	case PM_EVENT_RESUME:
		return ops->resume_noirq;
#endif /* CONFIG_SUSPEND */
#ifdef CONFIG_HIBERNATE_CALLBACKS
	case PM_EVENT_FREEZE:
	case PM_EVENT_QUIESCE:
		return ops->freeze_noirq;
	case PM_EVENT_HIBERNATE:
		return ops->poweroff_noirq;
	case PM_EVENT_THAW:
	case PM_EVENT_RECOVER:
		return ops->thaw_noirq;
	case PM_EVENT_RESTORE:
		return ops->restore_noirq;
#endif /* CONFIG_HIBERNATE_CALLBACKS */
	}

	return NULL;
}

static char *pm_verb(int event)
{
	switch (event) {
	case PM_EVENT_SUSPEND:
		return "suspend";
	case PM_EVENT_RESUME:
		return "resume";
	case PM_EVENT_FREEZE:
		return "freeze";
	case PM_EVENT_QUIESCE:
		return "quiesce";
	case PM_EVENT_HIBERNATE:
		return "hibernate";
	case PM_EVENT_THAW:
		return "thaw";
	case PM_EVENT_RESTORE:
		return "restore";
	case PM_EVENT_RECOVER:
		return "recover";
	default:
		return "(unknown PM event)";
	}
}

static void pm_dev_dbg(struct device *dev, pm_message_t state, char *info)
{
	dev_dbg(dev, "%s%s%s\n", info, pm_verb(state.event),
		((state.event & PM_EVENT_SLEEP) && device_may_wakeup(dev)) ?
		", may wakeup" : "");
}

static void pm_dev_err(struct device *dev, pm_message_t state, char *info,
			int error)
{
	printk(KERN_ERR "PM: Device %s failed to %s%s: error %d\n",
		dev_name(dev), pm_verb(state.event), info, error);
}

static void dpm_show_time(ktime_t starttime, pm_message_t state, char *info)
{
	ktime_t calltime;
	u64 usecs64;
	int usecs;

	calltime = ktime_get();
	usecs64 = ktime_to_ns(ktime_sub(calltime, starttime));
	do_div(usecs64, NSEC_PER_USEC);
	usecs = usecs64;
	if (usecs == 0)
		usecs = 1;
	hib_log("PM: %s%s%s of devices complete after %ld.%03ld msecs\n",
		info ?: "", info ? " " : "", pm_verb(state.event),
		usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
}

#ifdef CONFIG_PM_WAKEUP_TIMES
static void dpm_log_wakeup_stats(pm_message_t state, ktime_t *start_time,
        struct stats_wakeup_time *min_time,
        struct stats_wakeup_time *max_time,
        struct stats_wakeup_time *last_time,
        ktime_t *avg_time)
{
    ktime_t end_time, duration, prev_duration, sum;
    struct stats_wakeup_time prev;
    u64 avg_ns;
    char buf[32] = {0};
    unsigned int nr = 0;

    if (!ktime_to_ns(*start_time))
        return;

    switch (state.event) {
    case PM_EVENT_RESUME:
        snprintf(buf, sizeof(buf), "%s", "resume time:");
        break;
    case PM_EVENT_SUSPEND:
        snprintf(buf, sizeof(buf), "%s", "suspend time:");
        break;
    default:
        return;
    }

    end_time = ktime_get_boottime();
    prev = *last_time;
    prev_duration = ktime_sub(prev.end, prev.start);
    last_time->end = end_time;
    last_time->start = *start_time;
    duration = ktime_sub(end_time, *start_time);
    if (ktime_compare(duration,
        ktime_sub(max_time->end, max_time->start)) > 0)
        *max_time = *last_time;

    if (!ktime_to_ns(ktime_sub(min_time->end, min_time->start)))
        *min_time = *last_time;

    if (ktime_compare(duration,
        ktime_sub(min_time->end, min_time->start)) < 0)
        *min_time = *last_time;

    if (ktime_to_ns(prev_duration))
        nr++;

    if (ktime_to_ns(*avg_time))
        nr++;

    sum = ktime_add(ktime_add(*avg_time, prev_duration), duration);
    avg_ns = div_u64(ktime_to_ns(sum), (nr + 1));
    *avg_time = ktime_set(0, avg_ns);
    *start_time = ktime_set(0, 0);

    pr_debug("%s\n%s  %llums\n%s  %llums\n %s  %llums\n%s %llums\n", buf,
            "  min:",
            ktime_to_ms(ktime_sub(min_time->end, min_time->start)),
            "  max:",
            ktime_to_ms(ktime_sub(max_time->end, max_time->start)),
            "  last:", ktime_to_ms(duration),
            "  avg:", ktime_to_ms(*avg_time));
    suspend_stats_queue.resume_done = 1;
    wake_up(&suspend_stats_queue.wait_queue);
}

/**
 * log_resume_start - log resume start point.
 * @state: PM transition of the system being carried out.
 *
 */
void log_resume_start(pm_message_t state)
{
    if (state.event == PM_EVENT_RESUME)
        resume_start_time = ktime_get_boottime();
}
EXPORT_SYMBOL_GPL(log_resume_start);

/**
 * log_resume_end - log resume end point.
 * @state: PM transition of the system being carried out.
 *
 */
void log_resume_end(pm_message_t state)
{
    if (state.event == PM_EVENT_RESUME) {
        dpm_log_wakeup_stats(state, &resume_start_time,
            &suspend_stats.resume_min_time,
            &suspend_stats.resume_max_time,
            &suspend_stats.resume_last_time,
            &suspend_stats.resume_avg_time);
    } else {
        resume_start_time = ktime_set(0, 0);
    }
}
EXPORT_SYMBOL_GPL(log_resume_end);

/**
 * log_suspend_start - log suspend start point.
 * @state: PM transition of the system being carried out.
 *
 */
int log_suspend_start(pm_message_t state)
{
    if (state.event == PM_EVENT_SUSPEND)
        suspend_start_time = ktime_get_boottime();
    return 0;
}
EXPORT_SYMBOL_GPL(log_suspend_start);

/**
 * log_suspend_end - log suspend end point.
 * @state: PM transition of the system being carried out.
 *
 */
int log_suspend_end(pm_message_t state)
{
    if (state.event == PM_EVENT_SUSPEND) {
        dpm_log_wakeup_stats(state, &suspend_start_time,
            &suspend_stats.suspend_min_time,
            &suspend_stats.suspend_max_time,
            &suspend_stats.suspend_last_time,
            &suspend_stats.suspend_avg_time);
    } else {
        suspend_start_time = ktime_set(0, 0);
    }

    return 0;
}
EXPORT_SYMBOL_GPL(log_suspend_end);

#endif

static int dpm_run_callback(pm_callback_t cb, struct device *dev,
			    pm_message_t state, char *info)
{
	ktime_t calltime;
	int error;

	if (!cb)
		return 0;

	calltime = initcall_debug_start(dev);

	pm_dev_dbg(dev, state, info);
	error = cb(dev);
	suspend_report_result(cb, error);

	initcall_debug_report(dev, calltime, error);

	return error;
}

/**
 * dpm_wd_handler - Driver suspend / resume watchdog handler.
 *
 * Called when a driver has timed out suspending or resuming.
 * There's not much we can do here to recover so BUG() out for
 * a crash-dump
 */
static void dpm_wd_handler(unsigned long data)
{
	struct dpm_watchdog *wd = (void *)data;
	struct device *dev      = wd->dev;
	struct task_struct *tsk = wd->tsk;

	dev_emerg(dev, "**** DPM device timeout ****\n");
	show_stack(tsk, NULL);

	BUG();
}

/**
 * dpm_wd_set - Enable pm watchdog for given device.
 * @wd: Watchdog. Must be allocated on the stack.
 * @dev: Device to handle.
 */
static void dpm_wd_set(struct dpm_watchdog *wd, struct device *dev)
{
	struct timer_list *timer = &wd->timer;

	wd->dev = dev;
	wd->tsk = get_current();

	init_timer_on_stack(timer);
	timer->expires = jiffies + HZ * 12;
	timer->function = dpm_wd_handler;
	timer->data = (unsigned long)wd;
	add_timer(timer);
}

/**
 * dpm_wd_clear - Disable pm watchdog.
 * @wd: Watchdog to disable.
 */
static void dpm_wd_clear(struct dpm_watchdog *wd)
{
	struct timer_list *timer = &wd->timer;

	del_timer_sync(timer);
	destroy_timer_on_stack(timer);
}

/*------------------------- Resume routines -------------------------*/

/**
 * device_resume_noirq - Execute an "early resume" callback for given device.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 *
 * The driver of @dev will not receive interrupts while this function is being
 * executed.
 */
static int device_resume_noirq(struct device *dev, pm_message_t state)
{
	pm_callback_t callback = NULL;
	char *info = NULL;
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

	if (dev->power.syscore)
		goto Out;

	if (dev->pm_domain) {
		info = "noirq power domain ";
		callback = pm_noirq_op(&dev->pm_domain->ops, state);
	} else if (dev->type && dev->type->pm) {
		info = "noirq type ";
		callback = pm_noirq_op(dev->type->pm, state);
	} else if (dev->class && dev->class->pm) {
		info = "noirq class ";
		callback = pm_noirq_op(dev->class->pm, state);
	} else if (dev->bus && dev->bus->pm) {
		info = "noirq bus ";
		callback = pm_noirq_op(dev->bus->pm, state);
	}

	if (!callback && dev->driver && dev->driver->pm) {
		info = "noirq driver ";
		callback = pm_noirq_op(dev->driver->pm, state);
	}

	error = dpm_run_callback(callback, dev, state, info);

 Out:
	TRACE_RESUME(error);
	return error;
}

/**
 * dpm_resume_noirq - Execute "noirq resume" callbacks for all devices.
 * @state: PM transition of the system being carried out.
 *
 * Call the "noirq" resume handlers for all devices in dpm_noirq_list and
 * enable device drivers to receive interrupts.
 */
static void dpm_resume_noirq(pm_message_t state)
{
	ktime_t starttime = ktime_get();

	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_noirq_list)) {
		struct device *dev = to_device(dpm_noirq_list.next);
		int error;

		get_device(dev);
		list_move_tail(&dev->power.entry, &dpm_late_early_list);
		mutex_unlock(&dpm_list_mtx);

		error = device_resume_noirq(dev, state);
		if (error) {
			suspend_stats.failed_resume_noirq++;
			dpm_save_failed_step(SUSPEND_RESUME_NOIRQ);
			dpm_save_failed_dev(dev_name(dev));
			pm_dev_err(dev, state, " noirq", error);
		}

		mutex_lock(&dpm_list_mtx);
		put_device(dev);
	}
	mutex_unlock(&dpm_list_mtx);
	dpm_show_time(starttime, state, "noirq");
	resume_device_irqs();
	cpuidle_resume();
}

/**
 * device_resume_early - Execute an "early resume" callback for given device.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 *
 * Runtime PM is disabled for @dev while this function is being executed.
 */
static int device_resume_early(struct device *dev, pm_message_t state)
{
	pm_callback_t callback = NULL;
	char *info = NULL;
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

	if (dev->power.syscore)
		goto Out;

	if (dev->pm_domain) {
		info = "early power domain ";
		callback = pm_late_early_op(&dev->pm_domain->ops, state);
	} else if (dev->type && dev->type->pm) {
		info = "early type ";
		callback = pm_late_early_op(dev->type->pm, state);
	} else if (dev->class && dev->class->pm) {
		info = "early class ";
		callback = pm_late_early_op(dev->class->pm, state);
	} else if (dev->bus && dev->bus->pm) {
		info = "early bus ";
		callback = pm_late_early_op(dev->bus->pm, state);
	}

	if (!callback && dev->driver && dev->driver->pm) {
		info = "early driver ";
		callback = pm_late_early_op(dev->driver->pm, state);
	}

	error = dpm_run_callback(callback, dev, state, info);

 Out:
	TRACE_RESUME(error);

	pm_runtime_enable(dev);
	return error;
}

/**
 * dpm_resume_early - Execute "early resume" callbacks for all devices.
 * @state: PM transition of the system being carried out.
 */
static void dpm_resume_early(pm_message_t state)
{
	ktime_t starttime = ktime_get();

	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_late_early_list)) {
		struct device *dev = to_device(dpm_late_early_list.next);
		int error;

		get_device(dev);
		list_move_tail(&dev->power.entry, &dpm_suspended_list);
		mutex_unlock(&dpm_list_mtx);

		error = device_resume_early(dev, state);
		if (error) {
			suspend_stats.failed_resume_early++;
			dpm_save_failed_step(SUSPEND_RESUME_EARLY);
			dpm_save_failed_dev(dev_name(dev));
			pm_dev_err(dev, state, " early", error);
		}

		mutex_lock(&dpm_list_mtx);
		put_device(dev);
	}
	mutex_unlock(&dpm_list_mtx);
	dpm_show_time(starttime, state, "early");
}

/**
 * dpm_resume_start - Execute "noirq" and "early" device callbacks.
 * @state: PM transition of the system being carried out.
 */
void dpm_resume_start(pm_message_t state)
{
	dpm_resume_noirq(state);
	dpm_resume_early(state);
}
EXPORT_SYMBOL_GPL(dpm_resume_start);


static int device_suspend_index = 0;
static int device_resume_index = 0;
/**
 * device_resume - Execute "resume" callbacks for given device.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 * @async: If true, the device is being resumed asynchronously.
 */
/* SUSPEND_RESUME_WAKELOCK_LOG-00+[ */
typedef struct {
	u64 cost_time;
	struct device *dev;
} __TOP_TEN__;

#define TOP_CNT 10
__TOP_TEN__	top_async[TOP_CNT];
__TOP_TEN__	top_sync[TOP_CNT];

void init_top_ten(bool async) {
	if (async)
		memset(top_async, 0x00, sizeof(top_async));
	else
		memset(top_sync, 0x00, sizeof(top_sync));
}

void print_top_ten(bool async) {
	int i;
	__TOP_TEN__ *top_ten = async ? top_async : top_sync;

	pr_info("[PM] start to print %s top ten\n", async ? "A" : "S");

	for (i = 0; i < TOP_CNT; i++) {
		int usecs;

		if (!top_ten[i].dev)
			break;

		do_div(top_ten[i].cost_time, NSEC_PER_USEC);

		usecs = top_ten[i].cost_time;
		dev_info(top_ten[i].dev, "[PM] costs %ld.%03ld ms\n", 
			usecs / USEC_PER_MSEC , usecs % USEC_PER_MSEC);;
	}
}

void rec_top_ten(struct device *dev, bool async, u64 usecs64) {
	int i;
	__TOP_TEN__ temp_top_ten1, temp_top_ten2;
	__TOP_TEN__ *top_ten = async ? top_async : top_sync;

	for (i = 0; i < TOP_CNT; i ++) {
		if (usecs64 > top_ten[i].cost_time) {
			temp_top_ten1.cost_time = usecs64;
			temp_top_ten1.dev = dev;

			do {
				if (i < TOP_CNT) {
					temp_top_ten2 = top_ten[i];
					top_ten[i] = temp_top_ten1;
					temp_top_ten1 = temp_top_ten2;
				}
				else 
					break;
				i++;
			} while(1);

			break;
		}
	}
}
/* SUSPEND_RESUME_WAKELOCK_LOG-00+] */

static int device_resume(struct device *dev, pm_message_t state, bool async)
{
	pm_callback_t callback = NULL;
	char *info = NULL;
	int error = 0;
	struct dpm_watchdog wd;
/* SUSPEND_RESUME_WAKELOCK_LOG-00+[ */
	ktime_t starttime;

	if (debug_mask)
		starttime = ktime_get();
/* SUSPEND_RESUME_WAKELOCK_LOG-00+] */

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

	if (dev->power.syscore)
		goto Complete;

	dpm_wait(dev->parent, async);
	device_lock(dev);

	/*
	 * This is a fib.  But we'll allow new children to be added below
	 * a resumed device, even if the device hasn't been completed yet.
	 */
	dev->power.is_prepared = false;
	dpm_wd_set(&wd, dev);

	if (!dev->power.is_suspended)
		goto Unlock;

	if (dev->pm_domain) {
#ifdef LOG
		printk(KERN_DEBUG "[%d] power domain device_resume\n",device_resume_index);
		if (dev->driver)
			if(dev->driver->name)
				printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif		
		aee_sram_printk("%d\n", device_resume_index++);
		info = "power domain ";
		callback = pm_op(&dev->pm_domain->ops, state);
		goto Driver;
	}

	if (dev->type && dev->type->pm) {
#ifdef LOG		
		printk(KERN_DEBUG "[%d] type device_resume\n",device_resume_index);
		if (dev->driver)
			if(dev->driver->name)
				printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif		
		aee_sram_printk("%d\n", device_resume_index++);
		info = "type ";
		callback = pm_op(dev->type->pm, state);
		goto Driver;
	}

	if (dev->class) {
		if (dev->class->pm) {
#ifdef LOG
			printk(KERN_DEBUG "[%d] class device_resume\n",device_resume_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif			
			aee_sram_printk("%d\n", device_resume_index++);
			info = "class ";
			callback = pm_op(dev->class->pm, state);
			goto Driver;
		} else if (dev->class->resume) {
#ifdef LOG			
			printk(KERN_DEBUG "[%d] legacy class device_resume\n",device_resume_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);        
#endif			
			aee_sram_printk("%d\n", device_resume_index++); 
			info = "legacy class ";
			callback = dev->class->resume;
			goto End;
		}
	}

	if (dev->bus) {
		if (dev->bus->pm) {
#ifdef LOG			
			printk(KERN_DEBUG "[%d] bus device_resume\n",device_resume_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif			
			aee_sram_printk("%d\n", device_resume_index++);
			info = "bus ";
			callback = pm_op(dev->bus->pm, state);
		} else if (dev->bus->resume) {
#ifdef LOG			
			printk(KERN_DEBUG "[%d] legacy bus device_resume\n", device_resume_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif			
			aee_sram_printk("%d\n", device_resume_index++);
			info = "legacy bus ";
			callback = dev->bus->resume;
			goto End;
		}
	}

 Driver:
	if (!callback && dev->driver && dev->driver->pm) {
#ifdef LOG
			printk(KERN_DEBUG "[%d] driver device_resume\n", device_resume_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif
			aee_sram_printk("%d\n", device_resume_index++);
		info = "driver ";
		callback = pm_op(dev->driver->pm, state);
	}

 End:
	error = dpm_run_callback(callback, dev, state, info);

/* SUSPEND_RESUME_WAKELOCK_LOG-00+[ */
	if (debug_mask & DEBUG_ALL_DRIVER) {
		u64 usecs64;
		ktime_t endtime = ktime_get();

		usecs64 = ktime_to_ns(ktime_sub(endtime, starttime));

		/* if debugging top ten, don't print it immediately*/
		if (debug_mask & DEBUG_TOP_TEN) {

			/* to save time, do divide NSEC_PER_USEC here, divide it when print */
			rec_top_ten(dev, async, usecs64);
		}
		else {
			int usecs;

			do_div(usecs64, NSEC_PER_USEC);

			usecs = usecs64;

			dev_info(dev, "[PM] %s costs %ld.%03ld ms\n",
				async ? "A" : "S",
				usecs / USEC_PER_MSEC , usecs % USEC_PER_MSEC);
		}
	}
/* SUSPEND_RESUME_WAKELOCK_LOG-00+] */
	dev->power.is_suspended = false;

 Unlock:
	device_unlock(dev);
	dpm_wd_clear(&wd);

 Complete:
	complete_all(&dev->power.completion);

	TRACE_RESUME(error);

	return error;
}

static void async_resume(void *data, async_cookie_t cookie)
{
	struct device *dev = (struct device *)data;
	int error;

	error = device_resume(dev, pm_transition, true);
	if (error)
		pm_dev_err(dev, pm_transition, " async", error);
	put_device(dev);
}

static bool is_async(struct device *dev)
{
	return dev->power.async_suspend && pm_async_enabled
		&& !pm_trace_is_enabled();
}

/**
 * dpm_resume - Execute "resume" callbacks for non-sysdev devices.
 * @state: PM transition of the system being carried out.
 *
 * Execute the appropriate "resume" callback for all devices whose status
 * indicates that they are suspended.
 */
void dpm_resume(pm_message_t state)
{
	struct device *dev;
	ktime_t starttime = ktime_get();

	might_sleep();

	mutex_lock(&dpm_list_mtx);
	pm_transition = state;
	async_error = 0;

/* SUSPEND_RESUME_WAKELOCK_LOG-00+[ */
	init_top_ten(true);
	init_top_ten(false);
/* SUSPEND_RESUME_WAKELOCK_LOG-00+] */

	list_for_each_entry(dev, &dpm_suspended_list, power.entry) {
		INIT_COMPLETION(dev->power.completion);
		if (is_async(dev)) {
			get_device(dev);
			async_schedule(async_resume, dev);
		}
	}

	while (!list_empty(&dpm_suspended_list)) {
		dev = to_device(dpm_suspended_list.next);
		get_device(dev);
		if (!is_async(dev)) {
			int error;

			mutex_unlock(&dpm_list_mtx);

			error = device_resume(dev, state, false);
			if (error) {
				suspend_stats.failed_resume++;
				dpm_save_failed_step(SUSPEND_RESUME);
				dpm_save_failed_dev(dev_name(dev));
				pm_dev_err(dev, state, "", error);
			}

			mutex_lock(&dpm_list_mtx);
		}
		if (!list_empty(&dev->power.entry))
			list_move_tail(&dev->power.entry, &dpm_prepared_list);
		put_device(dev);
	}
	device_resume_index = 0;
	
	mutex_unlock(&dpm_list_mtx);
	async_synchronize_full();
/* SUSPEND_RESUME_WAKELOCK_LOG-00+[ */
	if (debug_mask & DEBUG_ALL_DRIVER) {
		print_top_ten(true);
		print_top_ten(false);
	}
/* SUSPEND_RESUME_WAKELOCK_LOG-00+] */
	dpm_show_time(starttime, state, NULL);
}
EXPORT_SYMBOL_GPL(dpm_resume);

/**
 * device_complete - Complete a PM transition for given device.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 */
static void device_complete(struct device *dev, pm_message_t state)
{
	void (*callback)(struct device *) = NULL;
	char *info = NULL;

	if (dev->power.syscore)
		return;

	device_lock(dev);

	if (dev->pm_domain) {
		info = "completing power domain ";
		callback = dev->pm_domain->ops.complete;
	} else if (dev->type && dev->type->pm) {
		info = "completing type ";
		callback = dev->type->pm->complete;
	} else if (dev->class && dev->class->pm) {
		info = "completing class ";
		callback = dev->class->pm->complete;
	} else if (dev->bus && dev->bus->pm) {
		info = "completing bus ";
		callback = dev->bus->pm->complete;
	}

	if (!callback && dev->driver && dev->driver->pm) {
		info = "completing driver ";
		callback = dev->driver->pm->complete;
	}

	if (callback) {
		pm_dev_dbg(dev, state, info);
		callback(dev);
	}

	device_unlock(dev);

	pm_runtime_put(dev);
}

/**
 * dpm_complete - Complete a PM transition for all non-sysdev devices.
 * @state: PM transition of the system being carried out.
 *
 * Execute the ->complete() callbacks for all devices whose PM status is not
 * DPM_ON (this allows new devices to be registered).
 */
void dpm_complete(pm_message_t state)
{
	struct list_head list;

	might_sleep();

	INIT_LIST_HEAD(&list);
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_prepared_list)) {
		struct device *dev = to_device(dpm_prepared_list.prev);

		get_device(dev);
		dev->power.is_prepared = false;
		list_move(&dev->power.entry, &list);
		mutex_unlock(&dpm_list_mtx);

		device_complete(dev, state);

		mutex_lock(&dpm_list_mtx);
		put_device(dev);
	}
	list_splice(&list, &dpm_list);
	mutex_unlock(&dpm_list_mtx);
}
EXPORT_SYMBOL_GPL(dpm_complete);

/**
 * dpm_resume_end - Execute "resume" callbacks and complete system transition.
 * @state: PM transition of the system being carried out.
 *
 * Execute "resume" callbacks for all devices and complete the PM transition of
 * the system.
 */
void dpm_resume_end(pm_message_t state)
{
	dpm_resume(state);
	dpm_complete(state);
}
EXPORT_SYMBOL_GPL(dpm_resume_end);


/*------------------------- Suspend routines -------------------------*/

/**
 * resume_event - Return a "resume" message for given "suspend" sleep state.
 * @sleep_state: PM message representing a sleep state.
 *
 * Return a PM message representing the resume event corresponding to given
 * sleep state.
 */
static pm_message_t resume_event(pm_message_t sleep_state)
{
	switch (sleep_state.event) {
	case PM_EVENT_SUSPEND:
		return PMSG_RESUME;
	case PM_EVENT_FREEZE:
	case PM_EVENT_QUIESCE:
		return PMSG_RECOVER;
	case PM_EVENT_HIBERNATE:
		return PMSG_RESTORE;
	}
	return PMSG_ON;
}

/**
 * device_suspend_noirq - Execute a "late suspend" callback for given device.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 *
 * The driver of @dev will not receive interrupts while this function is being
 * executed.
 */
static int device_suspend_noirq(struct device *dev, pm_message_t state)
{
	pm_callback_t callback = NULL;
	char *info = NULL;

	if (dev->power.syscore)
		return 0;

	if (dev->pm_domain) {
		info = "noirq power domain ";
		callback = pm_noirq_op(&dev->pm_domain->ops, state);
	} else if (dev->type && dev->type->pm) {
		info = "noirq type ";
		callback = pm_noirq_op(dev->type->pm, state);
	} else if (dev->class && dev->class->pm) {
		info = "noirq class ";
		callback = pm_noirq_op(dev->class->pm, state);
	} else if (dev->bus && dev->bus->pm) {
		info = "noirq bus ";
		callback = pm_noirq_op(dev->bus->pm, state);
	}

	if (!callback && dev->driver && dev->driver->pm) {
		info = "noirq driver ";
		callback = pm_noirq_op(dev->driver->pm, state);
	}

	return dpm_run_callback(callback, dev, state, info);
}

/**
 * dpm_suspend_noirq - Execute "noirq suspend" callbacks for all devices.
 * @state: PM transition of the system being carried out.
 *
 * Prevent device drivers from receiving interrupts and call the "noirq" suspend
 * handlers for all non-sysdev devices.
 */
static int dpm_suspend_noirq(pm_message_t state)
{
	ktime_t starttime = ktime_get();
	char suspend_abort[MAX_SUSPEND_ABORT_LEN];
	int error = 0;

	cpuidle_pause();
	suspend_device_irqs();
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_late_early_list)) {
		struct device *dev = to_device(dpm_late_early_list.prev);

		get_device(dev);
		mutex_unlock(&dpm_list_mtx);

		error = device_suspend_noirq(dev, state);

		mutex_lock(&dpm_list_mtx);
		if (error) {
			pm_dev_err(dev, state, " noirq", error);
			suspend_stats.failed_suspend_noirq++;
			dpm_save_failed_step(SUSPEND_SUSPEND_NOIRQ);
			dpm_save_failed_dev(dev_name(dev));
			put_device(dev);
			break;
		}
		if (!list_empty(&dev->power.entry))
			list_move(&dev->power.entry, &dpm_noirq_list);
		put_device(dev);

		if (pm_wakeup_pending()) {
			pm_get_active_wakeup_sources(suspend_abort,
				MAX_SUSPEND_ABORT_LEN);
			log_suspend_abort_reason(suspend_abort);
			error = -EBUSY;
			break;
		}
	}
	mutex_unlock(&dpm_list_mtx);
	if (error)
		dpm_resume_noirq(resume_event(state));
	else
		dpm_show_time(starttime, state, "noirq");
	return error;
}

/**
 * device_suspend_late - Execute a "late suspend" callback for given device.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 *
 * Runtime PM is disabled for @dev while this function is being executed.
 */
static int device_suspend_late(struct device *dev, pm_message_t state)
{
	pm_callback_t callback = NULL;
	char *info = NULL;

	__pm_runtime_disable(dev, false);

	if (dev->power.syscore)
		return 0;

	if (dev->pm_domain) {
		info = "late power domain ";
		callback = pm_late_early_op(&dev->pm_domain->ops, state);
	} else if (dev->type && dev->type->pm) {
		info = "late type ";
		callback = pm_late_early_op(dev->type->pm, state);
	} else if (dev->class && dev->class->pm) {
		info = "late class ";
		callback = pm_late_early_op(dev->class->pm, state);
	} else if (dev->bus && dev->bus->pm) {
		info = "late bus ";
		callback = pm_late_early_op(dev->bus->pm, state);
	}

	if (!callback && dev->driver && dev->driver->pm) {
		info = "late driver ";
		callback = pm_late_early_op(dev->driver->pm, state);
	}

	return dpm_run_callback(callback, dev, state, info);
}

/**
 * dpm_suspend_late - Execute "late suspend" callbacks for all devices.
 * @state: PM transition of the system being carried out.
 */
static int dpm_suspend_late(pm_message_t state)
{
	ktime_t starttime = ktime_get();
	char suspend_abort[MAX_SUSPEND_ABORT_LEN];
	int error = 0;

	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_suspended_list)) {
		struct device *dev = to_device(dpm_suspended_list.prev);

		get_device(dev);
		mutex_unlock(&dpm_list_mtx);

		error = device_suspend_late(dev, state);

		mutex_lock(&dpm_list_mtx);
		if (error) {
			pm_dev_err(dev, state, " late", error);
			suspend_stats.failed_suspend_late++;
			dpm_save_failed_step(SUSPEND_SUSPEND_LATE);
			dpm_save_failed_dev(dev_name(dev));
			put_device(dev);
			break;
		}
		if (!list_empty(&dev->power.entry))
			list_move(&dev->power.entry, &dpm_late_early_list);
		put_device(dev);

		if (pm_wakeup_pending()) {
			pm_get_active_wakeup_sources(suspend_abort,
				MAX_SUSPEND_ABORT_LEN);
			log_suspend_abort_reason(suspend_abort);
			error = -EBUSY;
			break;
		}
	}
	mutex_unlock(&dpm_list_mtx);
	if (error)
		dpm_resume_early(resume_event(state));
	else
		dpm_show_time(starttime, state, "late");

	return error;
}

/**
 * dpm_suspend_end - Execute "late" and "noirq" device suspend callbacks.
 * @state: PM transition of the system being carried out.
 */
int dpm_suspend_end(pm_message_t state)
{
	int error = dpm_suspend_late(state);
	if (error)
		return error;

	error = dpm_suspend_noirq(state);
	if (error) {
		dpm_resume_early(resume_event(state));
		return error;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dpm_suspend_end);

/**
 * legacy_suspend - Execute a legacy (bus or class) suspend callback for device.
 * @dev: Device to suspend.
 * @state: PM transition of the system being carried out.
 * @cb: Suspend callback to execute.
 */
static int legacy_suspend(struct device *dev, pm_message_t state,
			  int (*cb)(struct device *dev, pm_message_t state))
{
	int error;
	ktime_t calltime;

	calltime = initcall_debug_start(dev);

	error = cb(dev, state);
	suspend_report_result(cb, error);

	initcall_debug_report(dev, calltime, error);

	return error;
}

/**
 * device_suspend - Execute "suspend" callbacks for given device.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 * @async: If true, the device is being suspended asynchronously.
 */
static int __device_suspend(struct device *dev, pm_message_t state, bool async)
{
	pm_callback_t callback = NULL;
	char *info = NULL;
	int error = 0;
	struct dpm_watchdog wd;
	char suspend_abort[MAX_SUSPEND_ABORT_LEN];

	dpm_wait_for_children(dev, async);

	if (async_error)
		goto Complete;

	/*
	 * If a device configured to wake up the system from sleep states
	 * has been suspended at run time and there's a resume request pending
	 * for it, this is equivalent to the device signaling wakeup, so the
	 * system suspend operation should be aborted.
	 */
	if (pm_runtime_barrier(dev) && device_may_wakeup(dev))
		pm_wakeup_event(dev, 0);

	if (pm_wakeup_pending()) {
		pm_get_active_wakeup_sources(suspend_abort,
			MAX_SUSPEND_ABORT_LEN);
		log_suspend_abort_reason(suspend_abort);
		async_error = -EBUSY;
        hib_log("async_error(%d) not zero due pm_wakeup_pending return non zero!!\n", async_error);
		goto Complete;
	}

	if (dev->power.syscore)
		goto Complete;
	
	dpm_wd_set(&wd, dev);

	device_lock(dev);

	if (dev->pm_domain) {
#ifdef LOG
		printk(KERN_DEBUG "[%d] power domain device_suspend\n", device_suspend_index);
		if (dev->driver)
			if(dev->driver->name)
				printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif
		aee_sram_printk("%d\n", device_suspend_index++);
		info = "power domain ";
		callback = pm_op(&dev->pm_domain->ops, state);
		goto Run;
	}

	if (dev->type && dev->type->pm) {
#ifdef LOG		
		printk(KERN_DEBUG "[%d] type device_suspend\n", device_suspend_index);
		if (dev->driver)
			if(dev->driver->name)
				printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif		
		aee_sram_printk("%d\n", device_suspend_index++);
		info = "type ";
		callback = pm_op(dev->type->pm, state);
		goto Run;
	}

	if (dev->class) {
		if (dev->class->pm) {
#ifdef LOG			
			printk(KERN_DEBUG "[%d] class device_suspend\n", device_suspend_index);
				if (dev->driver)
					if(dev->driver->name)
						printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif			
			aee_sram_printk("%d\n", device_suspend_index++);
			info = "class ";
			callback = pm_op(dev->class->pm, state);
			goto Run;
		} else if (dev->class->suspend) {
#ifdef LOG			
			printk(KERN_DEBUG "[%d] legacy class device_suspend\n", device_suspend_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif					
			aee_sram_printk("%d\n", device_suspend_index++);
			pm_dev_dbg(dev, state, "legacy class ");
			error = legacy_suspend(dev, state, dev->class->suspend);
			goto End;
		}
	}

	if (dev->bus) {
		if (dev->bus->pm) {
#ifdef LOG			
			printk(KERN_DEBUG "[%d] bus device_suspend\n", device_suspend_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif			
			aee_sram_printk("%d\n", device_suspend_index++);
			info = "bus ";
			callback = pm_op(dev->bus->pm, state);
		} else if (dev->bus->suspend) {
#ifdef LOG			
			printk(KERN_DEBUG "[%d] legacy bus device_suspend\n", device_suspend_index);
			if (dev->driver)
				if(dev->driver->name)
					printk(KERN_DEBUG "dev->driver->name=%s\n", dev->driver->name);
#endif			
			aee_sram_printk("%d\n", device_suspend_index++);
			pm_dev_dbg(dev, state, "legacy bus ");
			error = legacy_suspend(dev, state, dev->bus->suspend);
			goto End;
		}
	}

 Run:
	if (!callback && dev->driver && dev->driver->pm) {
#ifdef LOG		
		printk(KERN_DEBUG "[%d] driver device_suspend\n", device_suspend_index);
		if (dev->driver)
			if(dev->driver->name)
				printk("dev->driver->name=%s\n", dev->driver->name);		
#endif		
		aee_sram_printk("%d\n", device_suspend_index++);
		info = "driver ";
		callback = pm_op(dev->driver->pm, state);
	}

	error = dpm_run_callback(callback, dev, state, info);

 End:
	if (!error) {
		dev->power.is_suspended = true;
		if (dev->power.wakeup_path
		    && dev->parent && !dev->parent->power.ignore_children)
			dev->parent->power.wakeup_path = true;
	}

	device_unlock(dev);

	dpm_wd_clear(&wd);

 Complete:
	complete_all(&dev->power.completion);
	if (error)
		async_error = error;

	return error;
}

static void async_suspend(void *data, async_cookie_t cookie)
{
	struct device *dev = (struct device *)data;
	int error;

	error = __device_suspend(dev, pm_transition, true);
	if (error) {
		dpm_save_failed_dev(dev_name(dev));
		pm_dev_err(dev, pm_transition, " async", error);
	}

	put_device(dev);
}

static int device_suspend(struct device *dev)
{
	INIT_COMPLETION(dev->power.completion);

	if (pm_async_enabled && dev->power.async_suspend) {
		get_device(dev);
        hib_log("using async mode (check value of \"/sys/power/pm_async\"\n");
		async_schedule(async_suspend, dev);
		return 0;
	}

	return __device_suspend(dev, pm_transition, false);
}

/**
 * dpm_suspend - Execute "suspend" callbacks for all non-sysdev devices.
 * @state: PM transition of the system being carried out.
 */
int dpm_suspend(pm_message_t state)
{
	ktime_t starttime = ktime_get();
	int error = 0;

	might_sleep();

	mutex_lock(&dpm_list_mtx);
	pm_transition = state;
	async_error = 0;
	while (!list_empty(&dpm_prepared_list)) {
		struct device *dev = to_device(dpm_prepared_list.prev);

		get_device(dev);
		mutex_unlock(&dpm_list_mtx);

		error = device_suspend(dev);

		mutex_lock(&dpm_list_mtx);
		if (error) {
			pm_dev_err(dev, state, "", error);
			dpm_save_failed_dev(dev_name(dev));
			put_device(dev);
            hib_log("Device %s failed to %s: error %d\n", dev_name(dev), pm_verb(state.event), error);
			break;
		}
		if (!list_empty(&dev->power.entry))
			list_move(&dev->power.entry, &dpm_suspended_list);
		put_device(dev);
		if (async_error) {
            hib_log("async_error(%d)\n", async_error);
			break;
        }
	}
	device_suspend_index = 0;
	
	mutex_unlock(&dpm_list_mtx);
	async_synchronize_full();
	if (!error)
		error = async_error;
	if (error) {
		suspend_stats.failed_suspend++;
		dpm_save_failed_step(SUSPEND_SUSPEND);
	} else
		dpm_show_time(starttime, state, NULL);

    hib_log("return error(%d)\n", error);
	return error;
}
EXPORT_SYMBOL_GPL(dpm_suspend);

/**
 * device_prepare - Prepare a device for system power transition.
 * @dev: Device to handle.
 * @state: PM transition of the system being carried out.
 *
 * Execute the ->prepare() callback(s) for given device.  No new children of the
 * device may be registered after this function has returned.
 */
static int device_prepare(struct device *dev, pm_message_t state)
{
	int (*callback)(struct device *) = NULL;
	char *info = NULL;
	int error = 0;

	if (dev->power.syscore)
		return 0;

	/*
	 * If a device's parent goes into runtime suspend at the wrong time,
	 * it won't be possible to resume the device.  To prevent this we
	 * block runtime suspend here, during the prepare phase, and allow
	 * it again during the complete phase.
	 */
	pm_runtime_get_noresume(dev);

	device_lock(dev);

	dev->power.wakeup_path = device_may_wakeup(dev);

	if (dev->pm_domain) {
		info = "preparing power domain ";
		callback = dev->pm_domain->ops.prepare;
	} else if (dev->type && dev->type->pm) {
		info = "preparing type ";
		callback = dev->type->pm->prepare;
	} else if (dev->class && dev->class->pm) {
		info = "preparing class ";
		callback = dev->class->pm->prepare;
	} else if (dev->bus && dev->bus->pm) {
		info = "preparing bus ";
		callback = dev->bus->pm->prepare;
	}

	if (!callback && dev->driver && dev->driver->pm) {
		info = "preparing driver ";
		callback = dev->driver->pm->prepare;
	}

	if (callback) {
		error = callback(dev);
		suspend_report_result(callback, error);
	}

	device_unlock(dev);

	return error;
}

/**
 * dpm_prepare - Prepare all non-sysdev devices for a system PM transition.
 * @state: PM transition of the system being carried out.
 *
 * Execute the ->prepare() callback(s) for all devices.
 */
int dpm_prepare(pm_message_t state)
{
	int error = 0;

	might_sleep();

	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_list)) {
		struct device *dev = to_device(dpm_list.next);

		get_device(dev);
		mutex_unlock(&dpm_list_mtx);

		error = device_prepare(dev, state);

		mutex_lock(&dpm_list_mtx);
		if (error) {
			if (error == -EAGAIN) {
				put_device(dev);
				error = 0;
				continue;
			}
			printk(KERN_INFO "PM: Device %s not prepared "
				"for power transition: code %d\n",
				dev_name(dev), error);
			put_device(dev);
			break;
		}
		dev->power.is_prepared = true;
		if (!list_empty(&dev->power.entry))
			list_move_tail(&dev->power.entry, &dpm_prepared_list);
		put_device(dev);
	}
	mutex_unlock(&dpm_list_mtx);
	return error;
}
EXPORT_SYMBOL_GPL(dpm_prepare);

/**
 * dpm_suspend_start - Prepare devices for PM transition and suspend them.
 * @state: PM transition of the system being carried out.
 *
 * Prepare all non-sysdev devices for system PM transition and execute "suspend"
 * callbacks for them.
 */
int dpm_suspend_start(pm_message_t state)
{
	int error;

	error = dpm_prepare(state);
	if (error) {
		suspend_stats.failed_prepare++;
		dpm_save_failed_step(SUSPEND_PREPARE);
	} else
		error = dpm_suspend(state);
	return error;
}
EXPORT_SYMBOL_GPL(dpm_suspend_start);

void __suspend_report_result(const char *function, void *fn, int ret)
{
	if (ret)
		printk(KERN_ERR "%s(): %pF returns %d\n", function, fn, ret);
}
EXPORT_SYMBOL_GPL(__suspend_report_result);

/**
 * device_pm_wait_for_dev - Wait for suspend/resume of a device to complete.
 * @dev: Device to wait for.
 * @subordinate: Device that needs to wait for @dev.
 */
int device_pm_wait_for_dev(struct device *subordinate, struct device *dev)
{
	dpm_wait(dev, subordinate->power.async_suspend);
	return async_error;
}
EXPORT_SYMBOL_GPL(device_pm_wait_for_dev);

/**
 * dpm_for_each_dev - device iterator.
 * @data: data for the callback.
 * @fn: function to be called for each device.
 *
 * Iterate over devices in dpm_list, and call @fn for each device,
 * passing it @data.
 */
void dpm_for_each_dev(void *data, void (*fn)(struct device *, void *))
{
	struct device *dev;

	if (!fn)
		return;

	device_pm_lock();
	list_for_each_entry(dev, &dpm_list, power.entry)
		fn(dev, data);
	device_pm_unlock();
}
EXPORT_SYMBOL_GPL(dpm_for_each_dev);
