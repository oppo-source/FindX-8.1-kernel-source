/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef VENDOR_EDIT
/*Zhaoan.Xu@PSW.MM.AudioDriver.SmartPA, 2018/01/18, Add for Max98928*/
#define VENDOR_EDIT
#endif /* VENDOR_EDIT */

#define CONFIG_PINCTRL_WCD 1
#define CONFIG_SND_SOC_WCD934X 1
#define CONFIG_SND_SOC_WCD9XXX_V2 1
#define CONFIG_SND_SOC_WCD_CPE 1
#define CONFIG_SND_SOC_WCD_MBHC 1
#define CONFIG_SND_SOC_WSA881X 1
#define CONFIG_SND_SOC_WCD_SPI 1
#define CONFIG_SND_SOC_WCD934X_MBHC 1
#define CONFIG_SND_SOC_WCD934X_DSD 1
#define CONFIG_MSM_QDSP6V2_CODECS 1
#define CONFIG_MSM_ULTRASOUND 1
#define CONFIG_MSM_QDSP6_APRV2_GLINK 1
#define CONFIG_SND_SOC_MSM_QDSP6V2_INTF 1
#define CONFIG_MSM_ADSP_LOADER 1
#define CONFIG_REGMAP_SWR 1
#define CONFIG_MSM_QDSP6_SSR 1
#define CONFIG_MSM_QDSP6_PDR 1
#define CONFIG_MSM_QDSP6_NOTIFIER 1
#define CONFIG_SND_SOC_MSM_HOSTLESS_PCM 1
#define CONFIG_SND_SOC_SDM845 1
#define CONFIG_MSM_GLINK_SPI_XPRT 1
#define CONFIG_SOUNDWIRE 1
#define CONFIG_SOUNDWIRE_WCD_CTRL 1
#define CONFIG_SND_SOC_WCD_MBHC_ADC 1
#define CONFIG_SND_SOC_QDSP6V2 1
#define CONFIG_MSM_CDC_PINCTRL 1
#define CONFIG_QTI_PP 1
#define CONFIG_SND_HWDEP 1
#define CONFIG_DTS_EAGLE 1
#define CONFIG_DOLBY_DS2 1
#define CONFIG_DOLBY_LICENSE 1
#define CONFIG_DTS_SRS_TM 1
#define CONFIG_WCD9XXX_CODEC_CORE 1
#define CONFIG_SND_SOC_MSM_STUB 1
#define CONFIG_WCD_DSP_GLINK 1
#define CONFIG_MSM_AVTIMER 1
#define CONFIG_SND_SOC_MSM_HDMI_CODEC_RX 1
#ifdef VENDOR_EDIT
/*Zhaoan.Xu@PSW.MM.AudioDriver.SmartPA, 2018/01/08, Add for Max98928*/
#define CONFIG_SND_SOC_MAX98928 1
/*Zhaoan.Xu@PSW.MM.AudioDriver.Codec.1263116, 2018/01/30, Add for smart mic*/
#define CONFIG_SND_SOC_AUDIENCE_ALL 1
#define CONFIG_SND_SOC_IA6XX 1
#define CONFIG_SND_SOC_IA_UART 1
#define CONFIG_SND_SOC_IA_I2S 1
#define CONFIG_SND_SOC_IA_I2S_HOST 1
#define CONFIG_SND_SOC_IA_BARGEIN 1
#define CONFIG_SND_SOC_IA_FW_RECOVERY 1
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
/*Le.Li@PSW.MM.AudioDriver.Mathine.1272920, 2018/01/22, Add for FSA4480 audio switch*/
#define CONFIG_SND_SOC_FSA4480 1
#define CONFIG_SND_SOC_AS6313 1
#endif /* VENDOR_EDIT */
