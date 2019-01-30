/*
 * iacore-raw.c  --  iacore raw read/write support
 *
 * Copyright 2016 Audience, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "iaxxx.h"

#include "iacore.h"
#include "iacore-raw.h"
#include "iacore-spi.h"
#include "iacore-uart-common.h"
#include "iacore-cdev.h"

static int enable_buffered_raw_read(struct iacore_priv *iacore)
{
	int err = 0;

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	err = iacore_pm_get_sync(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	if (err < 0) {
		pr_err("pm_get_sync failed :%d\n", err);
		return err;
	}

	err = streamdev_open(iacore);
	if (err) {
		pr_err("streamdev_open err = %d\n", err);
		IACORE_MUTEX_LOCK(&iacore->access_lock);
		iacore_pm_put_autosuspend(iacore);
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);

		goto err_out;
	}

	iacore->raw_buffered_read_sts = true;

err_out:
	return err;
}

static int disable_buffered_raw_read(struct iacore_priv *iacore)
{
	int err = 0;

	if (iacore->raw_buffered_read_sts != true) {
		pr_err("Raw Buffered Read not enabled\n");
		return -EINVAL;
	}

	err = streamdev_release(iacore);
	if (err)
		pr_err("streamdev_release error = %d\n", err);

	iacore->raw_buffered_read_sts = false;

#if defined(CONFIG_SND_SOC_IA_UART)
	iacore_uart_clean_rx_fifo(iacore);
#endif

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	iacore_pm_put_autosuspend(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return err;
}

ssize_t raw_read(struct file *filp, char __user *buf,
					size_t count, loff_t *f_pos)
{
	struct iacore_priv *iacore = (struct iacore_priv *)filp->private_data;
	int rc = 0;
	void *kbuf = NULL;
	int i;

	pr_debug("called count %ld\n", count);

	if (!iacore) {
		pr_err("Invalid private pointer\n");
		rc = -EINVAL;
		goto inval_priv;
	}

	if (iacore->raw_buffered_read_sts == true) {
		rc = streaming_read(filp, buf, count, f_pos);
		return rc;
	}

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("open failed %d\n", rc);
		goto inval_priv;
	}

	kbuf = devm_kzalloc(iacore->dev, count, GFP_KERNEL);
	if (!kbuf) {
		rc = -ENOMEM;
		goto raw_read_err;
	}

	rc = iacore->bus.ops.read(iacore, kbuf, count);
	if (rc < 0) {
		pr_err("failed to read data: %d", rc);
		rc = -EIO;
		goto raw_read_err;
	}

	if (count <= 4) {
		for (i = 0; i < count; i++) {
			pr_info("#### 0x%x\n", *((char *)kbuf + i));
		}
	}

	rc = copy_to_user(buf, kbuf, count);
	if (rc) {
		pr_err("failed to copy response to userspace %d", rc);
		rc = -EIO;
		goto raw_read_err;
	}

	rc = count;

raw_read_err:
	devm_kfree(iacore->dev, kbuf);
	iacore_datablock_close(iacore);
inval_priv:
	return rc;
}

ssize_t raw_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *f_pos)
{
	struct iacore_priv *iacore = (struct iacore_priv *)filp->private_data;
	int rc = 0;
	void *kbuf;
	int i;

	if (!iacore) {
		pr_err("Invalid private pointer\n");
		rc = -EINVAL;
		goto inval_priv;
	}

	pr_debug("called count %ld\n", count);

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("open failed %d\n", rc);
		goto inval_priv;
	}

	kbuf = memdup_user(buf, count);
	if (!kbuf) {
		pr_err("failed to copy user data of len: %d\n", (u32)count);
		rc = -ENOMEM;
		goto raw_write_err;
	}

	if (count <= 4) {
		for (i = 0; i < count; i++)
			pr_info("#### 0x%x\n", *((char *)kbuf + i));
	}

	rc = iacore->bus.ops.write(iacore, kbuf, count);
	if (rc < 0) {
		pr_err("failed to write data: %d\n", rc);
		rc = -EIO;
	} else {
		rc = count;
	}

	kfree(kbuf);

raw_write_err:
	iacore_datablock_close(iacore);
inval_priv:
	return rc;
}

long raw_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iacore_priv * const iacore
			= (struct iacore_priv *)file->private_data;
	int ret = 0;
	u32 bus_config = 0;
	u32 local_arg;

	if (iacore == NULL) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	pr_info("cmd (0x%x)\n", cmd);

	ret = copy_from_user(&local_arg, (void __user *)arg, sizeof(u32));
	if (ret) {
		pr_err("copy_from_user fail. error: %d\n", ret);
		return -EFAULT;
	}

	switch (cmd) {
	case IA_CONFIGURE_BUS_SPEED:
		pr_info("configure bus speed to %d\n", local_arg);
#if defined(CONFIG_SND_SOC_IA_UART)
		ret = iacore_raw_configure_tty(iacore, local_arg);
#elif defined(CONFIG_SND_SOC_IA_SPI)
		ret = iacore_spi_setup(iacore, local_arg);
#else
		ret = -EINVAL;
#endif
		break;
	case IA_POWER_CTRL:
		pr_info("change power state of chip %d\n", local_arg);
		if (local_arg == IA_RAW_POWER_OFF)
			iacore_power_ctrl(iacore, IA_POWER_OFF);
		else if (local_arg == IA_RAW_POWER_ON)
			iacore_power_ctrl(iacore, IA_POWER_ON);
		else
			ret = -EINVAL;
		break;
	case IA_WAKEUP:
		pr_info("wakeup chip\n");
		IACORE_MUTEX_LOCK(&iacore->access_lock);
		ret = iacore_wakeup_unlocked(iacore);
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		break;
	case IA_CHANGE_MODE:
		pr_info("change mode %d\n", local_arg);
		if (local_arg == IA_RAW_PROXY_MODE)
			ret = iacore_change_state_lock_safe(iacore, PROXY_MODE);
		else if (local_arg == IA_RAW_NORMAL_MODE)
			ret = iacore_change_state_lock_safe(iacore, POWER_OFF);
		else
			ret = -EINVAL;
		break;
	case IA_ACTIVE_BUS_CONFIG:
		pr_info("get active bus config\n");
#if defined(CONFIG_SND_SOC_IA_SPI)
		bus_config = IA_RAW_BUS_SPI;
#elif defined(CONFIG_SND_SOC_IA_I2C)
		bus_config = IA_RAW_BUS_I2C;
#elif defined(CONFIG_SND_SOC_IA_UART)
		bus_config = IA_RAW_BUS_UART;
#elif defined(CONFIG_SND_SOC_IA_SDW_X86)
		bus_config = IA_RAW_BUS_SWIRE;
#endif

#if defined(CONFIG_SND_SOC_IA_I2S_CODEC)
		bus_config |= IA_RAW_BUS_I2S_CODEC;
#elif defined(CONFIG_SND_SOC_IA_I2S_HOST)
		bus_config |= IA_RAW_BUS_I2S_HOST;
#elif defined(CONFIG_SND_SOC_IA_I2S_PERF)
		bus_config |= IA_RAW_BUS_I2S_PERF;
#else
		bus_config |= IA_RAW_BUS_PDM;
#endif

		if (copy_to_user((void *)arg, &bus_config, sizeof(u32))) {
			pr_err("copy_to_user bus_config failed\n");
			ret = -EFAULT;
		}
		break;
	case IA_ENABLE_DISABLE_IRQ:
		pr_info("enable disable irq %d\n", local_arg);
		if (local_arg == IA_RAW_DISABLE_IRQ)
			iacore_disable_irq(iacore);
		else if (local_arg == IA_RAW_ENABLE_IRQ)
			iacore_enable_irq(iacore);
		else
			ret = -EINVAL;
		break;
	case IA_ENABLE_BUFF_READ:
		pr_info("enable Read Buffer\n");
		ret = enable_buffered_raw_read(iacore);
		break;
	case IA_DISABLE_BUFF_READ:
		pr_info("Disable Read Buffer\n");
		ret = disable_buffered_raw_read(iacore);
		break;
	default:
		pr_err("Invalid ioctl command received %x\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}
