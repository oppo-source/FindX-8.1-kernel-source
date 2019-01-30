/************************************************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_proximity_power.h
**
** Description:
**      Definitions for m1120 motor driver ic proximity_power.
**
** Version: 1.0
** Date created: 2018/01/14,20:27
** Author: Fei.Mo@PSW.BSP.Sensor
**
** --------------------------- Revision History: -------------------------------------
* <version>		<date>		<author>		<desc>
* Revision 1.0		2018/01/14	Fei.Mo@PSW.BSP.Sensor	Created
**************************************************************************************/
#ifndef __OPPO_proximity_power_H__
#define __OPPO_proximity_power_H__

enum {
	GPIO_LOW = 0,
	GPIO_HIGH
};

struct oppo_proximity_chip {
	struct device	*dev;
	struct pinctrl *pctrl;
	struct pinctrl_state *power_state;
	unsigned int power_gpio;
};

#endif // __OPPO_proximity_power_H__

