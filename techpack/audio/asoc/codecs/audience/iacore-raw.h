/*
 * iacore-raw.h  -- iacore raw read/write interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IACORE_RAW_H
#define _IACORE_RAW_H

#define IA_CONFIGURE_BUS_SPEED	_IO('T', 0x011)
#define IA_POWER_CTRL		_IO('T', 0x012)
#define IA_WAKEUP		_IO('T', 0x013)
#define IA_CHANGE_MODE		_IO('T', 0x014) /* host mode/proxy mode */
#define IA_ACTIVE_BUS_CONFIG	_IO('T', 0x015)
#define IA_ENABLE_DISABLE_IRQ	_IO('T', 0x016)
#define IA_ENABLE_BUFF_READ	_IO('T', 0x017)
#define IA_DISABLE_BUFF_READ	_IO('T', 0x018)

enum {
	IA_RAW_POWER_OFF = 0,
	IA_RAW_POWER_ON,
};

enum {
	IA_RAW_DISABLE_IRQ = 0,
	IA_RAW_ENABLE_IRQ,
};

enum {
	IA_RAW_PROXY_MODE = 0,
	IA_RAW_NORMAL_MODE,
};

enum {
	IA_RAW_BUS_UART		= 0x01,
	IA_RAW_BUS_SPI		= 0x02,
	IA_RAW_BUS_I2C		= 0x04,
	IA_RAW_BUS_SWIRE	= 0x08,

	IA_RAW_BUS_PDM	= 0x10,
	IA_RAW_BUS_I2S_CODEC	= 0x20,
	IA_RAW_BUS_I2S_HOST	= 0x40,
	IA_RAW_BUS_I2S_PERF	= 0x80,
};

ssize_t raw_read(struct file *filp, char __user *buf,
					size_t count, loff_t *f_pos);
ssize_t raw_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *f_pos);
long raw_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* _IACORE_RAW_H */
