/*
 * iacore-uart.h  --  Audience ia6xx UART interface
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

#ifndef _IACORE_UART_H
#define _IACORE_UART_H

#define IACORE_UART_SBL_SYNC_WRITE_RETRY 10

enum {
	UART_RATE_1152, UART_RATE_4608,	UART_RATE_9216,	UART_RATE_1kk,
	UART_RATE_1M, UART_RATE_1152k, UART_RATE_2kk,
	UART_RATE_2M, UART_RATE_3kk, UART_RATE_3M,
	UART_RATE_MAX
};

struct iacore_uart_device {
	struct tty_struct *tty;
	struct file *file;
	unsigned int baudrate_sbl;
	unsigned int baudrate_vs;
	int vote_counter;
};

struct iacore_priv;

int iacore_uart_get_sync_response(struct iacore_priv *iacore);
int iacore_uart_clean_rx_fifo(struct iacore_priv *iacore);
int iacore_fix_extra_byte_read(struct iacore_priv *iacore, u32 cmd_to_send,
								u32 *response);

#endif
