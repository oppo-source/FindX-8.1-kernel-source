/**********************************************************************************
* Copyright (c)  2008-2015  Guangdong OPPO Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: Charger IC management module for charger system framework.
*                 Manage all charger IC and define abstarct function flow.
* Version    : 1.0
* Date       : 2015-06-22
* Author     : fanhui@PhoneSW.BSP
*            : Fanhong.Kong@ProDrv.CHG
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                                 <desc>
* Revision 1.0        2015-06-22        fanhui@PhoneSW.BSP                 Created for new architecture
* Revision 1.0        2015-06-22        Fanhong.Kong@ProDrv.CHG            Created for new architecture
* Revision 2.0        2018-04-14        Fanhong.Kong@ProDrv.CHG            Upgrade for SVOOC
***********************************************************************************/

#ifndef _OPPO_GAUGE_H_
#define _OPPO_GAUGE_H_

#include <linux/i2c.h>
#include <linux/power_supply.h>

struct oppo_gauge_chip {
        struct i2c_client            *client;
        struct device                *dev;
        struct oppo_gauge_operations *gauge_ops;
        struct power_supply          *batt_psy;
        int                          device_type;
};

struct oppo_gauge_operations {
        int (*get_battery_mvolts)(void);
        int (*get_battery_temperature)(void);
        int (*get_batt_remaining_capacity)(void);
        int (*get_battery_soc)(void);
        int (*get_average_current)(void);
        int (*get_battery_fcc)(void);
        int (*get_battery_cc)(void);
        int (*get_battery_soh)(void);
        bool (*get_battery_authenticate)(void);
        void (*set_battery_full)(bool);
        int (*get_prev_battery_mvolts) (void);
        int (*get_prev_battery_temperature) (void);
        int (*get_prev_battery_soc) (void);
        int (*get_prev_average_current) (void);
        int (*get_battery_mvolts_2cell_max) (void);
        int (*get_battery_mvolts_2cell_min) (void);
        int (*get_prev_battery_mvolts_2cell_max) (void);
        int (*get_prev_battery_mvolts_2cell_min) (void);
		int (*update_battery_dod0) (void);
		int (*update_soc_smooth_parameter) (void);
};

/****************************************
 * oppo_gauge_init - initialize oppo_gauge_chip
 * @chip: pointer to the oppo_gauge_cip
 * @clinet: i2c client of the chip
 *
 * Returns: 0 - success; -1/errno - failed
 ****************************************/
void oppo_gauge_init(struct oppo_gauge_chip *chip);

int oppo_gauge_get_batt_mvolts(void);
int oppo_gauge_get_batt_mvolts_2cell_max(void);
int oppo_gauge_get_batt_mvolts_2cell_min(void);

int oppo_gauge_get_batt_temperature(void);
int oppo_gauge_get_batt_soc(void);
int oppo_gauge_get_batt_current(void);
int oppo_gauge_get_remaining_capacity(void);
int oppo_gauge_get_device_type(void);

int oppo_gauge_get_batt_fcc(void);

int oppo_gauge_get_batt_cc(void);
int oppo_gauge_get_batt_soh(void);
bool oppo_gauge_get_batt_authenticate(void);
void oppo_gauge_set_batt_full(bool);
bool oppo_gauge_check_chip_is_null(void);

int oppo_gauge_get_prev_batt_mvolts(void);
int oppo_gauge_get_prev_batt_mvolts_2cell_max(void);
int oppo_gauge_get_prev_batt_mvolts_2cell_min(void);
int oppo_gauge_get_prev_batt_temperature(void);
int oppo_gauge_get_prev_batt_soc(void);
int oppo_gauge_get_prev_batt_current(void);
int oppo_gauge_update_battery_dod0(void);
int oppo_gauge_update_soc_smooth_parameter(void);

#if defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
extern int oppo_fuelgauged_init_flag;
extern struct power_supply	*oppo_batt_psy;
extern struct power_supply	*oppo_usb_psy;
extern struct power_supply	*oppo_ac_psy;
#endif /* CONFIG_OPPO_CHARGER_MTK6763 */
#endif /* _OPPO_GAUGE_H */
