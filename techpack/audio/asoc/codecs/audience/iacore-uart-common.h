/*
 * iacore-uart-common.h  --  UART interface for Audience ia6xx chips
 *
 * Copyright 2013 Audience, Inc.
 *
 * Author: Matt Lupfer <mlupfer@cardinalpeak.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IACORE_UART_COMMON_H
#define _IACORE_UART_COMMON_H

/* UART Baud rates */

#define IA_UARTHS_BAUD_RATE_460K  0x00 /* 460.8 kHz */
#define IA_UARTHS_BAUD_RATE_921K  0x01 /* 921.6 kHz */
#define IA_UARTHS_BAUD_RATE_1000K 0x02 /* 1.000 MHz */
#define IA_UARTHS_BAUD_RATE_1024K 0x03 /* 1.024 MHz */
#define IA_UARTHS_BAUD_RATE_1152K 0x04 /* 1.152 MHz */
#define IA_UARTHS_BAUD_RATE_2000K 0x05 /* 2.000 MHz */
#define IA_UARTHS_BAUD_RATE_2048K 0x06 /* 2.048 MHz */
#define IA_UARTHS_BAUD_RATE_3000K 0x07 /* 3.000 MHz */
#define IA_UARTHS_BAUD_RATE_3072K 0x08 /* 3.072 MHz */

#if (defined(CONFIG_ARCH_MSM) ||  defined(CONFIG_ARCH_QCOM))

#if (defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_SND_SOC_SDM845))
/* When using LS UART this should be set to /dev/ttyHSL1
 * When using HS UART this should be set to /dev/ttyHS1
 */
	#define UART_TTY_DEVICE_NODE		"/dev/ttyHS1"
#else
	#define UART_TTY_DEVICE_NODE		"/dev/ttyHS1"
#endif

#elif defined(CONFIG_ARCH_OMAP)
#define UART_TTY_DEVICE_NODE		"/dev/ttyO3"
#elif defined(CONFIG_ARCH_EXYNOS)
#define UART_TTY_DEVICE_NODE		"/dev/ttySAC1"
#elif defined(CONFIG_ARCH_MXC)
#define UART_TTY_DEVICE_NODE		"/dev/ttymxc1"
#elif defined(CONFIG_ARCH_MT6797)
#define UART_TTY_DEVICE_NODE		"/dev/ttyMT1"
#endif

#define UART_TTY_BAUD_RATE_28_8_K	28800
#define UART_TTY_BAUD_RATE_115_2_K	115200
#define UART_TTY_BAUD_RATE_460_8_K	460800
#define UART_TTY_BAUD_RATE_2_048_M	2048000
#define UART_TTY_BAUD_RATE_2_M		2000000
#define UART_TTY_BAUD_RATE_3_M		3000000

#define UART_TTY_STOP_BITS		2
#define UART_TTY_WRITE_SZ		512

#define IACORE_SBL_SYNC_CMD		0x00
#define IACORE_SBL_SYNC_ACK		IACORE_SBL_SYNC_CMD
#define IACORE_SBL_AUTO_DET_CMD		0xB7
#define IACORE_SBL_AUTO_DET_ACK		IACORE_SBL_AUTO_DET_CMD
#define IACORE_SBL_BOOT_CMD		0x01
#define IACORE_SBL_BOOT_ACK		IACORE_SBL_BOOT_CMD
#define IACORE_SBL_FW_ACK		0x02

#define IACORE_SBL_SET_RATE_REQ_CMD	0x8019
#define MAX_EAGAIN_RETRY		10
#define MAX_EAGAIN_STREAM_RETRY		50
#define MAX_READ_FAILURE_RETRY		2
#define EAGAIN_RETRY_DELAY		IA_DELAY_1MS
#define IA_TTY_BUF_AVAIL_WAIT_DELAY	IA_DELAY_2MS
#define IA_TTY_WAIT_TIMEOUT		500

#define UART_OP_CLK_ON		0x5441
#define UART_OP_CLK_OFF		0x5442
#define UART_OP_CLK_STATE	0x5443

u32 iacore_cpu_to_uart(struct iacore_priv *iacore, u32 resp);
u32 iacore_uart_to_cpu(struct iacore_priv *iacore, u32 resp);
int iacore_uart_read(struct iacore_priv *iacore, void *buf, int len);
int iacore_uart_read_internal(struct iacore_priv *iacore, void *buf, int len);
int iacore_uart_write(struct iacore_priv *iacore, const void *buf, int len);
int iacore_uart_cmd(struct iacore_priv *iacore, u32 cmd, u32 *resp);
int iacore_configure_tty(struct tty_struct *tty, u32 bps, int stop);
int iacore_uart_clock_control(struct iacore_priv *iacore, int cmd);
int iacore_uart_open(struct iacore_priv *iacore);
int iacore_uart_close(struct iacore_priv *iacore);
int iacore_raw_configure_tty(struct iacore_priv *iacore, u32 bps);

int iacore_uart_calibration(struct iacore_priv *iacore);

extern struct ia_stream_device ia_uart_streamdev;

#endif
