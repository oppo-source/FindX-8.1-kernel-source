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

#ifndef __HYPNUS_SYSFS_H__
#define __HYPNUS_SYSFS_H__

#include <linux/kobject.h>

extern struct kobj_type ktype_hypnus;

extern int hypnus_sysfs_init(struct hypnus_data *hypdata);
extern void hypnus_sysfs_remove(struct hypnus_data *hypdata);

#endif
