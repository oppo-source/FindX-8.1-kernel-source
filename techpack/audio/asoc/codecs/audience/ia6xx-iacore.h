/*
 * ia6xx-iacore.h  --  Audience ia6xx ALSA SoC Audio driver
 *
 * Copyright 2011 Audience, Inc.
 *
 * Author:
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IA6XX_IACORE_H
#define _IA6XX_IACORE_H

#include "iacore.h"

/*
 * Device parameter command codes
 */
#define IA6XX_DEV_PARAM_OFFSET		0x2000
#define IA6XX_GET_DEV_PARAM		0x800B
#define IA6XX_SET_DEV_PARAM_ID		0x900C
#define IA6XX_SET_DEV_PARAM		0x900D
#define IA6XX_I2S_AUD_DATA_PDM_TO_PCM	0x0A09
#define IA6XX_I2S_AUD_DATA_MASTER	0x0001
#define IA6XX_I2S_MASTER_MODE_HIGH	0x1700

#define IA6XX_I2SM_16K_16B_2CH		0x000A
#define IA6XX_I2SM_48K_16B_2CH		0x0013

/*
 * Algoithm parameter command codes
 */
#define IA6XX_GET_ALGO_PARAM		0x8016
#define IA6XX_SET_ALGO_PARAM_ID		0x9017
#define IA6XX_SET_ALGO_PARAM		0x9018

/*
 * addresses
 */
enum {
	IA6XX_RUNTIME_PM,
	IA6XX_ALGO_PROCESSING,
	IA6XX_CHANGE_STATUS,
	IA6XX_FW_FIRST_CHAR,
	IA6XX_FW_NEXT_CHAR,
	IA6XX_EVENT_RESPONSE,
	IA6XX_VOICE_SENSE_ENABLE,
	IA6XX_VOICE_SENSE_SET_KEYWORD,
	IA6XX_VOICE_SENSE_EVENT,
	IA6XX_VOICE_SENSE_TRAINING_MODE,
	IA6XX_VOICE_SENSE_DETECTION_SENSITIVITY,
	IA6XX_VOICE_ACTIVITY_DETECTION_SENSITIVITY,
	IA6XX_VOICE_SENSE_TRAINING_RECORD,
	IA6XX_VOICE_SENSE_TRAINING_STATUS,
	IA6XX_VOICE_SENSE_TRAINING_MODEL_LENGTH,
	IA6XX_VOICE_SENSE_DEMO_ENABLE,
	IA6XX_VS_STORED_KEYWORD,
	IA6XX_VS_INT_OSC_MEASURE_START,
	IA6XX_VS_INT_OSC_MEASURE_STATUS,
	IA6XX_API_ADDR_MAX,
};

enum {
	I2S_SLAVE_16K_16B = 1,
	I2S_MASTER_16K_16B = 2,
	I2S_SLAVE_48K_16B = 3,
	I2S_MASTER_48K_16B = 4,
	LL_I2S_SLAVE_16K_16B = 5,
	LL_I2S_SLAVE_16K_32B = 6,
	LL_I2S_MASTER_16K_16B = 7,
	LL_I2S_SLAVE_48K_16B = 8,
	LL_I2S_SLAVE_48K_32B = 9,
	LL_I2S_MASTER_48K_16B = 10,
	I2S_SLAVE_48K_16B_BARGEIN = 11,
	I2S_SLAVE_48K_32B_BARGEIN = 12
};

extern struct snd_soc_codec_driver ia6xx_driver;
extern struct snd_soc_dai_driver ia6xx_dai[];

enum {
	MONO = 1,
	STEREO = 2,
};

enum {
	IA6XX_SLAVE,
	IA6XX_MASTER,
};

#define SAMPLE_16000_KHZ		16000
#define SAMPLE_48000_KHZ		48000

/* Base name used by character devices. */
#define IA6XX_CDEV_NAME "adnc"

extern int ia6xx_core_probe(struct device *dev);
int ia6xx_core_remove(struct device *dev);

int ia6xx_set_streaming_burst_unlocked(struct iacore_priv *iacore, int value);
int iacore_i2s_route_config_unlocked(struct iacore_priv *iacore, int value);
int iacore_i2s_master_config(struct iacore_priv *iacore);
int iacore_i2s_rate_channel_config(struct iacore_priv *iacore, int value);
//int ia6xx_add_codec_controls(struct snd_soc_codec *codec);

#endif /* _IA6XX_IACORE_H */
