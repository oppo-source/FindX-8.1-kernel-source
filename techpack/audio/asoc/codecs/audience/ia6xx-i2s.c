/*
 * ia6xx-i2s.c  --  ia6xx ALSA SoC Microphone codec driver
 *
 * Copyright 2015 Knowles Corporation, All Rights Reserved.
 *
 * This IA6xx I2S/PCM MIC codec driver will be used to communicate with MIC
 * using any alsa complaint HOST platforms.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "iaxxx.h"

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "ia6xx-iacore.h"
#include "iacore-vs.h"

#define ia6xx_suspend NULL
#define ia6xx_resume  NULL

static int ia6xx_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	pr_debug("ENTER");

	return 0;
}

/* set codec format */
static int ia6xx_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct iacore_priv *ia6xx = dev_get_drvdata(codec->dev);

	pr_err("ENTER, fmt 0x%08x\n", fmt);
	if (!ia6xx) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}
	ia6xx->codec_ismaster = IA6XX_SLAVE;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		ia6xx->codec_ismaster = IA6XX_MASTER;
		break;
	default:
		pr_err("unsupported MIC DAIFMT\n");
		return -EINVAL;
	}

	return 0;
}

/* set codec sysclk */
static int ia6xx_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				   int clk_id, unsigned int freq, int dir)
{
	pr_debug("\n");

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 * input: params_rate, params_fmt
 */
static int ia6xx_pcm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct iacore_priv *ia6xx = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(ia6xx);
	int channels   = params_channels(params);
	int rate       = params_rate(params);
	int samplebits = params_physical_width(params);
	u8 kw_prsrv;
	u8 preset;
	int rc = 0;
	u32 command, rsp = 0;

	pr_info("substream %s stream %d channel %d rate %d bits %d\n",
		substream->name, substream->stream, channels, rate, samplebits);

	pr_err("called by %s(%d)\n", current->comm, task_pid_nr(current));

	if (!voice_sense) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("Invalid stream type\n");
		return -EINVAL;
	}

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	/*
	 * This is avoid pcm_open to send channel and rate configuration
	 * commands during the performance download.
	 */
	if (iacore_i2sperf_active(ia6xx))
		return 0;
#endif
	IACORE_MUTEX_LOCK(&ia6xx->access_lock);

	if (ia6xx->spl_pdm_mode == true) {
		pr_info("spl_pdm_mode is true, skip\n");
		goto hw_params_error;
	}

	if (iacore_check_and_reload_fw_unlocked(ia6xx)) {
		rc = -EINVAL;
		goto hw_params_error;
	}

	if ((ia6xx->fw_type != IA_CVQ_MODE) || (ia6xx->fw_state != VS_MODE)) {
		pr_info("CVQ over i2s can't work in current type =%d, mode = %d\n", ia6xx->fw_type, ia6xx->fw_state);
		if (voice_sense->cvq_stream_on_i2s_req == true) {
			voice_sense->cvq_stream_on_i2s_req = false;
			goto hw_params_error;
		}
	}

	/* check if pcm open call is for Normal CVQ Streaming */
	if (voice_sense->cvq_stream_on_i2s_req == true) {
		pr_info("setup and start bursting over i2s\n");

		/*
		 * IA_CONFIG_DATA_PORT - Config Data Port command
		 *
		 *	hight 4 bits | middle 4 bit	| lower 8 bits
		 *	(sample rate) (channel count) (enable/disable)
		 *
		 */
		switch (rate) {
		case 48000:
			command = (IA_CONFIG_DATA_PORT << 16) |
				(IA_CONFIG_48k << 12 | IA_CONFIG_1CHAN << 8);
			break;

		case 96000:
			command = (IA_CONFIG_DATA_PORT << 16) |
				(IA_CONFIG_96k << 12 | IA_CONFIG_1CHAN << 8);
			break;

		case 192000:
			command = (IA_CONFIG_DATA_PORT << 16) |
				(IA_CONFIG_192k << 12 | IA_CONFIG_1CHAN << 8);
			break;

		default:
			pr_err("samplerate (%d) not supported", rate);
			rc = -EINVAL;
			goto hw_params_error;
		}

		rc = iacore_cmd_nopm(ia6xx, command, &rsp);
		if (rc) {
			pr_err("I2S Config Data port command fail %d", rc);
			goto hw_params_error;
		}

		/* Select Device Param PCM port settings */
		command = IA_SET_DEVICE_PARAMID << 16 | 0x0A00;
		rc = iacore_cmd_nopm(ia6xx, command, &rsp);
		if (rc) {
			pr_err("set param command failed - %d", rc);
			goto hw_params_error;
		}

		/* word length */
		switch (samplebits) {
		case 16:
			command = IA_SET_DEVICE_PARAM << 16 | 0x000F;
			break;

		case 24:
			command = IA_SET_DEVICE_PARAM << 16 | 0x0017;
			break;

		case 32:
			command = IA_SET_DEVICE_PARAM << 16 | 0x001F;
			break;

		default:
			pr_err("samplebits (%d) not supported",
							samplebits);
			rc = -EINVAL;
			goto hw_params_error;
		}

		rc = iacore_cmd_nopm(ia6xx, command, &rsp);
		if (rc) {
			pr_err("set param value failed - %d", rc);
			goto hw_params_error;
		}

		/* Start Bursting over I2S */
		command = IACORE_CVQ_STREAM_BURST_I2S_CMD << 16 |
					IACORE_CVQ_STREAM_BURST_I2S_START;
		rc = iacore_cmd_nopm(ia6xx, command, &rsp);
		if (rc) {
			pr_err("start bursting command failed - %d", rc);
			goto hw_params_error;
		}

		/*
		 * Before going to audio capture, enable
		 * IRQ for fw crash handling
		 */
		iacore_enable_irq(ia6xx);

		goto hw_params_error;
	}

	if (channels != MONO)
	{
		pr_err("Multi-channel (%d) is not supported! go on!!", channels);
	//	rc = -EINVAL;
	//	goto hw_params_error;
	}

	/*
	 * setup capture device as barge-in streaming device if bypass is not
	 * enabled & barge-in firmware is loaded
	 */
	if (ia6xx->bypass_mode == IA_BYPASS_OFF) {
		pr_err("iacore Barge-in not open i2s!\n");
		goto hw_params_error;

		if (ia6xx->fw_type != IA_BARGEIN_MODE) {
			pr_err("Barge-in firmware not loaded. ia6xx mic not open");
			rc = -EINVAL;
			goto hw_params_error;
		}

#ifndef CONFIG_SND_SOC_IA_OK_GOOGLE
		preset = voice_sense->params.vq_preset & PRESET_MASK;
		pr_info("preset selected %d", preset);

		if (preset == PRESET_NONE) {
			/* stop any active route Route */
			rc = iacore_stop_active_route_unlocked(ia6xx);
			if (rc)
				goto hw_params_error;

			kw_prsrv = voice_sense->params.kw_preserve;
			pr_debug("iacore kw_preserve %d", kw_prsrv);
			if (kw_prsrv != IA_PRESERVE_KW)	{
				/* Disable VoiceQ */
				rc = iacore_set_bargein_vq_mode(ia6xx, IA_BARGEIN_VOICEQ_DISABLE);
				if (rc) {
					pr_err("Disable BargeIn VQ Intr fail %d\n", rc);
					goto hw_params_error;
				}
			}

			switch (samplebits) {
				case 16:
					pr_info("iacore I2S_SLAVE_48K_16B_BARGEIN");
					rc = iacore_i2s_route_config_unlocked(ia6xx, I2S_SLAVE_48K_16B_BARGEIN);
					break;

				case 32:
					pr_info("iacore I2S_SLAVE_48K_32B_BARGEIN");
					rc = iacore_i2s_route_config_unlocked(ia6xx, I2S_SLAVE_48K_32B_BARGEIN);
					break;

				default:
					pr_err("samplebits (%d) not supported", samplebits);
					IACORE_MUTEX_UNLOCK(&ia6xx->access_lock);
					return -EINVAL;
			}

			if (rc)
				goto hw_params_error;

			if (voice_sense->params.kw_preserve == IA_PRESERVE_KW)
				iacore_enable_irq(ia6xx);
		}
#else
		iacore_enable_irq(ia6xx);
#endif
	} else {
		/* In case of master we will configure IA6xx MIC
		 * as master with channel and rate configuration
		 * and enable the IA6xx route.
		 * In case of slave we have just enable the route
		 * on IA6xx MIC
		 */

		if (ia6xx->codec_ismaster == IA6XX_MASTER) {
			iacore_i2s_master_config(ia6xx);

			if (rate == SAMPLE_16000_KHZ) {
				iacore_i2s_rate_channel_config(ia6xx, I2S_MASTER_16K_16B);
				iacore_i2s_route_config_unlocked(ia6xx, I2S_MASTER_16K_16B);
			} else if (rate == SAMPLE_48000_KHZ) {
				iacore_i2s_rate_channel_config(ia6xx, I2S_MASTER_48K_16B);
				iacore_i2s_route_config_unlocked(ia6xx, I2S_MASTER_48K_16B);
			} else {
				pr_err("Sample rate (%d) not supported\n", rate);
				rc = -EINVAL;
				goto hw_params_error;
			}
		} else { /* if codec is slave */
			pr_info("codec is slave\n");
			if (rate == SAMPLE_16000_KHZ){
				pr_info("iacore i2s channel mono sampleRate 16K\n");
			   	switch (samplebits) {
					case 16:
						rc = iacore_i2s_route_config_unlocked(ia6xx, LL_I2S_SLAVE_16K_16B);
						break;

					case 32:
						rc = iacore_i2s_route_config_unlocked(ia6xx, LL_I2S_SLAVE_16K_32B);
						break;

					default:
						pr_err("samplebits not supported\n");
						rc = -EINVAL;
						goto hw_params_error;
				}
				if (!rc) {
					/*
					 * This flag will be used to notify the
					 * HAL layer about
					 *	- KW detection *and*
					 *	- Crash recovery
					 * Reset it before starting any process
					 */
					ia6xx->iacore_cv_kw_detected = false;

					/*
					 * Before going to audio capture, enable
					 * IRQ for fw crash handling
					 */
					iacore_enable_irq(ia6xx);
				}
			} else if (rate == SAMPLE_48000_KHZ) {
				pr_info("iacore i2s sampleRate 48K\n");
				switch (samplebits) {
					case 16:
						pr_info("iacore LL_I2S_SLAVE_48K_16B\n");
						rc = iacore_i2s_route_config_unlocked(ia6xx, LL_I2S_SLAVE_48K_16B);
						break;

					case 32:
						pr_info("iacore LL_I2S_SLAVE_48K_32B\n");
						rc = iacore_i2s_route_config_unlocked(ia6xx, LL_I2S_SLAVE_48K_32B);
						break;

					default:
						pr_err("%d bits not supported\n", samplebits);
						rc = -EINVAL;
						goto hw_params_error;
					}
				if (!rc) {
					/*
					 * This flag will be used to notify the
					 * HAL layer about
					 *	- KW detection *and*
					 *	- Crash recovery
					 * Reset it before starting any process
					 */
					ia6xx->iacore_cv_kw_detected = false;

					/*
					 * Before going to audio capture, enable
					 * IRQ for fw crash handling
					 */
					iacore_enable_irq(ia6xx);
				}
			} else {
				pr_err("Sample rate (%d) not supported\n", rate);
				rc = -EINVAL;
				goto hw_params_error;
			}
		}
	}

hw_params_error:
	IACORE_MUTEX_UNLOCK(&ia6xx->access_lock);
	return rc;
}

static int ia6xx_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	pr_debug("ENTER");
	return 0;
}

int ia6xx_i2s_hw_free(struct snd_pcm_substream *substream,
						struct snd_soc_dai *codec_dai)
{
	int rc = 0;
	u32 rsp = 0, cmd;
	struct snd_soc_codec *codec = codec_dai->codec;
	struct iacore_priv *ia6xx = dev_get_drvdata(codec->dev);
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(ia6xx);

	pr_info("substream = %s stream = %d\n", substream->name, substream->stream);

	pr_info("called by %s(%d)\n", current->comm, task_pid_nr(current));

	if (!voice_sense) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("Invalid stream type.\n");
		return -EINVAL;
	}

	IACORE_MUTEX_LOCK(&ia6xx->access_lock);

	if (ia6xx->spl_pdm_mode == true) {
		pr_info("spl_pdm_mode is true, skip.\n");
		goto err_out;
	}

	if (iacore_check_and_reload_fw_unlocked(ia6xx)) {
		rc = -EINVAL;
		goto err_out;
	}

	/* check if pcm open call is for Normal CVQ Streaming */
	if (voice_sense->cvq_stream_on_i2s_req == true) {
		voice_sense->cvq_stream_on_i2s_req = false;

		pr_info("stop bursting over i2s\n");

		iacore_disable_irq_nosync(ia6xx);
		/* stop Bursting over I2S */
		cmd = IACORE_CVQ_STREAM_BURST_I2S_CMD << 16 |
					IACORE_CVQ_STREAM_BURST_I2S_STOP;
		rc = iacore_cmd_nopm(ia6xx, cmd, &rsp);
		if (rc) {
			pr_err("stop bursting cmd failed - %d\n", rc);
			goto err_out;
		}

		/* Send CVQ Buffer Overflow check command */
		rc = iacore_cmd_nopm(ia6xx, (IA_CVQ_BUFF_OVERFLOW << 16), &rsp);
		if (rc) {
			pr_err("CVQ Buffer Overflow cmd fail %d\n", rc);
		} else {
			if (rsp & CVQ_BUFF_OVERFLOW_OCCURRED) {
				pr_err("CVQ Buff Overflow 0x%08x\n", rsp);
				//rc = -EINVAL;
				//goto close_exit;
			}
		}
		goto err_out;
	}
	/*
	 * Request to disable Bypass mode (= 0) might have already put chip
	 * back to VS Sleep / Barge-in Sleep. In such cases, Skip Irq disable &
	 * stop route.
	 */
	if ((iacore_get_power_state(ia6xx) == BARGEIN_SLEEP) &&
					(voice_sense->rec_to_bargein == true)) {
		voice_sense->rec_to_bargein = false;
		pr_info("exit\n");
		goto err_out;
	}

	/*
	 * If we are coming from I2S recording, FW crash handling using irq is
	 * ON. In this case, record stop means, irq should be disabled.
	 */
	if (ia6xx->bypass_mode == IA_BYPASS_ON)
		iacore_disable_irq_nosync(ia6xx);

	/* steps for barge-in pcm close */
	if (voice_sense->bargein_sts == true) {
		iacore_disable_irq_nosync(ia6xx);

		/*
		 * If barge-in is true, upon pcm_close, disable Barge-in
		 * Vq Detection
		 */
		rc = iacore_set_bargein_vq_mode(ia6xx,
				IA_BARGEIN_VOICEQ_DISABLE);
		if (rc) {
			pr_err("BargeIn VQ Intr Disable fail %d\n", rc);
		}
	}

	/*
	 * check the state of the chip. If it is in VS sleep or Deep sleep,
	 * just return.
	 */
	pr_info("power state = %d, exit\n", iacore_get_power_state(ia6xx));
	if ((iacore_get_power_state(ia6xx) == VS_SLEEP) ||
		(iacore_get_power_state(ia6xx) == FW_SLEEP) ||
		/*(iacore_get_power_state(ia6xx) == VS_MODE) ||*/
		(iacore_get_power_state(ia6xx) == DEEP_SLEEP)) {
		pr_info("exit\n");
		goto err_out;
	}

	/* stop any active route Route */
	rc = iacore_stop_active_route_unlocked(ia6xx);
	if (rc)
		pr_err("stop active route fail %d\n", rc);

	pr_info("exit");

err_out:
	IACORE_MUTEX_UNLOCK(&ia6xx->access_lock);
	return rc;
}

#define IA6XX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)
#define IA6XX_RATES (SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_48000)

static const struct snd_soc_dai_ops ia6xx_ops = {
	.hw_free = ia6xx_i2s_hw_free,
	.hw_params = ia6xx_pcm_hw_params,
	.digital_mute = ia6xx_digital_mute,
	.set_fmt = ia6xx_set_dai_fmt,
	.set_sysclk = ia6xx_set_dai_sysclk,
};

enum {
	IA6XX_DAI_I2S_ID = 0,
	IA6XX_DAI_PDM_ID,
	IA6XX_NUM_DAI_IDS,
};

struct snd_soc_dai_driver ia6xx_dai[] = {
	[IA6XX_DAI_I2S] = {
		.name = "ia6xx-mic",
		.id = IA6XX_DAI_I2S_ID,
		.capture = {
			.stream_name = "capture-pcm",
			.channels_min = 1,
			.channels_max = 2,
			.rates = IA6XX_RATES,
			.formats = IA6XX_FORMATS,
		},
		.ops = &ia6xx_ops,
		.symmetric_rates = 1,
	},
};

int ia6xx_dai_nr(void)
{
	return ARRAY_SIZE(ia6xx_dai);
}

static int ia6xx_probe(struct snd_soc_codec *codec)
{
	pr_info("ENTER");

	//ia6xx_add_codec_controls(codec);

	return 0;
}

static int ia6xx_remove(struct snd_soc_codec *codec)
{
	return 0;
}

struct snd_soc_codec_driver ia6xx_driver = {
	.probe = ia6xx_probe,
	.remove = ia6xx_remove,
	.suspend = ia6xx_suspend,
	.resume = ia6xx_resume,
	.set_bias_level = ia6xx_set_bias_level,
};

MODULE_DESCRIPTION("Knowles ia6xx ALSA SoC Microphone Driver");
MODULE_LICENSE("GPL");
