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

#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <asm-generic/ioctl.h>
#include "hypnus.h"
#include "hypnus_dev.h"
#include "hypnus_uapi.h"

static const struct hypnus_ioctl hypnus_ioctl_funcs[] = {
	/* decision */
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_SUBMIT_DECISION,
				hypnus_ioctl_submit_decision),
	/* sched */
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_GET_BOOST,
				hypnus_ioctl_get_boost),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_SUBMIT_BOOST,
				hypnus_ioctl_submit_boost),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_GET_MIGRATION,
				hypnus_ioctl_get_migration),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_SUBMIT_MIGRATION,
				hypnus_ioctl_submit_migration),
	/* CPU */
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_SUBMIT_CPUNR,
				hypnus_ioctl_submit_cpunr),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_GET_CPULOAD,
				hypnus_ioctl_get_cpuload),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_GET_RQ,
				hypnus_ioctl_get_rq),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_SUBMIT_CPUFREQ,
				hypnus_ioctl_submit_cpufreq),
	/* GPU */
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_GET_GPULOAD,
				hypnus_ioctl_get_gpuload),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_GET_GPUFREQ,
				hypnus_ioctl_get_gpufreq),
	HYPNUS_IOCTL_FUNC(IOCTL_HYPNUS_SUBMIT_GPUFREQ,
				hypnus_ioctl_submit_gpufreq),
	/* Storage */
};

static int hypnus_pdrv_probe(struct platform_device *pdev)
{
	return 0;
}

static int hypnus_pdrv_remove(struct platform_device *pdev)
{
	return 0;
}

static int hypnus_main_suspend(struct device *dev)
{
	return 0;
}

static int hypnus_main_resume(struct device *dev)
{
	return 0;
}

static void hypnus_dev_release(struct device *dev)
{
}

static const struct dev_pm_ops hypnus_pm_ops = {
	.suspend = hypnus_main_suspend,
	.freeze = hypnus_main_suspend,
	.resume = hypnus_main_resume,
	.restore = hypnus_main_resume,
	.thaw = hypnus_main_resume,
};

static struct platform_device hypnus_dev = {
	.name = "hypnus-dev",
	.id = -1,
	.dev.release = hypnus_dev_release,
};

static struct platform_driver hypnus_pdrv = {
	.probe = hypnus_pdrv_probe,
	.remove = hypnus_pdrv_remove,
	.driver.name = "hypnus-dev",
	.driver.pm = &hypnus_pm_ops,
	.driver.owner = THIS_MODULE,
};

static int hypnus_open(struct inode *inode, struct file *fp)
{
	struct hypnus_data *hypdata;

	hypdata = container_of(inode->i_cdev, struct hypnus_data, cdev);

	fp->private_data = hypdata;

	return 0;
}

static unsigned int hypnus_poll(struct file *fp, poll_table *wait)
{
	return 0;
}

static ssize_t hypnus_read(struct file *fp, char __user *buffer,
	size_t count, loff_t *ppos)
{
	return count;
}

static long hypnus_ioctl_in(unsigned int cmd, unsigned long arg,
	unsigned char *ptr)
{
	unsigned int copy;
	int ret = 0;

	copy = _IOC_SIZE(cmd);

	if ((copy > 0) && (cmd & IOC_IN)) {
		ret = copy_from_user(ptr, (void __user *) arg, copy);
		if (ret)
			pr_err("%s %d\n", __func__, ret);
	}

	return ret;
}

static long hypnus_ioctl_out(unsigned int cmd, unsigned long arg,
	unsigned char *ptr)
{
	unsigned int copy;
	int ret = 0;

	copy = _IOC_SIZE(cmd);

	if ((copy > 0) && (cmd & IOC_OUT)) {
		ret = copy_to_user((void __user *) arg, ptr, copy);
		if (ret)
			pr_err("%s %d\n", __func__, ret);
	}

	return ret;
}

static long hypnus_ioctl(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	long ret;
	unsigned char data[64];
	struct hypnus_data *hypdata;
	unsigned int nr;

	hypdata = filep->private_data;
	nr = _IOC_NR(cmd);

	memset(data, 0x0, sizeof(data));

	if (nr >= ARRAY_SIZE(hypnus_ioctl_funcs)
		|| !hypnus_ioctl_funcs[nr].func)
		return -ENOIOCTLCMD;

	if (_IOC_SIZE(hypnus_ioctl_funcs[nr].cmd) > sizeof(data)) {
		pr_err("data too big for ioctl!\n");
		return -EINVAL;
	}

	if (_IOC_SIZE(hypnus_ioctl_funcs[nr].cmd)) {
		ret = hypnus_ioctl_in(hypnus_ioctl_funcs[nr].cmd, arg, data);
		if (ret)
			return ret;
	}

	/* invoke hypnus ioctl funcs */
	ret = hypnus_ioctl_funcs[nr].func(hypdata, cmd, data);
	if (ret)
		return ret;

	if (ret == 0 && _IOC_SIZE(hypnus_ioctl_funcs[nr].cmd))
		ret = hypnus_ioctl_out(hypnus_ioctl_funcs[nr].cmd, arg, data);

	return ret;
}

#ifdef CONFIG_COMPAT
static long hypnus_compat_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	hypnus_ioctl(filp, cmd, arg);

	return 0;
}
#endif

static const struct file_operations hypnus_fops = {
	.owner = THIS_MODULE,
	.open = hypnus_open,
	.read = hypnus_read,
	.poll = hypnus_poll,
	.llseek = noop_llseek,
	.unlocked_ioctl = hypnus_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = hypnus_compat_ioctl,
#endif
};

int hypnus_dev_init(struct hypnus_data *hypdata)
{
	int ret = 0;
	struct device *dev;

	ret = platform_device_register(&hypnus_dev);
	if (ret) {
		pr_err("Failed to register hypnus device!\n");
		return ret;
	}

	ret = platform_driver_register(&hypnus_pdrv);
	if (ret) {
		pr_err("Failed to register hypnus driver!\n");
		goto err_driver;
	}

	mutex_init(&hypdata->cdev_mutex);

	ret = alloc_chrdev_region(&hypdata->dev_no, 0, 1, "oppo_hypnus");
	if (ret) {
		pr_err("Fail to alloc devid, ret = %d\n", ret);
		goto err_cdev_alloc;
	}

	cdev_init(&hypdata->cdev, &hypnus_fops);

	ret = cdev_add(&hypdata->cdev, hypdata->dev_no, 1);
	if (ret) {
		pr_err("Fail to add cdev\n");
		goto err_cdev_add;
	}

	hypdata->class = class_create(THIS_MODULE, "oppo_hypnus");
	if (!hypdata->class || IS_ERR(hypdata->class)) {
		pr_err("Fail to add hypnus class\n");
		goto err_class_add;
	}

	dev = device_create(hypdata->class, NULL, hypdata->dev_no,
				NULL, "oppo_hypnus");
	if (!dev || IS_ERR(dev)) {
		pr_err("Fail to create device\n");
		goto err_device_add;
	}

	return ret;

err_device_add:
	class_destroy(hypdata->class);
err_class_add:
	cdev_del(&hypdata->cdev);
err_cdev_add:
	unregister_chrdev_region(hypdata->dev_no, 1);
err_cdev_alloc:
	platform_driver_unregister(&hypnus_pdrv);
err_driver:
	platform_device_unregister(&hypnus_dev);

	return ret;
}

int hypnus_dev_uninit(struct hypnus_data *hypdata)
{
	device_destroy(hypdata->class, hypdata->dev_no);
	class_destroy(hypdata->class);
	cdev_del(&hypdata->cdev);
	unregister_chrdev_region(hypdata->dev_no, 1);
	platform_driver_unregister(&hypnus_pdrv);
	platform_device_unregister(&hypnus_dev);
	return 0;
}
