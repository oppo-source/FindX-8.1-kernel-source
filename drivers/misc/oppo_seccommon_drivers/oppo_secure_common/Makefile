ifeq ($(TARGET_PRODUCT),sdm845)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=845
else ifeq ($(TARGET_PRODUCT),sdm670)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=670
else ifeq ($(TARGET_PRODUCT),sdm710)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=710
else ifeq ($(TARGET_PRODUCT),sdm660_64)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=660
else ifneq ($(filter msm8953_64 S88051AA1, $(TARGET_PRODUCT)),)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=8953
else ifeq ($(TARGET_PRODUCT),msm8952_64)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=8976
else ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6771)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6771
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6763)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6763
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=0
endif

ccflags-y += $(DEFS_PLATFORM)
obj-y     +=  oppo_secure_common.o

