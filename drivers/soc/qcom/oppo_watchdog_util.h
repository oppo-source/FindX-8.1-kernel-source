/**********************************************************************************
* Copyright (c)  2008-2016  Guangdong OPPO Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: Provide some utils for watchdog to enhance log and action after wdt
* Version   : 1.0
* Date      : 2016-06-22
* Author    : fanhui@PhoneSW.BSP
* ------------------------------ Revision History: --------------------------------
* <version>       <date>        <author>		<desc>
* Revision 1.0    2016-06-22    fanhui@PhoneSW.BSP	Created file
***********************************************************************************/

#ifndef _OPPO_WATCHDOG_UTIL_H_
#define _OPPO_WATCHDOG_UTIL_H_

#include <linux/cpu.h>
#include <linux/workqueue.h>

extern void dump_cpu_online_mask(void);
extern void get_cpu_ping_mask(cpumask_t *pmask);
extern void print_smp_call_cpu(void);
extern void dump_wdog_cpu(struct task_struct *w_task);
extern int try_to_recover_pending(struct task_struct *w_task);
extern void reset_recovery_tried(void);
int init_oppo_watchlog(void);

#endif /*_OPPO_WATCHDOG_UTIL_H_*/
