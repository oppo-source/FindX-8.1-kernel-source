/**********************************************************************************
* Copyright (c)  2008-2015  Guangdong OPPO Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: Charger IC management module for charger system framework.
*              Manage all charger IC and define abstarct function flow.
* Version    : 1.0
* Date       : 2015-06-22
* Author     : fanhui@PhoneSW.BSP
*            : Fanhong.Kong@ProDrv.CHG
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                          <desc>
* Revision 1.0        2015-06-22       fanhui@PhoneSW.BSP            Created for new architecture
* Revision 1.0        2015-06-22       Fanhong.Kong@ProDrv.CHG       Created for new architecture
***********************************************************************************/

#include "oppo_gauge.h"

static struct oppo_gauge_chip *g_gauge_chip = NULL;

int oppo_gauge_get_batt_mvolts(void)
{
        if (!g_gauge_chip) {
                return 3800;
        } else {
                return g_gauge_chip->gauge_ops->get_battery_mvolts();
        }
}

int oppo_gauge_get_batt_mvolts_2cell_max(void)
{
	if(!g_gauge_chip)
		return 3800;
	else
		return g_gauge_chip->gauge_ops->get_battery_mvolts_2cell_max();
}

int oppo_gauge_get_batt_mvolts_2cell_min(void)
{
	if(!g_gauge_chip)
		return 3800;
	else
		return g_gauge_chip->gauge_ops->get_battery_mvolts_2cell_min();
}

int oppo_gauge_get_batt_temperature(void)
{
        if (!g_gauge_chip) {
                return 250;
        } else {
                return g_gauge_chip->gauge_ops->get_battery_temperature();
        }
}

int oppo_gauge_get_batt_soc(void)
{
        if (!g_gauge_chip) {
                return 50;
        } else {
                return g_gauge_chip->gauge_ops->get_battery_soc();
        }
}

int oppo_gauge_get_batt_current(void)
{
        if (!g_gauge_chip) {
                return 100;
        } else {
                return g_gauge_chip->gauge_ops->get_average_current();
        }
}

int oppo_gauge_get_remaining_capacity(void)
{
        if (!g_gauge_chip) {
                return 0;
        } else {
                return g_gauge_chip->gauge_ops->get_batt_remaining_capacity();
        }
}

int oppo_gauge_get_device_type(void)
{
        if (!g_gauge_chip) {
                return 0;
        } else {
                return g_gauge_chip->device_type;
        }
}

int oppo_gauge_get_batt_fcc(void)
{
        if (!g_gauge_chip) {
                return 0;
        } else {
                return g_gauge_chip->gauge_ops->get_battery_fcc();
        }
}

int oppo_gauge_get_batt_cc(void)
{
        if (!g_gauge_chip) {
                return 0;
        } else {
                return g_gauge_chip->gauge_ops->get_battery_cc();
        }
}

int oppo_gauge_get_batt_soh(void)
{
        if (!g_gauge_chip) {
                return 0;
        } else {
                return g_gauge_chip->gauge_ops->get_battery_soh();
        }
}

bool oppo_gauge_get_batt_authenticate(void)
{
        if (!g_gauge_chip) {
                return false;
        } else {
                return g_gauge_chip->gauge_ops->get_battery_authenticate();
        }
}

void oppo_gauge_set_batt_full(bool full)
{
        if (g_gauge_chip) {
                g_gauge_chip->gauge_ops->set_battery_full(full);
        }
}

bool oppo_gauge_check_chip_is_null(void)
{
        if (!g_gauge_chip) {
                return true;
        } else {
                return false;
        }
}

void oppo_gauge_init(struct oppo_gauge_chip *chip)
{
        g_gauge_chip = chip;
}

int oppo_gauge_get_prev_batt_mvolts(void)
{
	if (!g_gauge_chip)
		return 3800;
	else
		return g_gauge_chip->gauge_ops->get_prev_battery_mvolts();
}

int oppo_gauge_get_prev_batt_mvolts_2cell_max(void)
{
	if(!g_gauge_chip)
		return 3800;
	else
		return g_gauge_chip->gauge_ops->get_prev_battery_mvolts_2cell_max();
}

int oppo_gauge_get_prev_batt_mvolts_2cell_min(void)
{
	if(!g_gauge_chip)
		return 3800;
	else
		return g_gauge_chip->gauge_ops->get_prev_battery_mvolts_2cell_min();
}

int oppo_gauge_get_prev_batt_temperature(void)
{
	if (!g_gauge_chip)
		return 250;
	else
		return g_gauge_chip->gauge_ops->get_prev_battery_temperature();
}

int oppo_gauge_get_prev_batt_soc(void)
{
	if (!g_gauge_chip)
		return 50;
	else
		return g_gauge_chip->gauge_ops->get_prev_battery_soc();
}

int oppo_gauge_get_prev_batt_current(void)
{
	if (!g_gauge_chip)
		return 100;
	else
		return g_gauge_chip->gauge_ops->get_prev_average_current();
}

int oppo_gauge_update_battery_dod0(void)
{
	if (!g_gauge_chip)
		return 0;
	else
		return g_gauge_chip->gauge_ops->update_battery_dod0();
}


int oppo_gauge_update_soc_smooth_parameter(void)
{
	if (!g_gauge_chip)
		return 0;
	else
		return g_gauge_chip->gauge_ops->update_soc_smooth_parameter();
}
