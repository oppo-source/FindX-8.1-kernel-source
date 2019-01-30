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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/cpufreq.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/msm_kgsl.h>
#include <linux/topology.h>
#include "hypnus.h"
#include "hypnus_op.h"
#include "hypnus_dev.h"
#include "hypnus_sysfs.h"
#include "hypnus_uapi.h"


static struct hypnus_data *g_hypdata;

long hypnus_ioctl_get_rq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_rq_prop *prop = data;

	if (!hypdata->cops->get_running_avg)
		return -ENOTSUPP;

	hypdata->cops->get_running_avg(&prop->avg, &prop->big_avg,
					&prop->iowait_avg);

	return 0;
}

long hypnus_ioctl_get_cpuload(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_cpuload_prop *prop = data;
	unsigned int cpu = 0;

	if (!hypdata->cops->get_cpu_load)
		return -ENOTSUPP;

	for_each_possible_cpu(cpu) {
		if (cpu_online(cpu))
			prop->cpu_load[cpu] = hypdata->cops->get_cpu_load(cpu);
		else
			prop->cpu_load[cpu] = -1;
	}

	return 0;
}

long hypnus_ioctl_submit_cpufreq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_cpufreq_prop *prop = data;
	struct cpumask *cluster_mask, pmask;
	struct cluster_data *cluster;
	int i, cpu, ret = 0;

	if (!hypdata->cops->set_cpu_freq_limit)
		return -ENOTSUPP;

	for (i = 0; i < hypdata->cluster_nr; i++) {
		cluster = &hypdata->cluster_data[i];
		cluster_mask = &cluster->cluster_mask;
		cpumask_and(&pmask, cluster_mask, cpu_online_mask);
		for_each_cpu(cpu, &pmask) {
			ret |= hypdata->cops->set_cpu_freq_limit(cpu,
				prop->freq_prop[i].min,
				prop->freq_prop[i].max);
		}
	}

	return ret;
}

long hypnus_ioctl_get_gpuload(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_gpuload_prop *prop = data;
	int i;

	if (!hypdata->cops->get_gpu_load)
		return -ENOTSUPP;

	for (i = 0; i < hypdata->gpu_nr; i++)
		prop->gpu_load[i] = hypdata->cops->get_gpu_load(i);

	return 0;
}

long hypnus_ioctl_get_gpufreq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_gpufreq_prop *prop = data;
	unsigned int *min, *max, *cur;
	int i;

	if (!hypdata->cops->get_gpu_freq)
		return -ENOTSUPP;

	for (i = 0; i < hypdata->gpu_nr; i++) {
		min = &prop->freq_prop[i].min;
		max = &prop->freq_prop[i].max;
		cur = &prop->freq_prop[i].cur;
		hypdata->cops->get_gpu_freq(i, min, cur, max);
	}

	return 0;
}

long hypnus_ioctl_submit_gpufreq(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_gpufreq_prop *prop = data;
	unsigned int min, max;
	int i;

	if (!hypdata->cops->set_gpu_freq_limit)
		return -ENOTSUPP;

	for (i = 0; i < hypdata->gpu_nr; i++) {
		min = prop->freq_prop[i].min;
		max = prop->freq_prop[i].max;
		hypdata->cops->set_gpu_freq_limit(i, min, max);
	}

	return 0;
}

long hypnus_ioctl_submit_lpm(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	return 0;
}

int hypnus_ioclt_submit_ddr(struct hypnus_data *hypdata, u32 type)
{
	/* Todo */
	return 0;
}

int hypnus_ioclt_submit_thermal_policy(struct hypnus_data *hypdata)
{
	/* Todo */
	return 0;
}

long hypnus_ioctl_get_boost(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_boost_prop *prop = data;

	if (hypdata->cops->get_boost)
		prop->sched_boost = hypdata->cops->get_boost();
	else
		return -ENOTSUPP;

	return 0;
}

long hypnus_ioctl_submit_boost(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	int ret = 0;
	struct hypnus_boost_prop *prop = data;

	if (hypdata->cops->set_boost) {
		ret = hypdata->cops->set_boost(prop->sched_boost);
		if (ret)
			pr_err("%s err %d\n", __func__, ret);
	} else
		return -ENOTSUPP;

	return ret;
}

long hypnus_ioctl_get_migration(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	int ret = 0;
	struct hypnus_migration_prop *prop = data;
	int *up, *down;

	if (!hypdata->cops->set_updown_migrate)
		return -ENOTSUPP;

	up = &prop->up_migrate;
	down = &prop->down_migrate;

	ret = hypdata->cops->get_updown_migrate(up, down);
	if (ret)
		pr_err("%s err %d\n", __func__, ret);

	return ret;
}

long hypnus_ioctl_submit_migration(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	int ret;
	struct hypnus_migration_prop *prop = data;
	int up, down;

	if (!hypdata->cops->set_updown_migrate)
		return -ENOTSUPP;

	up = prop->up_migrate;
	down = prop->down_migrate;

	ret = hypdata->cops->set_updown_migrate(up, down);
	if (ret)
		pr_err("%s err %d\n", __func__, ret);

	return ret;
}

static inline unsigned int
cpu_available_count(struct cpumask *cluster_mask)
{
	struct cpumask mask;

	cpumask_and(&mask, cluster_mask, cpu_online_mask);
	cpumask_andnot(&mask, &mask, cpu_isolated_mask);

	return cpumask_weight(&mask);
}

static int hypnus_unisolate_cpu(struct hypnus_data *hypdata, unsigned int cpu)
{
	int ret = 0;

	if (cpu_isolated(cpu) && !hypdata->cpu_data[cpu].not_preferred) {
		ret = hypdata->cops->unisolate_cpu(cpu);
		if (ret)
			pr_err("Unisolate CPU%u failed! err %d\n", cpu, ret);
	}

	return ret;
}

static int hypnus_isolate_cpu(struct hypnus_data *hypdata, unsigned int cpu)
{
	int ret;

	ret = hypdata->cops->isolate_cpu(cpu);
	if (ret)
		pr_err("Isolate CPU%u failed! err %d\n", cpu, ret);

	return ret;
}

long hypnus_ioctl_submit_cpunr(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	struct hypnus_cpunr_prop *prop = data;
	unsigned int cpu, now_cpus, need_cpus;
	struct cluster_data *cluster;
	struct cpumask *cluster_mask, pmask;
	int i, j, ret, err;

	ret = err = now_cpus = cpu = 0;

	if (!hypdata->cops->isolate_cpu || !hypdata->cops->unisolate_cpu)
		return -ENOTSUPP;

	for (i = 0; i < hypdata->cluster_nr; i++) {
		cluster = &hypdata->cluster_data[i];
		cluster_mask = &cluster->cluster_mask;
		now_cpus = cpu_available_count(cluster_mask);
		need_cpus = prop->need_cpus[i];

		if (need_cpus > now_cpus) {
			cpumask_and(&pmask, cluster_mask, cpu_online_mask);
			cpumask_and(&pmask, &pmask, cpu_isolated_mask);
			for_each_cpu(cpu, &pmask) {
				hypnus_unisolate_cpu(hypdata, cpu);
				if (need_cpus
					<= cpu_available_count(cluster_mask))
					break;
			}
		} else if (need_cpus < now_cpus) {
			cpu = cpumask_first(cluster_mask);

			for (j = cluster->num_cpus - 1; j >= 0; j--) {
				if (cpu_isolated(cpu + j)
					|| !cpu_online(cpu + j))
					continue;
				hypnus_isolate_cpu(hypdata, cpu + j);
				if (need_cpus
					>= cpu_available_count(cluster_mask))
					break;
			}
		}

		ret |= (need_cpus != cpu_available_count(cluster_mask));
	}

	return ret;
}


long hypnus_ioctl_submit_decision(struct hypnus_data *hypdata,
	unsigned int cmd, void *data)
{
	return 0;
}

struct hypnus_data *hypnus_get_hypdata(void)
{
	return g_hypdata;
}

static struct hypnus_data *hypnus_alloc_hypdata(void)
{
	struct hypnus_data *hypdata;

	hypdata = vzalloc(sizeof(struct hypnus_data));
	if (!hypdata)
		pr_err("alloc hypdata failed!\n");

	return hypdata;
}

static void hypnus_free_hypdata(void)
{
	vfree(g_hypdata);
	g_hypdata = NULL;
}

static int hypnus_parse_cpu_topology(struct hypnus_data *hypdata)
{
	struct list_head *head = get_cpufreq_policy_list();
	struct cpufreq_policy *policy;
	int cluster_nr = 0;

	if (!head)
		return -EINVAL;

	list_for_each_entry(policy, head, policy_list) {
		int first_cpu = cpumask_first(policy->related_cpus);
		int index, cpu;
		struct cpu_data *pcpu = NULL;

		if (unlikely(first_cpu > NR_CPUS)) {
			pr_err("Wrong related cpus 0x%x\n",
				(int)cpumask_bits(policy->related_cpus)[0]);
			return -EINVAL;
		}

		for_each_cpu(cpu, policy->related_cpus) {
			pcpu = &hypdata->cpu_data[cpu];
			pcpu->id = cpu;
			pcpu->cluster_id = topology_physical_package_id(cpu);
		}

		index = topology_physical_package_id(first_cpu);
		pr_info("cluster idx = %d, cpumask = 0x%x\n", index,
				(int)cpumask_bits(policy->related_cpus)[0]);
		hypdata->cluster_data[index].id = index;
		cpumask_copy(&hypdata->cluster_data[index].cluster_mask,
				policy->related_cpus);
		cluster_nr++;
	}
	hypdata->cluster_nr = cluster_nr;
	pr_info("Totally %d clusters\n", hypdata->cluster_nr);
	return 0;
}

static int cpu_data_init(struct hypnus_data *hypdata, unsigned int cpuid)
{
	unsigned int cpu;
	struct cpufreq_policy *policy;
	struct cpu_data *cpudata = &hypdata->cpu_data[cpuid];
	struct cluster_data *c_cluster;
	unsigned int first_cpu;

	if (!cpu_online(cpuid))
		return 0;

	policy = cpufreq_cpu_get(cpuid);
	if (!policy)
		return 0;

	for_each_cpu(cpu, policy->related_cpus) {
		cpudata = &hypdata->cpu_data[cpu];
		c_cluster = &hypdata->cluster_data[cpudata->cluster_id];
		first_cpu = cpumask_first(&c_cluster->cluster_mask);
		cpudata->id_in_cluster = cpu - first_cpu;
		c_cluster->num_cpus = cpumask_weight(&c_cluster->cluster_mask);
		c_cluster->avail_cpus = c_cluster->num_cpus;

		if (cpu_online(cpu)) {
			cpudata->online = true;
			c_cluster->online_cpus++;
		}
	}
	cpufreq_cpu_put(policy);

	return 0;
}

int __init gpu_info_init(struct hypnus_data *hypdata)
{
	unsigned int gpu_nr =
	    KGSL_DEVICE_MAX < NR_GPUS ? KGSL_DEVICE_MAX : NR_GPUS;

	hypdata->gpu_nr = gpu_nr;
	return 0;
}

static int __init hypnus_init(void)
{
	int ret;
	struct hypnus_data *hypdata = NULL;
	unsigned int cpu;

	hypdata = hypnus_alloc_hypdata();
	if (!hypdata) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	g_hypdata = hypdata;

	ret = hypnus_parse_cpu_topology(hypdata);
	if (ret)
		goto err_topology;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		ret = cpu_data_init(hypdata, cpu);
		if (ret < 0) {
			pr_err("%s cpu data init err!\n", __func__);
			goto err_cpu;
		}
	}
	put_online_cpus();

	gpu_info_init(hypdata);

	/* initialize chipset operation hooks */
	hypnus_chipset_op_init(hypdata);

	ret = hypnus_sysfs_init(hypdata);
	if (ret)
		goto err_sysfs_init;

	ret = hypnus_dev_init(hypdata);
	if (ret)
		goto err_dev_init;

	return 0;

err_dev_init:
	hypnus_sysfs_remove(hypdata);
err_sysfs_init:
err_cpu:
err_topology:
	hypnus_free_hypdata();
err_alloc:
	return ret;
}

static void __exit hypnus_exit(void)
{
	hypnus_dev_uninit(g_hypdata);
	hypnus_sysfs_remove(g_hypdata);
	hypnus_free_hypdata();
}


module_init(hypnus_init);
module_exit(hypnus_exit);

MODULE_DESCRIPTION("Hypnus system controller");
MODULE_VERSION(HYPNUS_VERSION);
MODULE_LICENSE("GPL");
