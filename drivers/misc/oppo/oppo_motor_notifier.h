/************************************************************************************
** Copyright (C), 2008-2018, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_motor_notifier.H
**
** Description:
**      Definitions for motor  notifier.
**
** Version: 1.0
** Date created: 2017/05/05
** Author: Fei.Mo@PSW.BSP.Sensor
**
** --------------------------- Revision History: -------------------------------------------
* <version>	<date>		<author>              			    	<desc>
* Revision 1.0      2018/04/27        Fei.Mo@EXP.BSP.Sensor   			Created
**************************************************************************************/

#ifndef _OPPO_MOTOR_NOTIFIER
#define _OPPO_MOTOR_NOTIFIER

#include <linux/notifier.h>

enum motor_event {
	MOTOR_UP_EVENT = 0,
	MOTOR_DOWN_EVENT,
	MOTOR_BLOCK_EVENT,
};

extern int register_motor_notifier(struct notifier_block *nb);
extern int unregister_motor_notifier(struct notifier_block *nb);
extern int motor_notifier_call_chain(unsigned long val);

#endif //_OPPO_MOTOR_NOTIFIER