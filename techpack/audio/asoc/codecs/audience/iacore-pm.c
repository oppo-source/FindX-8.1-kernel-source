/*
 * Copyright 2014 Audience, Inc.
 *
 * Author: Steven Tarr  <starr@audience.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Locking notes: This file should take no access_lock.
 * The caller of functions defined in this file must make sure the
 * access_lock is taken before using the functions.
 * With system wide suspend and resume implementation, it is required to take
 * access_lock in system suspend/resume routines.
 */

#include "iaxxx.h"

#include <linux/pm_runtime.h>
#include "iacore.h"

#define IA_PM_AUTOSUSPEND_DELAY                3000 /* 3 sec */

int iacore_pm_suspend(struct device *dev)
{
#ifdef FW_SLEEP_TIMER_TEST
	struct iacore_priv *iacore = dev_get_drvdata(dev);
#endif

	pr_debug("system suspend\n");

#ifdef FW_SLEEP_TIMER_TEST
	if (timer_pending(&iacore->fs_timer)) {
		pr_err("fs timer active. Not ready for suspend\n");
		return -EAGAIN;
	}
#endif

	return 0;
}

int iacore_pm_resume(struct device *dev)
{
	int rc = 0;

	pr_debug("system resume\n");

	return rc;
}

int iacore_pm_runtime_suspend(struct device *dev)
{
#ifdef FW_SLEEP_TIMER_TEST
	struct iacore_priv *iacore = dev_get_drvdata(dev);
#endif

	pr_debug("runtime suspend\n");

#ifdef FW_SLEEP_TIMER_TEST
	if (timer_pending(&iacore->fs_timer)) {
		pr_err("fs timer active. Not ready for suspend\n");
		return -EAGAIN;
	}
#endif

#if (defined(CONFIG_SND_SOC_IA_SOUNDWIRE) && defined(CONFIG_SND_SOC_IA_SDW_X86))
	iacore_sdw_reinit_slave_enume_compl(dev);
#endif

	return 0;
}

int iacore_pm_runtime_resume(struct device *dev)
{
	int rc = 0;

	pr_debug("runtime resume\n");

#if (defined(CONFIG_SND_SOC_IA_SOUNDWIRE) && defined(CONFIG_SND_SOC_IA_SDW_X86))
	rc = iacore_sdw_wait_for_slave_enumeration(dev);
#endif

	return rc;
}

void iacore_pm_complete(struct device *dev)
{
	struct iacore_priv *iacore = dev_get_drvdata(dev);

	pr_debug("\n");

	wake_up_interruptible(&iacore->irq_waitq);
}

void iacore_pm_enable(struct iacore_priv *iacore)
{
	int ret = 0;

	pr_info("enter\n");

	ret = pm_runtime_set_active(iacore->dev);
	if (ret < 0)
		pr_err("pm_runtime_set_active fail %d\n", ret);

	pm_runtime_mark_last_busy(iacore->dev);
	pm_runtime_set_autosuspend_delay(iacore->dev, IA_PM_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(iacore->dev);
	pm_runtime_enable(iacore->dev);
	ret = device_init_wakeup(iacore->dev, true);
	if (ret < 0)
		pr_err("device_init_wakeup fail %d\n", ret);

	ret = pm_runtime_get_sync(iacore->dev);
	if (ret >= 0) {
		ret = pm_runtime_put_sync_autosuspend(iacore->dev);
		if (ret < 0) {
			pr_err("pm_runtime_put_sync_autosuspend fail %d\n", ret);
		}
	} else {
		pr_err("pm_runtime_get_sync fail %d\n", ret);
	}
	pr_info("leave\n");
	return;
}

int iacore_pm_get_sync(struct iacore_priv *iacore)
{
	int ret = 0;

	pr_debug("\n");

	ret = pm_runtime_get_sync(iacore->dev);
	if (ret < 0)
		pr_err("Fail %d\n", ret);

	return ret;
}

void iacore_pm_put_autosuspend(struct iacore_priv *iacore)
{
	int ret = 0;

	pr_debug("\n");

	pm_runtime_mark_last_busy(iacore->dev);

	ret = pm_runtime_put_sync_autosuspend(iacore->dev);
	if (ret)
		pr_err("fail %d\n", ret);
}

int iacore_pm_put_sync_suspend(struct iacore_priv *iacore)
{
	int ret;

	pr_debug("\n");

	pm_runtime_mark_last_busy(iacore->dev);

	ret = pm_runtime_put_sync_suspend(iacore->dev);
	if (ret)
		pr_err("fail %d\n", ret);

	return ret;
}
