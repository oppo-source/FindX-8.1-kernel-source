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

#ifndef __HYPNUS_UAPI_H__
#define __HYPNUS_UAPI_H__

/* ioctl part */
#define HYPNUS_IOC_MAGIC	0xF4

#define IOCTL_HYPNUS_SUBMIT_DECISION \
	_IOWR(HYPNUS_IOC_MAGIC, 0x1, struct hypnus_decision_prop)
#define IOCTL_HYPNUS_GET_BOOST \
	_IOR(HYPNUS_IOC_MAGIC, 0x2, struct hypnus_boost_prop)
#define IOCTL_HYPNUS_SUBMIT_BOOST \
	_IOW(HYPNUS_IOC_MAGIC, 0x3, struct hypnus_boost_prop)
#define IOCTL_HYPNUS_GET_MIGRATION \
	_IOR(HYPNUS_IOC_MAGIC, 0x4, struct hypnus_migration_prop)
#define IOCTL_HYPNUS_SUBMIT_MIGRATION \
	_IOW(HYPNUS_IOC_MAGIC, 0x5, struct hypnus_migration_prop)
#define	IOCTL_HYPNUS_SUBMIT_CPUNR \
	_IOW(HYPNUS_IOC_MAGIC, 0x20, struct hypnus_cpunr_prop)
#define	IOCTL_HYPNUS_GET_CPULOAD \
	_IOR(HYPNUS_IOC_MAGIC, 0x21, struct hypnus_cpuload_prop)
#define	IOCTL_HYPNUS_GET_RQ \
	_IOR(HYPNUS_IOC_MAGIC, 0x22, struct hypnus_rq_prop)
#define IOCTL_HYPNUS_SUBMIT_CPUFREQ \
	_IOW(HYPNUS_IOC_MAGIC, 0x23, struct hypnus_cpufreq_prop)
#define	IOCTL_HYPNUS_GET_GPULOAD \
	_IOR(HYPNUS_IOC_MAGIC, 0x32, struct hypnus_gpuload_prop)
#define	IOCTL_HYPNUS_GET_GPUFREQ \
	_IOR(HYPNUS_IOC_MAGIC, 0x31, struct hypnus_gpufreq_prop)
#define	IOCTL_HYPNUS_SUBMIT_GPUFREQ \
	_IOW(HYPNUS_IOC_MAGIC, 0x33, struct hypnus_gpufreq_prop)

struct hypnus_boost_prop {
	u32 sched_boost;
};

struct hypnus_migration_prop {
	int up_migrate;
	int down_migrate;
};

struct hypnus_cpuload_prop {
	int cpu_load[NR_CPUS]; /* >0: cpu load, <0: offline*/
};

struct hypnus_rq_prop {
	int avg;
	int big_avg;
	int iowait_avg;
};

struct hypnus_cpunr_prop {
	unsigned int need_cpus[NR_CLUSTERS];
};

struct freq_prop {
	unsigned int min;
	unsigned int max;
	unsigned int cur;
};

struct hypnus_cpufreq_prop {
	struct freq_prop freq_prop[NR_CLUSTERS];
};

struct hypnus_gpunr_prop {
	unsigned int need_gpus[0];
};

struct hypnus_gpuload_prop {
	unsigned int gpu_load[NR_GPUS];
};

struct hypnus_gpufreq_prop {
	struct freq_prop freq_prop[NR_GPUS];
};

struct hypnus_decision_prop {

};

#endif
