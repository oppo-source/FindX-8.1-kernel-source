ifeq ($(CONFIG_OPPO_CHARGER_MTK),y)
ifeq ($(CONFIG_OPPO_CHARGER_6750T),y)
else ifeq ($(CONFIG_OPPO_CHARGER_MTK6763),y)
else ifeq ($(CONFIG_OPPO_CHARGER_MTK6771),y)
else
include $(srctree)/drivers/misc/mediatek/Makefile.custom
endif

endif
obj-y	+= oppo_bq27541.o
#obj-y	+= oppo_sha1_hmac.o
