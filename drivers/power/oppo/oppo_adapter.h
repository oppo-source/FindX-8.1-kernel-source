/**********************************************************************************
* Copyright (c)  2008-2015  Guangdong OPPO Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: Charger IC management module for charger system framework.
*                          Manage all charger IC and define abstarct function flow.
**
** Version: 1.0
** Date created: 21:03:46, 05/04/2012
** Author: Fuchun.Liao@BSP.CHG.Basic
**
** --------------------------- Revision History: ------------------------------------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0     2015-06-22        Fuchun.Liao@BSP.CHG.Basic         Created for new architecture from R9
* Revision 1.1     2018-04-12        Fanhong.Kong@BSP.CHG.Basic        divided for svooc from oppo_vooc.c 
************************************************************************************************************/

//#ifndef _OPPO_ADAPTER_H_
//#define _OPPO_ADAPTER_H_

#include <linux/workqueue.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
#include <linux/wakelock.h>
#endif
#include <linux/timer.h>
#include <linux/slab.h>
#include <soc/oppo/device_info.h>
#include <linux/firmware.h>

enum {
        ADAPTER_FW_UPDATE_NONE,
        ADAPTER_FW_NEED_UPDATE,
        ADAPTER_FW_UPDATE_SUCCESS,
        ADAPTER_FW_UPDATE_FAIL,
};


struct oppo_adapter_chip {
		struct delayed_work              adapter_update_work;
        const struct    oppo_adapter_operations    *vops;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
        struct wake_lock                 adapter_wake_lock;
#else
        struct wakeup_source			*adapter_ws;
#endif
};

struct oppo_adapter_operations {
        bool (*adapter_update)(unsigned long tx_pin, unsigned long rx_pin);
};


void oppo_adapter_fw_update(void);
void oppo_vooc_reset_mcu(void);
void oppo_vooc_set_ap_clk_high(void);
int oppo_vooc_get_vooc_switch_val(void);
bool oppo_vooc_check_chip_is_null(void);
void oppo_adapter_init(struct oppo_adapter_chip *chip);
bool oppo_adapter_check_chip_is_null(void);

//#endif /* _OPPO_ADAPTER_H_ */
