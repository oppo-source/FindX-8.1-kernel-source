/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, KBUILD_MODNAME

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <soc/qcom/smem.h>

enum master_smem_id {
	MPSS = 605,
	ADSP,
	CDSP,
	SLPI,
	GPU,
	DISPLAY,
};

enum master_pid {
	PID_APSS = 0,
	PID_MPSS = 1,
	PID_ADSP = 2,
	PID_SLPI = 3,
	PID_CDSP = 5,
	PID_GPU = PID_APSS,
	PID_DISPLAY = PID_APSS,
};

struct msm_rpmh_master_data {
	char *master_name;
	enum master_smem_id smem_id;
	enum master_pid pid;
};

static const struct msm_rpmh_master_data rpmh_masters[] = {
	{"MPSS", MPSS, PID_MPSS},
	{"ADSP", ADSP, PID_ADSP},
	{"CDSP", CDSP, PID_CDSP},
	{"SLPI", SLPI, PID_SLPI},
	{"GPU", GPU, PID_GPU},
	{"DISPLAY", DISPLAY, PID_DISPLAY},
};

struct msm_rpmh_master_stats {
	uint32_t version_id;
	uint32_t counts;
	uint64_t last_entered_at;
	uint64_t last_exited_at;
	uint64_t accumulated_duration;
};

struct rpmh_master_stats_prv_data {
	struct kobj_attribute ka;
	struct kobject *kobj;
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/11 add for get rpm_stats
    struct kobj_attribute oppoka;
	struct kobject *oppokobj;
#endif /*VENDOR_EDIT*/
};

static DEFINE_MUTEX(rpmh_stats_mutex);
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/11 add for get rpm_stats
static DEFINE_MUTEX(oppo_rpmh_stats_mutex);
#endif /*VENDOR_EDIT*/
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/11 add for get rpm_stats
#define MSM_ARCH_TIMER_FREQ 19200000
static inline u64 get_time_in_msec(u64 counter)
{
	do_div(counter, MSM_ARCH_TIMER_FREQ);
	counter *= MSEC_PER_SEC;
	return counter;
}
#endif /* VENDOR_EDIT */


static ssize_t msm_rpmh_master_stats_print_data(char *prvbuf, ssize_t length,
				struct msm_rpmh_master_stats *record,
				const char *name)
{
	return snprintf(prvbuf, length, "%s\n\tVersion:0x%x\n"
			"\tSleep Count:0x%x\n"
			"\tSleep Last Entered At:0x%llx\n"
			"\tSleep Last Exited At:0x%llx\n"
			"\tSleep Accumulated Duration:0x%llx\n\n",
			name, record->version_id, record->counts,
			record->last_entered_at, record->last_exited_at,
			record->accumulated_duration);
}

#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/11 add for get rpm_stats
static ssize_t oppo_rpmh_master_stats_print_data(char *prvbuf, ssize_t length,
				struct msm_rpmh_master_stats *record,
				const char *name)
{
	return snprintf(prvbuf, length, "%s:%x:%llx\n",
			name,record->counts,
			get_time_in_msec(record->accumulated_duration));
}

#endif /*VENDOR_EDIT*/

static ssize_t msm_rpmh_master_stats_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	ssize_t length;
	int i = 0;
	unsigned int size = 0;
	struct msm_rpmh_master_stats *record = NULL;

	/*
	 * Read SMEM data written by masters
	 */

	mutex_lock(&rpmh_stats_mutex);

	for (i = 0, length = 0; i < ARRAY_SIZE(rpmh_masters); i++) {
		record = (struct msm_rpmh_master_stats *) smem_get_entry(
					rpmh_masters[i].smem_id, &size,
					rpmh_masters[i].pid, 0);
		if (!IS_ERR_OR_NULL(record) && (PAGE_SIZE - length > 0))
			length += msm_rpmh_master_stats_print_data(
					buf + length, PAGE_SIZE - length,
					record,
					rpmh_masters[i].master_name);
	}

	mutex_unlock(&rpmh_stats_mutex);

	return length;
}

#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/11 add for get rpm_stats
static ssize_t oppo_rpmh_master_stats_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	ssize_t length;
	int i = 0;
	unsigned int size = 0;
	struct msm_rpmh_master_stats *record = NULL;

	/*
	 * Read SMEM data written by masters
	 */

	mutex_lock(&oppo_rpmh_stats_mutex);

	for (i = 0, length = 0; i < ARRAY_SIZE(rpmh_masters); i++) {
		record = (struct msm_rpmh_master_stats *) smem_get_entry(
					rpmh_masters[i].smem_id, &size,
					rpmh_masters[i].pid, 0);
		if (!IS_ERR_OR_NULL(record) && (PAGE_SIZE - length > 0))
			length += oppo_rpmh_master_stats_print_data(
					buf + length, PAGE_SIZE - length,
					record,
					rpmh_masters[i].master_name);
	}

	mutex_unlock(&oppo_rpmh_stats_mutex);

	return length;
}

#endif /*VENDOR_EDIT*/

static int msm_rpmh_master_stats_probe(struct platform_device *pdev)
{
	struct rpmh_master_stats_prv_data *prvdata = NULL;
	struct kobject *rpmh_master_stats_kobj = NULL;
	int ret = 0;

	if (!pdev)
		return -EINVAL;

	prvdata = kzalloc(sizeof(struct rpmh_master_stats_prv_data),
							GFP_KERNEL);
	if (!prvdata) {
		ret = -ENOMEM;
		goto fail;
	}

	rpmh_master_stats_kobj = kobject_create_and_add(
					"rpmh_stats",
					power_kobj);
	if (!rpmh_master_stats_kobj) {
		ret = -ENOMEM;
		kfree(prvdata);
		goto fail;
	}

	prvdata->kobj = rpmh_master_stats_kobj;

	sysfs_attr_init(&prvdata->ka.attr);
	prvdata->ka.attr.mode = 0444;
	prvdata->ka.attr.name = "master_stats";
	prvdata->ka.show = msm_rpmh_master_stats_show;
	prvdata->ka.store = NULL;

	ret = sysfs_create_file(prvdata->kobj, &prvdata->ka.attr);
	if (ret) {
		pr_err("sysfs_create_file failed\n");
		kobject_put(prvdata->kobj);
		kfree(prvdata);
		goto fail;
	}

#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/11 add for get rpm_stats
    prvdata->oppokobj = rpmh_master_stats_kobj;

	sysfs_attr_init(&prvdata->oppoka.attr);
	prvdata->oppoka.attr.mode = 0444;
	prvdata->oppoka.attr.name = "oppo_rpmh_master_stats";
	prvdata->oppoka.show = oppo_rpmh_master_stats_show;
	prvdata->oppoka.store = NULL;

	ret = sysfs_create_file(prvdata->oppokobj, &prvdata->oppoka.attr);
	if (ret) {
		pr_err("sysfs_create_file failed\n");
		kobject_put(prvdata->oppokobj);
		kfree(prvdata);
		goto fail;
	}
#endif /*VENDOR_EDIT*/
	platform_set_drvdata(pdev, prvdata);

fail:
	return ret;
}

static int msm_rpmh_master_stats_remove(struct platform_device *pdev)
{
	struct rpmh_master_stats_prv_data *prvdata;

	if (!pdev)
		return -EINVAL;

	prvdata = (struct rpmh_master_stats_prv_data *)
				platform_get_drvdata(pdev);

	sysfs_remove_file(prvdata->kobj, &prvdata->ka.attr);
	kobject_put(prvdata->kobj);
#ifdef VENDOR_EDIT
//PengNan@BSP.Power.Basic 2018/06/11 add for get rpm_stats
    sysfs_remove_file(prvdata->oppokobj, &prvdata->oppoka.attr);
	kobject_put(prvdata->oppokobj);
#endif /*VENDOR_EDIT*/
	kfree(prvdata);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id rpmh_master_table[] = {
	{.compatible = "qcom,rpmh-master-stats"},
	{},
};

static struct platform_driver msm_rpmh_master_stats_driver = {
	.probe	= msm_rpmh_master_stats_probe,
	.remove = msm_rpmh_master_stats_remove,
	.driver = {
		.name = "msm_rpmh_master_stats",
		.owner = THIS_MODULE,
		.of_match_table = rpmh_master_table,
	},
};

static int __init msm_rpmh_master_stats_init(void)
{
	return platform_driver_register(&msm_rpmh_master_stats_driver);
}

static void __exit msm_rpmh_master_stats_exit(void)
{
	platform_driver_unregister(&msm_rpmh_master_stats_driver);
}

module_init(msm_rpmh_master_stats_init);
module_exit(msm_rpmh_master_stats_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM RPMH Master Statistics driver");
MODULE_ALIAS("platform:msm_rpmh_master_stat_log");
