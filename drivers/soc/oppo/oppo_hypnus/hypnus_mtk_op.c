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

#include "hypnus_op.h"

static int mt_get_running_avg(int *avg, int *big_avg)
{
	return 0;
}

static int mt_get_cpu_load(u32 cpu)
{
	return 0;
}

static int mt_set_boost(u32 boost)
{
	return 0;
}

static int mt_set_cpu_freq_limit(u32 c_index, u32 min, u32 max)
{
	return 0;
}

static int mt_get_gpu_info(void)
{
	return 0;
}

static int mt_set_gpu_freq_limit(u32 gpu_index, u32 min_freq, u32 max_freq)
{
	return 0;
}

static int mt_set_lpm_gov(u32 type)
{
	return 0;
}

int __weak mt_set_ddr_state(u32 state)
{
	return 0;
}

#ifdef USE_FPSGO
int __weak mt_set_fpsgo_engine(u32 enable)
{
	return 0;
}
#endif

static int mt_set_thermal_policy(bool use_default)
{
	return 0;
}

static int mt_unisolate_cpu(int cpu)
{
	return 0;
}

static u64 mt_get_frame_cnt(void)
{
	return 0;
}

static int mt_get_display_resolution(unsigned int *xres, unsigned int *yres)
{
	return 0;
}


static struct hypnus_chipset_operations mediatek_op = {
	.name = "mediatek",
	.get_running_avg = mt_get_running_avg,
	.get_cpu_load = mt_get_cpu_load,
	.get_gpu_info = mt_get_gpu_info,
	.set_boost = mt_set_boost,
	.set_cpu_freq_limit = mt_set_cpu_freq_limit,
	.set_gpu_freq_limit = mt_set_gpu_freq_limit,
	.set_lpm_gov = mt_set_lpm_gov,
	.set_storage_scaling = NULL,
	.set_ddr_state = mt_set_ddr_state,
#ifdef USE_FPSGO
	.set_fpsgo_engine = mt_set_fpsgo_engine,
#endif
	.set_thermal_policy = mt_set_thermal_policy,
	.isolate_cpu = sched_isolate_cpu,
	.unisolate_cpu = mt_unisolate_cpu,
	.set_sched_prefer_idle = NULL,
	.get_frame_cnt = mt_get_frame_cnt,
	.get_display_resolution = mt_get_display_resolution,
};

void hypnus_chipset_op_init(struct hypnus_data *hypdata)
{
	hypdata->cops = &mediatek_op;
}
