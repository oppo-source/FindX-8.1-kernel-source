/*
 * iacore-spi.h  --  Audience ia6xx SPI interface
 *
 * Copyright 2011-2016 Audience, Inc.
 *
 * Author: Hemal Meghpara <hmeghpara@audience.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IACORE_SPI_H
#define _IACORE_SPI_H

#define IA_SPI_BOOT_CMD		0x00000001
#define IA_SPI_BOOT_ACK		0x00000001
#define IA_SPI_SYNC_CMD		0xB7B7B7B7
#define IA_SPI_SYNC_ACK		0xB7B7B7B7

#if defined(CONFIG_ARCH_MSM)

#ifdef CONFIG_ARCH_MSM8996
#define SPI_TRANSFER_MSB_TO_LSB		0
#else
#define SPI_TRANSFER_MSB_TO_LSB		1
#endif

#elif defined(CONFIG_ARCH_MXC)
#define SPI_TRANSFER_MSB_TO_LSB		0

#elif defined(CONFIG_ARCH_EXYNOS)
#define SPI_TRANSFER_MSB_TO_LSB		/*TODO figure out*/

#elif defined(CONFIG_ARCH_MT6797)
#define SPI_TRANSFER_MSB_TO_LSB		1

#endif

/* This is obtained after discussion with FW team.*/
#define IACORE_SPI_PACKET_LEN 256

#ifdef CONFIG_SND_SOC_IA_SPI_WRITE_DMA_MODE
#define IA_SPI_DMA_MIN_BYTES	512
#endif

#ifdef CONFIG_ARCH_MT6797
#undef IA_SPI_DMA_MIN_BYTES
#define IA_SPI_DMA_MIN_BYTES	1024
#endif

extern int iacore_spi_plat_endian(void *data_buff, u32 len);
int iacore_spi_setup(struct iacore_priv *iacore, u32 speed);

#endif
