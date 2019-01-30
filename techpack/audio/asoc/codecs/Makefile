snd-soc-wcd9xxx-objs := wcd9xxx-common-v2.o wcd9xxx-resmgr-v2.o \
			wcdcal-hwdep.o wcd9xxx-soc-init.o wcd-dsp-utils.o \
			wcd-dsp-mgr.o audio-ext-clk-up.o
snd-soc-wcd-cpe-objs := wcd_cpe_services.o wcd_cpe_core.o
snd-soc-wsa881x-objs := wsa881x.o wsa881x-tables.o wsa881x-regmap.o wsa881x-temp-sensor.o
snd-soc-wcd-mbhc-objs := wcd-mbhc-v2.o
ifneq (,$(filter $(CONFIG_SND_SOC_WCD_MBHC_LEGACY),y m))
	snd-soc-wcd-mbhc-objs += wcd-mbhc-legacy.o
endif
ifneq (,$(filter $(CONFIG_SND_SOC_WCD_MBHC_ADC),y m))
	snd-soc-wcd-mbhc-objs += wcd-mbhc-adc.o
endif
snd-soc-wcd-spi-objs := wcd-spi.o
snd-soc-wcd9335-objs := wcd9335.o
snd-soc-wcd-cpe-objs := wcd_cpe_services.o wcd_cpe_core.o

obj-$(CONFIG_SND_SOC_WCD9335) += snd-soc-wcd9335.o
obj-$(CONFIG_SND_SOC_WCD_CPE)   += snd-soc-wcd-cpe.o
obj-$(CONFIG_SND_SOC_WCD934X)	+= wcd934x/
obj-$(CONFIG_SND_SOC_SDM660_CDC) += sdm660_cdc/
obj-$(CONFIG_SND_SOC_MSM_SDW) += msm_sdw/

obj-$(CONFIG_SND_SOC_WCD9XXX_V2) += snd-soc-wcd9xxx.o
obj-$(CONFIG_SND_SOC_WCD_MBHC)  += snd-soc-wcd-mbhc.o
obj-$(CONFIG_SND_SOC_WSA881X)	+= snd-soc-wsa881x.o
obj-$(CONFIG_SND_SOC_WCD_SPI)  += snd-soc-wcd-spi.o

#ifdef VENDOR_EDIT
#Zhaoan.Xu@PSW.MM.AudioDriver.SmartPA, 2018/01/06, Add for Max98928
obj-$(CONFIG_SND_SOC_MAX98928)	+= max989xx/
#Zhaoan.Xu@PSW.MM.AudioDriver.Codec.1263116, 2018/01/30, Add for smart mic
obj-$(CONFIG_SND_SOC_IA6XX) += audience/
#endif /* VENDOR_EDIT */
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
# Le.Li@PSW.MM.AudioDriver.HeadsetDet.1272920, 2018/01/22, Add for Audio SW fsa4480
obj-$(CONFIG_SND_SOC_FSA4480)	+= fsa4480/
# Le.Li@PSW.MM.AudioDriver.HeadsetDet, 2018/08/20, Add for Audio SW as6313
obj-$(CONFIG_SND_SOC_AS6313)	+= as6313/
#endif /* VENDOR_EDIT */

snd-soc-msm-stub-objs := msm_stub.o
obj-$(CONFIG_SND_SOC_MSM_STUB)  += snd-soc-msm-stub.o

wcd-core-objs                  := wcd9xxx-rst.o wcd9xxx-core-init.o \
				wcd9xxx-core.o wcd9xxx-irq.o \
				wcd9xxx-slimslave.o wcd9xxx-utils.o \
				wcd9335-regmap.o wcd9335-tables.o \
				msm-cdc-pinctrl.o msm-cdc-supply.o
wcd-core-objs	+= wcd934x/wcd934x-regmap.o
wcd-core-objs	+= wcd934x/wcd934x-tables.o

obj-$(CONFIG_WCD9XXX_CODEC_CORE) += wcd-core.o
obj-$(CONFIG_SND_SOC_MSM_HDMI_CODEC_RX) += msm_hdmi_codec_rx.o
