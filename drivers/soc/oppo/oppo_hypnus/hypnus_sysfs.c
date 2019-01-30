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

#include <linux/sysfs.h>
#include "hypnus.h"
#include "hypnus_sysfs.h"

/* sysfs */
static ssize_t show_version(struct hypnus_data *hypdata, char *buf)
{
	return 0;
}

static ssize_t store_not_preferred(struct hypnus_data *hypdata,
				   const char *buf, size_t count)
{
	return count;
}

static ssize_t show_not_preferred(struct hypnus_data *hypdata, char *buf)
{
	return 0;
}

struct hypnus_attr {
	struct attribute attr;
	ssize_t (*show)(struct hypnus_data *, char *);
	ssize_t (*store)(struct hypnus_data *, const char *, size_t count);
};

#define hypnus_attr_ro(_name)			\
static struct hypnus_attr _name =		\
__ATTR(_name, 0444, show_##_name, NULL)

#define hypnus_attr_wo(_name)			\
static struct hypnus_attr _name =		\
__ATTR(_name, 0220, NULL, store_##_name)

#define hypnus_attr_rw(_name)			\
static struct hypnus_attr _name =		\
__ATTR(_name, 0644, show_##_name, store_##_name)

hypnus_attr_ro(version);
hypnus_attr_rw(not_preferred);

static struct attribute *attrs[] = {
	&version.attr,
	&not_preferred.attr,
	NULL
};

#define to_hypnus_data(k) container_of(k, struct hypnus_data, kobj)
#define to_attr(a) container_of(a, struct hypnus_attr, attr)

static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct hypnus_data *hypdata = to_hypnus_data(kobj);
	struct hypnus_attr *cattr = to_attr(attr);
	ssize_t ret = -EIO;

	if (cattr->show)
		ret = cattr->show(hypdata, buf);

	return ret;
}

static ssize_t store(struct kobject *kobj, struct attribute *attr,
		     const char *buf, size_t count)
{
	struct hypnus_data *hypdata = to_hypnus_data(kobj);
	struct hypnus_attr *cattr = to_attr(attr);
	ssize_t ret = -EIO;

	if (cattr->store)
		ret = cattr->store(hypdata, buf, count);

	return ret;
}

static const struct sysfs_ops sysfs_ops = {
	.show = show,
	.store = store,
};

struct kobj_type ktype_hypnus = {
	.sysfs_ops = &sysfs_ops,
	.default_attrs = attrs,
};

int hypnus_sysfs_init(struct hypnus_data *hypdata)
{
	int ret;

	kobject_init(&hypdata->kobj, &ktype_hypnus);

	ret = kobject_add(&hypdata->kobj, kernel_kobj, "hypnus");
	if (ret) {
		pr_err("[%s] failed to create a sysfs kobject\n", __func__);
		goto err_kobj_add;
	}

	return ret;

err_kobj_add:
	kobject_put(&hypdata->kobj);
	return ret;
}

void hypnus_sysfs_remove(struct hypnus_data *hypdata)
{
	kobject_put(&hypdata->kobj);
}

