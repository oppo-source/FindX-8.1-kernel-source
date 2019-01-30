ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
	dtbo-$(CONFIG_ARCH_SDM845) += \
		sdm845-v2.1-mtp-overlay-17127.dtbo 

sdm845-v2.1-mtp-overlay-17127.dtbo-base := sdm845-v2.1-17127.dtb
else
dtb-$(CONFIG_ARCH_SDM845) += 	sdm845-mtp.dtb 
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb
