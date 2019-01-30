/*
 * Copyright (C) 2015-2018 OPPO, Inc.
 * Author: Chuck Huang <huangcheng-m@oppo.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __HYPNUS_DEV_H__
#define __HYPNUS_DEV_H__

#include <linux/mutex.h>
#include <asm-generic/ioctl.h>
#include "hypnus.h"


int hypnus_dev_init(struct hypnus_data *hypdata);
int hypnus_dev_uninit(struct hypnus_data *hypdata);


/* ioctl part */

#define HYPNUS_IOCTL_FUNC(_cmd, _func) \
	[_IOC_NR((_cmd))] = { .cmd = (_cmd), .func = (_func) }

struct hypnus_ioctl {
	unsigned int cmd;
	long (*func)(struct hypnus_data *, unsigned int, void *);
};

#endif
