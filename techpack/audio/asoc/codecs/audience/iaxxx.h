/*
 * iaxxx.h - header for iaxxx platform data
 *
 * Copyright (C) 2011-2012 Audience, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#ifndef __IAXXX_H__
#define __IAXXX_H__

#define pr_fmt(fmt) "iacore: %s(): %d: " fmt, __func__, __LINE__
#include <linux/regulator/consumer.h>

#define CONFIG_SND_SOC_IA_I2S_SPL_PDM_MODE 1
#define CONFIG_SND_SOC_IA_UART_SBL_BAUD 115200
#define CONFIG_SND_SOC_IA_UART_VS_BAUD 2000000
#define CONFIG_SND_SOC_IA_UARTHS_BAUD 2000000
/*
 * IRQ type
 */
enum {
	IA_DISABLED,
	IA_ACTIVE_LOW,
	IA_ACTIVE_HIGH,
	IA_FALLING_EDGE,
	IA_RISING_EDGE,
};

struct iaxxx_platform_data {
	int	wakeup_gpio;
	int	irq_pin;
	int	spi_speed;
	int	ldo_en_pin;
};

#endif /* __IAXXX_H__ */
