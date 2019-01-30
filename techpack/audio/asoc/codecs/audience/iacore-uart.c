/*
 * iacore-uart.c  --  Audience ia6xx UART interface
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

#include "iaxxx.h"

#include <linux/tty.h>

#include "iacore.h"
#include "iacore-uart.h"
#include "iacore-uart-common.h"
#include "iacore-cdev.h"
#ifdef CONFIG_SND_SOC_IA_I2S_PERF
#include "iacore-i2s-perf.h"
#endif
#include "iacore-vs.h"

static int iacore_uart_probe(struct platform_device *dev);
static int iacore_uart_remove(struct platform_device *dev);

static u32 iacore_default_uart_baud = UART_TTY_BAUD_RATE_2_048_M;

static u32 iacore_uarths_baud[UART_RATE_MAX] = {
	115200, 460800, 921600, 1000000, 1024000, 1152000,
	2000000, 2048000, 3000000, 3072000 };

static u32 iacore_uarths_baud_values[UART_RATE_MAX] = {
	0x480, 0x1200, 0x2400, 0x2710, 0x2800, 0x2D00,
	0x4E20, 0x5000, 0x7530, 0x7800,
};

/* read all extra bytes on the UART line */
static void iacore_uart_discard_rxbytes(struct iacore_priv *iacore)
{
	int rc;
	u32 word;

	do {
		rc = iacore_uart_read_internal(iacore, &word, sizeof(word));
		if (!rc)
			pr_debug("reading extra bytes on UART 0x%x\n", word);
	} while (rc && rc > 0);
}

int set_sbl_baud_rate(struct iacore_priv *iacore, u32 sbl_rate)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	char msg[4] = {0};
	char *buf;
	u8 bytes_to_read;
	u8 retry = IACORE_UART_SBL_SYNC_WRITE_RETRY;
	int rc;
	u32 baudrate_change_resp;
	u32 rate_req_cmd, sbl_rate_req_cmd = IACORE_SBL_SET_RATE_REQ_CMD << 16;
	u16 sbl_rate_value = 0;
	u8 i;

	/* Get Baud rate value for the request SBL rate */
	for (i = 0; i < ARRAY_SIZE(iacore_uarths_baud); i++) {
		if (iacore_uarths_baud[i] == sbl_rate)
			sbl_rate_value = iacore_uarths_baud_values[i];
	}

	if (!sbl_rate_value) {
		pr_err("Unsupported SBL rate %d\n", sbl_rate);
		rc = -EINVAL;
		return rc;
	}

	sbl_rate_req_cmd |= sbl_rate_value;
	pr_debug("Sending uart baud rate cmd 0x%x\n", sbl_rate_req_cmd);

	rate_req_cmd = sbl_rate_req_cmd;
	sbl_rate_req_cmd = cpu_to_be32(sbl_rate_req_cmd);
	rc = iacore_uart_write(iacore, &sbl_rate_req_cmd,
			sizeof(sbl_rate_req_cmd));
	if (rc) {
		pr_err("Baud rate setting for UART fail %d\n", rc);
		rc = -EIO;
		return rc;
	}
	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		rc = -EINVAL;
		return rc;
	}

	/* Configure the host side UART speed */
	iacore_configure_tty(iacore_uart->tty, sbl_rate, UART_TTY_STOP_BITS);
	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
	baudrate_change_resp = 0;

	/* Once UART rate request command is sent, SBL will switch to rate
	 * calibration mode and will be waiting for 00 over UART with the new
	 * baud rate. */
	memset(msg, 0, 4);
	do {
		rc = iacore_uart_write(iacore, msg, 4);
	} while (rc && retry--);

	if (rc) {
		rc = -EIO;
		return rc;
	}

	/* SHP-1964: firmware take ~28ms to send the response for set baud rate
	 * command */
	usleep_range(IA_DELAY_35MS, IA_DELAY_35MS + 5000);
	/* Sometimes an extra byte (0x00) is received over UART
	 * which should be discarded.
	 */
	rc = iacore_uart_read(iacore, msg, sizeof(char));
	if (rc < 0) {
		pr_err("Set Rate Request read fail rc = %d\n", rc);
		return rc;
	}

	bytes_to_read = sizeof(baudrate_change_resp);
	if (msg[0] == 0) {
		/* Discard this byte */
		pr_debug("Received extra zero\n");
		buf = &msg[0];
	} else {
		/* 1st byte was valid, now read only remaining bytes */
		bytes_to_read -= sizeof(char);
		buf = &msg[1];
	}

	rc = iacore_uart_read(iacore, buf, bytes_to_read);
	if (rc < 0) {
		pr_err("Set Rate Request read fail rc = %d\n", rc);
		return rc;
	}

	baudrate_change_resp |= *(u32 *)msg;

	update_cmd_history(rate_req_cmd, be32_to_cpu(baudrate_change_resp));

	if (baudrate_change_resp != sbl_rate_req_cmd) {
		pr_err("Invalid response to Rate Request :0x%x, 0x%x\n",
				baudrate_change_resp, sbl_rate_req_cmd);
		return -EINVAL;
	}

	return rc;
}

/*
 * iacore_fix_extra_byte_read() - Read & Verify response status
 *
 * On uart interface, for wakeup, irq use cases, IA610 provides a low to high
 * transition to indicate the event. This may cause the Host UART to see the
 * transition as a valid data "0x00".
 * Example.
 *	To wakeup the chip Host sends a wakeup byte 0x00. To confirm chip
 *	wakeup, Host sends a Sync Byte "0x8000 0x0000". Expected Response to
 *	this  command is "0x8000 0x0000". But due to one extra unexpected byte,
 *	Host UART controller will see the response as "0x0080 0x0000".
 *
 *	one more command "0x806D 0x0000". Response expected "0x806D 0x0000"
 *	Response received "0x0080 0x6D00"
 *
 * This may not be the case always.The best fix is to read one byte from the
 * response.
 *	If the response is "0x00", host will do a fresh 4 byte read to get
 *	proper response.
 *	If the Response != 0x00, then its an expected response & host will read
 *	the remaining 3 bytes
 */
int iacore_fix_extra_byte_read(struct iacore_priv *iacore, u32 cmd_to_send,
								u32 *response)
{
	int rc = 0;
	u32 cmd, rsp = 0;
	char msg[4] = {0};
	int retry = IA_MAX_RETRIES;
	int send_cmd_iter_cnt = IA_MAX_RETRIES;

	pr_info("cmd_to_send 0x%08x\n", cmd_to_send);

	INC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

	rc = iacore_uart_open(iacore);
	if (rc < 0) {
		pr_err("UART open Failed %d\n", rc);
		goto open_failed;
	}

retry_from_1st:
	rsp = 0;
	retry = IA_MAX_RETRIES;
	rc = 0;

	cmd = cpu_to_be32(cmd_to_send);
	rc = iacore_uart_write(iacore, &cmd, sizeof(cmd));
	if (rc) {
		pr_err("write cmd (0x%08x) fail %d\n", cmd_to_send, rc);
		rc = -EIO;
		update_cmd_history(cmd_to_send, rsp);
		goto cmd_exit;
	}

	do {
		usleep_range(IA_RESP_POLL_TOUT,
				IA_RESP_POLL_TOUT + 500);

		memset(msg, 0, 4);
		rc = iacore_uart_read(iacore, msg, 1);
		if (rc) {
			pr_err("read cmd response fail %d\n", rc);
			goto first_read_fail;
		}

		pr_info("msg0 = 0x%x\n", msg[0]);
		if (msg[0] == 0) {
			memset(msg, 0, 4);
			rc = iacore_uart_read(iacore, msg, 4);
		} else {
			rc = iacore_uart_read(iacore, &msg[1], 3);
		}

		rsp = *(u32 *)msg;
		rsp = be32_to_cpu(rsp);
		pr_info("rsp = 0x%08x\n", rsp);
		*response = rsp;

		if (rc) {
			pr_err("read cmd fail %d\n", rc);
		} else if ((rsp & IA_ILLEGAL_CMD) == IA_ILLEGAL_CMD) {
			pr_err("illegal resp 0x%08x for command 0x%08x\n",
				rsp, cmd_to_send);
			rc = -EINVAL;
			update_cmd_history(cmd_to_send, rsp);
			goto cmd_exit;
		} else if (rsp == IA_NOT_READY) {
			pr_err("uart read not ready\n");
			rc = -EBUSY;
		} else 	if ((rsp &  0xFFFF0000) != (cmd_to_send &  0xFFFF0000)) {
			rc = -EINVAL;
			update_cmd_history(cmd_to_send, rsp);
			goto cmd_exit;
		}  else {
			update_cmd_history(cmd_to_send, rsp);
			goto cmd_exit;
		}

first_read_fail:
		--retry;
	} while (retry != 0);

	update_cmd_history(cmd_to_send, rsp);

cmd_exit:

	if (rc) {
		/*
		* Handle genuine illegal command (0xffff 0xXXXX) separately
		*  from illegal response (0xffff 0x0000)
		*/
		if (rsp == ((cmd_to_send >> 16) | IA_ILLEGAL_CMD)) {
			pr_err("Illegal Command 08%x\n", rsp);
			rc = -EINVAL;
			DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
		} else if ((rsp & IA_ILLEGAL_RESP) == IA_ILLEGAL_RESP) {
			/*
			* Calibrate the UART if host receives some unexpected
			* response from the chip
			*/
			rc = iacore_uart_calibration(iacore);

			DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
		} else {
			pr_err("cmd failed after %d retry's\n",
				(IA_MAX_RETRIES - send_cmd_iter_cnt + 1));

			if (send_cmd_iter_cnt--) {
				usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
				goto retry_from_1st;
			}

			DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

			if (rc == -EINTR) {
				pr_err("i/o is broken, skip\n");
			} else {
				IACORE_FW_RECOVERY_FORCED_OFF(iacore);
			}
		}
	}else {
		DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
	}

	iacore_uart_close(iacore);
open_failed:

	return rc;
}

static int iacore_uart_wakeup(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	u32 wakeup_byte = 0x00;
	u32 cmd = IA_SYNC_CMD << 16;
	u32 rsp;
	int ret = 0;

	pr_info("\n");

	ret = iacore_pm_get_sync(iacore);
	if (ret < 0) {
		pr_err("pm_get_sync failed :%d\n", ret);
		goto ret_out;
	}

	ret = iacore_uart_open(iacore);
	if (ret < 0) {
		pr_err("UART open Failed %d\n", ret);
		goto iacore_uart_open_failed;
	}

	ret = iacore_uart_write(iacore, &wakeup_byte, 1);
	if (ret < 0) {
		pr_err("UART wakeup failed:%d\n", ret);
		goto iacore_uart_wakeup_exit;
	}

	/* Wait till wakeup command is completely sent to the chip */
	tty_wait_until_sent(iacore_uart->tty,
				msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));

	update_cmd_history(0xabcd1234, 0x00);

	/* TODO fix the sleep */
	msleep(15);

	/* Flush the RX fifo after Chip wakeup */
	iacore_uart_clean_rx_fifo(iacore);

	rsp = 0;
	cmd = IA_SYNC_CMD << 16;
	ret = iacore_fix_extra_byte_read(iacore, cmd, &rsp);
	if (ret < 0)
		pr_err("Error reading sync event: %d\n", ret);

iacore_uart_wakeup_exit:
	iacore_uart_close(iacore);
iacore_uart_open_failed:
	iacore_pm_put_autosuspend(iacore);
ret_out:
	return ret;
}

int iacore_uart_interface_detect(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	u8 retry = IACORE_UART_SBL_SYNC_WRITE_RETRY;

	u16 sbl_sync_cmd = IACORE_SBL_SYNC_CMD;
	u8 sbl_auto_det_cmd = IACORE_SBL_AUTO_DET_CMD;
	char msg[4] = {0};
	int rc;
	u32 cmd, rsp;

	pr_debug("\n");

	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}

	/* set Host UART speed to bootloader baud */
	rc = iacore_configure_tty(iacore_uart->tty,
			iacore_uart->baudrate_sbl, UART_TTY_STOP_BITS);
	if (rc)
		pr_err("TTY BAUD (%d) configuration failed %d\n",
				iacore_uart->baudrate_sbl, rc);

	iacore_uart_discard_rxbytes(iacore);

	memcpy(msg, (char *)&sbl_sync_cmd, 2);

	/* write SBL SYNC BYTES 0x0000 */
	pr_info("write IACORE_SBL_SYNC_CMD = 0x%04x\n", sbl_sync_cmd);

	do {
		rc = iacore_uart_write(iacore, msg, 2);
	} while (rc && retry--);

	if (rc) {
		rc = -EIO;
		goto uart_interface_det_fail;
	}

	/* TODO fix the sleep */
	usleep_range(IA_DELAY_20MS, IA_DELAY_20MS + 500);

	iacore_uart_discard_rxbytes(iacore);

	/* write SBL AUTODET BYTE 0xB7 */
	pr_info("write IACORE_SBL_AUTO_DET_CMD = 0x%02x\n", sbl_auto_det_cmd);
	rc = iacore_uart_write(iacore, &sbl_auto_det_cmd, 1);
	if (rc) {
		pr_err("uart sbl auto detect cmd write %d\n", rc);
		rc = -EIO;
		goto uart_interface_det_fail;
	}

	/* SBL AUTODET BYTE ACK 0xB7 */
	memset(msg, 0, 4);

	/* TODO fix the sleep */
	usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 50);
	rc = iacore_uart_read(iacore, msg, 1);
	if (msg[0] == 0) {
		pr_err("sbl auto det ack0 = 0x%08x\n", msg[0]);
		memset(msg, 0, 4);
		rc = iacore_uart_read(iacore, msg, 1);
	}
	cmd = sbl_auto_det_cmd;
	rsp = msg[0];
	update_cmd_history(cmd, rsp);

	if (rc) {
		pr_err("uart sbl auto detect cmd read %d\n", rc);
		goto uart_interface_det_fail;
	}

	pr_info("sbl auto det ack0 0x%08x, ack1 0x%08x\n", msg[0], msg[1]);

	if (msg[0] != IACORE_SBL_AUTO_DET_ACK) {
		pr_err("uart sbl auto detect ack pattern fail 0x%08x\n", msg[0]);
		rc = -EIO;
	}

uart_interface_det_fail:
	return rc;
}

int iacore_configure_uart(struct iacore_priv *iacore)
{
	int rc;

	/* set Host UART speed to bootloader baud */
	rc = iacore_uart_interface_detect(iacore);
	if (rc)
		goto iacore_configure_failed;

iacore_configure_failed:
	return rc;
}

#if !defined(CONFIG_SND_SOC_IA_I2S_PERF)
static int iacore_uart_boot_setup(struct iacore_priv *iacore, bool bootsetup)
{
	u8 sbl_boot_cmd = IACORE_SBL_BOOT_CMD, sbl_boot_ack;
#if defined(CONFIG_SND_SOC_IA_I2S)
	u32 aud_data_port_cmd = IA_I2S_AUD_DATA_PORT_CMD;
#endif
	char msg[4] = {0};
	int rc;

	pr_debug("called from %pS\n", __builtin_return_address(0));

	if (bootsetup) {
		rc = iacore_configure_uart(iacore);
		if (rc < 0) {
			pr_err("failed to configure uart for bootup %d\n", rc);
			goto iacore_bootup_failed;
		}

#if defined(CONFIG_SND_SOC_IA_UARTHS_BAUD)
		iacore_default_uart_baud = CONFIG_SND_SOC_IA_UARTHS_BAUD;
#endif

		pr_info("Setting the baud rate to %d\n",
						iacore_default_uart_baud);

		rc = set_sbl_baud_rate(iacore, iacore_default_uart_baud);
		if (rc < 0) {
			pr_err("set_sbl_baud_rate fail %d\n", rc);
			goto iacore_bootup_failed;
		}
	}

#if defined(CONFIG_SND_SOC_IA_I2S)

	aud_data_port_cmd = cpu_to_be32(aud_data_port_cmd);
	rc = iacore_uart_write(iacore, &aud_data_port_cmd,
				sizeof(aud_data_port_cmd));
	if (rc < 0) {
		pr_err("FW aud_data_port_cmd setup write fail %d\n", rc);
	}

	update_cmd_history(be32_to_cpu(aud_data_port_cmd), 0x00);

	/* TODO fix the sleep */
	usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
#endif

	/* SBL BOOT BYTE 0x01 */
	memset(msg, 0, 4);
	pr_info("write IACORE_SBL_BOOT_CMD = 0x%02x\n",  sbl_boot_cmd);
	memcpy(msg, (char *)&sbl_boot_cmd, 1);
	rc = iacore_uart_write(iacore, msg, 1);
	if (rc) {
		pr_err("firmware load failed sbl boot write %d\n", rc);
		rc = -EIO;
		goto iacore_bootup_failed;
	}

	/* SBL BOOT BYTE ACK 0x01 */
	usleep_range(IA_DELAY_20MS, IA_DELAY_20MS + 500);
	memset(msg, 0, 4);
	rc = iacore_uart_read(iacore, msg, 1);
	if (rc) {
		pr_err("firmware load failed boot ack %d\n", rc);
		goto iacore_bootup_failed;
	}
	pr_info("sbl boot ack = 0x%02x\n", msg[0]);
	sbl_boot_ack = *(u32 *)msg;
	update_cmd_history(sbl_boot_cmd, msg[0]);

	if (msg[0] != IACORE_SBL_BOOT_ACK) {
		pr_err("firmware load failed boot ack pattern 0x%08x\n", msg[0]);
		rc = -EIO;
		goto iacore_bootup_failed;
	}
	rc = 0;

iacore_bootup_failed:
	return rc;
}

int iacore_uart_get_sync_response(struct iacore_priv *iacore)
{
	int rc = 0;
	u32 sync_cmd = (IA_SYNC_CMD << 16) | IA_SYNC_POLLING;
	u32 sync_ack;
	int sync_retry = IA_SYNC_MAX_RETRY;
	struct iacore_uart_device *iacore_uart = iacore->dev_data;

	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}

	/* sometimes ia6xx chip sends success in second sync command */
	do {
		/* Since we now use uart rx for irq, make sure uart rx fifo
		 * is empty. Do this by discarding extra bytes if present
		 * from the UART rx fifo.
		 */
		iacore_uart_discard_rxbytes(iacore);

		/* Discard extra bytes from iacore after streaming.
		 * Host gets extra bytes after stop streaming.
		 */
		rc = tty_perform_flush(iacore_uart->tty, TCIOFLUSH);
		if (rc)
			pr_err("TTY buffer Flush failed %d\n", rc);

		pr_info("write IA_SYNC_CMD = 0x%08x\n", sync_cmd);
		rc = iacore_uart_cmd(iacore, sync_cmd, &sync_ack);
		if (rc) {
			pr_err("failed sync cmd - %d\n", rc);
			break;
		}

		pr_info("sync_ack = 0x%08x\n", sync_ack);
		if (sync_ack != IA_SYNC_ACK) {
			pr_err("sync ack pattern 0x%08x fail\n", sync_ack);
			rc = -EIO;
		} else {
			pr_info("sync ack pattern success\n");
			break;
		}
	} while (sync_retry--);

	return rc;
}

int iacore_uart_clean_rx_fifo(struct iacore_priv *iacore)
{
	int rc = 0;
	struct iacore_uart_device *iacore_uart = iacore->dev_data;

	rc = iacore_pm_get_sync(iacore);
	if (rc < 0) {
		pr_err("pm_get_sync failed :%d\n", rc);
		return rc;
	}

	/* Since we now use uart rx for irq, make sure uart rx fifo
	 * is empty. Do this by discarding extra bytes if present
	 * from the UART rx fifo.
	 */
	rc = iacore_uart_open(iacore);
	if (rc < 0) {
		pr_err("UART open Failed %d\n", rc);
		goto err_out;
	}
	iacore_uart_discard_rxbytes(iacore);

	rc = tty_perform_flush(iacore_uart->tty, TCIOFLUSH);
	if (rc)
		pr_err("TTY buffer Flush failed %d\n", rc);

	iacore_uart_close(iacore);
err_out:
	iacore_pm_put_autosuspend(iacore);
	return rc;
}

static int iacore_uart_boot_finish(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int rc;
	u32 sync_cmd = (IA_SYNC_CMD << 16) | IA_SYNC_POLLING;
	u32 sync_ack;
	int sync_retry = IA_SYNC_MAX_RETRY;

	pr_debug("\n");

	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}

	/* Wait till fw is completely sent to the chip */
	tty_wait_until_sent(iacore_uart->tty,
					msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));

	/*
	 * Give the chip some time to become ready after firmware
	 * download. (FW is still transferring)
	 */
	msleep(20);

	/* now switch to firmware baud to talk to chip */
	iacore_configure_tty(iacore_uart->tty,
			iacore_uart->baudrate_vs, UART_TTY_STOP_BITS);

	/* sometimes ia6xx chip sends success in second sync command */
	do {
		/* Discard extra bytes from iacore after firmware load.
		 * Host gets extra bytes after VS firmware download.
		 */
		iacore_uart_discard_rxbytes(iacore);

		rc = tty_perform_flush(iacore_uart->tty, TCIOFLUSH);
		if (rc)
			pr_err("TTY buffer Flush failed %d\n", rc);

		pr_info("write IA_SYNC_CMD = 0x%08x\n", sync_cmd);
		rc = iacore_uart_cmd(iacore, sync_cmd, &sync_ack);
		if (rc) {
			pr_err("fw load failed in write sync cmd - %d\n", rc);
			/*
			 * on failure, uart_cmd will call fw recovery,
			 * which may reset the chip and put it to deep sleep
			 * So before trying again, call wakeup
			 */
			iacore_uart_wakeup(iacore);
			continue;
		}
		pr_info("sync_ack = 0x%08x\n", sync_ack);
		if (sync_ack != IA_SYNC_ACK) {
			pr_err("fw load failed sync ack pattern 0x%08x\n", sync_ack);
			rc = -EIO;
		} else {
			pr_info("firmware load success\n");
			break;
		}
	} while (sync_retry--);

	return rc;
}
#endif

/* SBL Bypass mode is supported only in PDM bus combination */
#if !defined(CONFIG_SND_SOC_IA_I2S)
static int iacore_uart_sbl_bypass_setup(struct iacore_priv *iacore)
{
	int rc;

	pr_debug("prepare for SBL bypass\n");

	rc = iacore_uart_open(iacore);
	if (rc)
		goto iacore_sbl_bypass_fail;

	/* set Host UART speed to bootloader baud */
	rc = iacore_uart_interface_detect(iacore);

	iacore_uart_close(iacore);

iacore_sbl_bypass_fail:
	return rc;
}
#endif

static int iacore_uart_setup_intf(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int rc = 0;

	iacore->bus.ops.open = iacore_uart_open;
	iacore->bus.ops.close = iacore_uart_close;
	iacore->bus.ops.read = iacore_uart_read;
	iacore->bus.ops.write = iacore_uart_write;
#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	iacore->bus.ops.block_write = iacore_i2sperf_block_write;
#else
	iacore->bus.ops.block_write = iacore_uart_write;
#endif
	iacore->bus.ops.cmd = iacore_uart_cmd;
	iacore->bus.ops.wakeup = iacore_uart_wakeup;

	iacore->streamdev = ia_uart_streamdev;
	iacore->bus.ops.cpu_to_bus = iacore_cpu_to_uart;
	iacore->bus.ops.bus_to_cpu = iacore_uart_to_cpu;
#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	iacore->boot_ops.setup = iacore_i2sperf_boot_setup;
	iacore->boot_ops.finish = iacore_i2sperf_boot_finish;
#else
	iacore->boot_ops.setup = iacore_uart_boot_setup;
	iacore->boot_ops.finish = iacore_uart_boot_finish;
#endif

/* SBL Bypass mode is supported only in PDM bus combination */
#if !defined(CONFIG_SND_SOC_IA_I2S)
	iacore->boot_ops.sbl_bypass_setup = iacore_uart_sbl_bypass_setup;
#endif

#if defined(CONFIG_SND_SOC_IA_UART_SBL_BAUD)
	iacore_uart->baudrate_sbl = CONFIG_SND_SOC_IA_UART_SBL_BAUD;
#else
	iacore_uart->baudrate_sbl = UART_TTY_BAUD_RATE_460_8_K;
#endif

#if defined(CONFIG_SND_SOC_IA_UART_VS_BAUD)
	iacore_uart->baudrate_vs = CONFIG_SND_SOC_IA_UART_VS_BAUD;
#else
	iacore_uart->baudrate_vs = UART_TTY_BAUD_RATE_3_M;
#endif

	iacore->boot_ops.interface_detect = iacore_uart_interface_detect;

	return rc;
}

static struct of_device_id iacore_uart_id[] = {
	{ .compatible = "knowles,ia6xx-uart",
	  .data = NULL
	},
	{}
};
MODULE_DEVICE_TABLE(of, iacore_uart_id);

static int iacore_uart_probe(struct platform_device *pdev)
{
	int rc = 0;
	const char *vs_filename = "audience/ia6xx/ia6xx-vs-uart.bin";
#if defined(CONFIG_SND_SOC_IA_BARGEIN)
	const char *bargein_filename = "audience/ia6xx/ia6xx-bargein-uart.bin";
#endif
	struct device *dev = &pdev->dev;
	struct iacore_priv *iacore;
	struct iacore_uart_device *iacore_uart;

	pr_info("enter\n");

	/* Create iacore private-data struct */
	iacore = devm_kzalloc(dev, sizeof(struct iacore_priv), GFP_KERNEL);
	if (!iacore)
		return -ENOMEM;

	/* Create driver private-data struct */
	iacore_uart = devm_kzalloc(dev, sizeof(struct iacore_uart_device),
								GFP_KERNEL);
	if (!iacore_uart)
		return -ENOMEM;

	/* voice sense uart firmware */
	iacore->vs_filename = vs_filename;
#if defined(CONFIG_SND_SOC_IA_BARGEIN)
	iacore->bargein_filename = bargein_filename;
#endif

	iacore->dev = dev;
	iacore->dev_data = iacore_uart;
	iacore->bus.setup_bus_intf = iacore_uart_setup_intf;
	dev_set_drvdata(dev, iacore);

	rc = ia6xx_core_probe(dev);
	if (rc) {
		pr_err("UART common probe fail %d\n", rc);
		goto bootup_error;
	}
#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	rc = iacore_i2sperf_init(iacore);
#endif

	pr_info("leave\n");

	return rc;

bootup_error:
	pr_err("exit with error");
	devm_kfree(dev, iacore);
	dev_set_drvdata(dev, NULL);
	return rc;
}

extern struct iacore_priv *iacore_global;
static int iacore_uart_remove(struct platform_device *pdev)
{
	int rc = 0;
	struct iacore_priv *iacore = dev_get_drvdata(&pdev->dev);
	//struct iacore_uart_device *iacore_uart = iacore->dev_data;

	//if (iacore_uart->file)
		//iacore_uart_close(iacore);

	//iacore_uart->tty = NULL;
	//iacore_uart->file = NULL;

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	iacore_i2sperf_exit(iacore);
#endif
	iacore_global = NULL;
	dev_set_drvdata(&pdev->dev, NULL);
	devm_kfree(&pdev->dev, iacore);

	return rc;
}

static const struct dev_pm_ops iacore_uart_pm_ops = {
	.complete = iacore_pm_complete,
	SET_SYSTEM_SLEEP_PM_OPS(iacore_pm_suspend, iacore_pm_resume)
	SET_RUNTIME_PM_OPS(iacore_pm_runtime_suspend,
			iacore_pm_runtime_resume, NULL)
};

static struct platform_driver iacore_uart_driver = {
	.driver = {
		.name = "iacore-uart",
		.owner = THIS_MODULE,
		.of_match_table = iacore_uart_id,
		.pm = &iacore_uart_pm_ops,
	},
	.probe = iacore_uart_probe,
	.remove = iacore_uart_remove,
};

static __init int iacore_uart_bus_init(void)
{
	int rc;

	rc = platform_driver_register(&iacore_uart_driver);
	if (rc)
		return rc;

	pr_debug("Registered iacore platform driver\n");
	return rc;
}

static __exit void iacore_uart_bus_exit(void)
{
	platform_driver_unregister(&iacore_uart_driver);
}

module_init(iacore_uart_bus_init);
module_exit(iacore_uart_bus_exit);

MODULE_DESCRIPTION("ASoC IACORE driver");
MODULE_AUTHOR("Greg Clemson <gclemson@audience.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:iacore-codec");
