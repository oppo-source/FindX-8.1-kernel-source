/*
 * iacore.c  --  Audience ia6xx Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "iaxxx.h"

#include <linux/tty.h>
#include "iacore.h"
#include "iacore-cdev.h"
#include "iacore-uart.h"
#include "iacore-uart-common.h"

extern int msm_geni_iacore_uart_pinctrl_enable(int enable);
int iacore_collect_rdb(struct iacore_priv *iacore)
{
	int rc = 0;

	if (iacore->debug_buff == NULL) {
		iacore->debug_buff = kmalloc(DBG_BLK_SIZE, GFP_KERNEL);
		if (iacore->debug_buff == NULL) {
			pr_err("No kmem to read fw debug data\n");
			rc = -ENOMEM;
			goto skip_rdb;
		}
	}

	pr_info("before rdb\n");

	rc = read_debug_data(iacore, iacore->debug_buff);
	if (rc < 0) {
		pr_err("Read Debug data failed %d\n", rc);
		kfree(iacore->debug_buff);
		iacore->debug_buff = NULL;
	}

	pr_info("after rdb\n");

skip_rdb:
	return rc;
}

/* is_forced allows to force recover irrespective of the value of
 * disable_fw_recovery_cnt. Helps in asynchronous cases (e.g. interrupt)
 * NOTE: This function must be called with access_lock acquired */
int iacore_fw_recovery_unlocked(struct iacore_priv *iacore, int is_forced)
{
	int rc = 0;
#ifdef CONFIG_SND_SOC_IA_FW_RECOVERY
	char *event[] = { "ACTION=ADNC_FW_RECOVERY", NULL };
	u8 fw_state = iacore_get_power_state(iacore);

	pr_debug("called, disable_fw_recovery_cnt %d, is_forced %d\n",
			iacore->disable_fw_recovery_cnt, is_forced);

	iacore->rdb_buf_len = 0;
	if (iacore->disable_fw_recovery_cnt && !is_forced) {
		pr_info("return\n");
		return 0;
	}

	lockdep_assert_held(&iacore->access_lock);

	pr_info("in recovery %d\n", iacore->in_recovery);

	/* if ia6xx FW crash is not handled properly in FW
	 * it is getting stuck in FW instead of entering into SBL mode
	 * and when host send diagnostic log cmd it fails since FW is
	 * not in SBL and host keep entering into recovery mode and
	 * at one point kernel stack over flows and it leads to kernel crash.
	 * To avoid kernel crash added a check for recovery re-entry case.
	 */
	if (iacore->in_recovery) {
		pr_info("return\n");
		return 0;
	}

	pr_info("firmware is loaded in %s mode\n",
		 iacore_fw_state_str(iacore->fw_state));

	/* Disable irq during fw recovery */
	iacore_disable_irq_nosync(iacore);

	if (fw_state != SBL && fw_state != POWER_OFF) {

		iacore_stop_streaming_thread(iacore);

		pr_info("is_forced = %d\n", is_forced);

		iacore->in_recovery = 1;
		iacore_collect_rdb(iacore);

		/* if everything is fine, then chip must have woken up */
		switch (iacore->fw_state) {
		case VS_SLEEP:
		case BARGEIN_SLEEP:
		case VS_BURSTING:
		case BARGEIN_BURSTING:
			iacore->fw_state = VS_MODE;
			break;
		case DEEP_SLEEP:
		case HW_BYPASS:
			iacore->fw_state = SBL;
			break;
		default:
			iacore->fw_state = FW_LOADED;
			break;
		}

		pr_info("firmware is loaded in %s mode\n",
			 iacore_fw_state_str(iacore->fw_state));

		/* send uevent irrespective of read success */
		kobject_uevent_env(&iacore->kobj, KOBJ_CHANGE, event);
	} else {
		iacore->in_recovery = 1;
		pr_info("in recovery 1");
	}

	pr_info("in_recovery %d\n", iacore->in_recovery);

	iacore_recover_chip_to_fw_sleep_unlocked(iacore);

	/* setting up for app recovery */
	iacore->iacore_cv_kw_detected = true;
	iacore->iacore_event_type = IA_VS_FW_RECOVERY;
	wake_up_interruptible(&iacore->cvq_wait_queue);

	iacore->in_recovery = 0;
#endif

	pr_info("in_recovery %d leave\n", iacore->in_recovery);

	return rc;
}

/* History structure, log route commands to debug */
/* Send a single command to the chip.
 *
 * If the SR (suppress response bit) is NOT set, will read the
 * response and cache it the driver object retrieve with iacore_resp().
 *
 * Returns:
 * 0 - on success.
 * EITIMEDOUT - if the chip did not respond in within the expected time.
 * E* - any value that can be returned by the underlying HAL.
 */

int iacore_cmd_nopm(struct iacore_priv *iacore, u32 cmd, u32 *resp)
{
	int sr;
	int err;

	*resp = 0;
	sr = cmd & BIT(28);

	err = iacore_pm_get_sync(iacore);
	if (err < 0) {
		pr_err("pm_get_sync failed: %d\n", err);
		return err;
	}

	err = iacore->bus.ops.cmd(iacore, cmd, resp);
	if (err || sr)
		goto exit;

	if ((*resp) == 0) {
		err = -ETIMEDOUT;
		pr_err("no response to command 0x%08x\n", cmd);
	} else {
		iacore->bus.last_response = *resp;
		get_monotonic_boottime(&iacore->last_resp_time);
	}

exit:
	iacore_pm_put_autosuspend(iacore);
	return err;
}

int iacore_cmd_locked(struct iacore_priv *iacore, u32 cmd, u32 *resp)
{
	int ret;

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	//ret = iacore_pm_get_sync(iacore);
	//if (ret > -1) {
		ret = iacore_cmd_nopm(iacore, cmd, resp);
	//	iacore_pm_put_autosuspend(iacore);
	//}
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return ret;
}

int iacore_cmd(struct iacore_priv *iacore, u32 cmd, u32 *resp)
{
	int ret;
	ret = iacore_pm_get_sync(iacore);
	if (ret > -1) {
		ret = iacore_cmd_nopm(iacore, cmd, resp);
		iacore_pm_put_autosuspend(iacore);
	}
	return ret;
}
int iacore_write_block(struct iacore_priv *iacore, const u32 *cmd_block)
{
	int ret = 0;
	u32 resp;
	IACORE_MUTEX_LOCK(&iacore->access_lock);
	ret = iacore_pm_get_sync(iacore);
	if (ret > -1) {
		while (*cmd_block != 0xffffffff) {
			ret = iacore_cmd_nopm(iacore, *cmd_block, &resp);
			if (ret)
				break;

			usleep_range(IA_DELAY_1MS, IA_DELAY_1MS + 5);
			cmd_block++;
		}
		iacore_pm_put_autosuspend(iacore);
	}
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return ret;
}

static int iacore_prepare_msg(struct iacore_priv *iacore, unsigned int reg,
			      unsigned int value, char *msg, int *len,
			      int msg_type)
{
	struct iacore_api_access *api_access;
	u32 api_word[2] = {0};
	unsigned int val_mask;
	int msg_len;

	if (reg > iacore->api_addr_max) {
		pr_err("invalid address = 0x%04x\n", reg);
		return -EINVAL;
	}

	pr_debug("reg=%08x val=%d\n", reg, value);

	api_access = &iacore->api_access[reg];
	val_mask = (1 << get_bitmask_order(api_access->val_max)) - 1;

	if (msg_type == IA_MSG_WRITE) {
		msg_len = api_access->write_msg_len;
		memcpy((char *)api_word, (char *)api_access->write_msg,
				msg_len);

		switch (msg_len) {
		case 8:
			api_word[1] |= ((val_mask & value) <<
						api_access->val_shift);
			break;
		case 4:
			api_word[0] |= ((val_mask & value) <<
						api_access->val_shift);
			break;
		}
	} else {
		msg_len = api_access->read_msg_len;
		memcpy((char *)api_word, (char *)api_access->read_msg,
				msg_len);
	}

	*len = msg_len;
	memcpy(msg, (char *)api_word, *len);

	return 0;

}

static int _iacore_read(struct iacore_priv *iacore, unsigned int reg)
{
	u32 api_word[2] = {0};
	unsigned int msg_len;
	unsigned int value = 0;
	u32 resp;
	int rc;

	if (!iacore) {
		pr_err("Invalid argument for iacore\n");
		return -EINVAL;
	}

	rc = iacore_prepare_msg(iacore, reg, value, (char *) api_word,
			&msg_len, IA_MSG_READ);
	if (rc) {
		pr_err("Prepare read message fail %d\n", rc);
		goto out;
	}

	rc = iacore_cmd_nopm(iacore, api_word[0], &resp);
	if (rc < 0) {
		pr_err("iacore_cmd failed, rc = %d\n", rc);
		return rc;
	}
	api_word[0] = iacore->bus.last_response;

	value = api_word[0] & 0xffff;
out:
	return value;
}

/* READ API to firmware:
 * This API may be interrupted. If there is a series of READs  being issued to
 * firmware, there must be a fw_access lock acquired in order to ensure the
 * atomicity of entire operation.
 */
int iacore_read(struct iacore_priv *iacore, unsigned int reg)
{
	unsigned int ret = 0;
	int rc;
	rc = iacore_pm_get_sync(iacore);
	if (rc > -1) {
		ret = _iacore_read(iacore, reg);
		iacore_pm_put_autosuspend(iacore);
	}
	return ret;
}

static int _iacore_write(struct iacore_priv *iacore, unsigned int reg,
		       unsigned int value)
{
	u32 api_word[2] = {0};
	int msg_len;
	u32 resp;
	int rc;
	int i;

	if (!iacore) {
		pr_err("Invalid argument for iacore\n");
		return -EINVAL;
	}

	rc = iacore_prepare_msg(iacore, reg, value, (char *) api_word,
			&msg_len, IA_MSG_WRITE);
	if (rc) {
		pr_err("Failed to prepare write message %d\n", rc);
		goto out;
	}

	for (i = 0; i < msg_len / 4; i++) {
		rc = iacore_cmd_nopm(iacore, api_word[i], &resp);
		if (rc < 0) {
			pr_err("iacore_cmd_nopm err %d\n", rc);
			return rc;
		}
	}

out:
	return rc;
}

int iacore_datablock_open(struct iacore_priv *iacore)
{
	int rc = 0;
	if (iacore->bus.ops.open)
		rc = iacore->bus.ops.open(iacore);
	return rc;
}

int iacore_datablock_close(struct iacore_priv *iacore)
{
	int rc = 0;
	if (iacore->bus.ops.close)
		rc = iacore->bus.ops.close(iacore);
	return rc;
}

int iacore_datablock_wait(struct iacore_priv *iacore)
{
	int rc = 0;
	if (iacore->bus.ops.wait)
		rc = iacore->bus.ops.wait(iacore);
	return rc;
}

/* Sends RDB ID to fw and gets data block size from the response */
int iacore_get_rdb_size(struct iacore_priv *iacore, int id)
{
	int rc;
	int size;
	u32 resp;
	u32 cmd = (IA_READ_DATA_BLOCK << 16) | (id & 0xFFFF);

	pr_info("\n");

	rc = iacore->bus.ops.cmd(iacore, cmd, &resp);
	if (rc < 0) {
		pr_err("bus.ops.cmd() failed rc = %d\n", rc);
		return rc;
	}

	if ((resp >> 16) != IA_READ_DATA_BLOCK) {
		pr_err("Invalid response received: 0x%08x\n", resp);
		return -EINVAL;
	}

	pr_info("firmware is loaded in %s mode\n",
			 iacore_fw_state_str(iacore->fw_state));

	size = resp & 0xFFFF;
	pr_debug("RDB size = %d\n", size);
	if (size == 0 || size % 4 != 0) {
		pr_err("Read Data Block with invalid size:%d\n", size);
		return -EINVAL;
	}

	return size;
}

/* Call iacore_get_rdb_size before calling this as that's the one that actually
 * sends RDB ID to fw. Attempt to read before sending ID would cause error */
int iacore_datablock_read(struct iacore_priv *iacore, void *buf,
		size_t len)
{
	int rc;
	int rdcnt = 0;

	if (iacore->bus.ops.rdb) {
		/* returns 0 on success */
		rc = iacore->bus.ops.rdb(iacore, buf, len);
		if (rc)
			return rc;
		return len;
	}

	for (rdcnt = 0; rdcnt < len;) {
		rc = iacore->bus.ops.read(iacore, buf, 4);
		if (rc < 0) {
			pr_err("Read Data Block error %d\n", rc);
			pr_err("read bytes: %d, total bytes: %ld\n",
				rdcnt, (unsigned long)len);
			return rc;
		}
		rdcnt += 4;
		buf += 4;
	}

	return len;
}

int iacore_datablock_write(struct iacore_priv *iacore, const void *buf,
			   size_t wdb_len, size_t actual_len)
{
	int rc;
	u32 cmd, resp;
	int retry = 10;
#ifdef CONFIG_SND_SOC_IA_UART
	struct iacore_uart_device *iacore_uart = iacore->dev_data;

	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}
#endif

	pr_debug("wdb len = %zd, actual len = %zd\n", wdb_len, actual_len);

	if (actual_len % IA6XX_WDB_BLOCK_SIZE) {
		pr_err("block size %zd is not aligned to : %d\n",
			actual_len, IA6XX_WDB_BLOCK_SIZE);

#ifndef CONFIG_SND_SOC_IA_I2S_PERF
		rc = -EINVAL;
		goto out;
#endif
	}

	cmd = (IA_WRITE_DATA_BLOCK << 16) | (wdb_len & 0xFFFF);

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	rc = iacore_i2sperf_wdb_prepare(iacore);
	if (rc < 0) {
		pr_err("I2S perf wdb prepare failed\n");
		rc = -EIO;
		goto out;
	}
#endif
	cmd = iacore->bus.ops.cpu_to_bus(iacore, cmd);
	pr_debug("cmd :%08x\n", cmd);

	rc = iacore->bus.ops.write(iacore, &cmd, sizeof(cmd));
	if (rc < 0) {
		pr_err("write failed rc = %d\n", rc);
		goto out;
	}

#ifdef CONFIG_SND_SOC_IA_UART
	/* Wait till wdb set command is completely sent to the chip */
	tty_wait_until_sent(iacore_uart->tty,
					msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));
#endif

	/* 15ms Delay is required between WDB command and actual
	 * KW model file download. SHP-2893/SHP-4854.
	 */
	usleep_range(IA_DELAY_15MS, IA_DELAY_15MS + 5);

	rc = iacore->bus.ops.block_write(iacore, buf, actual_len);
	if (rc < 0) {
		pr_err("write error:%d\n", rc);
		goto out;
	}

#ifdef CONFIG_SND_SOC_IA_UART
	/* Wait till wdb set command is completely sent to the chip */
	tty_wait_until_sent(iacore_uart->tty,
					msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));
#endif

	do {
		usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 50);
		rc = iacore->bus.ops.read(iacore, &resp, sizeof(resp));
		if (rc < 0) {
			pr_err("read response failed rc = %d\n",
			       rc);
			continue;
		}
	} while (rc && retry--);

	if (rc)
		goto out;

	resp = iacore->bus.ops.cpu_to_bus(iacore, resp);
	pr_info("response received post wdb 0x%x\n", resp);
	if (resp != (IA_WRITE_DATA_BLOCK << 16)) {
		pr_err("write failed with error: 0x%x\n",
				(resp & 0xffff));
		rc = -EIO;
	}

	update_cmd_history(iacore->bus.ops.cpu_to_bus(iacore, cmd), resp);

	if (rc)
		goto out;

	pr_debug("write succeeded\n");

	return actual_len;
out:
	return rc;
}

/*
 * This function assumes chip isn't in sleep mode; which is when you typically
 * need debug data
 */
int read_debug_data(struct iacore_priv *iacore, void *buf)
{
	int ret;
	int len;

	pr_info("enter\n");
	ret = iacore_datablock_open(iacore);
	if (ret) {
		pr_err("can't open datablock device = %d\n", ret);
		return ret;
	}
	pr_debug("before rdb sz\n");

	/* ignore size as it's fix; we are calling this to send RDB ID */
	len = iacore_get_rdb_size(iacore, DBG_ID);
	if (len < 0)
		goto datablock_close;

	if (len > DBG_BLK_SIZE) {
		pr_err("rdb read size (%d) > rdb buf size (%d)\n",
						len, DBG_BLK_SIZE);
		ret = -EINVAL;
		goto datablock_close;
	}

	iacore->rdb_buf_len = len;

	pr_debug("after rdb sz");

	ret = iacore_datablock_read(iacore, buf, len);
	if (ret < 0)
		pr_err("failed to read debug data; err: %d\n", ret);

datablock_close:
	iacore_datablock_close(iacore);
	pr_info("leave\n");
	return ret;
}

/* WRITE API to firmware:
 * This API may be interrupted. If there is a series of WRITEs or READs  being
 * issued to firmware, there must be a fw_access lock acquired in order to
 * ensure the atomicity of entire operation.
 */
int iacore_write(struct iacore_priv *iacore, unsigned int reg,
		       unsigned int value)
{
	int ret;
	ret = iacore_pm_get_sync(iacore);
	if (ret > -1) {
		ret = _iacore_write(iacore, reg, value);
		iacore_pm_put_autosuspend(iacore);
	}
	return ret;

}

/* NOTE: This function must be called with access_lock acquired */
int iacore_wakeup_unlocked(struct iacore_priv *iacore)
{
#ifndef CONFIG_SND_SOC_IA_UART
	u32 cmd = IA_SYNC_CMD << 16;
	u32 rsp;
#endif
	int rc = 0;
	int retry = 2;
	u8 current_state = POWER_OFF;

	lockdep_assert_held(&iacore->access_lock);

	/*
	 * if we are waking up the chip, it means we dont need irq.
	 * Disable it immediately
	 */
	iacore_disable_irq_nosync(iacore);

	pr_info("firmware is loaded in %s mode\n",
			 iacore_fw_state_str(iacore->fw_state));

	/* If debug FW loaded, wakeup is not required */
	if (iacore->dbg_fw == true) {
		pr_err("debug streaming eanbled. just return\n");
		return 0;
	}

	do {
		if (iacore->low_latency_route_set == true)
			goto skip_state_check;

		if (iacore->spl_pdm_mode == true)
			goto skip_state_check;

		current_state = iacore_get_power_state(iacore);

		if (current_state == PROXY_MODE)
			goto skip_state_check;

		if (current_state != FW_HW_BYPASS &&
		    current_state != SPL_PDM_RECORD &&
		    current_state != VS_SLEEP &&
		    current_state != HW_BYPASS &&
		    current_state != FW_SLEEP &&
		    current_state != DEEP_SLEEP) {
			pr_err("firmware is not in sleep\n");
			break;
		}

skip_state_check:
		/* Bus specific callback is called, if any callback is
		 * registered. */
		if (iacore->bus.ops.wakeup) {
			rc = iacore->bus.ops.wakeup(iacore);
			if (rc) {
				pr_err("Wakeup failed rc = %d\n", rc);
				goto iacore_wakeup_exit;
			}

		/* Toggle the wakeup pin H->L then L->H */
		} else if (iacore->pdata->wakeup_gpio != -1) {
			/* wakeup line driving low is causing bavaria
			 * to enter unknown state, so set the wakeup
			 * line to high after chip wakeup */

			gpio_set_value(iacore->pdata->wakeup_gpio, 1);
			usleep_range(IA_DELAY_1MS, IA_DELAY_1MS + 5);
			gpio_set_value(iacore->pdata->wakeup_gpio, 0);
			usleep_range(IA_DELAY_1MS, IA_DELAY_1MS + 5);
			gpio_set_value(iacore->pdata->wakeup_gpio, 1);
		} else {
			pr_err("no wakeup mechanism defined\n");
			rc = -EINVAL;
			goto iacore_wakeup_exit;
		}

#ifndef CONFIG_SND_SOC_IA_UART
		msleep(30);

		/* Send sync command to verify device is active */
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc < 0) {
			pr_err("failed sync cmd resume %d\n", rc);
		}

		/* Response will not be same as command if the chip is in SBL */
		if (cmd != (rsp & (0xffff << 16))) {
			/*
			 * However, Chip could have reset to SBL. Recheck if
			 * chip is in SBL. If in SBL, reset it to FW_LOADED
			 * state.
			 */
			if (rsp == IA_SBL_SYNC_ACK) {
				pr_err("failed sync rsp in SBL = 0x%x\n", rsp);
				iacore->fw_state = SBL;
				iacore_change_state_unlocked(iacore, FW_LOADED);
			} else {
				pr_err("failed sync rsp resume %d\n", rc);
				rc = -EIO;
			}
		}
#endif
		if (current_state == PROXY_MODE) {
			rc = 0;
			goto iacore_wakeup_exit;
		}


	} while (rc && --retry);

iacore_wakeup_exit:
	return rc;
}

int iacore_get_streaming_mode(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if defined(CONFIG_SND_SOC_IA_PDM)
	ucontrol->value.enumerated.item[0] = 0;
#elif defined(CONFIG_SND_SOC_IA_I2S_HOST)
	ucontrol->value.enumerated.item[0] = 1;
#elif defined(CONFIG_SND_SOC_IA_I2S_CODEC)
	ucontrol->value.enumerated.item[0] = 2;
#elif defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
	ucontrol->value.enumerated.item[0] = 3;
#elif defined(CONFIG_SND_SOC_IA_I2S_PERF)
	ucontrol->value.enumerated.item[0] = 4;
#else
	ucontrol->value.enumerated.item[0] = 5;
#endif

	return 0;
}

int iacore_power_init(struct iacore_priv *iacore)
{
	int rc = 0;

	iacore->pwr_vdd = regulator_get(iacore->dev, "adnc,vdd-ldo");
	if (IS_ERR_OR_NULL(iacore->pwr_vdd)) {
		rc = PTR_ERR(iacore->pwr_vdd);
		pr_err("Regulator get fail %d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(iacore->pwr_vdd) > 0) {
		rc = regulator_set_voltage(iacore->pwr_vdd,
					IACORE_PWR_VTG_MIN_UV,
					IACORE_PWR_VTG_MAX_UV);
		if (rc) {
			pr_err("Regulator set_vtg fail %d\n", rc);
			goto reg_vdd_put;
		}
	}

	return rc;

reg_vdd_put:
	regulator_put(iacore->pwr_vdd);
	return rc;
}

void iacore_power_ctrl(struct iacore_priv *iacore, bool value)
{
	int rc = 0;
	if (value) {
		gpio_direction_output(iacore->pdata->ldo_en_pin, 1);
		pr_err("set ldo_en_pin 1 high\n");
		msleep(50);
		rc = msm_geni_iacore_uart_pinctrl_enable(1);
		if (rc) {
			pr_err("set uart highz mode Failed %d\n", rc);
		}
	} else {
		rc = msm_geni_iacore_uart_pinctrl_enable(0);
		if (rc) {
			pr_err("set uart highz mode Failed %d\n", rc);
		}
		gpio_direction_output(iacore->pdata->ldo_en_pin, 0);
		pr_err("set ldo_en_pin 0 low\n");
		msleep(150);

	}
	usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 5000);

}

void iacore_enable_irq(struct iacore_priv *iacore)
{
	mutex_lock(&iacore->irq_lock);
	pr_debug("enter\n");
	if (!iacore->irq_enabled) {
		enable_irq(iacore->pdata->irq_pin);
		iacore->irq_enabled = true;
		pr_info("done\n");
	}
	pr_debug("leave\n");
	mutex_unlock(&iacore->irq_lock);
}

void iacore_disable_irq(struct iacore_priv *iacore)
{
	mutex_lock(&iacore->irq_lock);
	pr_debug("enter\n");
	if (iacore->irq_enabled) {
		disable_irq(iacore->pdata->irq_pin);
		iacore->irq_enabled = false;
		pr_info("done\n");
	}
	pr_debug("leave\n");
	mutex_unlock(&iacore->irq_lock);
}

void iacore_disable_irq_nosync(struct iacore_priv *iacore)
{
	mutex_lock(&iacore->irq_lock);
	pr_debug("enter\n");
	if (iacore->irq_enabled) {
		disable_irq_nosync(iacore->pdata->irq_pin);
		iacore->irq_enabled = false;
		pr_info("done\n");
	}
	pr_debug("leave\n");
	mutex_unlock(&iacore->irq_lock);
}
