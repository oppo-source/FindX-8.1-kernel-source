/*
 * iacore-cdev.h  --  Audience ia6xx character device interface.
 *
 * Copyright 2013 Audience, Inc.
 *
 * Author: Marc Butler <mbutler@audience.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _IACORE_CDEV_H
#define _IACORE_CDEV_H

int streamdev_open(struct iacore_priv *iacore);
int streamdev_release(struct iacore_priv *iacore);
ssize_t streaming_read(struct file *filp, char __user *buf,
			      size_t count, loff_t *f_pos);

/* This interface is used to support development and deployment
 * tasks. It does not replace ALSA as a control interface.
 */

int iacore_cdev_init(struct iacore_priv *iacore);
void iacore_cdev_cleanup(struct iacore_priv *iacore);
void iacore_stop_streaming_thread(struct iacore_priv *iacore);
int streamdev_stop_and_clean_thread(struct iacore_priv *iacore);

#endif
