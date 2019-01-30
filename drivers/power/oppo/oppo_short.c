/**********************************************************************************
* Copyright (c)  2017-2019  Guangdong OPPO Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: For short circuit battery check
* Version   : 1.0
* Date      : 2017-10-01
* Author    : SJC@PhoneSW.BSP		   	
* ------------------------------ Revision History: --------------------------------
* <version>       <date>        	<author>              		<desc>
* Revision 1.0    2017-10-01  	SJC@PhoneSW.BSP    		Created for new architecture
***********************************************************************************/
#include <linux/delay.h>
#include <linux/power_supply.h>	
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
#include <mach/mt_boot.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_gpio.h>
#endif
#include <linux/slab.h>
#else
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/spmi.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/rtc.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/batterydata-lib.h>
#include <linux/of_batterydata.h>
#include <linux/msm_bcl.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <soc/oppo/boot_mode.h>
#endif

#include "oppo_charger.h"
#include "oppo_short.h"
#include "oppo_vooc.h"

int oppo_short_c_batt_err_code_init(void)
{
	return SHORT_C_BATT_STATUS__NORMAL;
}

int oppo_short_c_batt_chg_switch_init(void)
{
	return SHORT_C_BATT_SW_STATUS__OFF;
}

int oppo_short_c_batt_feature_sw_status_init(void)
{
	return SHORT_C_BATT_FEATURE_SW_STATUS__ON;
}

int oppo_short_c_batt_feature_hw_status_init(void)
{
	return SHORT_C_BATT_FEATURE_HW_STATUS__ON;
}

bool oppo_short_c_batt_is_prohibit_chg(struct oppo_chg_chip *chip)
{
	return false;
}

bool oppo_short_c_batt_is_disable_rechg(struct oppo_chg_chip *chip)
{
	return false;
}

bool oppo_short_c_batt_get_cv_status(struct oppo_chg_chip *chip)
{
	return false;
}

void oppo_short_c_batt_update_change(struct oppo_chg_chip *chip, int update_value)
{
	return;
}

void oppo_chg_short_c_battery_check(struct oppo_chg_chip *chip)
{
	return;
}
