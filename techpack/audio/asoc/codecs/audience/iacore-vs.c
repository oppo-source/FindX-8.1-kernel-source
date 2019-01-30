/*
 * iacore-vs.c  --  Audience Voice Sense component ALSA Audio driver
 *
 * Copyright 2013 Audience, Inc.
 *
 * Author: Greg Clemson <gclemson@audience.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "iaxxx.h"

#include <linux/tty.h>
#include "iacore.h"
#include "iacore-vs.h"
#include "iacore-uart-common.h"

#define GET_KW_INFO(vs) \
	((!vs || !vs->kw_info) ? NULL : vs->kw_info)

enum iacore_kw_type_id {
	KW_VID_A_ID = 0,
	KW_VID_B_ID,
	KW_OEM_ID,
	KW_UTK_ID,
	KW_MAX_ID,
};

#define KW_VID_A	0x0A
#define KW_VID_B	0x0B
#define KW_OEM		0x0D
#define KW_UTK		0x09

#define KW_AVAILABLE	0x00
#define KW_ALLOCATED	0xFF

static u8 bargein_kw_allocation[KW_MAX_ID];

static inline bool isvalid_frame_size(u8 frame_size)
{
	bool valid = false;
	switch (frame_size) {
	case IA_1MS_FRAME:
	case IA_2MS_FRAME:
	case IA_8MS_FRAME:
	case IA_10MS_FRAME:
	case IA_15MS_FRAME:
	case IA_16MS_FRAME:
		valid = true;
		break;
	}

	return valid;
}

static inline bool isvalid_rate(u8 rate)
{
	bool valid = false;
	switch (rate) {
	case IA_8KHZ:
	case IA_16KHZ:
	case IA_24KHZ:
	case IA_48KHZ:
		valid = true;
		break;
	}

	return valid;
}

static inline bool isvalid_format(u8 format)
{
	if (format != IA_FORMAT_Q11 && format != IA_FORMAT_Q15)
		return false;
	return true;
}

static inline bool isvalid_mode(u8 mode)
{
	bool rc = false;

	switch (mode) {
	case IA_VQ_MODE:
	case IA_CVQ_MODE:
	case IA_BARGEIN_MODE:
		return true;

	default:
		return false;
	}

	return rc;
}

static inline bool isvalid_kw_option(u8 option)
{
	if (option != IA_IGNORE_KW && option != IA_PRESERVE_KW)
		return false;
	return true;
}

static int iacore_vs_route_preset(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	u32 cmd, rsp, route;
	int rc = 0;

	pr_info("enter\n");

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		rc = -EINVAL;
		goto route_error;
	}

	cmd = IA_SET_SAMPLE_RATE << 16 | voice_sense->params.rate;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set rate cmd failed %d\n", rc);
		goto route_error;
	}

	cmd = IA_SET_FRAME_SIZE << 16 | voice_sense->params.frame_size;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set frame size cmd failed %d\n", rc);
		goto route_error;
	}

	/* Buffered Data Format command is not required for barge-in */
	if (iacore->fw_type != IA_BARGEIN_MODE) {
		cmd = IA_SET_BUFFERED_DATA_FORMAT << 16 |
						voice_sense->params.format;
		pr_err("Set buffer format cmd failed 0x%x\n", cmd);
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set buffer format cmd failed %d\n", rc);
			goto route_error;
		}
	}

	if ((voice_sense->bargein_sts == true) &&
					(iacore->fw_type == IA_BARGEIN_MODE)) {
		/* Enable VoiceQ */
		rc = iacore_set_bargein_vq_mode(iacore,
						IA_BARGEIN_VOICEQ_ENABLE);
		if (rc) {
			pr_err("Enable VoiceQ in Barge-In fail %d\n", rc);
			goto route_error;
		}

		/* set IA6xx port clock frequency again in case of barge-in */
		cmd = IA_SET_AUD_PORT_CLK_FREQ << 16 | IA_48KHZ_16BIT_2CH;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("I2S aud port clk freq cmd fail %d\n", rc);
			goto route_error;
		}

		route = IA_2CH_48K_PCM_PDM_IN_NO_OUT;
		pr_info("route = %d\n", route);
	} else {
		if (voice_sense->params.vad == IA_MIC_VAD)
			route = IA_2CH_VQ_BUFFERED_PDM_IN;
		else
			/* No vad */
			route = IA_1CH_VQ_PDM_IN0;
	}

	rc = iacore_set_active_route(iacore, false, route);
	if (rc)
		pr_err("Set route cmd 0x%08x failed %d\n", cmd, rc);

route_error:

	pr_info("leave\n");
	return rc;
}

static int iacore_set_kw_detect_sensitivity(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	u32 cmd, rsp;
	int rc = 0;
	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		rc = -EINVAL;
		goto err_ret;
	}
	/*
	 * HAL Might change the Sensitivity values as per usecase. HAL changes
	 * takes the precedence over the Kcontrol values.
	 */
	if (voice_sense->params.oem_sense != voice_sense->oem_kw_sensitivity)
		voice_sense->oem_kw_sensitivity = voice_sense->params.oem_sense;

	if (voice_sense->params.user_sense != voice_sense->usr_kw_sensitivity)
		voice_sense->usr_kw_sensitivity =
						voice_sense->params.user_sense;

	if (voice_sense->params.vid_sense !=
					voice_sense->voiceid_kw_sensitivity)
		voice_sense->voiceid_kw_sensitivity =
						voice_sense->params.vid_sense;

	/* send OEM KW Sensitivity Threshold */
	cmd = IA_SET_ALGO_PARAM_ID << 16 | IA_VS_OEM_DETECT_SENSITIVITY;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set OEM Detect Sensitivity cmd fail %d\n", rc);
		goto err_ret;
	}

	cmd = IA_SET_ALGO_PARAM << 16 |
			(voice_sense->oem_kw_sensitivity & 0xFFFF);
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set OEM Detect Sensitivity value fail %d\n", rc);
		goto err_ret;
	}

	usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 10);

	/* send User KW Sensitivity Threshold */
	cmd = IA_SET_ALGO_PARAM_ID << 16 | IA_VS_USR_DETECT_SENSITIVITY;
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set USR Detect Sensitivity cmd fail %d\n", rc);
		goto err_ret;
	}

	cmd = IA_SET_ALGO_PARAM << 16 |
			(voice_sense->usr_kw_sensitivity & 0xFFFF);
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set USR Detect Sensitivity value fail %d\n", rc);
		goto err_ret;
	}

	usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 10);

	/* VoiceID not supported on Bargein */
	if (voice_sense->bargein_sts != true) {
		/* send VoiceID KW Sensitivity Threshold */
		cmd = IA_SET_ALGO_PARAM_ID << 16 |
					IA_VS_VOICEID_DETECT_SENSITIVITY;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set VoiceID Detect Sensitivity cmd fail %d\n", rc);
			goto err_ret;
		}

		cmd = IA_SET_ALGO_PARAM << 16 |
				(voice_sense->voiceid_kw_sensitivity & 0xFFFF);
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set VoiceID Detect Sensitivity value fail %d\n", rc);
			goto err_ret;
		}

		usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 10);
	}

err_ret:
	return rc;
}

static irqreturn_t iacore_irq_handler (int irq, void *ptr)//corner case 0530
{
	struct iacore_priv *iacore = (struct iacore_priv *)ptr;
	iacore->irq_event_detected = true;
	return IRQ_WAKE_THREAD;
}

/* NOTE: This function must be called with access_lock acquired */
static int iacore_vs_set_algo_params_unlocked(struct iacore_priv *iacore, bool set)
{
	u32 cmd, rsp;
	int rc = 0;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	pr_info("Set = %d\n", set);

	if (set) {
		rc = iacore_set_kw_detect_sensitivity(iacore);
		if (rc)
			goto vs_algo_param_err;

		/* send VS processing command */
		cmd = IA_SET_ALGO_PARAM_ID << 16 | IA_VS_PROCESSING_MODE;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set Algo Param VS processing cmd fail %d\n", rc);
			goto vs_algo_param_err;
		}

		cmd = IA_SET_ALGO_PARAM << 16 | IA_VS_DETECT_KEYWORD;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set Algo Param keyword detect cmd fail %d\n", rc);
			goto vs_algo_param_err;
		}

		usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 10);

	} else {
#if defined(CONFIG_SND_SOC_IA_PDM)
		rc = iacore_stop_bypass_unlocked(iacore);
		if (rc) {
			pr_err("failed to stop hw bypass = %d\n", rc);
		}
#elif defined(CONFIG_SND_SOC_IA_I2S_SPL_PDM_MODE)
		if (iacore->spl_pdm_mode == true) {
			rc = iacore_stop_bypass_unlocked(iacore);
			if (rc) {
				pr_err("failed to stop hw bypass = %d\n", rc);
			}
		}
#endif
		if (voice_sense->bargein_sts == false) {
			cmd = IA_ALGO_RESET << 16 | (0x5000);
		} else {
			rc = iacore_set_bargein_vq_mode(iacore, IA_BARGEIN_VOICEQ_ENABLE);
			if (rc) {
				pr_err("BargeIn VQ Intr Enable fail %d\n", rc);
				goto vs_algo_param_err;
			}

			cmd = IA_ALGO_RESET << 16 | (0xff00);
		}

		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set Algo Param VS processing cmd fail %d\n", rc);
			goto vs_algo_param_err;
		}
	}

vs_algo_param_err:
	return rc;
}

void iacore_request_fw_cb(const struct firmware *fw, void *context)
{
	struct iacore_priv *iacore = context;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	const char *hw_filename = "audience/ia6xx/ia6xx-vs-hw.bin";
	const struct firmware *hw_fw;
	int rc = 0;
#if defined(CONFIG_SND_SOC_IA_BARGEIN)
	const char *bargein_hw_filename = "audience/ia6xx/ia6xx-bargein-hw.bin";
	const char *bargein_filename = iacore->bargein_filename;
	const struct firmware *bargein_fw, *bargein_hw_fw;
#endif
	u32 sysconf_major, bosko_major;

	if (!voice_sense) {
		pr_err("invalid voice sense private pointer\n");
		return;
	}

	/* initially reset all firmware handlers */
	voice_sense->vs = NULL;
	voice_sense->hw = NULL;
	voice_sense->bargein_hw = NULL;
	voice_sense->bargein_sysconfig = NULL;

	if (!fw || !fw->data || fw->size == 0) {
		pr_err("Failed to get firmware : %s\n",
			iacore->vs_filename);
		rc = -EINVAL;
		goto err_exit;
	}
	voice_sense->vs = (struct firmware *)fw;

	rc = request_firmware(&hw_fw, hw_filename, iacore->dev);
	if (rc || !hw_fw || !hw_fw->data || hw_fw->size == 0) {
		pr_err("Failed to get firmware : %s\n",
			hw_filename);
		rc = -EINVAL;
		release_firmware(fw);
		goto err_exit;
	}

	/* Check major number of sysconfig and bosko for compatibility */
	memcpy(&sysconf_major,
	       (char *)fw->data + IA_SYSCONFIG_MAJOR_OFFSET,
	       sizeof(sysconf_major));
	memcpy(&bosko_major,
	       (char *)hw_fw->data + IA_BOSKO_MAJOR_OFFSET,
	       sizeof(bosko_major));

	pr_info("VoiceQ FW: Major number SysConfig:0x%08x, Bosko:0x%08x\n",
		sysconf_major, bosko_major);
	if (sysconf_major != bosko_major) {
		pr_err("incompatible VoiceQ Fw SysConfig:0x%08x Bosko:0x%08x\n",
			sysconf_major, bosko_major);
		rc = -EINVAL;
		release_firmware(fw);
		release_firmware(hw_fw);
		goto err_exit;
	}

	voice_sense->hw = (struct firmware *)hw_fw;
	voice_sense->vs = (struct firmware *)fw;

#if defined(CONFIG_SND_SOC_IA_BARGEIN)
	rc = request_firmware(&bargein_fw, bargein_filename, iacore->dev);
	if (rc || !bargein_fw || !bargein_fw->data || bargein_fw->size == 0) {
		pr_err("Failed to get firmware : %s\n", bargein_filename);
		rc = -EINVAL;
		goto skip_barge_in_fw_check;
	}

	rc = request_firmware(&bargein_hw_fw, bargein_hw_filename, iacore->dev);
	if (rc || !bargein_hw_fw || !bargein_hw_fw->data || bargein_hw_fw->size == 0) {
		pr_err("Failed to get firmware : %s\n", bargein_hw_filename);
		release_firmware(bargein_fw);
		goto skip_barge_in_fw_check;
	}

	memcpy(&sysconf_major,
	       (char *)bargein_fw->data + IA_SYSCONFIG_MAJOR_OFFSET,
	       sizeof(sysconf_major));
	memcpy(&bosko_major,
	       (char *)bargein_hw_fw->data + IA_BOSKO_MAJOR_OFFSET,
	       sizeof(bosko_major));

	pr_info("Barge-in FW : Major number SysConfig:0x%08x, Bosko:0x%08x\n",
		sysconf_major, bosko_major);
	if (sysconf_major != bosko_major) {
		pr_err("incompatible Barge-in Fw SysConfig:0x%08x Bosko:0x%08x\n",
			sysconf_major, bosko_major);
		release_firmware(bargein_fw);
		release_firmware(bargein_hw_fw);
		goto skip_barge_in_fw_check;
	}

	voice_sense->bargein_hw = bargein_hw_fw;
	voice_sense->bargein_sysconfig = bargein_fw;

#endif

	pr_info("firmware is requested successfully!\n");

skip_barge_in_fw_check:

#if defined(CONFIG_SND_SOC_IA_I2S)
#if	!defined(CONFIG_SND_SOC_IA_I2S_PERF)
	/* During performance mode firmware download, as it has dependency
	 * over the pcm devices, it is not possible to perform any pcm
	 * related operations until codec is registered.
	 */
	pr_info("change state to FW_SLEEP\n");
	rc = iacore_change_state_lock_safe(iacore, FW_SLEEP);
	if (rc) {
		pr_err("FW sleep during device boot failed rc = %d\n", rc);
		if (iacore->fw_state != FW_SLEEP) {
			rc = 0;
			goto err_exit;
		}
	}
#endif
#else
	rc = iacore_change_state_lock_safe(iacore, FW_SLEEP);
	if (rc) {
		pr_err("FW sleep during device boot failed rc = %d\n", rc);
		goto err_exit;
	}
#endif	/* CONFIG_SND_SOC_IA_I2S */

	pr_info("firmware is downloaded successfully!\n");

	if (iacore->pdata->irq_pin != -1) {
		int irq_pin = gpio_to_irq(iacore->pdata->irq_pin);
		iacore->pdata->irq_pin = irq_pin;
		irq_set_status_flags(irq_pin, IRQ_DISABLE_UNLAZY);
		rc = request_threaded_irq(irq_pin,
					iacore_irq_handler,//corner case 0530,
					iacore_irq_work,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_EARLY_RESUME | IRQF_NO_SUSPEND,
					//IRQF_TRIGGER_FALLING | IRQF_ONESHOT| IRQF_EARLY_RESUME | IRQF_NO_SUSPEND,
					"iacore_irq_work", iacore);
		if (rc) {
			pr_err("event request_irq() fail %d\n", rc);
			goto err_exit;
		}

		pr_info("set event irq wake start\n");
		rc = enable_irq_wake(irq_pin);
		if (rc < 0) {
			pr_err("set event irq wake fail %d\n", rc);
			iacore_disable_irq(iacore);
			free_irq(irq_pin, iacore);
			goto err_exit;
		}

		/* Disable the interrupt till needed */
		iacore->irq_enabled = true;
		iacore_disable_irq_nosync(iacore);

	} else {
		pr_info("ia6xx irq pin undefined\n");
	}

err_exit:
	return;
}

void iacore_release_models_unlocked(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	struct ia_kw_info *kw_info = GET_KW_INFO(voice_sense);
	struct ia_kw_priv *kw_priv;
	int i;
	if ((!voice_sense)||(!kw_info)) {
		pr_err("invalid private pointer\n");
		return;
	}
	for (i = 0; kw_info && i < kw_info->kw_count; i++) {
		kw_priv = &kw_info->kw[i];
		kfree((void *)(uintptr_t)(kw_priv->kw_buff_addr));
	}

	devm_kfree(iacore->dev, kw_info);
	voice_sense->kw_info = NULL;
	voice_sense->kw_model_loaded = false;
}

void iacore_release_models(struct iacore_priv *iacore)
{
	if (!iacore) {
		pr_err("Invalid private pointer\n");
		return;
	}

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	iacore_release_models_unlocked(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
}


static void iacore_init_models(struct iacore_priv *iacore,
			       struct ia_kw_info *kw_model_info)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	if (!voice_sense) {
		pr_err("Invalid private pointer\n");
		return;
	}

	//IACORE_MUTEX_LOCK(&iacore->access_lock);
	/* release previously stored model files if any */
	if (GET_KW_INFO(voice_sense))
		iacore_release_models_unlocked(iacore);

	voice_sense->kw_info = kw_model_info;
	//IACORE_MUTEX_UNLOCK(&iacore->access_lock);
}
static bool check_bargein_kw_valid(int kw_slot, u8 kw_type){
	bool valid = false;
	u8 kw_id;

	pr_info("kw_slot =%d, kw_type=0x%x\n", kw_slot, kw_type);
	switch (kw_type) {
	case KW_VID_A:
		kw_id = KW_VID_A_ID;
		break;

	case KW_VID_B:
		kw_id = KW_VID_B_ID;
		break;

	case KW_OEM:
		kw_id = KW_OEM_ID;
		break;

	case KW_UTK:
		kw_id = KW_UTK_ID;
		break;

	default:
		valid = false;
		goto check_exit;
	}

	if (bargein_kw_allocation[kw_id] == 0) {
		if (kw_type == KW_OEM) {
			memset(bargein_kw_allocation, KW_ALLOCATED, KW_MAX_ID); /*mark other keywords unavailable*/
		} else {
			bargein_kw_allocation[KW_OEM_ID] = KW_ALLOCATED;
		}
		bargein_kw_allocation[kw_id] = kw_slot;
		valid = true;
	}

check_exit:
	pr_info("valid =%d\n", valid);
	return valid;
}

/* NOTE: This function must be called with access_lock acquired */
static int iacore_vs_write_keywords_unlocked(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	struct ia_kw_info *kw_info = GET_KW_INFO(voice_sense);
	struct ia_kw_priv *kw_priv;
	u8 *kw_buff;
	int rc = 0;
	int i;
	u32 *model_hdr_ptr;

	if (!kw_info || !kw_info->kw_count) {
		pr_err("no keyword mode saved\n");
		rc = -EINVAL;
		goto iacore_vs_write_keywords_exit;
	}

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("uart open failed %d\n", rc);
		goto iacore_vs_write_keywords_exit;
	}

	if (voice_sense->bargein_sts == true)
		memset(bargein_kw_allocation, KW_AVAILABLE, KW_MAX_ID);

	pr_info("kw count %d", kw_info->kw_count);
	for (i = 0; kw_info && i < kw_info->kw_count; i++) {
		kw_priv = &kw_info->kw[i];

		kw_buff = (void *)(uintptr_t)(kw_priv->kw_buff_addr);
		model_hdr_ptr = (u32 *)kw_buff;
		pr_info("Kw Model Header 0x%08x", *model_hdr_ptr);

		if (voice_sense->bargein_sts == true){
			if (!check_bargein_kw_valid(i+1, ((*model_hdr_ptr) & 0xff00) >> 8)) {
				dev_err(iacore->dev,"%s(): Ignore current kw%d, keep barge in mode safe\n",
						__func__, i + 1);
				continue;
			}
		}

		rc = iacore_datablock_write(iacore, kw_buff,
					    kw_priv->wdb_size,
					    kw_priv->kw_size);
		if ((rc < 0) || (rc < kw_priv->kw_size)) {
			pr_err("kw %d write failed rc = %d\n", i + 1, rc);
			iacore_datablock_close(iacore);
			goto iacore_vs_write_keywords_exit;
		}
	}

	iacore_datablock_close(iacore);

	return 0;

iacore_vs_write_keywords_exit:
	return rc;
}

/* NOTE: This function must be called with access_lock acquired */
static int iacore_vs_load_unlocked(struct iacore_priv *iacore, u32 fw_type)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	bool boot_setup_req = IA_BOOT_SETUP_REQ;
	const struct firmware *sysconfig, *hw;
	const char *fw_str = "vs";
	int rc = 0;

	pr_err("fw_type :%d\n", fw_type);

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	/* confirm the firmware type requested */
	if (fw_type == IA_BARGEIN_MODE) {
		sysconfig = voice_sense->bargein_sysconfig;
		hw = voice_sense->bargein_hw;
		fw_str = "bargein";
	} else {
		sysconfig = voice_sense->vs;
		hw = voice_sense->hw;
	}

	if (!sysconfig || !hw) {
		pr_err("invalid null pointer\n");
		return -EINVAL;
	}

	if (sysconfig->size == 0 || hw->size == 0) {
		pr_err("Invalid Firmware Size\n");
		return -EINVAL;
	}

	rc = iacore_pm_get_sync(iacore);
	if (rc < 0) {
		pr_err("pm_get_sync failed :%d\n", rc);
		return rc;
	}

	/*
	 * for fw load requests from states like Deep Sleep, boot setup
	 * is not required.
	*/
	if (iacore->skip_boot_setup == true) {
		boot_setup_req = IA_BOOT_SETUP_NREQ;
		iacore->skip_boot_setup = false;

		/* no recovery when loading the fw */
		INC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

		goto boot_setup;
	}

	/* no recovery when loading the fw */
	INC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

	if (!iacore->boot_ops.setup || !iacore->boot_ops.finish) {
		pr_err("boot setup or finish function undefined\n");
		rc = -EIO;
		goto vs_load_failed;
	}

boot_setup:
	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("open failed %d\n", rc);
		goto vs_load_failed;
	}
	pr_info("boot_setup_req = %d\n", boot_setup_req);
	rc = iacore->boot_ops.setup(iacore, boot_setup_req);
	if (rc) {
		pr_err("%s fw download start error %d\n",
			fw_str, rc);
		goto iacore_vs_fw_download_failed;
	}
	pr_debug("Done with boot setup, entering sysconfig block write\n");
	rc = iacore->bus.ops.block_write(iacore,
		((char *)hw->data) , hw->size);
	if (rc) {
		pr_err("%s firmware image write error %d\n",
			fw_str, rc);
		rc = -EIO;
		goto iacore_vs_fw_download_failed;
	}
	pr_debug("Out of the sysconfig block write\n");

	rc = iacore->boot_ops.setup(iacore, IA_BOOT_SETUP_NREQ);
	if (rc) {
		pr_err("%s fw download setup error %d\n",
			fw_str, rc);
		goto iacore_vs_fw_download_failed;
	}

	rc = iacore->bus.ops.block_write(iacore,
		((char *)sysconfig->data) , sysconfig->size);
	if (rc) {
		pr_err("%s firmware image write error %d\n",
			fw_str, rc);
		rc = -EIO;
		goto iacore_vs_fw_download_failed;
	}

	/* boot_ops.finish is required only in the case of POLL mode
	 * command completion*/
	rc = iacore->boot_ops.finish(iacore);
	if (rc) {
		pr_err("%s fw download finish error %d\n",
			fw_str, rc);
		goto iacore_vs_fw_download_failed;
	}

	iacore->fw_type = fw_type;

	pr_info("%s fw download done\n", fw_str);

	iacore_datablock_close(iacore);
	DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
	iacore_pm_put_autosuspend(iacore);
	return rc;

iacore_vs_fw_download_failed:
	iacore_datablock_close(iacore);
	/* If FW download fails, power cycle the chip */
	iacore_poweroff_chip(iacore);
	iacore_power_ctrl(iacore, IA_POWER_ON);
	iacore->fw_state = SBL;

vs_load_failed:
	DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

	iacore_pm_put_autosuspend(iacore);
	return rc;
}

/* NOTE: This function must be called with access_lock acquired */
int iacore_load_firmware_unlocked(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	u32 firmware_type = IA_CVQ_MODE;

	if (!voice_sense) {
		pr_err("invalid voice_sense pointer\n");
		return -EINVAL;
	}

	lockdep_assert_held(&iacore->access_lock);

	pr_info("bargein_sts %d, iacore->fw_type %d\n",
			voice_sense->bargein_sts, iacore->fw_type);
	/*
	 * if bargein is requested (->bargein_sts == true), then load Brage-in
	 * FW else load regular CVQ firmware.
	*/
	if (voice_sense->bargein_sts == true)
		firmware_type = IA_BARGEIN_MODE;

	return iacore_vs_load_unlocked(iacore, firmware_type);
}

/* NOTE: This function must be called with access_lock acquired */
int iacore_vs_configure_unlocked(struct iacore_priv *iacore)
{
	u8 preset;
	int rc;
	u32 cmd, rsp;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	struct ia_kw_info *kw_info = GET_KW_INFO(voice_sense);

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	if (!voice_sense->kw_model_loaded) {
		if (!kw_info || !kw_info->kw_count) {
			pr_err("no keyword stored\n");
			rc = -EINVAL;
			goto vs_config_err;
		}

		/* reset algo memory */
		rc = iacore_vs_set_algo_params_unlocked(iacore, IA_ALGO_MEM_RESET);
		if (rc) {
			pr_err("set algo params fail %d\n", rc);
			goto vs_config_err;
		}

		/* write background model and keywords files */
		rc = iacore_vs_write_keywords_unlocked(iacore);
		if (rc) {
			pr_err("datablock write fail rc = %d\n", rc);
			goto vs_config_err;
		}
		voice_sense->kw_model_loaded = true;
	}

	/* set level triggered low interrupt configuration
	 * based on customer platforms need to modify this
	 */
	cmd = IA_EVENT_RESPONSE_CMD << 16 | IA_EDGE_FALLING;/*IA_LEVEL_TRIGGER_HIGH;*/
	rc = iacore_cmd_nopm(iacore, cmd, &rsp);
	if (rc) {
		pr_err("Set Algo Param ID cmd fail %d\n", rc);
		goto vs_config_err;
	}

	preset = voice_sense->params.vq_preset & PRESET_MASK;
	pr_info("set preset from %d\n", preset);
	if (voice_sense->params.mode == IA_VQ_MODE) {
		preset = PRESET_VAD_OFF_VQ_NO_BUFFERING;
		voice_sense->params.vq_preset = preset;
	}

	pr_info("set preset to %d\n", preset);
	if (preset != PRESET_NONE) {
		rc = iacore_set_kw_detect_sensitivity(iacore);
		if (rc)
			goto vs_config_err;
	} else {
		/* Barge-in doesn't require KW preserv command */
		if ((voice_sense->bargein_sts == true) &&
					(iacore->fw_type == IA_BARGEIN_MODE)) {
			cmd = ((IA_CONFIG_DATA_PORT << 16) |
							(IA_CONFIG_48k << 12));
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("I2S Config Data port cmd fail %d\n", rc);
				goto vs_config_err;
			}
			goto skip_kw_preserv_command;
		}

		if (voice_sense->params.mode == IA_VQ_MODE)
			cmd = IA_KW_PRESERVATION << 16 | IA_DISABLE_BUFFERING;
		else if (voice_sense->params.kw_preserve == IA_PRESERVE_KW)
			cmd = IA_KW_PRESERVATION << 16 | IA_ENABLE_PRESERVATION;
		else
			cmd = IA_KW_PRESERVATION << 16 | IA_DISABLE_PRESERVATION;
		rc = iacore_cmd_nopm(iacore, cmd, &rsp);
		if (rc) {
			pr_err("Set keyword preserve cmd failed %d\n", rc);
			goto vs_config_err;
		}

		/* TODO fix the sleep */
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

skip_kw_preserv_command:

		rc = iacore_vs_route_preset(iacore);
		if (rc) {
			pr_err("vs route preset fail %d\n", rc);
			goto vs_config_err;
		}

		rc = iacore_vs_set_algo_params_unlocked(iacore, IA_ALGO_MEM_SET);
		if (rc) {
			pr_err("set algo params fail %d\n", rc);
			goto vs_config_err;
		}
	}

vs_config_err:
	return rc;
}

long ia_cvq_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iacore_priv * const iacore
			= (struct iacore_priv *)file->private_data;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	struct ia_kw_info *kw_info;
	struct ia_kw_priv *kw_priv;
	struct ia_cvq_params params;
	void __user *kw_buff;
	u32 kw_size, perf_mode = 0;
	int ret = 0;
	int i, j;
	u8 preset, barge_in_required, fw_state;
	u8 *sleep_str;

	pr_info("cmd 0x%x\n", cmd);

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	if (iacore_check_and_reload_fw_unlocked(iacore)) {
		pr_err("firmware is not loaded, error.\n");
		ret = -EINVAL;
		goto err_exit;
	}

	switch (cmd) {
	case IA_INIT_PARAMS:
		pr_info("initialize CVQ params\n");
		if (copy_from_user(&params, (void __user *)arg,
				   sizeof(struct ia_cvq_params))) {
			ret = -EFAULT;
			goto err_exit;
		}

		if (isvalid_rate(params.rate) && isvalid_mode(params.mode) &&
		    isvalid_kw_option(params.kw_preserve) &&
		    isvalid_format(params.format) &&
		    isvalid_frame_size(params.frame_size)) {
			memcpy(&voice_sense->params, &params, sizeof(params));

			pr_info("VQ/CVQ mode %d, kw preserve %d, preset %d\n",
				params.mode, params.kw_preserve, params.vq_preset);
			pr_info("rate %d, format %d, frame_size %d\n",
				params.rate, params.format, params.frame_size);
		} else {
			pr_err("one of the initialization values are invalid\n");
			ret = -EINVAL;
		}

		break;
	case IA_INIT_IOSTREAM_PARAMS:
		pr_info("initialize iostream params\n");
		if (copy_from_user(&params, (void __user *)arg,
				   sizeof(struct ia_cvq_params))) {
			ret = -EFAULT;
			goto err_exit;
		}

		if (isvalid_rate(params.rate) &&
		    isvalid_format(params.format) &&
		    isvalid_frame_size(params.frame_size)) {
			memcpy(&voice_sense->iostream_params, &params,
			       sizeof(params));
		} else {
			pr_err("iostream initialization values are invalid\n");
			ret = -EINVAL;
		}

		break;
	case IA_CVQ_START:
		pr_info("start CVQ\n");

#ifdef FW_SLEEP_TIMER_TEST
		/* Disable timer for entering FW_SLEEP */
		ret = setup_fw_sleep_timer_unlocked(iacore, 0);
		if (ret)
			pr_err("FW_SLEEP timer stop failed");
#endif

		if (copy_from_user(&barge_in_required, (void __user *)arg,
				   sizeof(u8))) {
			pr_err("copy_from_user barge_in_required failed\n");
			ret = -EFAULT;
			goto err_exit;
		}
		pr_info("barge_in_required = %d\n", barge_in_required);

		/* maintain consistency b/w kcontrol and cvq params from HAL */
		voice_sense->bargein_sts = barge_in_required;

		ret = iacore_datablock_open(iacore);
		if (ret) {
			pr_err("open_uart() fail %d\n", ret);
			break;
		}

		preset = voice_sense->params.vq_preset & PRESET_MASK;
		pr_info("iacore set preset %d\n", preset);

		/*if preset is set by upper layer && send it */
		if (preset == PRESET_NONE) {
			if (voice_sense->params.mode == IA_VQ_MODE) {
				preset = PRESET_VAD_OFF_VQ_NO_BUFFERING;
				voice_sense->params.vq_preset = preset;
			} else {
                if (voice_sense->bargein_sts == false) {
					preset = PRESET_VAD_OFF_CVQ_KW_PRSRV;
				} else {
					preset = PRESET_VAD_OFF_VQ_NO_BUFFERING;
				}
				voice_sense->params.vq_preset = preset;
			}
		}

		pr_info("iacore preset after correction %d\n", preset);

		/* choose proper sleep state as per the request */
		if (barge_in_required == true) {
			ret = iacore_change_state_unlocked(iacore, BARGEIN_SLEEP);
			sleep_str = "Barge-in";
		} else {
			/*
			 * For state transitions like
			 *	VS_SLEEP --> KW detect --> VS_MODE --> SW_BYPASS
			 * Exiting SW_BYPASS will put chip back into VS_SLEEP
			 *
			 * In such cases, no state transitions will happen for
			 * next CVQ_START ioctl calls. This means IRQ will not
			 * be enabled back.
			 * Fix is to force-fully check the previous state here.
			 * If the state is already VS_SLEEP, then enable the
			 * IRQ and continue.
			 */
			fw_state = iacore_get_power_state(iacore);
			if (fw_state == VS_SLEEP)
				iacore_enable_irq(iacore);
			else
				ret = iacore_change_state_unlocked(iacore, VS_SLEEP);

			sleep_str = "vq";
		}

		if (ret < 0) {
			pr_err("unable to move the chip to voice sense sleep :%d\n", ret);
		} else {
			voice_sense->cvq_sleep = true;
			pr_info("chip now in %s mode\n", sleep_str);
			pr_info("clear potential pending irq, set irq_event_detected as false\n");
			iacore->irq_event_detected = false;
		}
		iacore_datablock_close(iacore);
		break;
	case IA_CVQ_STOP:
		pr_info("stop CVQ\n");
		if (iacore->spl_pdm_mode == 1) {
			pr_err("Error, in SPL pdm mode. returning\n");
			ret = 0;
			goto err_exit;
		}

		if ((iacore_get_power_state(iacore) == VS_BURSTING) ||
			(iacore_get_power_state(iacore) == BARGEIN_BURSTING)) {
			pr_err("Error, FW in Bursting state. returning\n");
			ret = 0;
			goto err_exit;
		}

		preset = voice_sense->params.vq_preset & PRESET_MASK;
		pr_info("preset = %d\n", preset);

		iacore_release_models_unlocked(iacore);
		voice_sense->params.vq_preset = PRESET_NONE;

		ret = iacore_datablock_open(iacore);
		if (ret) {
			pr_err("open failed %d\n", ret);
			goto err_exit;
		}

		if ((iacore->fw_state != DEEP_SLEEP) &&
			(iacore->fw_state != FW_SLEEP)) {

			ret = iacore_change_state_unlocked(iacore, FW_SLEEP);
			if (ret < 0) {
				pr_err("Chip to FW Sleep Fail %d\n", ret);
				iacore_datablock_close(iacore);
				goto err_exit;
			}

			voice_sense->cvq_sleep = false;

		} else {
			voice_sense->cvq_sleep = false;
		}
		iacore_datablock_close(iacore);
		break;
	case IA_LOAD_KEYWORDS:
		pr_info("load keyword files\n");
		kw_info = devm_kzalloc(iacore->dev, sizeof(struct ia_kw_info), GFP_KERNEL);
		if (!kw_info) {
			ret = -ENOMEM;
			goto err_exit;
		}

		if (copy_from_user(kw_info, (void __user *)arg,
				   sizeof(struct ia_kw_info))) {
			devm_kfree(iacore->dev, kw_info);
			ret = -EFAULT;
			goto err_exit;
		}

		pr_info("iacore kw count %d\n", kw_info->kw_count);
		for (i = 0; i < kw_info->kw_count; i++) {
			kw_priv = &kw_info->kw[i];

			kw_size = kw_priv->kw_size;
			if (kw_size <= 0) {
				pr_err("invalid keyword size for kw id %d\n", i + 1);
				kw_priv->kw_buff_addr = 0;
				continue;
			}

			kw_buff = (void __user *)
					(uintptr_t)kw_priv->kw_buff_addr;
			kw_buff = memdup_user(kw_buff, kw_size);
			kw_priv->kw_buff_addr = (uintptr_t)kw_buff;
			pr_info("iacore kw_size %d\n", kw_size);
			if (IS_ERR(kw_buff)) {
				ret = PTR_ERR(kw_buff);
				pr_err("memdup failed %d\n", ret);

				/* deallocate remaining model data */
				for (j = 0; j < i; j++) {
					kw_priv = &kw_info->kw[j];
					kfree((void *)(uintptr_t)
						(kw_priv->kw_buff_addr));
				}
				devm_kfree(iacore->dev, kw_info);
				pr_err("iacore kw_buff error\n");
				ret = -EINVAL;
				goto err_exit;
			}
		}

		iacore_init_models(iacore, kw_info);
		break;
	case IA_UNLOAD_KEYWORDS:
		pr_info("flush keyword files\n");
		iacore_release_models_unlocked(iacore);
		break;
	case IA_GET_KW_ID:
		pr_info("get keyword id\n");
		if (copy_to_user((void *)arg, &iacore->iacore_event_type, sizeof(u32))) {
			pr_err("iacore:copy_to_user iacore_event_type failed\n");
			ret = -EFAULT;
		}
		pr_info("keyword id is %d\n", iacore->iacore_event_type);
		iacore->iacore_event_type = IA_NO_EVENT;
		break;
	case IA_IS_PERFMODE:
		pr_info("is performance mode\n");
#ifdef CONFIG_SND_SOC_IA_I2S_PERF
		perf_mode = IA_I2S_PERF_MODE;
#else
		perf_mode = IA_NON_PERF_MODE;
#endif
		if (copy_to_user((void *)arg, &perf_mode, sizeof(u32))) {
			pr_err("copy_to_user perf_mode failed");
			ret = -EFAULT;
		}
		break;
	case IA_LOAD_FIRMWARE:
		pr_info("Load Firmware to fw_sleep\n");
#ifdef CONFIG_SND_SOC_IA_I2S_PERF
		ret = iacore_change_state_unlocked(iacore, FW_SLEEP);
		if (ret < 0)
			pr_err("failed to change firmware state :%d\n", ret);
#endif
		break;
#if defined(CONFIG_SND_SOC_IA_BARGEIN)
	case IA_SETUP_BARGEIN:
		pr_info("Setup Barge-in\n");
		if (copy_from_user(&barge_in_required, (void __user *)arg,
				   sizeof(u8))) {
			pr_err("copy_from_user barge_in_required failed\n");
			ret = -EFAULT;
			goto err_exit;
		}
		pr_info("barge_in_required = %d\n", barge_in_required);
		barge_in_required = (barge_in_required ? true : false);

		ret = iacore_setup_bargein_mode_unlocked(iacore, barge_in_required);
		if (ret < 0) {
			pr_err("failed to change firmware state :%d\n", ret);
		}
		else {
			pr_info("clear potential pending irq, set irq_event_detected as false\n");
			iacore->irq_event_detected = false;
		}
		break;
#endif
	default:
		pr_err("Invalid ioctl command received 0x%x\n", cmd);
		ret = -EINVAL;
	}

err_exit:
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	return ret;
}

int iacore_set_vs_sleep(struct iacore_priv *iacore)
{
	int rc = 0;

#if !defined(CONFIG_SND_SOC_IA_SOUNDWIRE)
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	u8 preset;

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	preset = voice_sense->params.vq_preset & PRESET_MASK;
	pr_info("set preset %d\n", preset);
	if (preset != PRESET_NONE) {
		if (preset <= PRESET_VAD_ON_VQ_NO_BUFFERING)
			rc = iacore_set_chip_sleep(iacore, IA_CHIP_SLEEP);
		else
			rc = iacore_set_chip_sleep(iacore, IA_LOW_POWER);
	} else {
		if (voice_sense->params.vad == IA_MIC_VAD)
			rc = iacore_set_chip_sleep(iacore, IA_CHIP_SLEEP);
		else
			rc = iacore_set_chip_sleep(iacore, IA_LOW_POWER);
	}

	if (rc == 0)
		usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 10);
#endif

	return rc;
}

long ia_cvq_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	pr_err("%s()", __func__);
	return ia_cvq_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}

int iacore_set_iostreaming_unlocked(struct iacore_priv *iacore, int value)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
#ifdef CONFIG_SND_SOC_IA_UART
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
#endif
	u32 cmd, cmd_c2b, rsp = 0;
	int rc = 0, resp_req;
	int retry;

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		rc = -EINVAL;
		goto err_exit;
	}

	rc = iacore_datablock_open(iacore);
	if (rc) {
		pr_err("open failed %d\n", rc);
		goto err_exit;
	}

	if (value == IACORE_IOSTREAM_ENABLE) {
		if (voice_sense->bargein_sts == true) {
			/*
			 * since barge-in sts is true, disable irq before
			 * sending  any command.
			 */
			iacore_disable_irq_nosync(iacore);

			/* stop any active route Route */
			rc = iacore_stop_active_route_unlocked(iacore);
			if (rc)
				goto iostream_error;

			/* Disable VoiceQ */
			rc = iacore_set_bargein_vq_mode(iacore,
						IA_BARGEIN_VOICEQ_DISABLE);
			if (rc) {
				pr_err("Disable BargeIn VoiceQ fail %d\n", rc);
				goto iostream_error;
			}
			cmd = IA_SET_PRESET << 16 | PRESET_VAD_OFF_VQ_NO_BUFFERING;
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("Set preset cmd(0x%x) fail %d\n", cmd, rc);
				goto iostream_error;
			}
			iacore->active_route_set = true;
			usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 100);

			pr_debug("endpoint1 %d, endpoint2 %d\n",
					voice_sense->iostream_params.endpoint1,
					voice_sense->iostream_params.endpoint2);

			cmd = IA_SELECT_STREAMING << 16 |
				(voice_sense->iostream_params.endpoint1 |
						(IACORE_IOSTREAM_ENABLE << 15));
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("Set stream endpoint1(%d) fail %d\n",
					voice_sense->iostream_params.endpoint1, rc);
				goto iostream_error;
			}

			usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 100);

			if (voice_sense->iostream_params.endpoint2 > 0) {
				u8 endpoint2 =
					voice_sense->iostream_params.endpoint2;
				cmd = IA_SELECT_STREAMING << 16 |
						(endpoint2 |
						(IACORE_IOSTREAM_ENABLE << 15));

				rc = iacore_cmd_nopm(iacore, cmd, &rsp);
				if (rc) {
					pr_err("Set ep2 (%d) fail %d\n", endpoint2, rc);
					goto iostream_error;
				}

				usleep_range(IA_DELAY_10MS,
							IA_DELAY_10MS + 100);
			}

			cmd = IA_SET_IOSTREAMING << 16 | IACORE_IOSTREAM_ENABLE;
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("iostream enable failed %d\n", rc);
				goto iostream_error;
			}

		} else {
			cmd = IA_SET_PRESET << 16 | PRESET_VAD_OFF_CVQ_KW_PRSRV;
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("Set preset cmd(0x%x) fail %d\n", cmd, rc);
				goto iostream_error;
			}
			iacore->active_route_set = true;

			pr_info("endpoint1 %d, endpoint2 %d",
					voice_sense->iostream_params.endpoint1,
					voice_sense->iostream_params.endpoint2);

			cmd = IA_SELECT_STREAMING << 16 |
				(voice_sense->iostream_params.endpoint1 |
						(IACORE_IOSTREAM_ENABLE << 15));
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("Set stream endpoint1(%d) fail %d",
					voice_sense->iostream_params.endpoint1,
					rc);
				goto iostream_error;
			}
			cmd = IA_SET_IOSTREAMING << 16 | IACORE_IOSTREAM_ENABLE;
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("iostream enable failed %d\n", rc);
				goto iostream_error;
			}
		}
	} else if (value == IACORE_IOSTREAM_DISABLE) {

		cmd = (IA_STOP_IOSTREAM_BURST_CMD << 16) | value;
		resp_req = !(cmd & BIT(28));
		cmd_c2b = iacore->bus.ops.cpu_to_bus(iacore, cmd);

		rc = iacore->bus.ops.write(iacore, &cmd_c2b, sizeof(cmd_c2b));
		if (rc) {
			pr_err("Send STOP IOStream fail %d\n", rc);
			goto iostream_error;
		}

		/* TODO Fix this sleep */
		usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 100);

		/* Need to read all stream data from IA6xx to clean up the
		 * buffers. Firmware Tx buffer depends on various factors and
		 * the max number of 32 bit words in Tx buffer could be FIFO
		 * depth(16) + buffer size(32) = 48 words or 192 bytes.
		 */
		retry = IA_STOP_STRM_CMD_RETRY;

		if (resp_req) {
			do {
				rc = iacore->bus.ops.read(iacore, &rsp,
								sizeof(rsp));
				if (rc) {
					pr_err("resp read fail %d\n", rc);
				}

				rsp = iacore->bus.ops.bus_to_cpu(iacore, rsp);
				if (rsp == cmd)
					break;
				usleep_range(IA_DELAY_2MS, IA_DELAY_2MS + 5);
			} while (--retry);

			update_cmd_history(	iacore->bus.ops.cpu_to_bus(iacore, cmd), rsp);

			if (retry <= 0) {
				pr_err("stop iostream resp retry fail\n");
				IACORE_FW_RECOVERY_FORCED_OFF(iacore);
				rc = -EINVAL;
				goto iostream_error;
			}

		} else {
#ifdef CONFIG_SND_SOC_IA_UART
			if (IS_ERR_OR_NULL(iacore_uart->tty)) {
				pr_err("tty is not available\n");
				//return -EINVAL;
			} else {
				/* Wait till cmd is completely sent to chip */
				tty_wait_until_sent(iacore_uart->tty, msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));
			}

			update_cmd_history(cmd, rsp);
#else
			update_cmd_history(
				iacore->bus.ops.cpu_to_bus(iacore, cmd), rsp);
#endif
		}

		if (voice_sense->bargein_sts == true) {
			pr_info("endpoint1 %d, endpoint2 %d",
					voice_sense->iostream_params.endpoint1,
					voice_sense->iostream_params.endpoint2);

			usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 100);

			cmd = IA_SELECT_STREAMING << 16 |
				(voice_sense->iostream_params.endpoint1 |
				(IACORE_IOSTREAM_DISABLE << 15));
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);
			if (rc) {
				pr_err("EP1 (%d) disable fail %d\n",
					voice_sense->iostream_params.endpoint1, rc);
				goto iostream_error;
			}

			if (voice_sense->iostream_params.endpoint2 > 0) {
				u8 endpoint2 =
					voice_sense->iostream_params.endpoint2;
				cmd = IA_SELECT_STREAMING << 16 |
					(endpoint2 |
					(IACORE_IOSTREAM_DISABLE << 15));
				rc = iacore_cmd_nopm(iacore, cmd, &rsp);
				if (rc) {
					pr_err("EP2(%d) disable fail %d\n", endpoint2, rc);
					goto iostream_error;
				}
				usleep_range(IA_DELAY_10MS,
							IA_DELAY_10MS + 100);
			}
		} else {
			cmd = IA_SELECT_STREAMING << 16 |
				(IA_RX_ENDPOINT |
				(IACORE_IOSTREAM_DISABLE << 15));
			rc = iacore_cmd_nopm(iacore, cmd, &rsp);

			if (rc) {
				pr_err("iostream disable fail %d\n", rc);
				goto iostream_error;
			}
		}

		rc = iacore_stop_active_route_unlocked(iacore);
		if (rc) {
			pr_err("Stop Route fail %d\n", rc);
			goto iostream_error;
		}

		/* Sending SYNC command */
#ifdef CONFIG_SND_SOC_IA_UART
		rc = iacore_uart_get_sync_response(iacore);
#else
		rc = iacore_cmd_nopm(iacore,
			(IA_SYNC_CMD << 16) | IA_SYNC_POLLING, &rsp);
#endif
		if (rc) {
			pr_err("send SYNC cmd Fail %d\n", rc);
			goto iostream_error;
		}

	} else {
		pr_err("invalid option\n");
		rc = -EINVAL;
	}

iostream_error:
	iacore_datablock_close(iacore);
err_exit:
	return rc;
}

void iacore_vs_exit(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return;
	}
	iacore_release_models(iacore);
	if (voice_sense->vs)
		release_firmware(voice_sense->vs);
	if (voice_sense->hw)
		release_firmware(voice_sense->hw);

	/* release barge-in fw hooks while exit */
	if (voice_sense->bargein_sysconfig)
		release_firmware(voice_sense->bargein_sysconfig);
	if (voice_sense->bargein_hw)
		release_firmware(voice_sense->bargein_hw);

	devm_kfree(iacore->dev, voice_sense);

	iacore->voice_sense = NULL;
}

int iacore_vs_init(struct iacore_priv *iacore)
{
	int rc = 0;
	struct iacore_voice_sense *voice_sense;

	voice_sense = (struct iacore_voice_sense *)
	devm_kzalloc(iacore->dev, sizeof(struct iacore_voice_sense), GFP_KERNEL);
	if (!voice_sense) {
		rc = -ENOMEM;
		goto voice_sense_alloc_err;
	}

	iacore->voice_sense = (void *)voice_sense;

	/* Initialize variables */
	voice_sense->kw_info = NULL;
	voice_sense->cvq_sleep = 0;
	voice_sense->params.rate = IA_8KHZ;
	voice_sense->params.mode = IA_VQ_MODE;
	voice_sense->params.format = IA_FORMAT_Q15;
	voice_sense->params.frame_size = IA_16MS_FRAME;
	voice_sense->params.kw_preserve = IA_IGNORE_KW;
	voice_sense->params.vad = IA_NO_VAD;
	voice_sense->params.vq_preset = PRESET_NONE;

	voice_sense->iostream_params.rate = IA_16KHZ;
	voice_sense->iostream_params.format = IA_FORMAT_Q15;
	voice_sense->iostream_params.frame_size = IA_16MS_FRAME;
	voice_sense->iostream_params.endpoint1 = IA_RX_ENDPOINT;
	voice_sense->iostream_params.endpoint2 = 0x00;

	voice_sense->oem_kw_sensitivity = DEF_OEM_KW_SENSITIVITY_THRESHOLD;
	voice_sense->usr_kw_sensitivity = DEF_USER_KW_SENSITIVITY_THRESHOLD;
	voice_sense->voiceid_kw_sensitivity =
					DEF_VOICEID_KW_SENSITIVITY_THRESHOLD;

	/* default is barge-in disabled */
	voice_sense->bargein_sts = false;
	voice_sense->bargein_vq_enabled = false;
	voice_sense->rec_to_bargein = false;
	voice_sense->cvq_stream_on_i2s_req = false;

	rc = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
			iacore->vs_filename, iacore->dev,
			GFP_KERNEL, iacore, iacore_request_fw_cb);
	if (rc) {
		pr_err("request_firmware wait(%s) failed %d\n", iacore->vs_filename, rc);

		goto request_vs_firmware_error;
	}
	rc = iacore_register_snd_codec(iacore);
	if (rc) {
		pr_err("%s() iacore codec registration failed rc = %d\n",
			__func__, rc);
	}
	return rc;
request_vs_firmware_error:
	devm_kfree(iacore->dev, voice_sense);
	iacore->voice_sense = NULL;
voice_sense_alloc_err:
	return rc;
}
