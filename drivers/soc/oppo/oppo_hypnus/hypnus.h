/*
 * Copyright (C) 2015-2018 OPPO, Inc.
 * Author: Chuck Huang <huangcheng-m@oppo.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __HYPNUS_H__
#define __HYPNUS_H__

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "hypnus: " fmt

#include <linux/kobject.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/device.h>

#define HYPNUS_VERSION "001"

#define NR_CLUSTERS	2
#define NR_GPUS		1

struct uint_range {
	unsigned int max;
	unsigned int min;
};

struct cpu_data {
	unsigned int id;
	unsigned int cluster_id;
	unsigned int id_in_cluster;
	bool online;
	bool not_preferred;
};

struct cluster_data {
	unsigned int id;
	unsigned int num_cpus;
	unsigned int avail_cpus;
	unsigned int online_cpus;
	struct cpumask cluster_mask;
};

struct hypnus_data {
	unsigned int cluster_nr;
	struct cpu_data cpu_data[NR_CPUS];
	struct cluster_data cluster_data[NR_CLUSTERS];
	unsigned int gpu_nr;

	/* chipset operations*/
	struct hypnus_chipset_operations *cops;

	/* cdev */
	struct cdev cdev;
	struct mutex cdev_mutex;
	dev_t dev_no;
	struct class *class;

	/* sysfs */
	struct kobject kobj;
};


struct hypnus_data *hypnus_get_hypdata(void);

long hypnus_ioctl_get_rq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_get_cpuload(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_submit_cpufreq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_get_boost(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_submit_boost(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_get_migration(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_submit_cpunr(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_submit_migration(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_submit_decision(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_get_gpuload(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_get_gpufreq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);
long hypnus_ioctl_submit_gpufreq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data);


#endif
