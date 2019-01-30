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

#ifndef _HYPNUS_OP_H_
#define _HYPNUS_OP_H_

#include "hypnus.h"

struct hypnus_chipset_operations {
	const char *name;
	/* get running tasks information */
	int (*get_running_avg)(int *avg, int *big_avg, int *iowait);
	/* get cpu load information */
	int (*get_cpu_load)(u32 cpu);
	/* set cpu min/max frequency */
	int (*set_cpu_freq_limit)(u32 c_index, u32 min, u32 max);
	/* isolate CPU */
	int (*isolate_cpu)(int cpu);
	/* uniosolate cpu call back */
	int (*unisolate_cpu)(int cpu);

	int (*get_gpu_load)(u32 gpu);
	int (*get_gpu_freq)(u32 gpu, unsigned int *min,
		unsigned int *max, unsigned int *cur);
	/* set gpu min/max frequency */
	int (*set_gpu_freq_limit)(u32 gpu_index, u32 min, u32 max);
	/* display data init */
	int (*display_init)(void);
	/* get display resolution */
	int (*get_display_resolution)(unsigned int *xres, unsigned int *yres);
	/* get display frame count */
	u64 (*get_frame_cnt)(void);

	/* set low power management policy */
	int (*set_lpm_gov)(u32 type);
	/* set sched task packing policy, ONLY FOR MSM now */
	int (*set_task_packing)(u32 type);
	/* set storage frequency, ONLY FOR MSM now */
	int (*set_storage_scaling)(void);
	/* set ddr frequency*/
	int (*set_ddr_state)(u32 state);
	/* set fpsgo engine, ONLY FOR MTK now */
	int (*set_fpsgo_engine)(u32 state);
	/* set dynamic thermal policy, ONLY FOR MTK now */
	int (*set_thermal_policy)(bool use_default);

	/* set sched prefer idle policy */
	int (*set_sched_prefer_idle)(u32 prefer_idle);
	/* set sched boost */
	int (*get_boost)(void);
	int (*set_boost)(u32 boost);
	/* get up/down migrate */
	int (*get_updown_migrate)(unsigned int *up_migrate,
		unsigned int *down_migrate);
	/* set up/down migrate */
	int (*set_updown_migrate)(unsigned int up_migrate,
		unsigned int down_migrate);
};

void hypnus_chipset_op_init(struct hypnus_data *hypdata);


#endif /* _HYPNUS_OP_H_ */
