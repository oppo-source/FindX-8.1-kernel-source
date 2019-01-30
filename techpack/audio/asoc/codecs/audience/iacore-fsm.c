/*
 * iacore-fsm.c  --  Audience firmware state manager
 *
 * Copyright 2016 Audience, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "iaxxx.h"

#include <linux/tty.h>
#include "ia6xx-iacore.h"
#include "iacore-vs.h"
#include "iacore-uart.h"
#include "iacore-uart-common.h"

#define ENTRY(x) x /* to avoid complex define error in checkpatch.pl */
#define ENUM_NAME(NAME) ENTRY([NAME] = #NAME)
#define FW_SLEEP_TIMEOUT	5

const char *iacore_fw_state_str(u8 state)
{
	static const char *const fw_state[] = {
		ENUM_NAME(POWER_OFF),
		ENUM_NAME(SBL),
		ENUM_NAME(FW_LOADED),
		ENUM_NAME(HW_BYPASS),
		ENUM_NAME(FW_HW_BYPASS),
		ENUM_NAME(SPL_PDM_RECORD),
		ENUM_NAME(SW_BYPASS),
		ENUM_NAME(IO_STREAMING),
		ENUM_NAME(IO_BARGEIN_STREAMING),
		ENUM_NAME(VS_MODE),
		ENUM_NAME(VS_SLEEP),
		ENUM_NAME(BARGEIN_SLEEP),
		ENUM_NAME(VS_BURSTING),
		ENUM_NAME(BARGEIN_BURSTING),
		ENUM_NAME(FW_SLEEP),
		ENUM_NAME(DEEP_SLEEP),
		ENUM_NAME(PROXY_MODE),
	};

	if (state > PROXY_MODE)
		return "INVALID_STATE_ERROR";

	return fw_state[state];
}

bool iacore_fw_loaded_unlocked(struct iacore_priv *iacore)
{
	u8 is_loaded = 0;

	lockdep_assert_held(&iacore->access_lock);

	switch (iacore->fw_state) {
	case FW_LOADED:
	case FW_HW_BYPASS:
	case SPL_PDM_RECORD:
	case SW_BYPASS:
	case VS_MODE:
	case VS_SLEEP:
	case BARGEIN_SLEEP:
	case FW_SLEEP:
	case VS_BURSTING:
	case BARGEIN_BURSTING:
	case IO_STREAMING:
	case IO_BARGEIN_STREAMING:
		pr_debug("firmware is loaded in %s mode\n",
			 iacore_fw_state_str(iacore->fw_state));
		is_loaded = 1;
		break;
	case POWER_OFF:
	case SBL:
	case HW_BYPASS:
	case DEEP_SLEEP:
		pr_debug("firmware not loaded in %s mode\n",
			 iacore_fw_state_str(iacore->fw_state));
		is_loaded = 0;
		break;
	default:
		pr_err("Invalid transition requested = %d\n", iacore->fw_state);
		break;
	}

	return is_loaded;
}


bool iacore_fw_loaded_lock_safe(struct iacore_priv *iacore)
{
	u8 is_loaded = 0;

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	is_loaded = iacore_fw_loaded_unlocked(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return is_loaded;
}

int iacore_check_and_reload_fw_unlocked(struct iacore_priv *iacore)
{
	int rc = 0;

	if (iacore_fw_loaded_unlocked(iacore) == true) {
		pr_info("firmware is loaded\n");
	} else {
		rc = iacore_change_state_unlocked(iacore, FW_LOADED);
		if (rc)
			pr_err("firmware couldn't be loaded. rc %d\n", rc);
		else
			pr_info("firmware is reloaded\n");
	}

	return rc;
}

int iacore_set_chip_sleep(struct iacore_priv *iacore, u32 sleep_state)
{
	int rc = 0;

#if !defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
	u32 cmd, rsp;
#ifdef CONFIG_SND_SOC_IA_UART
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
#endif

	pr_info("Set chip to sleep \n");
	sleep_state &= SLEEP_STATE_MASK;
	cmd = (IA_SET_POWER_STATE << 16) | sleep_state;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set chip to sleep (%d) cmd fail %d\n", sleep_state, rc);
	}

#ifdef CONFIG_SND_SOC_IA_UART
	else {
		/*
		 * on uart interface, make sure the command is
		 * actual sent on the line
		 */
		if (IS_ERR_OR_NULL(iacore_uart->tty)) {
			pr_debug("tty is not available\n");
			return 0;
		}
		tty_wait_until_sent(iacore_uart->tty,
			msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));
	 }
#endif

#endif

	return rc;
}

/* NOTE: This function must be called with access_lock acquired */
int iacore_stop_active_route_unlocked(struct iacore_priv *iacore)
{
	u32 cmd;
	u32 rsp = 0;
	int rc = 0;

	lockdep_assert_held(&iacore->access_lock);

	pr_info("active_route_set %d low_latency_route_set %d\n",
		iacore->active_route_set, iacore->low_latency_route_set);

	if (iacore->active_route_set == false)
		return 0;

	rc = iacore_pm_get_sync(iacore);
	if (rc < 0) {
		pr_err("pm_get_sync failed :%d\n", rc);
		goto ret_out;
	}

	/* For low latency routes, just send wakeup byte to stop the route */
	if (iacore->low_latency_route_set == true) {
		rc = iacore_wakeup_unlocked(iacore);
		if (rc)
			pr_err("wakeup failed rc = %d\n", rc);

		iacore->low_latency_route_set = false;
		iacore->active_route_set = false;

		goto err_out;
	}

	/* stop any active route */
	cmd = IA_SET_STOP_ROUTE << 16 | IA_STOP_ROUTE_VALUE;
	rc = iacore_fix_extra_byte_read(iacore, cmd, &rsp);
	if (!rc) {
		usleep_range(IA_DELAY_25MS, IA_DELAY_25MS + 1000);
		iacore->active_route_set = false;
	} else {
		pr_err("stop active route fail %d\n", rc);
	}

err_out:
	iacore_pm_put_autosuspend(iacore);
ret_out:
	return rc;
}

int iacore_set_active_route(struct iacore_priv *iacore,
						bool ll_route, u32 route)
{
	u32 cmd, rsp;
	int rc;

	/* set active route */
	route = route & 0xffff;

	if (ll_route == true)
		cmd = IA_SET_LL_ROUTE << 16 | route;
	else
		cmd = IA_SET_ROUTE << 16 | route;

	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (!rc)
		iacore->active_route_set = true;
	else
		pr_err("set route fail %d\n", rc);

	return rc;
}

/* NOTE: This function must be called with access_lock acquired */
int iacore_stop_bypass_unlocked(struct iacore_priv *iacore)
{
	u32 cmd, rsp;
	int rc;

	/* wake up the chip */
	rc = iacore_wakeup_unlocked(iacore);
	if (rc) {
		pr_err("wakeup failed rc = %d\n", rc);
		return rc;
	}

	/* Disable PDM Bypass mode */
	cmd = IA_SET_BYPASS_MODE << 16 | IA_BYPASS_MODE_OFF;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc)
		pr_err("Set BYPASS MODE cmd OFF fail %d\n", rc);

	return rc;
}

/* NOTE: This function must be called with access_lock acquired */
static int iacore_set_bypass_unlocked(struct iacore_priv *iacore)
{
	u32 cmd, rsp = 0;
	int rc = 0;

	if (iacore->fw_state == SBL) {
		if (!iacore->boot_ops.sbl_bypass_setup) {
			pr_err("invalid set bypass setup callback\n");
			rc = -EINVAL;
			goto set_bypass_err;
		}

		/* send some bus related activity */
		rc = iacore->boot_ops.sbl_bypass_setup(iacore);
		if (rc) {
			pr_err("set Sleep setup fails %d\n", rc);
			goto set_bypass_err;
		}
		iacore->skip_boot_setup = true;

	} else if (iacore->fw_state == FW_LOADED) {

#ifdef CONFIG_SND_SOC_IA_I2S
#ifdef CONFIG_SND_SOC_IA_I2S_SPL_PDM_MODE
		if (iacore->spl_pdm_mode != true) {
			/* TODO: fix this sleep */
			usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 50);

			/* stop any active route Route */
			rc = iacore_stop_active_route_unlocked(iacore);
			if (rc)
				goto set_bypass_err;
		}

#else
		/* TODO: fix this sleep */
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 50);

		/* stop any active route Route */
		rc = iacore_stop_active_route_unlocked(iacore);
		if (rc)
			goto set_bypass_err;
#endif
#endif
	} else {
		pr_err("firmware is not in required power state: %d\n",
				iacore->fw_state);
		rc = -EINVAL;
		goto set_bypass_err;
	}

	//usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 50);
	cmd = IA_SET_BYPASS_MODE << 16 | (iacore->bypass_mode & 0xFFFF);
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set BYPASS MODE cmd ON fail %d\n", rc);
		goto set_bypass_err;
	}

	/* Put the chip in to sleep */
	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 50);

	rc = iacore_set_chip_sleep(iacore, IA_CHIP_SLEEP);
	if (rc == 0)
		usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 50);

set_bypass_err:
	return rc;
}

u8 iacore_get_power_state(struct iacore_priv *iacore)
{
	return iacore->fw_state;
}

void iacore_poweroff_chip(struct iacore_priv *iacore)
{
	iacore_disable_irq_nosync(iacore);
	usleep_range(IA_DELAY_20MS, IA_DELAY_20MS + 5);
	iacore_power_ctrl(iacore, IA_POWER_OFF);
	iacore->fw_state = POWER_OFF;
	vs_reset_model_file(iacore);
	iacore->skip_boot_setup = false;
	iacore->spl_pdm_mode = false;
}
#ifdef FW_SLEEP_TIMER_TEST
/*
 * iacore_timer_enter_fw_sleep :
 * Puts the chip into FW sleep (FW retention) when the
 * timer is elapsed or being forced to FW Sleep.
 */
static void iacore_timer_enter_fw_sleep(struct iacore_priv *iacore)
{
	int rc = 0;

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	if ((iacore_get_power_state(iacore) != FW_LOADED) && (!iacore->fs_thread_wait_flg)){
		pr_err("Ignore current fw sleep in SPL_PDM_RECORD mode\n");
		goto error;
	}

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("open failed %d\n", rc);
		goto error;
	}

	rc = iacore_set_chip_sleep(iacore, IA_FW_RETENTION);
	if (rc) {
		pr_err("Put to FW Sleep Failed %d\n", rc);
		goto err_out;
	}

	msleep(70);

	vs_reset_model_file(iacore);
	iacore->fw_state = FW_SLEEP;

	iacore_datablock_close(iacore);

	iacore_enable_irq(iacore);
	pr_info("firmware present state %s\n",
			iacore_fw_state_str(iacore->fw_state));
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return;

err_out:
	iacore_datablock_close(iacore);
error:
	pr_info("firmware present state %s\n",
	iacore_fw_state_str(iacore->fw_state));
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
}

/*
 * iacore_fw_sleep_thread :
 * Thread to call iacore_timer_enter_fw_sleep
 *
 */
int iacore_fw_sleep_thread(void *ptr)
{
	int rc;
	struct iacore_priv *iacore;

	iacore = (struct iacore_priv *) ptr;
	iacore->fs_thread_wait_flg = false;

	while (1) {
		rc = wait_event_interruptible(iacore->fs_thread_wait,
			((iacore->fs_thread_wait_flg != false))
			|| kthread_should_stop());

		if (kthread_should_stop() || rc == -ERESTARTSYS) {
			pr_err("exiting %d\n", rc);
			break;
		}

		spin_lock(&iacore->fs_tmr_lock);
		if (iacore->fs_thread_wait_flg == false) {
			pr_info("Calling FW Sleep skip.\n");
			spin_unlock(&iacore->fs_tmr_lock);
			continue;
		} else {
			iacore->fs_thread_wait_flg = false;
		}
		spin_unlock(&iacore->fs_tmr_lock);

		pr_info("Calling FW Sleep start.\n");
		iacore_timer_enter_fw_sleep(iacore);
	}

	return 0;
}

void iacore_fw_sleep_timer_fn(unsigned long data)
{
	struct iacore_priv *iacore = (struct iacore_priv *)data;

	wake_up(&iacore->fs_thread_wait);

	pr_info("done\n");

}

/*
 * setup_fw_sleep_timer_unlocked()
 * @timeout : timeout in seconds
 * timeout = 0 means disable the timer
 * NOTE: avoid access_lock deadlock
 */
int setup_fw_sleep_timer_unlocked(struct iacore_priv *iacore, u32 timeout)
{
	int rc = 0;
	unsigned long flags;

	pr_info("timeout %d\n", timeout);

	spin_lock_irqsave(&iacore->fs_tmr_lock, flags);

	if (timeout == 0) {
		/* del_timer_sync() is more properly, but
		 * may lead to disable irq too long. */
		iacore->fs_thread_wait_flg = false;
		if (timer_pending(&iacore->fs_timer))
			del_timer(&iacore->fs_timer);

		spin_unlock_irqrestore(&iacore->fs_tmr_lock, flags);

		return rc;
	}

	iacore->fs_thread_wait_flg = true;
	if (timer_pending(&iacore->fs_timer)) {
		if (mod_timer(&iacore->fs_timer, jiffies + (timeout * HZ))) {
			spin_unlock_irqrestore(&iacore->fs_tmr_lock, flags);
			return rc;
		}
	}

	iacore->fs_timer.function = iacore_fw_sleep_timer_fn;
	iacore->fs_timer.data = (unsigned long)iacore;
	iacore->fs_timer.expires = jiffies + timeout * HZ;

	add_timer(&iacore->fs_timer);

	spin_unlock_irqrestore(&iacore->fs_tmr_lock, flags);

	return rc;
}

/*
 * setup_fw_sleep_timer()
 * @force : Force switch to FW  sleep instead of timer wait.
 * force = 1 means switch to FW sleep immediately
 */
int setup_fw_sleep_timer(struct iacore_priv *iacore, u32 force)
{
	int rc = 0;
	unsigned long flags;

	pr_info("force %d\n", force);

	if (force == 1) {
		spin_lock_irqsave(&iacore->fs_tmr_lock, flags);
		iacore->fs_thread_wait_flg = false;
		if (timer_pending(&iacore->fs_timer))
			del_timer(&iacore->fs_timer);

		spin_unlock_irqrestore(&iacore->fs_tmr_lock, flags);

		iacore_timer_enter_fw_sleep(iacore);
	}

	return rc;
}

#endif
/*
 * is_chip_in_bootloader - check if chp is in bootloader or Firmware
 * Steps:
 *	1. Send "0x8000 0x0000"
 *	2. if required, retry 3 times reading the response and 3 times
 *	   re-sending the command
 *	3. If the response is
 *		"0x8000 0xFFFF" --> chip is in Bootloader
 *		"0x8000 0x0000" --> chip is in Firmware mode
 *	4. If the chip fails to respond to this sync command, no other option
 *	   but to do a power-reset and then try again
 *
 *	return:
 *		0x01 if in Bootloader
 *		0x00 if in Firmware
 *		< 0   if failed to get response
 *
 */
int is_chip_in_bootloader(struct iacore_priv *iacore)
{
	u32 sync_cmd, sync_cmd2, sync_ack;
	int rc = 0;
	int retry = IA_MAX_RETRY_3, send_sync_iter_cnt = IA_MAX_RETRY_3;
	int is_bootloader = -1;

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("open failed %d\n", rc);
		goto open_failed;
	}

	sync_cmd = (IA_SYNC_CMD << 16) | IA_SYNC_POLLING;
	sync_cmd2 = iacore->bus.ops.cpu_to_bus(iacore, sync_cmd);

	if (iacore->in_recovery)
		send_sync_iter_cnt = 2;

retry_send_sync:
	rc = iacore->bus.ops.write(iacore, &sync_cmd2, sizeof(sync_cmd2));
	if (rc) {
		pr_err("write sync cmd fail %d\n", rc);
		rc = -EIO;
		goto sync_failed;
	}

	usleep_range(IA_DELAY_2MS, IA_DELAY_2MS + 5);
	retry = IA_MAX_RETRY_3;

	if (iacore->in_recovery)
		retry = 2;

	do {
		rc = iacore->bus.ops.read(iacore, &sync_ack, sizeof(sync_ack));
		pr_debug("response received 0x%08x\n", sync_ack);
		if (rc) {
			pr_debug("sync ack read fail %d\n", rc);
		} else {
			sync_ack = iacore->bus.ops.bus_to_cpu(iacore, sync_ack);
			pr_info("response received after correction 0x%08x\n", sync_ack);
			if (sync_ack == sync_cmd) {
				is_bootloader = 0;
				break;
			} else if ((sync_ack & 0xFFFF) == 0xFFFF) {
				is_bootloader = 1;
				break;
			}
		}

		usleep_range(IA_DELAY_2MS, IA_DELAY_2MS + 5);
	} while (--retry);

#ifdef CONFIG_SND_SOC_IA_UART
	update_cmd_history(sync_cmd, sync_ack);
#else
	update_cmd_history(iacore->bus.ops.cpu_to_bus(iacore, sync_cmd),
								sync_ack);
#endif

	if (rc < 0 || retry <= 0) {
		pr_err("sync ack read fail after %d retries\n",
				(IA_MAX_RETRY_3 - send_sync_iter_cnt + 1));

		if (--send_sync_iter_cnt)
			goto retry_send_sync;
	}

sync_failed:

	iacore_datablock_close(iacore);

open_failed:
	if (rc < 0)
		return rc;
	else
		return is_bootloader;
}

static int setup_chip_unlocked(struct iacore_priv *iacore, bool reset_chip,
					bool download_fw, bool chip_to_sleep)
{
	int rc = 0;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	pr_info("reset_chip %d, download_fw %d, chip_to_sleep %d\n",
		reset_chip, download_fw, chip_to_sleep);

	if (!voice_sense) {
			pr_err("invalid private pointer");
			return -EINVAL;
	}

	if (reset_chip) {
		iacore_disable_irq_nosync(iacore);

		// Fixme : delays
		usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 5);
		usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 5);
		usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 5);
		iacore_power_ctrl(iacore, IA_POWER_OFF);
		iacore_power_ctrl(iacore, IA_POWER_ON);
		iacore->fw_state = SBL;

		/* Enable Interface Detection */
		iacore->skip_boot_setup = false;
		iacore->spl_pdm_mode = false;
		iacore->active_route_set = false;
		iacore->low_latency_route_set = false;
		iacore->bypass_mode = IA_BYPASS_OFF;

		if (voice_sense) {
			voice_sense->cvq_sleep = false;
			voice_sense->kw_model_loaded = false;
			/*voice_sense->bargein_sts = false;*/
			voice_sense->rec_to_bargein = false;
			voice_sense->bargein_vq_enabled = false;
			voice_sense->cvq_stream_on_i2s_req = false;
		}
	}

	pr_info("skip_boot_setup %d\n", iacore->skip_boot_setup);

	if (download_fw) {

		rc = iacore_load_firmware_unlocked(iacore);
		if (rc) {
			pr_err("FW load failed. needs power cycle %d\n", rc);

			/* 1st disable any interrupts */
			iacore_disable_irq_nosync(iacore);

			// Fixme : delays
			usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 5);
			usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 5);
			usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 5);
			usleep_range(IA_DELAY_20MS, IA_DELAY_20MS + 5);

			iacore_power_ctrl(iacore, IA_POWER_OFF);
			iacore_power_ctrl(iacore, IA_POWER_ON);

			/* Enable Interface Detection */
			iacore->skip_boot_setup = false;
			iacore->spl_pdm_mode = false;
			iacore->active_route_set = false;
			iacore->low_latency_route_set = false;
			iacore->bypass_mode = IA_BYPASS_OFF;

			if (voice_sense) {
				voice_sense->cvq_sleep = false;
				voice_sense->kw_model_loaded = false;
				/*voice_sense->bargein_sts = false;*/
				voice_sense->rec_to_bargein = false;
				voice_sense->bargein_vq_enabled = false;
				voice_sense->cvq_stream_on_i2s_req = false;
			}

			/* try fw download again. if fail, return */
			rc = iacore_load_firmware_unlocked(iacore);
			if (rc) {
				pr_err("Something seriously wrong. Abandon %d\n",
					rc);
				return rc;
			}
		}
		iacore->fw_state = FW_LOADED;
	}

	if (chip_to_sleep) {
		/*
		 * wakeup uses low latency flag & special pdm flag to
		 * skip state checks.
		 * Disable it
		 */
		iacore->low_latency_route_set = false;
		iacore->spl_pdm_mode = false;

		/* wake up the chip (if sleeping) */
		rc = iacore_wakeup_unlocked(iacore);
		if (rc) {
			pr_err("wakeup failed rc = %d\n", rc);
			return rc;
		}

		rc = iacore_set_chip_sleep(iacore, IA_FW_RETENTION);
		if (rc) {
			pr_err("FW state change to FW Retention sleep fail %d, retrying",
									rc);

			/* try FW sleep again. if fail, return */
			rc = iacore_set_chip_sleep(iacore, IA_FW_RETENTION);
			if (rc) {
				pr_err("Something wrong. Abandoning %d\n", rc);
				return rc;
			}
		}
		msleep(70);
		iacore->fw_state = FW_SLEEP;
		iacore_enable_irq(iacore);//enable irq only
	}

	return rc;
}

/* NOTE: This function must be called with access_lock acquired */
int iacore_recover_chip_to_fw_sleep_unlocked(struct iacore_priv *iacore)
{
	int is_btldr;
	int rc = -1;
	unsigned int current_state = 0;

	lockdep_assert_held(&iacore->access_lock);

	pr_info("enter!\n");
	current_state = iacore_get_power_state(iacore);
	if (current_state == POWER_OFF) {
		rc = setup_chip_unlocked(iacore, true, false, false);
		if (rc < 0)
			return rc;
	}

	pr_err("firmware is loaded in %s mode",
			 iacore_fw_state_str(iacore->fw_state));

	/* check if in Bootloader or FW */
	is_btldr = is_chip_in_bootloader(iacore);

	if (is_btldr < 0) {
		pr_err("chip not responding. needs power cycle %d", is_btldr);
		vs_reset_model_file(iacore);
		rc = setup_chip_unlocked(iacore, true, true, true);
		if (rc < 0)
			return rc;

		iacore_disable_irq_nosync(iacore);
	} else if (is_btldr == 1) {
		pr_info("Chip in Bootloader. Download fw & put chip to Sleep");
		pr_info("skip_boot_setup %d", iacore->skip_boot_setup);
		iacore->skip_boot_setup = true;
		vs_reset_model_file(iacore);
		rc = setup_chip_unlocked(iacore, false, true, true);
		if (rc < 0)
			return rc;

		iacore_disable_irq_nosync(iacore);
	} else if (is_btldr == 0) {
		pr_info("Chip is in Firmware mode. Set FW Sleep");

		rc = setup_chip_unlocked(iacore, false, false, true);
		if (rc < 0)
			return rc;
		iacore_disable_irq_nosync(iacore);
		vs_reset_model_file(iacore);
	}
	pr_info("rc %d, exit!\n", rc);

	return rc;
}

int iacore_chip_softreset(struct iacore_priv *iacore, bool put_to_sleep)
{
	bool is_fw_loaded;
	int rc = 0;
	int is_btldr;
	u32 cmd, rsp = 0;

	pr_debug("called");
	IACORE_MUTEX_LOCK(&iacore->access_lock);
	is_fw_loaded = iacore_fw_loaded_unlocked(iacore);
	if (!is_fw_loaded) {
		pr_err("Chip not in FW mode. Softreset not possible\n");
		rc = -EINVAL;
		goto err_ret;
	}

	/* wake up the chip (if sleeping) */
	rc = iacore_wakeup_unlocked(iacore);
	if (rc) {
		pr_err("wakeup failed rc = %d\n", rc);
		goto err_ret;
	}

	/* check if in Bootloader or FW */
	is_btldr = is_chip_in_bootloader(iacore);
	if (is_btldr < 0) {
		pr_err("chip not responding. Soft reset not possible %d",
			is_btldr);
		rc = -EIO;
		goto err_ret;

	} else if (is_btldr == 1) {
		pr_info("Chip in Bootloader Mode. Soft reset not required");
		rc = 0;
		goto err_ret;

	} else if (is_btldr == 0) {
		pr_info("Chip is in Firmware mode. Trying reset");

		/* 1st disable any interrupts */
		iacore_disable_irq_nosync(iacore);

		/* send reset command */
		cmd = IACORE_CHIP_SOFT_RESET_CMD;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Chip Soft Reset Cmd Fail %d\n", rc);
			goto err_ret;
		}

		usleep_range(IA_DELAY_30MS, IA_DELAY_30MS+5000);

		/* run interface detection callback (if exists) */
		if (iacore->boot_ops.interface_detect) {
			rc = iacore->boot_ops.interface_detect(iacore);
			if (rc < 0) {
				pr_err("Inetrface detect failed %d", rc);
				rc = -EIO;
				goto err_ret;
			}
#ifdef CONFIG_SND_SOC_IA_UART
			iacore->skip_boot_setup = false;
#else
			iacore->skip_boot_setup = true;
#endif

		} else {
			pr_debug("no interface_detect() callback");
		}

		/* reconfirm if chip is back in bootloader */
		is_btldr = is_chip_in_bootloader(iacore);
		if (is_btldr < 0) {
			pr_err("chip not responding. Soft reset failed %d",
				is_btldr);
			rc = -EIO;
			goto err_ret;

		} else if (is_btldr == 1) {
			pr_info("Chip in Bootloader Mode. Soft reset Success");
			rc = 0;

			iacore->fw_state = SBL;

			/* if sleep requested, go to sleep */
			if (put_to_sleep == true) {
				/* change fw state back to FW_SLEEP */
				pr_info("changing fw state back to FW_SLEEP");
				rc = iacore_change_state_unlocked(iacore, FW_SLEEP);
				if (rc)
					pr_err("goto FW sleep fail %d\n", rc);
				goto err_ret;
			}
		} else if (is_btldr == 0) {
			pr_info("Chip in Firmware Mode. Soft reset failed");
			rc = -EIO;
		}
	}

err_ret:
	pr_info("exit, rc %d", rc);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}

/* NOTE: This function must be called with access_lock acquired */
int iacore_change_state_unlocked(struct iacore_priv *iacore, u8 new_state)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
#ifdef CONFIG_SND_SOC_IA_UART
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
#endif
	int rc = 0;
	u32 cmd, rsp;
	u8 kw_prsrv;
	u8 preset;
	char *inval_state_trans = "Invalid state transition requested";
	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	lockdep_assert_held(&iacore->access_lock);

	if (iacore->fw_state != new_state)
		pr_info("power state transition from %s to %s",
			 iacore_fw_state_str(iacore->fw_state),
			 iacore_fw_state_str(new_state));

	//mutex_lock(&iacore->access_lock);

	if (new_state == POWER_OFF)
		iacore_poweroff_chip(iacore);

	if (new_state == SBL) {
		iacore_poweroff_chip(iacore);
		iacore_power_ctrl(iacore, IA_POWER_ON);
		iacore->fw_state = SBL;
	}
	//mutex_lock(&iacore->access_lock);

	if ((new_state == SW_BYPASS) ||
		(new_state == FW_HW_BYPASS) ||
		(new_state == SPL_PDM_RECORD) ||
		(new_state == VS_SLEEP) ||
		(new_state == BARGEIN_SLEEP)) {
		/*
		 * This flag will be used to notify the HAL layer about
		 *	- KW detection *and*
		 *	- Crash recovery
		 * Reset it before starting any process
		 */
		iacore->iacore_cv_kw_detected = false;
	}

	if (new_state == PROXY_MODE)
		iacore->fw_state = PROXY_MODE;

	/* Following are the possible valid power state transitions.
	 *
	 * POWER_OFF <-> SBL
	 *		 SBL <-> HW_BYPASS
	 *		 SBL <-> FW_LOADED
	 *			 FW_LOADED <-> FW_HW_BYPASS
	 *			 FW_LOADED <-> SPL_PDM_RECORD
	 *			 FW_LOADED <-> FW_SLEEP
	 *			 FW_LOADED <-> SW_BYPASS
	 *			 FW_LOADED <-> IO_STREAMING
	 *			 FW_LOADED <-> VS_MODE -> VS_SLEEP
	 *			 FW_LOADED <-> VS_MODE -> BARGEIN_SLEEP
	 *			 FW_LOADED <-> VS_MODE -> BARGEIN_SLEEP
	 *					<--> IO_BARGEIN_STREAMING
	 *
	 * VS_SLEEP --kw detect-> VS_MODE <-> VS_BURSTING
	 * BARGEIN_SLEEP --kw detect-> VS_MODE <-> BARGEIN_BURSTING
	 *
	 * Chip can move to POWER_OFF and SBL state from any of the state.
	 *
	 * Chip can move to PROXY_MODE from any state. But transition from this
	 * state is valid only to POWER_OFF or SBL.
	 */
	while (iacore->fw_state != new_state) {
		switch (iacore->fw_state) {
		case POWER_OFF:
			switch (new_state) {
			case FW_LOADED:
			case HW_BYPASS:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case VS_MODE:
			case VS_SLEEP:
			case BARGEIN_SLEEP:
			case FW_SLEEP:
			case DEEP_SLEEP:
			case IO_STREAMING:
				iacore_power_ctrl(iacore, IA_POWER_ON);
				iacore->fw_state = SBL;
				pr_err("fw_state = SBL\n");
				break;
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case SBL:
			switch (new_state) {
			case FW_SLEEP:
			case DEEP_SLEEP:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case VS_MODE:
			case VS_SLEEP:
			case BARGEIN_SLEEP:
			case FW_LOADED:
			case IO_STREAMING:
				pr_debug("iacore_change_state 4444");
				rc = iacore_load_firmware_unlocked(iacore);
				if (rc) {
					pr_err("FW load failed rc = %d\n", rc);
					//iacore_recover_chip_to_fw_sleep_unlocked(iacore);
					goto error;
				}
				iacore->fw_state = FW_LOADED;
				break;
			case HW_BYPASS:
				rc = iacore_set_bypass_unlocked(iacore);
				if (rc) {
					pr_err("failed to set chip to hw bypass mode rc = %d\n", rc);
					goto error;
				}
				iacore->fw_state = HW_BYPASS;
				break;
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case FW_LOADED:
			switch (new_state) {
			case FW_HW_BYPASS:
				/*
				 * This flag is used to nofify i2s framework that i2s close api
				 * is called after the disable Bypass mode (= 0). Reset
				 * it before starting the recording
				 */
				voice_sense->rec_to_bargein = false;

				rc = iacore_set_bypass_unlocked(iacore);
				if (rc) {
					pr_err("failed to set chip to sw bypass mode rc = %d\n", rc);
					goto error;
				}
				iacore->fw_state = FW_HW_BYPASS;
				break;
			case SW_BYPASS:
				/*
				 * This flag is used to nofify i2s framework that i2s close api
				 * is called after the disable Bypass mode (= 0). Reset
				 * it before starting the recording
				 */
				voice_sense->rec_to_bargein = false;

				iacore->fw_state = SW_BYPASS;
				break;
			case SPL_PDM_RECORD:
				/*
				 * if the transition to spl_pdm_record is from a sleep state,
				 * then stop route is not required
				 */
				if  (iacore->skip_stop_route_cmd == true) {
					iacore->skip_stop_route_cmd = false;
					pr_info("chip in sleep mode. So we can skip stop route\n");
				} else {
					iacore->active_route_set = true;

					rc = iacore_stop_active_route_unlocked(iacore);
					if (rc) {
						dev_err(iacore->dev, "%s(): Stop Route fail %d\n",
							__func__, rc);
						goto error;
					}
				}

				/* Send Preset & Confirm response */
				cmd = IA_SET_PRESET << 16 | PRESET_SPECIAL_PDM_48K; //set preset 7 to use 2.4M preset
				rc = iacore_cmd_nopm(iacore, cmd, &rsp);
				if (rc) {
					pr_err("Preset cmd (0x%08x) fail %d\n", cmd, rc);
					goto error;
				}

#ifdef CONFIG_SND_SOC_IA_UART
				if (IS_ERR_OR_NULL(iacore_uart->tty)) {
					pr_debug("tty is not available\n");
				} else {

					/* Wait till wakeup command is completely sent to the chip */
					tty_wait_until_sent(iacore_uart->tty,
						msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));
				}
#endif
				usleep_range(IA_DELAY_10MS, IA_DELAY_12MS);

				iacore->spl_pdm_mode = true;
				/* Set the power state to special pdm record mode */
				iacore->fw_state = SPL_PDM_RECORD;
				break;
			case VS_MODE:
			case VS_SLEEP:
			case BARGEIN_SLEEP:
				if (iacore_check_fw_reload(iacore) == true) {

					rc = iacore_set_chip_sleep(iacore,
									IA_DEEP_SLEEP);
					if (rc) {
						pr_err("Put Deep sleep fail %d\n", rc);
						goto error;
					}
					usleep_range(IA_DELAY_35MS,
								IA_DELAY_35MS + 5000);

					voice_sense->bargein_vq_enabled = false;
					vs_reset_model_file(iacore);
					iacore->fw_state = DEEP_SLEEP;

					/* wake up from deep sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = SBL;
					iacore->skip_boot_setup = true;

					pr_err("loading new fw");
					rc = iacore_load_firmware_unlocked(iacore);
					if (rc) {
						pr_err("FW load failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = FW_LOADED;
				}

				rc = iacore_vs_configure_unlocked(iacore);
				if (rc) {
					pr_err("failed to enable voice sense mode = %d\n", rc);
					goto error;
				}

				iacore->fw_state = VS_MODE;
				break;
			case FW_SLEEP:
#ifdef FW_SLEEP_TIMER_TEST
				rc = setup_fw_sleep_timer_unlocked(iacore, FW_SLEEP_TIMEOUT);
				if (rc) {
					pr_err("FW Sleep timer failed %d\n", rc);
					goto error;
				}

				/*
				 * Since we have enabled the FW sleep timer
				 * the new state will be updated from the timer
				 * function. To avoid the infinite loop here
				 * we will update the variable new_state to
				 * FW_LOADED.
				 */

				new_state = FW_LOADED;
			//	iacore_enable_irq(iacore);
#else
				rc = iacore_set_chip_sleep(iacore,
								IA_CHIP_SLEEP);
				if (rc) {
					pr_err("failed to put the chip to sleep = %d\n", rc);
					goto error;
				} else {
					usleep_range(IA_DELAY_50MS, IA_DELAY_50MS + 50);
				}
				vs_reset_model_file(iacore);
				iacore->fw_state = FW_SLEEP;
#endif
				break;
			case DEEP_SLEEP:
				rc = iacore_set_chip_sleep(iacore,
								IA_DEEP_SLEEP);
				if (rc) {
					pr_err("Put Deep sleep fail %d\n", rc);
					goto error;
				}
				usleep_range(IA_DELAY_35MS, IA_DELAY_35MS + 5000);

				vs_reset_model_file(iacore);
				voice_sense->bargein_vq_enabled = false;
				iacore->fw_state = DEEP_SLEEP;

				break;
			case IO_STREAMING:
				rc = iacore_set_iostreaming_unlocked(iacore,
						IACORE_IOSTREAM_ENABLE);
				if (rc) {
					pr_err("failed to start iostream = %d\n", rc);
					goto error;
				}
				iacore->fw_state = IO_STREAMING;
				break;
			case HW_BYPASS:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case HW_BYPASS:
			switch (new_state) {
			case SBL:
			case FW_LOADED:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case VS_MODE:
			case VS_SLEEP:
			case BARGEIN_SLEEP:
			case FW_SLEEP:
			case DEEP_SLEEP:
				/* wakeup the chip */
				rc = iacore_stop_bypass_unlocked(iacore);
				if (rc) {
					pr_err("failed to stop hw bypass = %d\n", rc);
					goto error;
				}
				iacore->fw_state = SBL;
				break;
			case SW_BYPASS:
			case IO_STREAMING:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case FW_HW_BYPASS:
			switch (new_state) {
			case BARGEIN_SLEEP:
				if (voice_sense->bargein_sts == true)
					voice_sense->rec_to_bargein = true;
			case FW_LOADED:
			case VS_MODE:
			case VS_SLEEP:
			case FW_SLEEP:
			case DEEP_SLEEP:
				rc = iacore_stop_bypass_unlocked(iacore);
				if (rc) {
					pr_err("failed to stop hw bypass = %d\n", rc);
					goto error;
				}
				iacore->fw_state = FW_LOADED;
				break;
			case HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case IO_STREAMING:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case SPL_PDM_RECORD:
			switch (new_state) {
			case FW_LOADED:
			case FW_SLEEP:
			case DEEP_SLEEP:
				//usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 100);
				usleep_range(IA_DELAY_20MS, IA_DELAY_25MS);

				rc = iacore_wakeup_unlocked(iacore);
				if (rc) {
					dev_err(iacore->dev, "%s() wakeup fail %d\n",
						__func__, rc);
					//iacore->spl_pdm_mode = 0;
					goto error;
				}

				iacore->spl_pdm_mode = 0;

				iacore->fw_state = FW_LOADED;
				break;

			case VS_SLEEP:
			case BARGEIN_SLEEP:
				pr_info("%s. current_state = %d, new_state %d, please try again later",
								inval_state_trans, iacore->fw_state, new_state);
				rc = -EAGAIN;
				break;

			case VS_MODE:
			case HW_BYPASS:
			case FW_HW_BYPASS:
			case SW_BYPASS:
			case IO_STREAMING:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case SW_BYPASS:
			switch (new_state) {
			case BARGEIN_SLEEP:
				if (voice_sense->bargein_sts == true)
					voice_sense->rec_to_bargein = true;
			case FW_LOADED:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case VS_MODE:
			case VS_SLEEP:
			case FW_SLEEP:
			case DEEP_SLEEP:
				rc = iacore_stop_active_route_unlocked(iacore);
				if (rc) {
					pr_err("failed to put the chip to sleep = %d\n", rc);
					goto error;
				}
				iacore->fw_state = FW_LOADED;
				break;
			case HW_BYPASS:
			case IO_STREAMING:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case IO_STREAMING:
			switch (new_state) {
			case FW_LOADED:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case VS_MODE:
			case VS_SLEEP:
			case BARGEIN_SLEEP:
			case FW_SLEEP:
			case DEEP_SLEEP:
				rc = iacore_set_iostreaming_unlocked(iacore,
						IACORE_IOSTREAM_DISABLE);
				if (rc) {
					pr_err("failed to stop iostreaming = %d\n", rc);
					goto error;
				}
				iacore->fw_state = FW_LOADED;
				break;
			case HW_BYPASS:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case IO_BARGEIN_STREAMING:
			switch (new_state) {
			case BARGEIN_SLEEP:
				rc = iacore_set_iostreaming_unlocked(iacore,
						IACORE_IOSTREAM_DISABLE);
				if (rc) {
					pr_err("failed to stop iostreaming = %d\n", rc);
					goto error;
				}

				rc = iacore_stop_active_route_unlocked(iacore);
				if (rc) {
					pr_err("stop active route fail %d\n", rc);
					goto error;
				}
				iacore->fw_state = FW_LOADED;
				break;
			case HW_BYPASS:
			case FW_LOADED:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case VS_MODE:
			case VS_SLEEP:
			case FW_SLEEP:
			case DEEP_SLEEP:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case VS_MODE:
			switch (new_state) {
			case BARGEIN_SLEEP:
				if (iacore_check_fw_reload(iacore) == true) {

					rc = iacore_set_chip_sleep(iacore,
									IA_DEEP_SLEEP);
					if (rc) {
						pr_err("Put Deep sleep fail %d\n", rc);
						goto error;
					}
					usleep_range(IA_DELAY_35MS,
								IA_DELAY_35MS + 5000);

					voice_sense->bargein_vq_enabled = false;
					vs_reset_model_file(iacore);
					iacore->fw_state = DEEP_SLEEP;

					/* wake up from deep sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = SBL;
					iacore->skip_boot_setup = true;

					pr_err("loading new fw");
					rc = iacore_load_firmware_unlocked(iacore);
					if (rc) {
						pr_err("FW load failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = FW_LOADED;
					break;
				}
				preset = voice_sense->params.vq_preset &
							PRESET_MASK;
				pr_err("iacore vq_preset = 0x%x", voice_sense->params.vq_preset);
				/*if preset is set by upper layer && send it */
				/* if (preset == PRESET_NONE) {
					preset = PRESET_VAD_OFF_VQ_NO_BUFFERING;
					voice_sense->params.vq_preset = preset;
					pr_err("iacore_change_state preset set to %d", preset);
				}*/
				iacore->active_route_set = true;
				rc = iacore_stop_active_route_unlocked(iacore);
				if (rc) {
					pr_err("Stop Route fail %d\n", rc);
					goto error;
				}
				cmd = IA_SET_PRESET << 16 | preset;
				rc = iacore_cmd_nopm(iacore, cmd, &rsp);
				if (rc) {
					pr_err("Set preset(%d) fail %d\n", preset, rc);
					goto error;
				}

				iacore->active_route_set = true;

				/* Enable VoiceQ */
				rc = iacore_set_bargein_vq_mode(iacore,
						IA_BARGEIN_VOICEQ_ENABLE);
				if (rc) {
					pr_err("Set VoiceQ IRQ fail %d\n", rc);
					goto error;
				}

#ifdef CONFIG_SND_SOC_IA_UART
				iacore_uart_clean_rx_fifo(iacore);
#endif

				if (preset != PRESET_NONE) {
					/* barge in doesn't support cvq, will only go into this case */
					iacore_enable_irq(iacore);
				}
				else {
					/*
					 * In case of Barge-in with KW
					 * preservation, HAL opens up pcm device
					 * immediately after requesting cvq
					 * start. This pcm open request sends
					 * few commands on uart line and may
					 * create unnecessary interrupts. To
					 * avoid, disable it and send it after
					 * pcm open in i2s module
					 */

					kw_prsrv = voice_sense->params.kw_preserve;
///test for ignore barge in cvq
					iacore_enable_irq(iacore);
					/* if only VQ, enable irq immediately */
				/*	if (voice_sense->params.mode == IA_VQ_MODE)
						iacore_enable_irq(iacore);
					else if (kw_prsrv != IA_PRESERVE_KW)
						iacore_enable_irq(iacore);*/
				}


				iacore->fw_state = BARGEIN_SLEEP;
				break;
			case VS_SLEEP:
				if (iacore_check_fw_reload(iacore) == true) {

					rc = iacore_set_chip_sleep(iacore,
									IA_DEEP_SLEEP);
					if (rc) {
						pr_err("Put Deep sleep fail %d\n", rc);
						goto error;
					}
					usleep_range(IA_DELAY_35MS,
								IA_DELAY_35MS + 5000);

					voice_sense->bargein_vq_enabled = false;
					vs_reset_model_file(iacore);
					iacore->fw_state = DEEP_SLEEP;

					/* wake up from deep sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = SBL;
					iacore->skip_boot_setup = true;

					pr_err("loading new fw");
					rc = iacore_load_firmware_unlocked(iacore);
					if (rc) {
						pr_err("FW load failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = FW_LOADED;
					break;
				}
				preset = voice_sense->params.vq_preset &
							PRESET_MASK;

				pr_debug("preset selected %d", preset);
				iacore->active_route_set = true;
				rc = iacore_stop_active_route_unlocked(iacore);
				if (rc) {
					pr_err("Stop Route fail %d\n", rc);
					goto error;
				}
				cmd = IA_SET_PRESET << 16 | preset;
				rc = iacore_cmd_nopm(iacore, cmd, &rsp);
				if (rc) {
					pr_err("Set preset(%d) fail %d\n", preset, rc);
					goto error;
				}

				iacore->active_route_set = true;

				pr_info("calling vs_sleep");
				rc = iacore_set_vs_sleep(iacore);
				if (rc) {
					pr_err("failed to put the chip to sleep = %d\n", rc);
					goto error;
				}

#ifdef CONFIG_SND_SOC_IA_UART
				iacore_uart_clean_rx_fifo(iacore);
#endif

				iacore_enable_irq(iacore);
				iacore->fw_state = VS_SLEEP;
				break;
			case FW_SLEEP:
				/*
				 * If barge-in is true, upon pcm_close, disable Barge-in
				 * Vq Detection.
				 */
				if (voice_sense->bargein_sts == true) {
					rc = iacore_set_bargein_vq_mode(iacore,
							IA_BARGEIN_VOICEQ_DISABLE);
					if (rc) {
						pr_err("BargeIn VQ Intr Disable fail %d\n", rc);
					}
				}

				/* fall back to barge-in off when cvq stop is called */
				voice_sense->bargein_sts = false;

				iacore->fw_state = FW_LOADED;
				break;

			case FW_LOADED:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case DEEP_SLEEP:
				rc = iacore_stop_active_route_unlocked(iacore);
				if (rc) {
					pr_err("failed to put the chip to sleep = %d\n", rc);
					goto error;
				}
				iacore->fw_state = FW_LOADED;
				break;

			case VS_BURSTING:
			case BARGEIN_BURSTING:
				/* Busting is possible only from VS mode */
				rc = ia6xx_set_streaming_burst_unlocked(iacore,
							 IACORE_STREAM_ENABLE);
				if (rc) {
					pr_err("failed to start streaming\n");
					rc = -EBUSY;
					goto error;
				}
				iacore->fw_state = new_state;
				break;
			case HW_BYPASS:
			case IO_STREAMING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;

		case VS_SLEEP:
			switch (new_state){
				case FW_LOADED:
				case FW_HW_BYPASS:
				case SPL_PDM_RECORD:
				case SW_BYPASS:
				case FW_SLEEP:
				case DEEP_SLEEP:

					/* wake up the chip from voice sense sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						iacore_enable_irq(iacore);
						goto error;
					}
					iacore->fw_state = VS_MODE;

					rc = iacore_stop_active_route_unlocked(iacore);
					if (rc)
						goto error;

					iacore->fw_state = FW_LOADED;
					break;

				case IO_STREAMING:
					/* wake up the chip from voice sense sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						//iacore_enable_irq(iacore);
						goto error;
					}
					iacore->fw_state = VS_MODE;

					rc = iacore_stop_active_route_unlocked(iacore);
					if (rc)
						goto error;

					iacore->fw_state = FW_LOADED;

					rc = iacore_set_iostreaming_unlocked(iacore,
							IACORE_IOSTREAM_ENABLE);
					if (rc) {
						pr_err("failed to start iostream = %d\n", rc);
						goto error;
					}
					iacore->fw_state = IO_STREAMING;
					break;

				case BARGEIN_SLEEP:
					/* wake up the chip from voice sense sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = VS_MODE;

					/*change betweed vs-sleep and barge-in need stop route */
					rc = iacore_stop_active_route_unlocked(iacore);
					if (rc) {
						pr_err("Stop Route fail %d\n", rc);
						goto error;
					}

					rc = iacore_set_chip_sleep(iacore,
									IA_DEEP_SLEEP);
					if (rc) {
						pr_err("Put Deep sleep fail %d\n", rc);
						goto error;
					}
					usleep_range(IA_DELAY_35MS,
								IA_DELAY_35MS + 5000);

					voice_sense->bargein_vq_enabled = false;
					vs_reset_model_file(iacore);
					iacore->fw_state = DEEP_SLEEP;
					break;
				case HW_BYPASS:
				case VS_MODE:
				case VS_BURSTING:
				case BARGEIN_BURSTING:
				default:
					pr_err("%s. current_state = %d, new_state %d",
						inval_state_trans, iacore->fw_state, new_state);
					rc = -EINVAL;
					break;
			}
			break;

		case BARGEIN_SLEEP:
			switch (new_state) {
				case FW_LOADED:
				case FW_HW_BYPASS:
				case SPL_PDM_RECORD:
				case SW_BYPASS:
				case FW_SLEEP:
				case DEEP_SLEEP:

					/* wake up the chip from barge-in sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						iacore_enable_irq(iacore);
						goto error;
					}

					/*
					 * If barge-in is true, upon pcm_close, disable Barge-in
					 * Vq Detection.
					 */
					if (voice_sense->bargein_sts == true) {
						rc = iacore_set_bargein_vq_mode(iacore,
								IA_BARGEIN_VOICEQ_DISABLE);
						if (rc) {
							pr_err("BargeIn VQ Intr Disable fail %d\n", rc);
							goto error;
						}

						/* fall back to barge-in off when cvq stop is called */
						voice_sense->bargein_sts = false;
					}

					rc = iacore_stop_active_route_unlocked(iacore);
					if (rc) {
						pr_err("Stop Route fail %d\n", rc);
						goto error;
					}

					iacore->fw_state = FW_LOADED;
					break;
				case IO_BARGEIN_STREAMING:
					rc = iacore_set_iostreaming_unlocked(iacore,
							IACORE_IOSTREAM_ENABLE);
					if (rc) {
						pr_err("failed to start iostream = %d\n", rc);
						goto error;
					}
					iacore->fw_state = IO_BARGEIN_STREAMING;
					break;
				case VS_SLEEP:
					/* wake up the chip from voice sense sleep */
					rc = iacore_wakeup_unlocked(iacore);
					if (rc) {
						pr_err("wakeup failed rc = %d\n", rc);
						goto error;
					}
					iacore->fw_state = VS_MODE;

					/*change betweed vs-sleep and barge-in need stop route */
					rc = iacore_stop_active_route_unlocked(iacore);
					if (rc) {
						pr_err("Stop Route fail %d\n", rc);
						goto error;
					}

					rc = iacore_set_chip_sleep(iacore,
									IA_DEEP_SLEEP);
					if (rc) {
						pr_err("Put Deep sleep fail %d\n", rc);
						goto error;
					}
					usleep_range(IA_DELAY_35MS,
								IA_DELAY_35MS + 5000);

					voice_sense->bargein_vq_enabled = false;
					vs_reset_model_file(iacore);
					iacore->fw_state = DEEP_SLEEP;
					break;
				case HW_BYPASS:
				case VS_MODE:
				case VS_BURSTING:
				case BARGEIN_BURSTING:
				default:
					pr_err("%s. current_state = %d, new_state %d",
						inval_state_trans, iacore->fw_state, new_state);
					rc = -EINVAL;
					break;
			}
			break;

		case VS_BURSTING:
		case BARGEIN_BURSTING:
			switch (new_state) {
				case FW_LOADED:
				case FW_HW_BYPASS:
				case SPL_PDM_RECORD:
				case SW_BYPASS:
				case VS_MODE:
				case FW_SLEEP:
				case DEEP_SLEEP:
					rc = ia6xx_set_streaming_burst_unlocked(iacore,
								IACORE_STREAM_DISABLE);
					if (rc) {
						pr_err("failed to turn off streaming: %d\n", rc);
						rc = -EIO;
						goto error;
					}
					iacore->fw_state = VS_MODE;
					break;
				case VS_SLEEP:
				case BARGEIN_SLEEP:
				case HW_BYPASS:
				case IO_STREAMING:
				default:
					pr_err("%s. current_state = %d, new_state %d",
						inval_state_trans, iacore->fw_state, new_state);
					rc = -EINVAL;
					break;
			}
			break;

		case FW_SLEEP:
			switch (new_state) {
			case FW_LOADED:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case VS_MODE:
			case VS_SLEEP:
			case BARGEIN_SLEEP:
			case IO_STREAMING:
			case DEEP_SLEEP:
				/* wake up the chip */
				rc = iacore_wakeup_unlocked(iacore);
				if (rc) {
					pr_err("wakeup failed rc = %d\n", rc);
					goto error;
				}
				pr_info("FW_LOAD\n");
				iacore->fw_state = FW_LOADED;
				break;
			case HW_BYPASS:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
				break;
			}
			break;
		case DEEP_SLEEP:
			switch (new_state) {
			case FW_LOADED:
			case FW_HW_BYPASS:
			case SPL_PDM_RECORD:
			case SW_BYPASS:
			case VS_MODE:
			case VS_SLEEP:
			case BARGEIN_SLEEP:
			case IO_STREAMING:
				pr_info("deep sleep exit");
				/* wake up the chip */
				rc = iacore_wakeup_unlocked(iacore);
				if (rc) {
					pr_err("wakeup failed rc = %d\n", rc);
					goto error;
				}
				iacore->fw_state = SBL;
				iacore->skip_boot_setup = true;

				break;
			case HW_BYPASS:
			case VS_BURSTING:
			case BARGEIN_BURSTING:
			default:
				pr_err("%s. current_state = %d, new_state %d",
					inval_state_trans, iacore->fw_state, new_state);
				rc = -EINVAL;
			}
			break;
		default:
			pr_err("current mode (%d - %s) is invalid\n", iacore->fw_state,
				iacore_fw_state_str(iacore->fw_state));

			rc = -EINVAL;
			break;
		}

		if (rc) {
			pr_err("Error changing power state:%d from %d to %d",
				rc, iacore->fw_state, new_state);
			break;
		}
	}
error:
	//mutex_unlock(&iacore->access_lock);
	pr_info("exit. firmware is loaded in %s mode",
		 iacore_fw_state_str(iacore->fw_state));

	return rc;
}

int iacore_change_state_lock_safe(struct iacore_priv *iacore, u8 new_state){
	int ret = 0;
	IACORE_MUTEX_LOCK(&iacore->access_lock);
	ret = iacore_change_state_unlocked(iacore, new_state);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return ret;
}
