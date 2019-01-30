/*
 * ia6xx_iacore.c  --  Audience ALSA SoC Audio driver
 *
 * Copyright 2011 Audience, Inc.
 *
 * Author: Greg Clemson <gclemson@audience.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "iaxxx.h"

#include <linux/tty.h>
#include "ia6xx-iacore.h"
#include "iacore-cdev.h"
#include "iacore-vs.h"
#include "iacore-uart.h"
#include "iacore-uart-common.h"

#include "ia6xx-access.h"

#define SIZE_OF_VERBUF 256

extern int msm_geni_iacore_uart_pinctrl_enable(int enable);


struct iacore_priv *iacore_global = NULL;

static ssize_t ia6xx_fw_version_show(struct iacore_priv *iacore,
				     struct iacore_sysfs_attr *attr,
				     char *buf)
{
	int idx = 0;
	unsigned int value;
	char versionbuffer[SIZE_OF_VERBUF];
	char *verbuf = versionbuffer;

	memset(verbuf, 0, SIZE_OF_VERBUF);

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	value = iacore_read(iacore, IA6XX_FW_FIRST_CHAR);
	*verbuf++ = (value & 0x00ff);
	for (idx = 0; idx < (SIZE_OF_VERBUF-2); idx++) {
		value = iacore_read(iacore, IA6XX_FW_NEXT_CHAR);
		*verbuf++ = (value & 0x00ff);
		if (!value)
			break;
	}
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	/* Null terminate the string*/
	*verbuf = '\0';
	pr_info("Audience fw ver %s\n", versionbuffer);
	return snprintf(buf, PAGE_SIZE, "FW Version = %s\n", versionbuffer);
}

static IACORE_ATTR(fw_version, 0444, ia6xx_fw_version_show, NULL);

static ssize_t iacore_vs_status_show(struct iacore_priv *iacore,
					struct iacore_sysfs_attr *attr,
					char *buf)
{
	unsigned int value = 0;
	char *status_name = "Voice Sense Status";

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	value = iacore->iacore_event_type;
	/* Reset the detection status after read */
	iacore->iacore_event_type = IA_NO_EVENT;

	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return snprintf(buf, PAGE_SIZE, "%s=0x%04x\n", status_name, value);
}

static IACORE_ATTR(vs_status, 0444, iacore_vs_status_show, NULL);

static ssize_t iacore_debug_show(struct iacore_priv *iacore,
				     struct iacore_sysfs_attr *attr,
				     char *buf)
{
	int ret;

	if (iacore->debug_buff != NULL) {
		memcpy(buf, iacore->debug_buff, DBG_BLK_SIZE);
		kfree(iacore->debug_buff);
		iacore->debug_buff = NULL;
		return DBG_BLK_SIZE;
	}

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	ret = iacore_pm_get_sync(iacore);
	if (ret < 0) {
		pr_err("pm_get_sync failed :%d\n", ret);
		goto ret_out;
	}

	ret = read_debug_data(iacore, buf);
	iacore_pm_put_autosuspend(iacore);

ret_out:
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return ret;
}
static IACORE_ATTR(iacore_debug, 0444, iacore_debug_show, NULL);

static ssize_t iacore_dbg_len_show(struct iacore_priv *iacore,
				     struct iacore_sysfs_attr *attr,
				     char *buf)
{
	pr_info("rdb_buf_len=%ld\n", (long int)iacore->rdb_buf_len);
	return snprintf(buf, PAGE_SIZE, "rdb_buf_len=%ld\n",
			(long int)iacore->rdb_buf_len);
}
static IACORE_ATTR(iacore_debug_len, 0444, iacore_dbg_len_show, NULL);

static ssize_t iacore_state_show(struct iacore_priv *iacore,
				     struct iacore_sysfs_attr *attr,
				     char *buf)
{
	unsigned int value = 0;

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	value = iacore_get_power_state(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return snprintf(buf, PAGE_SIZE, "%s\n", iacore_fw_state_str(value));
}

static IACORE_ATTR(ia_state, 0444, iacore_state_show, NULL);

static ssize_t iacore_bus_config_show(struct iacore_priv *iacore_priv,
				     struct iacore_sysfs_attr *attr,
				     char *buf)
{
#if defined(CONFIG_SND_SOC_IA_I2C)
	sprintf(buf, "I2C");
#elif defined(CONFIG_SND_SOC_IA_UART)
	sprintf(buf, "UART");
#elif defined(CONFIG_SND_SOC_IA_SPI)
	sprintf(buf, "SPI");
#elif defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
	sprintf(buf, "SOUNDWIRE");
#endif

#if defined(CONFIG_SND_SOC_IA_I2S)
	sprintf(buf, "%s + I2S", buf);
#endif

	return snprintf(buf, PAGE_SIZE, "%s\n", buf);
}

static IACORE_ATTR(iacore_bus_config, 0444, iacore_bus_config_show, NULL);

static ssize_t iacore_chip_soft_reset(struct iacore_priv *iacore,
					struct iacore_sysfs_attr *attr,
					const char *buf, size_t count)
{
	int ret = 0;

	if (count > 0) {
		switch (buf[0]) {
		case '0':
			pr_info("No soft reset\n");
			break;
		case '1':
			ret = iacore_chip_softreset(iacore, true);
			if (ret < 0)
				count = ret;

			break;
		default:
			pr_err("Invalid request: %s\n", buf);
		}
	}

	return count;
}

static IACORE_ATTR(chip_softreset, 0644, NULL, iacore_chip_soft_reset);

static ssize_t iacore_enable_debug_fw(struct iacore_priv *iacore,
					struct iacore_sysfs_attr *attr,
					const char *buf, size_t count)
{
	if (count > 0) {
		switch (buf[0]) {
		case '0':
			pr_info("Debug FW disabled\n");
			iacore->dbg_fw = false;
			break;
		case '1':
			pr_info("Debug FW enabled\n");
			iacore->dbg_fw = true;
			break;
		default:
			pr_err("Invalid request: %s\n", buf);
		}
	}

	return count;
}

static IACORE_ATTR(enable_debug_fw, 0644, NULL, iacore_enable_debug_fw);
static struct attribute *core_sysfs_attrs[] = {
	&attr_vs_status.attr,
	&attr_fw_version.attr,
	&attr_ia_state.attr,
	&attr_iacore_debug.attr,
	&attr_iacore_debug_len.attr,
	&attr_iacore_bus_config.attr,
	&attr_chip_softreset.attr,
	&attr_enable_debug_fw.attr,
	NULL
};

int ia6xx_set_streaming_burst_unlocked(struct iacore_priv *iacore, int value)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
#ifdef CONFIG_SND_SOC_IA_UART
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
#endif
	u32 resp = 0, cmd, cmd_c2b;
	int rc, resp_req;

	/* retry reason is explained below */
	int retry;

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	if (value == IACORE_STREAM_ENABLE) {

		rc = iacore_datablock_open(iacore);
		if (rc) {
			pr_err("open failed %d\n", rc);
			goto err_exit;
		}

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
		rc = iacore_i2sperf_bursting_prepare(iacore);
		if (rc < 0) {
			pr_err("I2S perf bursting failed\n");
			return -EIO;
		}
#endif

		if (voice_sense->bargein_sts != true) {
			rc = iacore_cmd_nopm(iacore,
				IACORE_SET_START_STREAM_BURST_CMD | value,
				&resp);
		} else {
			/* stop any active route Route */
			rc = iacore_stop_active_route_unlocked(iacore);
			if (rc)
				goto close_exit;

			/* Disable VoiceQ */
			rc = iacore_set_bargein_vq_mode(iacore,
						IA_BARGEIN_VOICEQ_DISABLE);
			if (rc) {
				pr_err("Disable BargeIn VoiceQ fail %d\n", rc);
				goto close_exit;
			}

			cmd = IA_SET_SAMPLE_RATE << 16 | 0x1;
			rc = iacore_cmd_nopm(iacore, cmd, &resp);
			if (rc) {
				pr_err("Disable BargeIn VoiceQ fail %d\n", rc);
				goto close_exit;
			}

			/* set IA6xx port clock frequency again for barge-in */
			cmd = IA_SET_AUD_PORT_CLK_FREQ << 16 |
							IA_48KHZ_16BIT_2CH;
			rc = iacore_cmd_nopm(iacore, cmd, &resp);
			if (rc) {
				pr_err("I2S aud port clk freq cmd fail %d\n", rc);
				goto close_exit;
			}

			cmd = IA_SET_FRAME_SIZE << 16 | 0x10;
			rc = iacore_cmd_nopm(iacore, cmd, &resp);
			if (rc) {
				pr_err("Set frame size failed %d\n", rc);
				goto close_exit;
			}

			cmd = ((IA_CONFIG_DATA_PORT << 16) |
							(IA_CONFIG_48k << 12));
			rc = iacore_cmd_nopm(iacore, cmd, &resp);
			if (rc) {
				pr_err("I2S Config Data port cmd fail %d\n", rc);
				goto close_exit;
			}

			usleep_range(IA_DELAY_20MS, IA_DELAY_20MS + 100);

			rc = iacore_set_active_route(iacore, false,
						IA_2CH_IN_1CH_IOSTREAM_BARGEIN);
			if (rc) {
				pr_err("Set route failed %d\n", rc);
				goto close_exit;
			}

			usleep_range(IA_DELAY_20MS, IA_DELAY_20MS + 100);

			cmd = IA_SELECT_STREAMING << 16 |
				(IA_BAF_MGR_TX0_ENDPOINT |
						(IACORE_IOSTREAM_ENABLE << 15));
			rc = iacore_cmd_nopm(iacore, cmd, &resp);
			if (rc) {
				pr_err("Set stream endpoint(%d) fail %d\n",
					IA_BAF_MGR_TX0_ENDPOINT, rc);
				goto close_exit;
			}

			cmd = IA_SET_IOSTREAMING << 16 | IACORE_IOSTREAM_ENABLE;
			rc = iacore_cmd_nopm(iacore, cmd, &resp);
			if (rc) {
				pr_err("iostream enable failed %d\n", rc);
				goto close_exit;
			}
		}
	} else {
		/* On sending stop streaming command during the streaming,
		 * The response of the command will be consumed by the
		 * streaming_producer thread, so using iacore_cmd_nopm will
		 * trigger the firmware recovery if the response is not
		 * received */
		if (voice_sense->bargein_sts != true)
			cmd = IACORE_SET_STOP_STREAM_BURST_CMD | value;
		else
			cmd = (IA_STOP_IOSTREAM_BURST_CMD << 16) | value;

		resp_req = !(cmd & BIT(28));
		cmd_c2b = iacore->bus.ops.cpu_to_bus(iacore, cmd);

#ifdef CONFIG_SND_SOC_IA_UART
		/* To remove the extra zero byte, prefixed in the command
		 * response, we close the UART bus and then open and clear the
		 * fifo. In this scenario while stream closing, the UART bus
		 * remains closed and a direct write to it will fail.
		 *
		 * Below fix makes sure that UART bus is open for communication
		 * before writing the command directly to the chip
		 */
		rc = iacore_datablock_open(iacore);
		if (rc < 0) {
			pr_err("UART open Failed %d\n", rc);
			return -EIO;
		}
#endif

		rc = iacore->bus.ops.write(iacore, &cmd_c2b, sizeof(cmd_c2b));

		/* TODO Fix this sleep */
		usleep_range(IA_DELAY_10MS, IA_DELAY_15MS);

		streamdev_stop_and_clean_thread(iacore);

		/* Need to read all stream data from IA6xx to clean up the
		 * buffers. Firmware Tx buffer depends on various factors and
		 * the max number of 32 bit words in Tx buffer could be FIFO
		 * depth(16) + buffer size(32) = 48 words or 192 bytes.
		 */
		retry = IA_STOP_STRM_CMD_RETRY;

		if (resp_req) {
			do {
				rc = iacore->bus.ops.read(iacore, &resp,
								sizeof(resp));
				if (rc) {
					pr_err("resp read fail err %d\n", rc);
				}

				resp = iacore->bus.ops.bus_to_cpu(iacore, resp);
				if (resp == cmd)
					break;
				usleep_range(IA_DELAY_2MS, IA_DELAY_2MS + 5);
			} while (--retry);

			update_cmd_history(
				iacore->bus.ops.cpu_to_bus(iacore, cmd), resp);

#ifdef CONFIG_SND_SOC_IA_UART
			iacore_datablock_close(iacore);
#endif

			if (retry <= 0) {
				pr_err("stop streaming resp retry fail\n");
				IACORE_FW_RECOVERY_FORCED_OFF(iacore);
				return -EIO;
			}

		} else {
#ifdef CONFIG_SND_SOC_IA_UART
			if (IS_ERR_OR_NULL(iacore_uart->tty)) {
				pr_err("tty is not available\n");
				//return -EINVAL;
			} else {
				/* Wait till cmd is completely sent to chip */
				tty_wait_until_sent(iacore_uart->tty,
					msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));
			}

			update_cmd_history(cmd, resp);

			/*
			 * Close UART bus to stop the UART FIFO from getting
			 * overflowed.
			 */
			iacore_datablock_close(iacore);
#else
			update_cmd_history(
				iacore->bus.ops.cpu_to_bus(iacore, cmd), resp);
#endif
		}

		if (voice_sense->bargein_sts == true) {
			cmd = IA_SELECT_STREAMING << 16 |
				(IA_BAF_MGR_TX0_ENDPOINT |
				(IACORE_IOSTREAM_DISABLE << 15));
			rc = iacore_cmd_nopm(iacore, cmd, &resp);
			if (rc) {
				pr_err("endpoint (%d) disable fail %d\n",
					IA_BAF_MGR_TX0_ENDPOINT, rc);
				return rc;
			}
		}

		/* Send SYNC command */
#ifdef CONFIG_SND_SOC_IA_UART

		/* To remove the extra zero byte, prefixed in the
		 * command response, we close the UART bus and then open
		 * and clear the fifo. In this scenario while stream
		 * closing, the UART bus remains closed and a direct
		 * write to it will fail.
		 *
		 * Below fix makes sure that UART bus is open for
		 * communication before writing the command directly to
		 * the chip.
		 */
		rc = iacore_datablock_open(iacore);
		if (rc < 0) {
			pr_err("UART open Failed %d\n", rc);
			return -EIO;
		}

		rc = iacore_uart_get_sync_response(iacore);
		if (rc) {
			pr_err("Fail to send SYNC cmd, error %d\n", rc);
			goto close_exit;
		}
#else
		rc = iacore_cmd_nopm(iacore,
			(IA_SYNC_CMD << 16) | IA_SYNC_POLLING, &resp);
		if (rc) {
			pr_err("Fail to send SYNC cmd, error %d\n", rc);
			goto close_exit;
		}
#endif

		if (voice_sense->bargein_sts != true) {
			/* Send CVQ Buffer Overflow check command */
			rc = iacore_cmd_nopm(iacore,
				(IA_CVQ_BUFF_OVERFLOW << 16), &resp);
			if (rc) {
				pr_err("CVQ Buffer Overflow cmd fail %d\n", rc);
			} else {
				if (resp & CVQ_BUFF_OVERFLOW_OCCURRED) {
					pr_err("CVQ Buff Overflow 0x%08x\n", resp);
					rc = -EINVAL;
					goto close_exit;
				}
			}
		}

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
		iacore_i2sperf_bursting_done(iacore);
#endif
	}
close_exit:
#ifdef CONFIG_SND_SOC_IA_UART
	iacore_datablock_close(iacore);
#endif
err_exit:
	return rc;
}

int iacore_i2s_route_config_unlocked(struct iacore_priv *iacore, int value)
{
	int rc = 0;
	u32 cmd, rsp = 0;
	u32 route = IA_1CH_PDM_IN0_PCM_OUT0;
	bool ll_route = false;

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("uart_open failed\n");
		return rc;
	}
	switch (value) {
	case I2S_SLAVE_16K_16B:
	case I2S_MASTER_16K_16B:
		pr_err("iacore I2S_MASTER_16K_16B ");
		cmd = IA_SET_FRAME_SIZE << 16 | IA_FRAME_SIZE_10MS;
		/* set IA6xx sample frame size */
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S BYPASS mode Set frame size cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

	case LL_I2S_SLAVE_16K_16B:
	case LL_I2S_MASTER_16K_16B:
		pr_err("iacore LL_I2S_MASTER_16K_16B ");
		/* set IA6xx sampling rate req */
		cmd = IA_SET_SAMPLE_RATE << 16 | IA_SAMPLE_RATE_16KHZ;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S BYPASS mode Set sample rate cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}

		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
		/* set IA6xx port clock frequency */
		cmd = IA_SET_AUD_PORT_CLK_FREQ << 16 | IA_16KHZ_16BIT_2CH;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S BYPASS mode aud port clk freq cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}
		break;

	case I2S_SLAVE_48K_16B_BARGEIN:
		pr_err("iacore I2S_SLAVE_48K_16B_BARGEIN\n");
		/* Barge in - Frame Size = 16ms, 16KHz, 16bit */
		/* set IA6xx sample frame size */
		cmd = IA_SET_FRAME_SIZE << 16 | IA_FRAME_SIZE_16MS;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S Set frame size cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}

		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

		/* set IA6xx sampling rate req */
		cmd = IA_SET_SAMPLE_RATE << 16 | IA_SAMPLE_RATE_16KHZ;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S Set sample rate cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}

		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

		/* set IA6xx port clock frequency */
		cmd = IA_SET_AUD_PORT_CLK_FREQ << 16 | IA_48KHZ_16BIT_2CH;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S aud port clk freq cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}

		cmd = ((IA_CONFIG_DATA_PORT << 16) | (IA_CONFIG_48k << 12));
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S Config Data port cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}
		break;

	case I2S_SLAVE_48K_32B_BARGEIN:
		pr_err("iacore I2S_SLAVE_48K_32B_BARGEIN\n");
		iacore->active_route_set = true;

		rc = iacore_stop_active_route_unlocked(iacore);
		if (rc) {
			pr_err("Stop Route fail %d\n", rc);
			goto i2s_route_config_err;
		}

		cmd = IA_SET_PRESET << 16 | PRESET_VAD_OFF_CVQ_KW_PRSRV;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set I2S_SLAVE_48K_32B_BARGEIN fail %d\n", rc);
			goto i2s_route_config_err;
		}
		iacore->active_route_set = true;
		iacore->low_latency_route_set = false;
		goto close_out;

	case I2S_SLAVE_48K_16B:
	case I2S_MASTER_48K_16B:
		cmd = IA_SET_FRAME_SIZE << 16 | IA_FRAME_SIZE_2MS;
		/* set IA6xx sample frame size */
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S BYPASS mode Set frame size cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}

		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

	case LL_I2S_MASTER_48K_16B:
		cmd = IA_SET_SAMPLE_RATE << 16 | IA_SAMPLE_RATE_48KHZ;
		/* set IA6xx sampling rate req */
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S BYPASS mode Set sample rate cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}

		/* set IA6xx Internal Mic Clock to 1.536 MHz */
		cmd = IA_SET_DEVICE_PARAMID << 16 | 0x1002;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("set param command failed - %d\n", rc);
			goto i2s_route_config_err;
		}

		cmd = IA_SET_DEVICE_PARAM << 16 | 0x0003;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("set param value failed - %d\n", rc);
			goto i2s_route_config_err;
		}

		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
		/* set IA6xx port clock frequency */
		cmd = IA_SET_AUD_PORT_CLK_FREQ << 16 | IA_48KHZ_16BIT_2CH;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S BYPASS mode aud port clk freq cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}
		cmd = IA_SET_DIGI_GAIN << 16 | IA_10DB_DIGI_GAIN;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set digital gain fail %d\n", rc);
			goto i2s_route_config_err;
		}
		break;

	case LL_I2S_SLAVE_48K_16B:
	case LL_I2S_SLAVE_48K_32B:
		iacore->active_route_set = true;

		rc = iacore_stop_active_route_unlocked(iacore);
		if (rc) {
			pr_err("Stop Route fail %d\n", rc);
			goto i2s_route_config_err;
		}

		cmd = IA_SET_PRESET_NOREP << 16 | PRESET_I2S_RECORDING_48K;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("set LL_I2S_SLAVE_48K_32B record cmd fail %d\n", rc);
			goto i2s_route_config_err;
		}
		iacore->low_latency_route_set = true;
		iacore->active_route_set = true;
		goto close_out;

	default:
		pr_err("invalid i2s mode (%d) request\n", value);
		rc = -EINVAL;
		goto i2s_route_config_err;

	}

	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

	switch (value) {
	case I2S_SLAVE_16K_16B:
	case I2S_MASTER_16K_16B:
	case I2S_SLAVE_48K_16B:
	case I2S_MASTER_48K_16B:
		route = IA_1CH_PDM_IN0_PCM_OUT0;
		ll_route = false;
		break;

	case I2S_SLAVE_48K_16B_BARGEIN:
		route = IA_2CH_IN_1CH_IOSTREAM_BARGEIN;
		ll_route = false;
		break;

	case LL_I2S_SLAVE_16K_16B:
	case LL_I2S_MASTER_16K_16B:
	case LL_I2S_SLAVE_48K_16B:
	case LL_I2S_MASTER_48K_16B:
		route = IA_1CH_PDM_IN0_PCM_OUT0_LL;
		ll_route = true;
		iacore->low_latency_route_set = true;
		break;
	}

	/* start PDM IN PCM OUT pass through route */
	rc = iacore_set_active_route(iacore, ll_route, route);
	if (rc)
		pr_err("I2S BYPASS mode Set audio route cmd fail %d\n", rc);

i2s_route_config_err:
close_out:
	iacore_datablock_close(iacore);
	return rc;
}

int iacore_i2s_master_config(struct iacore_priv *iacore)
{
	int rc = 0;
	u32 cmd, rsp = 0;

	cmd = IA6XX_SET_DEV_PARAM_ID << 16 | IA6XX_I2S_AUD_DATA_PDM_TO_PCM;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("I2S BYPASS Set audio pdm to pcm cmd fail %d\n", rc);
		goto i2s_master_slave_config_err;
	}
	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
	cmd = IA6XX_SET_DEV_PARAM << 16 | IA6XX_I2S_AUD_DATA_MASTER;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc)
		pr_err("I2S BYPASS Set i2s_master cmd fail %d\n", rc);
	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
i2s_master_slave_config_err:
	return rc;
}

int iacore_i2s_rate_channel_config(struct iacore_priv *iacore, int value)
{
	int rc = 0;
	u32 cmd, rsp = 0;

	cmd = IA6XX_SET_DEV_PARAM_ID << 16 | IA6XX_I2S_MASTER_MODE_HIGH;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("I2S BYPASS Set i2s mode high cmd fail %d\n", rc);
		goto i2s_rate_config_err;
	}
	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

	switch (value) {
	case I2S_MASTER_16K_16B:
	case LL_I2S_MASTER_16K_16B:
		cmd = IA6XX_SET_DEV_PARAM << 16 | IA6XX_I2SM_16K_16B_2CH;
		break;
	case I2S_MASTER_48K_16B:
	case LL_I2S_MASTER_48K_16B:
		cmd = IA6XX_SET_DEV_PARAM << 16 | IA6XX_I2SM_48K_16B_2CH;
		break;
	default:
		goto i2s_rate_config_err;

	}
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc)
		pr_err("I2S BYPASS Set i2s channel & rate cmd fail %d\n", rc);

i2s_rate_config_err:
	return rc;
}

bool iacore_check_fw_reload(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	bool change_required = true;

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return false;
	}

	pr_info("bargein_sts %d, iacore->fw_type %d\n",
			voice_sense->bargein_sts, iacore->fw_type);

	/*
	 * FW change is only required iff
	 *	bargein_sts == true, & fw_type != IA_BARGEIN_MODE
	 *		*or*
	 *	bargein_sts == false, & fw_type == IA_BARGEIN_MODE
	 *
	 * In other cases, we can assume right firmware is loaded & return;
	 */
	if ((voice_sense->bargein_sts == true) &&
					(iacore->fw_type == IA_BARGEIN_MODE))
		change_required = false;
	else if ((voice_sense->bargein_sts == false) &&
					(iacore->fw_type == IA_CVQ_MODE))
		change_required = false;

	pr_info("change_required %d\n", change_required);

	return change_required;
}

int iacore_reload_fw_unlocked(struct iacore_priv *iacore, u32 new_fw_state)
{
	int rc;
	u32 fw_state;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	pr_info("enter\n");

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return false;
	}

	lockdep_assert_held(&iacore->access_lock);

	/* make sure the firmwares are available before switching */
	if (new_fw_state == VS_SLEEP) {
		if (!voice_sense->vs || !voice_sense->hw) {
			pr_err("VoiceQ fw not ready\n");
			rc = -EINVAL;
			return rc;
		}
	} else if (new_fw_state == BARGEIN_SLEEP) {
		if (!voice_sense->bargein_sysconfig || !voice_sense->bargein_hw) {
			pr_err("barge in fw not ready\n");
			voice_sense->bargein_sts = false;
			rc = -EINVAL;
			return rc;
		}
	}

	fw_state = iacore_get_power_state(iacore);

	/* if chip is not in deep sleep or sbl, move the chip to deep sleep */
	if (fw_state != DEEP_SLEEP && fw_state != SBL) {
		rc = iacore_change_state_unlocked(iacore, DEEP_SLEEP);
		if (rc) {
			pr_err("FW State change to Deep Sleep Fail : %d", rc);
			return rc;
		}
	} else {
		/* if in deep sleep, skip boot setup api */
		iacore->skip_boot_setup = true;
	}

	rc = iacore_change_state_unlocked(iacore, new_fw_state);
	if (rc < 0) {
		pr_err("firmware state change to (%d) fail %d\n", new_fw_state, rc);
	}

	pr_info("leave\n");

	return rc;
}

int iacore_set_bargein_vq_mode(struct iacore_priv *iacore, u32 enable)
{
	int rc = 0;
	u32 request, command, rsp = 0;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	request = !!enable;
	if (!voice_sense->bargein_sysconfig || !voice_sense->bargein_hw) {
		pr_err("barge in fw not ready\n");
		request = IA_BARGEIN_VOICEQ_DISABLE;
	}
	if (request == voice_sense->bargein_vq_enabled) {
		pr_err("no change in barge-in vq status (%d)\n", request);
		return rc;
	}

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("uart_open failed\n");
		return rc;
	}

	/* Disable VoiceQ */
	command = IA_SET_ALGO_PARAM_ID << 16 | IA_BARGEIN_VOICEQ;
	rc = iacore_cmd_nopm(iacore, command, &rsp);
	if (rc) {
		pr_err("Barge-In VoiceQ cmd send fail %d\n", rc);
		goto ret_err;
	}

	command = IA_SET_ALGO_PARAM << 16 | (request & 0xffff);
	rc = iacore_cmd_nopm(iacore, command, &rsp);
	if (rc) {
		pr_err("%s VoiceQ in Barge-In fail %d\n",
			(request ? "enable" : "disable"), rc);
	} else {
		voice_sense->bargein_vq_enabled = request;
	}

#ifdef CONFIG_SND_SOC_IA_UART
	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 10);
#else
	usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 10);
#endif

ret_err:
	iacore_datablock_close(iacore);
	return rc;
}

#if defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
static int iacore_route_config(struct iacore_priv *iacore,
					enum iacore_audio_routes route)
{
	int rc;
	u32 cmd, rsp = 0;

	if (route == IA_ROUTE_SELECTED_NONE)
		return -EINVAL;

	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
	cmd = IA_SET_FRAME_SIZE << 16 | IA_FRAME_SIZE_2MS;

	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set frame size cmd fail %d\n", rc);
		goto route_config_err;
	}

	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
	cmd = IA_SET_SAMPLE_RATE << 16 | IA_SAMPLE_RATE_16KHZ;
	/* set IA6xx sampling rate req */
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set sample rate cmd fail %d\n", rc);
		goto route_config_err;
	}

	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
	rc = iacore_set_active_route(iacore, false, route);
	if (rc)
		pr_err("Set audio route cmd fail %d\n", rc);

route_config_err:
	return rc;
}
#endif /* CONFIG_SND_SOC_IA_SOUNDWIRE */

static const unsigned int ia6xx_bypass_mode_values[] = {
	IA_BYPASS_OFF, IA_BYPASS_ON, IA_BYPASS_SOUNDWIRE_DHWPT,
};

static int iacore_put_bypass_mode(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	unsigned int value;
	int rc = 0;

	if (!voice_sense) {
		pr_info("invalid private pointer\n");
		return -EINVAL;
	}

	value = ucontrol->value.integer.value[0];

	pr_info("value = %d\n", value);

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	if (iacore_check_and_reload_fw_unlocked(iacore)) {
		rc = -EINVAL;
		goto unlock_exit;
	}

	if (iacore->bypass_mode == value) {
		pr_info("not changing bypass mode (%d)\n", value);
		goto unlock_exit;
	}
#ifdef FW_SLEEP_TIMER_TEST
	/* When enabling by pass mode,
	 * disable the timer to enter FW_SLEEP */
	if (value) {
		rc = setup_fw_sleep_timer_unlocked(iacore, 0);
		if (rc)
			pr_err("FW_SLEEP timer stop failed\n");
	}
#endif
	if (iacore->spl_pdm_mode == true){
		pr_info("spl_pdm_mode is true\n");
		goto unlock_exit;
	}

	iacore->bypass_mode = ia6xx_bypass_mode_values[value];

	if (value) {
		/* Before going to audio capture, make sure IRQ is disabled */
		iacore_disable_irq_nosync(iacore);

#ifdef CONFIG_SND_SOC_IA_I2S
		rc = iacore_change_state_unlocked(iacore, SW_BYPASS);
#else
		rc = iacore_change_state_unlocked(iacore, FW_HW_BYPASS);
#endif
		if (rc) {
			pr_err("Failed to enable PDM bypass mode: %d\n", rc);
			iacore->bypass_mode = ia6xx_bypass_mode_values[0];
		}
	} else {
		/* Before stopping audio capture, make sure IRQ is disabled */
		iacore_disable_irq_nosync(iacore);

		if (voice_sense->bargein_sts == true) {
			rc = iacore_change_state_unlocked(iacore, BARGEIN_SLEEP);
		} else if (is_vs_enabled(iacore)) {
			rc = iacore_change_state_unlocked(iacore, VS_SLEEP);
		} else {
			rc = iacore_change_state_unlocked(iacore, FW_SLEEP);
		}

		if (rc) {
			pr_err("Failed to stop PDM bypass mode: %d", rc);
			iacore->bypass_mode = ia6xx_bypass_mode_values[0];
		}
	}

unlock_exit:
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}

static int iacore_get_bypass_mode(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	switch (iacore->bypass_mode) {
	case IA_BYPASS_OFF:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	case IA_BYPASS_ON:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	case IA_BYPASS_SOUNDWIRE_DHWPT:
		ucontrol->value.enumerated.item[0] = 2;
		break;
	}

	return 0;
}

static int iacore_put_ia6xx_i2s_mode(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	unsigned int value;
	int rc = 0;

	value = ucontrol->value.integer.value[0];
	iacore->ia6xx_i2s_config = value;

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	if (iacore_check_and_reload_fw_unlocked(iacore)) {
		pr_err("firmware is not loaded. error\n");
		rc = -EINVAL;
		goto i2s_mode_set_err;
	}

	if (value == 0) {
		pr_info("I2S Bypass mode disabled\n");
		goto i2s_mode_set_err;
	}

	switch (value) {
	case I2S_MASTER_16K_16B:
	case I2S_MASTER_48K_16B:
	case LL_I2S_MASTER_16K_16B:
	case LL_I2S_MASTER_48K_16B:
		rc = iacore_i2s_master_config(iacore);
		if (rc) {
			pr_err("I2S Bypass master config cmd failed %d\n", rc);
			goto i2s_mode_set_err;
		}
		rc = iacore_i2s_rate_channel_config(iacore, value);
		if (rc) {
			pr_err("I2S Bypass mode fmt config failed %d\n", rc);
			goto i2s_mode_set_err;
		}
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
	case I2S_SLAVE_16K_16B:
	case I2S_SLAVE_48K_16B:
	case LL_I2S_SLAVE_16K_16B:
	case LL_I2S_SLAVE_48K_16B:
		rc = iacore_i2s_route_config_unlocked(iacore, value);
		if (rc)
			pr_err("I2S Bypass mode route config failed %d\n", rc);
		break;
	default:
		pr_err("I2S Bypass mode invalid I2S route config\n");
	}
i2s_mode_set_err:
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}

static int iacore_get_ia6xx_i2s_mode(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	ucontrol->value.enumerated.item[0] = iacore->ia6xx_i2s_config;

	return 0;
}

#if defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
static const unsigned int ia6xx_select_route_values[];
static int iacore_put_ia6xx_route_selection(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	enum iacore_audio_routes value;
	int rc = 0;

	value = ucontrol->value.integer.value[0];

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	/* If a route has been selected, then stop it */
	if (iacore->selected_route != IA_ROUTE_SELECTED_NONE) {
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 50);
		rc = iacore_stop_active_route_unlocked(iacore);
		if (rc) {
			pr_err("stop route failed %d\n", rc);
			goto select_route_set_done;
		}
	}

	/* Route selected? */
	if (value != IA_ROUTE_SELECTED_NONE) {
		rc = iacore_route_config(iacore,
					 ia6xx_select_route_values[value]);
		if (rc) {
			pr_err("select route failed %d\n", rc);
			goto select_route_set_done;
		}
	}

	/* Update the state */
	iacore->selected_route = value;

select_route_set_done:
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}

static int iacore_get_ia6xx_route_selection(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	ucontrol->value.enumerated.item[0] = iacore->selected_route;

	return 0;
}
#endif /* CONFIG_SND_SOC_IA_SOUNDWIRE */

static int iacore_put_ia6xx_firmware_state(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	enum iacore_fw_state value;
	int rc = -1;

	value = ucontrol->value.integer.value[0];

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	pr_info("Requested power state transition: %s ==> %s\n",
			iacore_fw_state_str(iacore_get_power_state(iacore)),
			iacore_fw_state_str(value));

#ifdef FW_SLEEP_TIMER_TEST
	/* Disable timer to enter FW_SLEEP */
	rc = setup_fw_sleep_timer_unlocked(iacore, 0);
	if (rc)
		pr_err("FW_SLEEP timer stop failed\n");
#endif
	if (value <= FW_MAX && value >= FW_MIN) {
		rc = iacore_change_state_unlocked(iacore, value);
	} else {
		rc = -EINVAL;
		pr_err("Invalid power state requested: %d, failed\n", value);
	}
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return rc;
}

static int iacore_get_ia6xx_firmware_state(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	ucontrol->value.enumerated.item[0] = iacore_get_power_state(iacore);

	return 0;
}

static const char * const ia6xx_streaming_mode_texts[] = {
	"PDM", "HOST_BASIC", "CODEC", "SOUNDWIRE", "HOST_PERF", "INVALID",
};
static const struct soc_enum ia6xx_streaming_mode_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(ia6xx_streaming_mode_texts),
			ia6xx_streaming_mode_texts);

static const char * const ia6xx_i2s_mode_texts[] = {
	"OFF", "I2S_SLAVE_16K_16B", "I2S_MASTER_16K_16B",
	"I2S_SLAVE_48K_16B", "I2S_MASTER_48K_16B",
	"LL_I2S_SLAVE_16K_16B", "LL_I2S_SLAVE_16K_32B", "LL_I2S_MASTER_16K_16B",
	"LL_I2S_SLAVE_48K_16B", "LL_I2S_SLAVE_48K_32B", "LL_I2S_MASTER_48K_16B",
	"I2S_SLAVE_48K_16B_BARGEIN", "I2S_SLAVE_48K_32B_BARGEIN"
};
static const struct soc_enum ia6xx_i2s_mode_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(ia6xx_i2s_mode_texts),
			ia6xx_i2s_mode_texts);

static const char * const ia6xx_bypass_mode_texts[] = {
	"OFF", "ON", "SOUNDWIRE_DHWPT"
};

static const struct soc_enum ia6xx_bypass_mode_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(ia6xx_bypass_mode_texts),
			ia6xx_bypass_mode_texts);

#if defined(CONFIG_SND_SOC_IA_BARGEIN)


int iacore_setup_bargein_mode_unlocked(struct iacore_priv *iacore, bool value)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	bool old_bargein_value;
	u32 fw_state;
	int rc = 0;

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	if (!voice_sense->bargein_sysconfig || !voice_sense->bargein_hw) {
		pr_err("barge in fw not ready\n");
		value = false;
	}

	if (voice_sense->bargein_sts == value) {
		pr_info("no change in barge-in status (%d)\n", value);
		goto unlock_exit;
	}

	if (iacore_check_and_reload_fw_unlocked(iacore)) {
		rc = -EINVAL;
		goto unlock_exit;
	}

#ifdef FW_SLEEP_TIMER_TEST
	/* Disable the timer to enter FW_SLEEP */
	rc = setup_fw_sleep_timer_unlocked(iacore, 0);
	if (rc)
		pr_err("FW_SLEEP timer stop Failed\n");
#endif

	old_bargein_value = voice_sense->bargein_sts;

	pr_info("old_bargein_value %d\n", old_bargein_value);

	voice_sense->bargein_sts = value;

	if (iacore_check_fw_reload(iacore) == false) {
		goto unlock_exit;
	}

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("uart_open failed\n");
		goto unlock_exit;
	}

	pr_info("firmware is loaded in %s mode\n",
		 iacore_fw_state_str(iacore->fw_state));
	fw_state = iacore_get_power_state(iacore);

	/*
	 * FW check & re-load is done only for below fw state case
	 *	VS_SLEEP
	 *	BARGEIN_SLEEP
	 *
	 * For rest all states, Barge-in State change request is ignored
	 *
	 */
	switch (fw_state) {
	case VS_SLEEP:
		/* barge-in should already be disabled in this state */
		if (old_bargein_value == true) {
			rc = -EINVAL;
			pr_err("Chip in VS Sleep but bargein_sts is true. error. %d\n", rc);
			goto error;
		}

		//rc = iacore_reload_fw_unlocked(iacore, BARGEIN_SLEEP);
		rc = iacore_change_state_unlocked(iacore, BARGEIN_SLEEP);
		if (rc < 0) {
			pr_err("Fw reload error %d\n", rc);
			voice_sense->bargein_sts = old_bargein_value;
		} else {
			pr_info("chip now in Barge-in mode\n");
		}
		break;

	case BARGEIN_SLEEP:
		/* barge-in should already be enabled in this state */
		if (old_bargein_value == false) {
			rc = -EINVAL;
			pr_err("Chip in Bareg-in Sleep but bargein_sts is False. error %d\n", rc);
			goto error;
		}

		//rc = iacore_reload_fw_unlocked(iacore, VS_SLEEP);
		rc = iacore_change_state_unlocked(iacore, VS_SLEEP);
		if (rc < 0) {
			pr_err("Fw reload error %d\n", rc);
			voice_sense->bargein_sts = old_bargein_value;
		} else {
			pr_info("chip now in voiceq mode\n");
		}
		break;

	case VS_MODE:
		pr_info("Barge-in change request ignored in VS_MODE\n");
		iacore->iacore_cv_kw_detected = false;
		iacore->iacore_event_type = IA_IGNORE_EVENT;

		if (voice_sense->bargein_sts == true) {/*Barge-In*/
			rc = iacore_change_state_unlocked(iacore, BARGEIN_SLEEP);
			if (rc < 0) {
				pr_err("Fw reload error BARGEIN_SLEEP %d\n", rc);
				voice_sense->bargein_sts = old_bargein_value;
			} else {
				pr_info("chip now in voiceq mode\n");
			}
		} else {/*VS Sleep*/
			rc = iacore_change_state_unlocked(iacore, VS_SLEEP);
			if (rc < 0) {
				pr_err("Fw reload error VS_SLEEP %d\n", rc);
				voice_sense->bargein_sts = old_bargein_value;
			} else {
				pr_info("chip now in voiceq mode VS_SLEEP\n");
			}
		}
		break;

	case FW_LOADED:
	case FW_SLEEP:
	case VS_BURSTING:
	case BARGEIN_BURSTING:
	case IO_STREAMING:
	case IO_BARGEIN_STREAMING:
	case HW_BYPASS:
	case FW_HW_BYPASS:
	case SPL_PDM_RECORD:
	case SW_BYPASS:
	case POWER_OFF:
	case SBL:
	case DEEP_SLEEP:
		pr_info("Barge-in change request ignored, error\n");
		voice_sense->bargein_sts = old_bargein_value;
		rc = -EINVAL;
		break;

	default:
		pr_err("current state is invalid %d\n", iacore->fw_state);
		voice_sense->bargein_sts = old_bargein_value;
		rc = -EINVAL;
		break;
	}

error:
	iacore_datablock_close(iacore);
unlock_exit:
	return rc;
}

static const char *const ia6xx_barege_in_mode_text[] = {"OFF", "ON"};
static const struct soc_enum ia6xx_barege_in_mode_enum =
		SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ia6xx_barege_in_mode_text),
						ia6xx_barege_in_mode_text);

static int iacore_get_barge_in_status(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	if (!voice_sense) {
		pr_err("invalid voice sense private pointer\n");
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] =
				(voice_sense->bargein_sts == true ? 1 : 0);
	return 0;
}

static int iacore_put_barge_in_status(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	bool value;
	int rc;

	value = (!ucontrol->value.integer.value[0] ? false : true);
	pr_info("value %d\n", value);

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	rc = iacore_setup_bargein_mode_unlocked(iacore, value);
	if (rc)
		pr_err("setup bargein mode failed\n");

	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}
#endif

static const char * const ia6xx_bus_config_texts[] = {
	"Unknown Config",
	"SPI + PDM",
	"I2C + PDM",
	"UART + PDM",
	"I2C + I2S PERF",
	"I2C + I2S CODEC",
	"I2C + I2S HOST",
	"I2C + I2S HOST BARGE-IN",
	"UART + I2S PERF",
	"UART + I2S CODEC",
	"UART + I2S HOST",
	"UART + I2S HOST BARGE-IN",
};

static const struct soc_enum ia6xx_bus_config_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(ia6xx_bus_config_texts),
				  ia6xx_bus_config_texts);

static int iacore_get_ia6xx_bus_config(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	u32 bus_config;

#if defined(CONFIG_SND_SOC_IA_SPI)
	bus_config = 1;
#elif defined(CONFIG_SND_SOC_IA_I2C) && defined(CONFIG_SND_SOC_IA_BARGEIN)
	bus_config = 7;
#elif defined(CONFIG_SND_SOC_IA_I2C) && defined(CONFIG_SND_SOC_IA_I2S_CODEC)
	bus_config = 5;
#elif defined(CONFIG_SND_SOC_IA_I2C) && defined(CONFIG_SND_SOC_IA_I2S_PERF)
	bus_config = 4;
#elif defined(CONFIG_SND_SOC_IA_I2C) && defined(CONFIG_SND_SOC_IA_I2S_HOST)
	bus_config = 6;
#elif defined(CONFIG_SND_SOC_IA_I2C)
	bus_config = 2;
#elif defined(CONFIG_SND_SOC_IA_UART) && defined(CONFIG_SND_SOC_IA_BARGEIN)
	bus_config = 11;
#elif defined(CONFIG_SND_SOC_IA_UART) && defined(CONFIG_SND_SOC_IA_I2S_CODEC)
	bus_config = 9;
#elif defined(CONFIG_SND_SOC_IA_UART) && defined(CONFIG_SND_SOC_IA_I2S_PERF)
	bus_config = 8;
#elif defined(CONFIG_SND_SOC_IA_UART) && defined(CONFIG_SND_SOC_IA_I2S_HOST)
	bus_config = 10;
#elif defined(CONFIG_SND_SOC_IA_UART)
	bus_config = 3;
#else
	pr_err("Invalid Bus combination. Falling to SPI+PDM\n");
	bus_config = 0;
#endif

	ucontrol->value.enumerated.item[0] = bus_config;

	return 0;
}

#if defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
static const char * const ia6xx_select_route_texts[] = {
	"STOP_ROUTE",
	"IA_1CH_PCM_IN0_PCM_OUT0",
	"IA_1CH_PDM_IN0_PDM_OUT0",
	"IA_1CH_PDM_IN0_PDM_OUT1",
	"IA_1CH_SDW_PCM_IN_SDW_PCM_OUT",
	"IA_1CH_PDM_IN0_PCM_OUT0_LOW_LATENCY",
	"IA_1CH_PDM_IN0_SDW_PCM_OUT_LOW_LATENCY",
	"IA_1CH_VQ_PDM_IN0",
	"IA_1CH_VQ_PDM_IN2",
	"IA_2CH_VQ_BUFFERED_PDM_IN",
	"IA_1CH_PDM_IN0_PCM_OUT0",
	"IA_1CH_PDM_IN0_SDW_PCM_OUT",
	"IA_1CH_SDW_PDM_IN_SDW_PCM_OUT",
	"IA_1CH_PDM_IN0_SDW_PDM_OUT",
	"IA_1CH_SDW_PDM_IN_SDW_PDM_OUT",
	"IA_1CH_TX_AUDIO_BURST_PCM_OUT0",
	"IA_1CH_TX_AUDIO_BURST_SDW_PCM_OUT",
};
static const unsigned int ia6xx_select_route_values[] = {
	IA_ROUTE_SELECTED_NONE,
	IA_1CH_PCM_IN0_PCM_OUT0,
	IA_1CH_PDM_IN0_PDM_OUT0,
	IA_1CH_PDM_IN0_PDM_OUT1,
	IA_1CH_SDW_PCM_IN_SDW_PCM_OUT,
	IA_1CH_PDM_IN0_PCM_OUT0_LL,
	IA_1CH_PDM_IN0_SDW_PCM_OUT_LL,
	IA_1CH_VQ_PDM_IN0,
	IA_1CH_VQ_PDM_IN2,
	IA_2CH_VQ_BUFFERED_PDM_IN,
	IA_1CH_PDM_IN0_PCM_OUT0,
	IA_1CH_PDM_IN0_SDW_PCM_OUT,
	IA_1CH_SDW_PDM_IN_SDW_PCM_OUT,
	IA_1CH_PDM_IN0_SDW_PDM_OUT,
	IA_1CH_SDW_PDM_IN_SDW_PDM_OUT,
	IA_1CH_TX_AUDIO_BURST_PCM_OUT0,
	IA_1CH_TX_AUDIO_BURST_SDW_PCM_OUT,
};
static const struct soc_enum ia6xx_select_route_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(ia6xx_select_route_texts),
				  ia6xx_select_route_texts);
#endif /* CONFIG_SND_SOC_IA_SOUNDWIRE */

static const char * const ia6xx_firmware_state_texts[] = {
	"POWER_OFF",
	"SBL",
	"FW_LOADED",
	"HW_BYPASS",
	"FW_HW_BYPASS",
	"SPL_PDM_RECORD",
	"SW_BYPASS",
	"IO_STREAMING",
	"IO_BARGEIN_STREAMING",
	"VS_MODE",
	"VS_SLEEP",
	"BARGEIN_SLEEP",
	"VS_BURSTING",
	"BARGEIN_BURSTING",
	"FW_SLEEP",
	"DEEP_SLEEP",
	"PROXY_MODE",
};

static const struct soc_enum ia6xx_firmware_state_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(ia6xx_firmware_state_texts),
			ia6xx_firmware_state_texts);
static const char * const ia6xx_test_switch_texts[] = {
	"Off",
	"On",
};

static const struct soc_enum ia6xx_test_switch_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(ia6xx_test_switch_texts),
			ia6xx_test_switch_texts);


static int ia6xx_get_event_status(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	ucontrol->value.enumerated.item[0] = iacore->iacore_event_type;
	/* Reset the event status after read */
	iacore->iacore_event_type = IA_NO_EVENT;
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return 0;
}

static int iacore_put_oem_kw_sensitivity(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}
	voice_sense->oem_kw_sensitivity = ucontrol->value.integer.value[0];
	return 0;
}

static int iacore_get_oem_kw_sensitivity(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] = voice_sense->oem_kw_sensitivity;
	return 0;
}

static int iacore_put_usr_kw_sensitivity(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}
	voice_sense->usr_kw_sensitivity = ucontrol->value.integer.value[0];
	return 0;
}

static int iacore_get_usr_kw_sensitivity(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] = voice_sense->usr_kw_sensitivity;
	return 0;
}

static int iacore_put_voiceid_kw_sensitivity(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}
	voice_sense->voiceid_kw_sensitivity = ucontrol->value.integer.value[0];
	return 0;
}

static int iacore_get_voiceid_kw_sensitivity(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	if (!voice_sense) {
		pr_err("invalid voice sense private pointer\n");
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] = voice_sense->voiceid_kw_sensitivity;
	return 0;
}

#if defined(CONFIG_SND_SOC_IA_I2S_SPL_PDM_MODE)
static const char * const ia6xx_spl_pdm_mode_texts[] = {
	"OFF", "ON",
};

static const struct soc_enum ia6xx_spl_pdm_mode_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(ia6xx_spl_pdm_mode_texts),
				  ia6xx_spl_pdm_mode_texts);

static int iacore_get_spl_pdm_mode_sts(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);

	pr_info("spl_pdm_mode %d\n", iacore->spl_pdm_mode);
	ucontrol->value.integer.value[0] = iacore->spl_pdm_mode;

	return 0;
}

int iacore_set_spl_pdm_mode_sts(bool value)
{
	struct iacore_priv *iacore;
	int rc = 0;
	u32 fw_state;

	if (iacore_global == NULL) {
		pr_info("iacore_priv == NULL!!\n");
		return rc;
	}
	iacore = iacore_global;

	pr_info("Special PDM Mode %s (%d)",
				ia6xx_spl_pdm_mode_texts[value], value);
	IACORE_MUTEX_LOCK(&iacore->access_lock);

	if (iacore->spl_pdm_mode == value) {
		pr_info("no change in spl pdm mode status (%d)", value);
		goto unlock_exit;
	}

	if (0 == value) {
		msm_geni_iacore_uart_pinctrl_enable(1);
		/* PDM clock should haven been stopped, unset high-z for following uart operation */
	}

	if (iacore_check_and_reload_fw_unlocked(iacore)) {
		pr_err("firmware is not loaded, error\n");
		rc = -EINVAL;
		goto unlock_exit;
	}

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("open_uart failed\n");
		goto unlock_exit;
	}

#ifdef FW_SLEEP_TIMER_TEST
	/* Disable the timer to enter FW_SLEEP */
	rc = setup_fw_sleep_timer_unlocked(iacore, 0);
	if (rc)
		pr_err("FW_SLEEP timer stop failed");
#endif

	if (1 == value) {
		iacore_disable_irq_nosync(iacore);

		fw_state = iacore_get_power_state(iacore);

		/* if chip is in sleep state, stop route cmd is not needed */
		if ((fw_state == DEEP_SLEEP) ||
				(fw_state == FW_SLEEP) ||
				(fw_state == VS_SLEEP)) {
			pr_info("chip in sleep state. skip stop route cmd\n");
			iacore->skip_stop_route_cmd = true;
		}

		rc = iacore_change_state_unlocked(iacore, SPL_PDM_RECORD);
		if (rc < 0)
			pr_err("Special PDM record Start Failed %d\n", rc);

	} else {

		/* get the chip back to FW sleep state */
		rc = iacore_change_state_unlocked(iacore, FW_SLEEP);
		if (rc < 0)
			dev_err(iacore->dev, "%s(): chip FW sleep fail %d\n",
								__func__, rc);
	}

	iacore_datablock_close(iacore);
	if (1 == value) {
		msm_geni_iacore_uart_pinctrl_enable(0);
		/* set high-z for later PDM recording */
	}

unlock_exit:
	pr_info("Special PDM Mode func exit\n");
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}
EXPORT_SYMBOL(iacore_set_spl_pdm_mode_sts);

static int iacore_put_spl_pdm_mode_sts(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	bool value;
	int rc = 0;
	u32 fw_state;

	value = !!ucontrol->value.integer.value[0];

	pr_info("Special PDM Mode %s (%d)",
				ia6xx_spl_pdm_mode_texts[value], value);

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	if (iacore_check_and_reload_fw_unlocked(iacore)) {
		rc = -EINVAL;
		goto error_out;
	}

	if (iacore->spl_pdm_mode == value) {
		pr_info("no change in spl pdm mode status (%d)", value);
		goto error_out;
	}
#ifdef FW_SLEEP_TIMER_TEST
	/* Disable the timer to enter FW_SLEEP */
	rc = setup_fw_sleep_timer_unlocked(iacore, 0);
	if (rc)
		pr_err("FW_SLEEP timer stop failed");
#endif

	if (1 == value) {
		iacore_disable_irq_nosync(iacore);

		fw_state = iacore_get_power_state(iacore);

		/* if chip is in sleep state, stop route cmd is not needed */
		if ((fw_state == DEEP_SLEEP) ||
				(fw_state == FW_SLEEP) ||
				(fw_state == VS_SLEEP)) {
			pr_info("chip in sleep state. skip stop route cmd\n");
			iacore->skip_stop_route_cmd = true;
		}

		rc = iacore_change_state_unlocked(iacore, SPL_PDM_RECORD);
		if (rc < 0)
			pr_err("Special PDM record Start Failed %d\n", rc);
	} else {

		/* get the chip back to FW sleep state */
		rc = iacore_change_state_unlocked(iacore, FW_SLEEP);
		if (rc < 0)
			pr_err("chip FW sleep fail %d\n", rc);
	}

error_out:
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}
#endif

static const char *const ia6xx_cvq_stream_i2s_text[] = {"OFF", "ON"};
static const struct soc_enum ia6xx_cvq_stream_i2s_enum =
		SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ia6xx_cvq_stream_i2s_text),
						ia6xx_cvq_stream_i2s_text);

static int iacore_put_cvq_stream_i2s_request(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	bool value;

	value = (!ucontrol->value.integer.value[0] ? false : true);
	pr_info("value %d\n", value);

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	voice_sense->cvq_stream_on_i2s_req = ucontrol->value.integer.value[0];
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return 0;
}

static int iacore_get_cvq_stream_i2s_request(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = iacore_kcontrol_codec(kcontrol);
	struct iacore_priv *iacore = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	IACORE_MUTEX_LOCK(&iacore->access_lock);
	ucontrol->value.integer.value[0] = voice_sense->cvq_stream_on_i2s_req;
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return 0;
}

static struct snd_kcontrol_new ia6xx_digital_ext_snd_controls[] = {
	SOC_SINGLE_EXT("INTR Get Event Status",
			   SND_SOC_NOPM, 0, 65535, 0,
			   ia6xx_get_event_status, NULL),
	SOC_ENUM_EXT("Bypass Mode", ia6xx_bypass_mode_enum,
		       iacore_get_bypass_mode,
		       iacore_put_bypass_mode),
	SOC_ENUM_EXT("Streaming Mode", ia6xx_streaming_mode_enum,
			   iacore_get_streaming_mode, NULL),
	SOC_ENUM_EXT("IA6xx I2S Mode", ia6xx_i2s_mode_enum,
		       iacore_get_ia6xx_i2s_mode,
		       iacore_put_ia6xx_i2s_mode),
	SOC_ENUM_EXT("IA6XX Power State", ia6xx_firmware_state_enum,
				 iacore_get_ia6xx_firmware_state,
				 iacore_put_ia6xx_firmware_state),
	SOC_SINGLE_EXT("OEM KW Sensitivity Threshold", SND_SOC_NOPM, 0, 10, 0,
			iacore_get_oem_kw_sensitivity,
			iacore_put_oem_kw_sensitivity),
	SOC_SINGLE_EXT("User KW Sensitivity Threshold", SND_SOC_NOPM, 0, 10, 0,
			iacore_get_usr_kw_sensitivity,
			iacore_put_usr_kw_sensitivity),
	SOC_SINGLE_EXT("VoiceID KW Sensitivity Threshold", SND_SOC_NOPM, 0, 10,
			0,
			iacore_get_voiceid_kw_sensitivity,
			iacore_put_voiceid_kw_sensitivity),
	SOC_ENUM_EXT("IA6xx Bus Config", ia6xx_bus_config_enum,
					 iacore_get_ia6xx_bus_config, NULL),
#if defined(CONFIG_SND_SOC_IA_I2S_SPL_PDM_MODE)
	SOC_ENUM_EXT("IA6xx Spl PDM Mode", ia6xx_spl_pdm_mode_enum,
					 iacore_get_spl_pdm_mode_sts,
					 iacore_put_spl_pdm_mode_sts),
#endif
#if defined(CONFIG_SND_SOC_IA_BARGEIN)
	SOC_ENUM_EXT("Barge-In Mode", ia6xx_barege_in_mode_enum,
			iacore_get_barge_in_status, iacore_put_barge_in_status),
#endif
	SOC_ENUM_EXT("IA6xx CVQ Stream on I2S", ia6xx_cvq_stream_i2s_enum,
		       iacore_get_cvq_stream_i2s_request,
		       iacore_put_cvq_stream_i2s_request),
#if defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
	SOC_ENUM_EXT("IA6xx Route Selection", ia6xx_select_route_enum,
				 iacore_get_ia6xx_route_selection,
				 iacore_put_ia6xx_route_selection),
#endif /* CONFIG_SND_SOC_IA_SOUNDWIRE */
};

/*int ia6xx_add_codec_controls(struct snd_soc_codec *codec)
{
	int rc;

	rc = snd_soc_add_codec_controls(codec, ia6xx_digital_ext_snd_controls,
				ARRAY_SIZE(ia6xx_digital_ext_snd_controls));
	if (rc)
		dev_err(codec->dev,
			"%s(): ia6xx_digital_ext_snd_controls fail %d\n",
			__func__, rc);
	return rc;
}*/

#if !defined(CONFIG_SND_SOC_IA_SOUNDWIRE)

static int iacore_fw_crash_seen(struct iacore_priv *iacore)
{
	int rc = 0;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	pr_info("handling fw crash, resetting everything");

	if (!voice_sense) {
		pr_err("invalid voice sense private pointer\n");
		return -EINVAL;
	}
	iacore->fw_state = SBL;
	/*iacore->fw_type = IA_CVQ_MODE;*/
	iacore->skip_boot_setup = true;
	iacore->active_route_set = false;
	iacore->low_latency_route_set = false;
	iacore->bypass_mode = IA_BYPASS_OFF;
	iacore->iacore_event_type = IA_VS_FW_RECOVERY;

	if (voice_sense) {
		voice_sense->cvq_sleep = false;
		voice_sense->kw_model_loaded = false;
		/*voice_sense->bargein_sts = false;*/
		voice_sense->rec_to_bargein = false;
		voice_sense->bargein_vq_enabled = false;
		voice_sense->cvq_stream_on_i2s_req = false;
	}

	iacore_collect_rdb(iacore);
	iacore_recover_chip_to_fw_sleep_unlocked(iacore);

	return rc;
}

irqreturn_t iacore_irq_work(int irq, void *data)
{
	struct iacore_priv *iacore = (struct iacore_priv *)data;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	char *def_event[] = { "ACTION=ADNC_KW_DETECT", NULL };
	char *proxy_event[] = { "ACTION=PROXY_EVENT", NULL };
	int rc = 0;
	u32 event_type = 0;
	u32 cmd = 0;

	pr_info("enter\n");

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}
	/* During the execution of interrupt handlers
	 * the irq line for it will be disabled till
	 * the completion of handler execution, this is due
	 * to the irq being requested with IRQF_ONESHOT.
	 * Explicit disabling of the irq is not
	 * required. Thus commenting the call to
	 * the function.
	 */
	/* iacore_disable_irq_nosync(iacore); */

	wake_lock_timeout(&iacore->wakelock,2*HZ);

repeat:
	rc = wait_event_interruptible_timeout(iacore->irq_waitq,
			(iacore->dev->power.is_suspended != true),
			msecs_to_jiffies(3000));
	if (unlikely(rc == -ERESTARTSYS)) {
		pr_err("Signal received, try again\n");
		goto repeat;
	}  else if (rc == 0) {
		pr_err("wait event timeout\n");
		return -ETIMEDOUT;
	}

	usleep_range(IA_DELAY_5MS, IA_DELAY_10MS);
	IACORE_MUTEX_LOCK(&iacore->access_lock);
	/* iacore_disable_irq_nosync() could set
	 * iacore->irq_enabled = false, so exit
	 */
	if (iacore->irq_enabled != true) {
		pr_info("disable irq no sync.\n");
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return IRQ_HANDLED;
	}

	if (iacore->irq_event_detected != true) {//corner case 0530
		pr_info("irq event not detected.\n");
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return IRQ_HANDLED;
	}
	/* if current host is in proxy mode */
	if (iacore_get_power_state(iacore) == PROXY_MODE) {
		pr_info("Proxy event detected\n");
		kobject_uevent_env(&iacore->kobj, KOBJ_CHANGE, proxy_event);
		iacore_disable_irq_nosync(iacore);
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return IRQ_HANDLED;
	}

	INC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

	rc = iacore_pm_get_sync(iacore);
	if (rc < 0) {
		pr_err("pm_get_sync() failed :%d\n", rc);
		goto pm_get_err;
	}
	usleep_range(IA_DELAY_30MS, IA_DELAY_30MS + 15);//for event not correct issue

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("uart_open() failed %d\n", rc);
		iacore_pm_put_autosuspend(iacore);
		iacore_disable_irq_nosync(iacore);
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return -EIO;
	}

	/*
	 * Chip gives IRQ on Keyword detection by placing 0x0000001 on the
	 * UART RX line. Do a dummy read to get this data and ignore it.
	 */
	rc = iacore->bus.ops.read(iacore, &event_type, 4);
	if (rc < 0) {
		pr_err("Dummy read failed:%d\n", rc);
		/*
		 * instead of returning with error, continue to read the get
		 * event response. That way we will actually find the cause
		 */
	}

	event_type = 0;

	cmd = IA_GET_EVENT << 16;
	rc = iacore_cmd_nopm(iacore, cmd, &event_type);
	if (rc < 0) {
		pr_err("Error reading IRQ event: %d\n", rc);
		goto event_resp_err;
	}

	if ((event_type &  0xFFFF0000) != cmd)
		event_type = IA_FW_CRASH;

	event_type &= IA_MASK_INTR_EVENT;

	iacore->iacore_event_type = event_type;

	/* check if the firmware has crashed */
	if (event_type == IA_FW_CRASH) {
		iacore_fw_crash_seen(iacore);
	}
#if 0
	else if (event_type != IA_NO_EVENT) {
		/* Set the power state to voice sense mode */
		iacore->fw_state = VS_MODE;
	}
#else
	else {
		/*
		 * False transaction on the irq line may generate an Interrupt.
		 * So update the fw state accordingly.
		 */
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
	}
#endif

	if (event_type != IA_NO_EVENT) {

		/*
		 * In case of barge-in, after kw is detected, FW is still in
		 * detection stage. So any key word utterred will trigger a kw
		 * detect irq.
		 * So upon, kw detection, disable kw detection in fw.
		 * When upper layer starts cvq again, kw detection will be
		 * enabled.
		 */
		if ((voice_sense->bargein_sts == true) &&
					(iacore->fw_type == IA_BARGEIN_MODE)) {

			/* Disable barge-in vq irq */
			rc = iacore_set_bargein_vq_mode(iacore,
						IA_BARGEIN_VOICEQ_DISABLE);
			if (rc) {
				pr_err("Disable BargeIn VoiceQ fail %d\n", rc);
			}
		}

		pr_info("VS event detected 0x%04x\n", event_type);
		kobject_uevent_env(&iacore->kobj, KOBJ_CHANGE, def_event);
		iacore->iacore_cv_kw_detected = true;
		wake_up_interruptible(&iacore->cvq_wait_queue);
	}

	iacore_pm_put_autosuspend(iacore);
	DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

	iacore_disable_irq_nosync(iacore);

	iacore_datablock_close(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	pr_info("done\n");

	return IRQ_HANDLED;

event_resp_err:
	iacore_pm_put_autosuspend(iacore);
pm_get_err:
	DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
	if (rc < 0) {
		rc = iacore_fw_recovery_unlocked(iacore, FORCED_FW_RECOVERY_ON);
		if (rc < 0)
			pr_err("Firmware recovery failed %d\n", rc);
	}
	iacore_disable_irq_nosync(iacore);
	pr_err("unable to receive intr event\n");
	iacore_datablock_close(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return rc;
}

static struct iaxxx_platform_data *ia6xx_populate_dt_pdata(struct device *dev)
{
	struct iaxxx_platform_data *pdata;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("platform data allocation failed\n");
		goto err;
	}

	pdata->irq_pin = of_get_named_gpio(dev->of_node,
						"adnc,irq-pin", 0);
	if (pdata->irq_pin < 0) {
		pr_err("get irq_pin failed\n");
		pdata->irq_pin = -1;
	}
	pr_info("gpio for irq %d\n", pdata->irq_pin);

	pdata->wakeup_gpio = of_get_named_gpio(dev->of_node,
						"adnc,wakeup-gpio", 0);
	if (pdata->wakeup_gpio < 0) {
		pr_err("get wakeup_gpio failed\n");
		pdata->wakeup_gpio = -1;
	}
	pr_info("wakeup gpio %d\n", pdata->wakeup_gpio);

	pdata->ldo_en_pin = of_get_named_gpio(dev->of_node,
			"adnc,ldo-en-pin", 0);
	if (pdata->ldo_en_pin < 0) {
		dev_err(dev, "%s(): get ldo_en_pin failed\n", __func__);
		pdata->ldo_en_pin = -1;
	}

	return pdata;

err:
	return NULL;
}
#endif

int iacore_register_snd_codec(struct iacore_priv *iacore)
{
	int rc = 0;

	rc = snd_soc_register_codec(iacore->dev,
			iacore->codec_drv,
			iacore->dai,
			iacore->dai_nr);
	if (rc)
		pr_err("Codec registration failed %d\n", rc);
	else
		pr_info("ia6xx Codec registered\n");

	return rc;

}

/* Our custom sysfs_ops that we will associate with our ktype later on */
static ssize_t iacore_attr_show(struct kobject *kobj,
			     struct attribute *attr,
			     char *buf)
{
	struct iacore_sysfs_attr *attribute;
	struct iacore_priv *iacore;

	attribute = to_iacore_priv_attr(attr);
	iacore = to_iacore_priv(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(iacore, attribute, buf);
}

static ssize_t iacore_attr_store(struct kobject *kobj,
			      struct attribute *attr,
			      const char *buf, size_t len)
{
	struct iacore_sysfs_attr *attribute;
	struct iacore_priv *iacore;

	attribute = to_iacore_priv_attr(attr);
	iacore = to_iacore_priv(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(iacore, attribute, buf, len);
}


static const struct sysfs_ops iacore_sysfs_ops = {
	.show = iacore_attr_show,
	.store = iacore_attr_store,
};

static struct kobj_type iacore_priv_ktype = {
	.sysfs_ops = &iacore_sysfs_ops,
	.default_attrs = core_sysfs_attrs,
};

#if (defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_ARCH_MT6797) || \
	defined(CONFIG_SND_SOC_SDM845))

static int ia6xx_get_pinctrl(struct iacore_priv *iacore)
{
	struct pinctrl_info *pnctrl_info;
	struct pinctrl *pinctrl;

	pr_info("Checking for pinctrl Support");

	pnctrl_info = &iacore->pnctrl_info;
	pnctrl_info->pinctrl = NULL;
	pnctrl_info->has_pinctrl = false;

	pinctrl = devm_pinctrl_get(iacore->dev);
	if (IS_ERR_OR_NULL(pinctrl)) {
		pr_err("Unable to get pinctrl handle\n");
		return -EINVAL;
	}

	pnctrl_info->pinctrl = pinctrl;
	pnctrl_info->has_pinctrl = true;

	pr_info("leave.\n");

	return 0;
}

#endif

#if 0// (defined(CONFIG_ARCH_MSM) || defined(CONFIG_ARCH_MT6797))


static int ia6xx_lookup_and_select_pinctrl(struct iacore_priv *iacore,
			struct pinctrl_state **state_pin, const char *state_str)
{
	struct pinctrl_info *pnctrl_info;
	struct pinctrl *pinctrl;
	int ret;

	pr_info("setting up %s pinctrl", state_str);

	pnctrl_info = &iacore->pnctrl_info;

	if (pnctrl_info->has_pinctrl == false) {
		ret = -ENOSYS;
		pr_err("Pinctrl not present\n");
		goto err;
	}

	pinctrl = pnctrl_info->pinctrl;

	*state_pin = pinctrl_lookup_state(pinctrl, state_str);
	if (IS_ERR(*state_pin)) {
		ret = PTR_ERR(*state_pin);
		pr_err("lookup for %s state fail %d\n", state_str, ret);
		goto err;
	}

	/* Select the pins to state requested */
	ret = pinctrl_select_state(pnctrl_info->pinctrl, *state_pin);
	if (ret)
		pr_err("select state %s fail %d\n", state_str, ret);

err:
	return ret;
}

#endif
int ia6xx_core_probe(struct device *dev)
{
	struct iacore_priv *iacore;
	struct iaxxx_platform_data *pdata;
	int rc = 0;

	pr_err("ia6xx_core_probe");
	if (dev == NULL) {
		pr_err("Invalid device pointer");
		return -EINVAL;
	}

	iacore = dev_get_drvdata(dev);
	iacore_global = iacore;

	/*set the chip specific configurations */
	device_rename(dev, "ia6xx-codec");
	iacore->dai_nr = ia6xx_dai_nr();

	iacore->remove = ia6xx_core_remove;

#if !defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
	if (dev->of_node) {
		pr_err("Platform data from device tree\n");
		pdata = ia6xx_populate_dt_pdata(dev);
		dev->platform_data = pdata;
	} else {
		pr_err("Platform data from board file\n");
		pdata = dev->platform_data;
	}

	if (pdata == NULL) {
		pr_err("pdata is NULL");
		rc = -EIO;
		goto exit;
	}

#else

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("platform data allocation failed\n");
		goto exit;
	}

	pdata->wakeup_gpio = -1;
	pdata->irq_pin = -1;
#endif

	iacore->pdata = pdata;

	iacore->api_addr_max = IA6XX_API_ADDR_MAX;
	iacore->api_access = ia6xx_api_access;

	iacore->codec_drv = &ia6xx_driver;
	iacore->codec_drv->component_driver.controls = ia6xx_digital_ext_snd_controls;
	iacore->codec_drv->component_driver.num_controls = ARRAY_SIZE(ia6xx_digital_ext_snd_controls);
	iacore->dai = ia6xx_dai;
	iacore->bypass_mode = 0;
	iacore->in_recovery = 0;
	iacore->irq_enabled = false;//true;
	iacore->irq_event_detected = false;
	iacore->skip_boot_setup = false;

	/* setup bus specific callbacks */
	iacore->bus.setup_bus_intf(iacore);

	mutex_init(&iacore->streaming_mutex);
	mutex_init(&iacore->access_lock);
	mutex_init(&iacore->uart_lock);
	mutex_init(&iacore->irq_lock);
	wake_lock_init(&iacore->wakelock, WAKE_LOCK_SUSPEND,"iacore_irqlock");

	init_waitqueue_head(&iacore->irq_waitq);

	init_waitqueue_head(&iacore->cvq_wait_queue);
	iacore->iacore_cv_kw_detected = false;

	iacore->selected_route = IA_ROUTE_SELECTED_NONE;
	iacore->active_route_set = false;
	iacore->low_latency_route_set = false;
	iacore->spl_pdm_mode = false;
	iacore->dbg_fw = false;

	//iacore_power_ctrl(iacore, IA_POWER_ON);

#ifdef FW_SLEEP_TIMER_TEST
	spin_lock_init(&iacore->fs_tmr_lock);
	init_timer(&iacore->fs_timer);
	iacore->fs_timer.function = iacore_fw_sleep_timer_fn;
	iacore->fs_timer.data = (unsigned long)iacore;

	init_waitqueue_head(&iacore->fs_thread_wait);

	/* start thread to buffer streaming data */
	iacore->fs_thread = kthread_run(iacore_fw_sleep_thread,
					(void *) iacore, "iacore fs thread");
	if (IS_ERR_OR_NULL(iacore->fs_thread)) {
		pr_err("can't create fw sleep thread = %p",
		       iacore->fs_thread);
		rc = PTR_ERR(iacore->fs_thread);
		goto exit;
	}
#endif
	iacore->kset = kset_create_and_add("ia6xx", NULL, kernel_kobj);
	iacore->kobj.kset = iacore->kset;
	if (iacore->kset) {
		rc = kobject_init_and_add(&iacore->kobj, &iacore_priv_ktype,
					  NULL, "%s", "iacore_fs");
		if (rc) {
			kobject_put(&iacore->kobj);
			kset_unregister(iacore->kset);
			iacore->kset = NULL;
			pr_err("Failed to create kobject\n");
		}
	} else {
		pr_err("Failed to create and add kset\n");
	}

#if (defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_ARCH_MT6797)|| defined(CONFIG_SND_SOC_SDM845))
	ia6xx_get_pinctrl(iacore);
#endif

#if !defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
	pr_info("wakeup_gpio = %d\n", pdata->wakeup_gpio);

	if (pdata->wakeup_gpio != -1) {
		rc = devm_gpio_request(dev, pdata->wakeup_gpio, "ia6xx_wakeup");
		if (rc < 0) {
			pr_err("ia6xx_wakeup request fail %d\n", rc);
			goto exit;
		}

#if 0 /*((defined(CONFIG_ARCH_MSM) || defined(CONFIG_ARCH_MT6797)) &&*/
				defined(CONFIG_SND_SOC_IA_I2C))

		rc = ia6xx_lookup_and_select_pinctrl(iacore,
			&iacore->pnctrl_info.wake_active, "wake-active");
		if (rc < 0)
			pr_err("setup pinctrl fail %d\n", rc);
#endif

		rc = gpio_direction_output(pdata->wakeup_gpio, 1);
		if (rc < 0) {
			pr_err("ia6xx_wakeup direction fail %d\n", rc);
			goto exit;
		}

	} else {
		pr_err("wakeup gpio undefined\n");
	}

	pr_err("ia6xx_intr = %d\n", pdata->irq_pin);

	if (pdata->irq_pin != -1) {

		rc = gpio_request(pdata->irq_pin, "ia6xx_intr");
		if (rc < 0) {
			pr_err("ia6xx_intr gpio request fail %d\n", rc);
			goto exit;
		}

		rc = gpio_direction_input(pdata->irq_pin);
		if (rc < 0) {
			pr_err("ia6xx_intr gpio set input fail %d\n", rc);
			goto exit;
		}

#if 0// defined(CONFIG_ARCH_MT6797)
		rc = ia6xx_lookup_and_select_pinctrl(iacore,
			&iacore->pnctrl_info.irq_active, "irq-active");
		if (rc < 0)
			pr_err("setup pinctrl fail %d\n", rc);
#endif

	} else {
		pr_err("ia6xx_intr undefined\n");
	}

#endif

#ifdef CONFIG_SND_SOC_IA_UART
	ia_uart_kthread_init(iacore);
#endif

	rc = iacore_cdev_init(iacore);
	if (rc) {
		pr_err("Error enabling CDEV interface: %d", rc);
		goto exit;
	}

	rc = iacore_vs_init(iacore);
	if (rc) {
		pr_err("voice sense init failed %d\n", rc);
		goto vs_init_error;
	}

	/* Don't call following function if Runtime PM support
	 * is required to be disabled */
	iacore_pm_enable(iacore);

	return rc;

vs_init_error:
	iacore_vs_exit(iacore);
exit:
	pr_err("exit with error\n");
	return rc;
}

int ia6xx_core_remove(struct device *dev)
{
	struct iacore_priv *iacore = dev_get_drvdata(dev);

#ifdef CONFIG_SND_SOC_IA_UART
	ia_uart_kthread_exit(iacore);
#endif

	kobject_put(&iacore->kobj);

	if (iacore->kset)
		kset_unregister(iacore->kset);

	iacore_cdev_cleanup(iacore);

	iacore_vs_exit(iacore);

	snd_soc_unregister_codec(iacore->dev);

	return 0;
}

MODULE_DESCRIPTION("ASoC IA6XX driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ia6xx-codec");
