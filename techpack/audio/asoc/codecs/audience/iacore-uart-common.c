/*
 * iacore-uart-common.c  --  Audience ia6xx UART interface
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
#include <asm/ioctls.h>
#include <linux/wait.h>
#include <linux/freezer.h>

#include "iacore.h"
#include "iacore-uart-common.h"

enum{
	UART_OPEN = 1,
	UART_CLOSE = 2,
};

#define UART_OPEN_TIMEOUT 60000
#define UART_MORE_TIMEOUT (UART_OPEN_TIMEOUT + 1000)

int iacore_uart_read_internal(struct iacore_priv *iacore, void *buf, int len)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int rc;
	mm_segment_t oldfs;
	loff_t pos = 0;
	int retry = MAX_EAGAIN_RETRY;

	if (unlikely(!iacore->uart_ready)) {
		pr_err("Error: UART is not ready\n");
		dump_stack();
		return -EIO;
	}

	if (atomic_read(&iacore->uart_users) < 1) {
		pr_err("Error: UART is not open\n");
		return -EIO;
	}

	if (IS_ERR_OR_NULL(iacore_uart->file)) {
		pr_err("Error: invalid uart fd\n");
		return -EBADF;
	}

	/*
	 * we may call from user context via char dev, so allow
	 * read buffer in kernel address space
	 */
	oldfs = get_fs();
	set_fs(KERNEL_DS);

	do {
		rc = vfs_read(iacore_uart->file, (char __user *)buf, len, &pos);
		if (rc == -EAGAIN) {
			usleep_range(EAGAIN_RETRY_DELAY,
				EAGAIN_RETRY_DELAY + 50);
			retry--;
		}
	} while (rc == -EAGAIN && retry);

	/* restore old fs context */
	set_fs(oldfs);

	return rc;
}

int iacore_uart_stream_read_internal(struct iacore_priv *iacore,
							void *buf, int len)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int rc;
	mm_segment_t oldfs;
	loff_t pos = 0;
	int retry = MAX_EAGAIN_STREAM_RETRY;

	pr_debug("size %d\n", len);

	/*
	 * we may call from user context via char dev, so allow
	 * read buffer in kernel address space
	 */
	oldfs = get_fs();
	set_fs(KERNEL_DS);

	do {/* vfs_read() is more secture */
		rc = vfs_read(iacore_uart->file,
			(char __user *)buf, len, &pos);
		if (likely(rc == len))
			goto bail_out;

		if (rc == -EAGAIN)
			retry--;

	} while (rc == -EAGAIN && retry);

bail_out:
	/* restore old fs context */
	set_fs(oldfs);

	pr_debug("read bytes %d\n", rc);

	return rc;
}

u32 iacore_cpu_to_uart(struct iacore_priv *iacore, u32 resp)
{
	return cpu_to_be32(resp);
}

u32 iacore_uart_to_cpu(struct iacore_priv *iacore, u32 resp)
{
	return be32_to_cpu(resp);
}

int iacore_uart_fix_byte_misalignment(struct iacore_priv *iacore)
{
	u32 align_byte = 0x00;
	int ret = 0;
	int retry = 4;
	u32 sync_cmd, sync_word = (IA_SYNC_CMD << 16) | IA_SYNC_POLLING;
	u32 sync_ack;

	INC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

	ret = iacore_uart_open(iacore);
	if (ret < 0) {
		pr_err("UART open Failed %d\n", ret);
		goto open_failed;
	}

	sync_cmd = cpu_to_be32(sync_word);
	do {
		pr_info("wrting align_byte\n");
		ret = iacore_uart_write(iacore, &align_byte, 1);
		if (ret < 0) {
			pr_err("UART wakeup failed:%d\n", ret);
			goto err_exit;
		}

		update_cmd_history(0xabcd1234, 0x00);
		pr_info("sync_cmd = 0x%08x\n", sync_word);

		/* TODO: Fix the sleep */
		usleep_range(IA_DELAY_2MS, IA_DELAY_5MS);
		/* Flush the RX fifo after Chip wakeup */
		iacore_uart_clean_rx_fifo(iacore);

		/* send sync command */
		ret = iacore_uart_write(iacore, &sync_cmd, sizeof(sync_cmd));
		if (ret < 0) {
			pr_err("failed in write sync cmd %d\n", ret);
			update_cmd_history(sync_word, sync_ack);
			goto err_exit;
		}

		usleep_range(IA_DELAY_2MS, IA_DELAY_5MS);
		sync_ack = 0;
		ret = iacore_uart_read_internal(iacore, &sync_ack,
							sizeof(sync_ack));
		if (ret < sizeof(sync_ack)) {
			pr_err("failed to read sync response %d\n", ret);
			update_cmd_history(sync_word, sync_ack);
			continue;
		}

		pr_info("sync_ack = 0x%08x\n", sync_ack);
		sync_ack = be32_to_cpu(sync_ack);
		update_cmd_history(sync_word, sync_ack);

		pr_info("sync_cmd = 0x%08x, sync_ack = 0x%08x\n", sync_word, sync_ack);
		if (sync_ack == sync_word) {
			pr_info("UART byte alignment successful\n");
			ret = 0;
			goto err_exit;
		}

		pr_info("retry cnt %d\n", retry);

	} while (retry--);

	/* alignment fix failed. Continue with calibration */
	ret = 1;

err_exit:
	iacore_uart_close(iacore);
open_failed:
	DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
	return ret;
}

int iacore_uart_calibration(struct iacore_priv *iacore)
{
	int rc = 0;
	int retry = IA_MAX_RETRY_3;
	u32 set_calib_mode = 0xffffffff;
	u16 calib_sync_bytes = 0x0000;
	u32 sync_cmd, sync_word = (IA_SYNC_CMD << 16) | IA_SYNC_POLLING;
	u32 sync_ack;

	pr_info("enter\n");

	/*
	 * first check if the chip is recoverable using buye alignment
	 * steps
	 *	1. send byte (0x00)
	 *	2. send sync command and check response.
	 *		- If response is fine, return.
	 *	3. else repeat steps 1-2 3 more times. a total of 4 bytes
	 *	4. if response is received in any iteration return
	 *	   else continue with calibration.
	 */
	rc = iacore_uart_fix_byte_misalignment(iacore);
	if (rc <= 0)
		return rc;

	sync_cmd = cpu_to_be32(sync_word);
	while (retry--) {
		rc = iacore_uart_write(iacore, &set_calib_mode,
				sizeof(set_calib_mode));
		if (rc < 0) {
			pr_err("switching to calibration mode failed %d\n", rc);
			/* TODO: Fix the sleep */
			usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 500);
			continue;
		}

		update_cmd_history(0xffffffff, 0x00);

		/* TODO: Fix the sleep */
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

		rc = iacore_uart_write(iacore, &calib_sync_bytes,
				       sizeof(calib_sync_bytes));
		if (rc < 0) {
			pr_err("writing calib sync bytes failed %d\n", rc);
			/* TODO: Fix the sleep */
			usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 500);
			continue;
		}
		update_cmd_history(0xca1b0000, 0x00);

		/* TODO: Fix the sleep */
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);

		/* send sync command */
		rc = iacore_uart_write(iacore, &sync_cmd, sizeof(sync_cmd));
		if (rc < 0) {
			pr_err("failed in write sync cmd %d\n", rc);
			/* TODO: Fix the sleep */
			usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 500);
			continue;
		}

		sync_ack = 0;
		rc = iacore_uart_read_internal(iacore, &sync_ack,
					       sizeof(sync_ack));
		if (rc < sizeof(sync_ack)) {
			pr_err("failed to read sync response %d\n", rc);
			/* TODO: Fix the sleep */
			usleep_range(IA_DELAY_10MS, IA_DELAY_10MS + 500);
			continue;
		}

		update_cmd_history(sync_word, be32_to_cpu(sync_ack));

		pr_debug("reading calib sync cmd was success\n");
		if (sync_ack == sync_cmd) {
			pr_info("UART Calibration successful\n");
			rc = 0;
			break;
		}

		pr_err("invalid sync ack resp: 0x%x\n", sync_ack);

		/* TODO: Fix the sleep */
		usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 500);
	}

	pr_info("done\n");

	return rc;
}

int iacore_uart_read(struct iacore_priv *iacore, void *buf, int len)
{
	int rc = 0;
	int retry = MAX_READ_FAILURE_RETRY;

	if (unlikely(!iacore->uart_ready)) {
		pr_err("Error UART is not open\n");
		dump_stack();
		return -EIO;
	}

	/* SHP-1963: sometimes hosts tries to read for response earlier than
	 * firmware sends responds */
	do {
		rc = iacore_uart_read_internal(iacore, buf, len);
		if (rc <= 0) {
			pr_debug("no bytes received from the chip\n");
			usleep_range(IA_DELAY_5MS, IA_DELAY_5MS + 5);
		}
	} while (retry-- && rc <= 0);

	//pr_crit("value read : 0x%x", *((u32 *) buf));

	if (rc < len) {
		pr_err("Uart Read Failed for len: %d, rc = %d\n", len, rc);
		IACORE_FW_RECOVERY_FORCED_OFF(iacore);
		pr_err("read failed\n");
		return -EIO;
	}

	return 0;
}

int iacore_uart_write(struct iacore_priv *iacore, const void *buf, int len)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int rc = 0;
	int count_remain = len;
	int bytes_wr = 0;
	mm_segment_t oldfs;
	loff_t pos = 0;
	void *data_buf = (void *) buf;
	int retry = IA_MAX_RETRIES;

	if (unlikely(!iacore->uart_ready)) {
		pr_err("Error UART is not ready\n");
		dump_stack();
		return -EIO;
	}

	if (atomic_read(&iacore->uart_users) < 1) {
		pr_err("Error: UART is not open\n");
		return -EIO;
	}

	if (IS_ERR_OR_NULL(iacore_uart->file)) {
		pr_err("Error: invalid uart fd\n");
		return -EBADF;
	}

	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}

	pr_debug("size %d\n", len);

	/*
	 * we may call from user context via char dev, so allow
	 * read buffer in kernel address space
	 */
	oldfs = get_fs();
	set_fs(KERNEL_DS);

	while (count_remain > 0) {
		/* block until tx buffer space is available */
		while (tty_write_room(iacore_uart->tty) < UART_TTY_WRITE_SZ)
			usleep_range(IA_TTY_BUF_AVAIL_WAIT_DELAY,
					IA_TTY_BUF_AVAIL_WAIT_DELAY + 50);

		rc = vfs_write(iacore_uart->file,
			       (__force const char __user *) data_buf + bytes_wr,
			       min(UART_TTY_WRITE_SZ, count_remain),
			       &pos);
		if (rc == -ERESTARTSYS) {
			pr_err("was interrupted by a signal by %s(%d)\n", current->comm, task_pid_nr(current));

			clear_thread_flag(TIF_SIGPENDING);
			if (retry--)
				continue;
			else
				goto err_out;
		}
		if (rc < 0) {
			pr_err("uart write failed for len %d, rc %d\n", count_remain, rc);
			goto err_out;
		}

		bytes_wr += rc;
		count_remain -= rc;
	}

err_out:
	/* restore old fs context */
	set_fs(oldfs);
	if (rc == -ERESTARTSYS) {
		rc = -EINTR;
	} else if (count_remain) {
		pr_err("uart write failed, count_remain %d\n", count_remain);
		IACORE_FW_RECOVERY_FORCED_OFF(iacore);
		pr_err("write failed\n");
		rc = -EIO;
	} else {
		rc = 0;
	}
	pr_debug("returning %d\n", rc);

	return rc;
}

int iacore_uart_cmd(struct iacore_priv *iacore, u32 cmd_to_send, u32 *resp)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int err = 0;
	int sr;
	int retry = IA_MAX_RETRIES;
	int send_cmd_iter_cnt = IA_MAX_RETRIES;
	u32 rv = 0;
	u32 cmd;

	INC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

	err = iacore_uart_open(iacore);
	if (err) {
		pr_err("uart_open() failed %d\n", err);
		return -EIO;
	}

	sr = cmd_to_send & BIT(28);
	pr_info("cmd 0x%08x, sr 0x%x\n", cmd_to_send, sr);

retry_from_1st:
	*resp = 0;
	retry = IA_MAX_RETRIES + 1;
	err = 0;

	cmd = cpu_to_be32(cmd_to_send);

	err = iacore_uart_write(iacore, &cmd, sizeof(cmd));
	if (err || sr) {
		if (err) {
			pr_err("uart_write() fail %d\n", err);
		} else {
			/* Wait till cmd is completely sent to chip */
			tty_wait_until_sent(iacore_uart->tty,
				msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));
		}

		update_cmd_history(cmd_to_send, *resp);
		goto cmd_exit;
	}

	do {
		usleep_range(IA_RESP_POLL_TOUT,
				IA_RESP_POLL_TOUT + 500);
		rv = 0;
		err = iacore_uart_read(iacore, &rv, sizeof(rv));
		pr_debug("err = %d", err);
		*resp = be32_to_cpu(rv);
		pr_debug("*resp = 0x%08x", *resp);
		if (err) {
			pr_err("uart_read() fail %d\n", err);
		} else if ((*resp & IA_ILLEGAL_CMD) == IA_ILLEGAL_CMD) {
			pr_err("illegal resp 0x%08x for command 0x%08x\n",
				*resp, cmd_to_send);
			err = -EINVAL;
			update_cmd_history(cmd_to_send, *resp);
			goto cmd_exit;
		} else if (*resp == IA_NOT_READY) {
			pr_err("uart_read() not ready\n");
			err = -EBUSY;
		} else 	if ((*resp &  0xFFFF0000) != (cmd_to_send &  0xFFFF0000)) {
			err = -EINVAL;
			update_cmd_history(cmd_to_send, *resp);
			goto cmd_exit;
		} else {
			update_cmd_history(cmd_to_send, *resp);
			goto cmd_exit;
		}

		--retry;
	} while (retry != 0);

	update_cmd_history(cmd_to_send, *resp);
cmd_exit:
	if (err) {
		/*
		* Handle genuine illegal command (0xffff 0xXXXX) separately
		*  from illegal response (0xffff 0x0000)
		*/
		if (*resp == ((cmd_to_send >> 16) | IA_ILLEGAL_CMD)) {
			pr_err("Illegal Command 08%x\n", *resp);
			err = -EINVAL;
			DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
		} else if ((*resp & IA_ILLEGAL_RESP) == IA_ILLEGAL_RESP) {
			/*
			 * Calibrate the UART if host receives some unexpected
			 * response from the chip
			 */
			iacore_uart_calibration(iacore);

			DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
		} else {
			pr_err("cmd failed after %d retry's\n",
				(IA_MAX_RETRIES - send_cmd_iter_cnt + 1));
			if (send_cmd_iter_cnt--) {
				usleep_range(IA_DELAY_2MS, IA_DELAY_2MS + 5);
				goto retry_from_1st;
			}

			DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);

			if (err == -EINTR) {
				pr_err("i/o is broken, skip\n");
			} else {
				IACORE_FW_RECOVERY_FORCED_OFF(iacore);
			}
		}
	} else {
		DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
	}

	iacore_uart_close(iacore);

	pr_info("cmd 0x%08x, resp 0x%08x exit\n", cmd_to_send, *resp);

	return err;
}

int iacore_configure_tty(struct tty_struct *tty, u32 bps, int stop)
{
	int rc = 0;
	struct ktermios termios;

	if (IS_ERR_OR_NULL(tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}

	termios = tty->termios;

	pr_info("Requesting baud %u\n", bps);

	termios.c_cflag &= ~(CBAUD | CSIZE | PARENB);   /* clear csize, baud */
	termios.c_cflag |= BOTHER;	      /* allow arbitrary baud */
	termios.c_cflag |= CS8;

	if (stop == 2)
		termios.c_cflag |= CSTOPB;

	/* set uart port to raw mode (see termios man page for flags) */
	termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
		| INLCR | IGNCR | ICRNL | IXON);
	termios.c_oflag &= ~(OPOST);
	termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	/* set baud rate */
	termios.c_ospeed = bps;
	termios.c_ispeed = bps;

	/* Added to set baudrate dynamically */
	tty_wait_until_sent(tty, msecs_to_jiffies(IA_TTY_WAIT_TIMEOUT));

	rc = tty_set_termios(tty, &termios);

	pr_info("New baud %u\n", tty->termios.c_ospeed);

	return rc;
}
EXPORT_SYMBOL_GPL(iacore_configure_tty);

int iacore_raw_configure_tty(struct iacore_priv *iacore, u32 bps)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int err;

	pr_info("iacore_raw_configure_tty!\n");
	err = iacore_uart_open(iacore);
	if (err) {
		pr_err("iacore_uart_open() failed %d\n", err);
		return -EIO;
	}

	err = iacore_configure_tty(iacore_uart->tty, bps, UART_TTY_STOP_BITS);
	iacore_uart_close(iacore);
	return err;
}
EXPORT_SYMBOL_GPL(iacore_raw_configure_tty);


static long iacore_uart_tty_ioctl(struct file *f, unsigned int op)
{
	unsigned long param = 0;

	if (f->f_op->unlocked_ioctl)
		return f->f_op->unlocked_ioctl(f, op, param);

	pr_err("no unlocked_ioctl defined\n");
	return -ENOTTY;
}


int iacore_uart_clock_control(struct iacore_priv *iacore, int cmd)
{
	int err = 0;
	struct iacore_uart_device *iacore_uart = iacore->dev_data;

	pr_debug("called 0x%x", cmd);

	switch (cmd) {
	case UART_OP_CLK_ON:
		pr_debug("Clock On\n");
		iacore_uart->vote_counter++;
		if(iacore_uart->vote_counter == 1)
			err = iacore_uart_tty_ioctl(iacore_uart->file, cmd);
		break;
	case UART_OP_CLK_OFF:
		pr_debug("Clock Off\n");
		iacore_uart->vote_counter--;
		if(iacore_uart->vote_counter < 0)
			iacore_uart->vote_counter = 0;
		else if(iacore_uart->vote_counter == 0)
			err = iacore_uart_tty_ioctl(iacore_uart->file, cmd);
		break;
	case UART_OP_CLK_STATE:
		pr_debug("Get clock state\n");
		err = iacore_uart_tty_ioctl(iacore_uart->file, cmd);
		break;
	}

	pr_info("cmd 0x%x, ret %d", cmd, err);

	return err;
}

static int iacore_uart_open_raw(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	long err = 0;
	int ret = 0;
	struct file *fp = NULL;

	/* Timeout increased to 60 Seconds to avoid UART open failure in KK */
	unsigned long timeout = jiffies + msecs_to_jiffies(UART_OPEN_TIMEOUT);
	int attempt = 0;

	mutex_lock(&iacore->uart_lock);

	pr_debug("UART users: %d enter\n",
		atomic_read(&iacore->uart_users));

	if (atomic_inc_return(&iacore->uart_users) > 1) {
		pr_info("UART is already opened, users: %d\n",
			 atomic_read(&iacore->uart_users));
		goto unlock_exit;
	}

	pr_info("UART start open tty\n");

	/* try to probe tty node every 50 ms for 6 sec */
	do {
		if (attempt)
			msleep(50);

		pr_debug("UART probing for tty on %s (attempt %d)\n",
				UART_TTY_DEVICE_NODE, attempt);

		fp = filp_open(UART_TTY_DEVICE_NODE,
			       O_RDWR | O_NONBLOCK | O_NOCTTY, 0);
		err = PTR_ERR(fp);
		if (IS_ERR(fp)) {
			pr_debug("UART failed, err(%ld), retry\n", err);
		}
		attempt = (-EACCES == err || -ENOENT == err);
	} while (time_before(jiffies, timeout) && attempt);

	if (IS_ERR_OR_NULL(fp)) {
		pr_err("UART device node open failed, err = %ld\n", err);
		atomic_dec(&iacore->uart_users);
		ret = -ENODEV;
		goto unlock_exit;
	}

	/* set uart_dev members */
	iacore_uart->file = fp;
	iacore_uart_clock_control(iacore, UART_OP_CLK_ON);
	iacore_uart->tty =
		((struct tty_file_private *)fp->private_data)->tty;

	iacore->uart_ready = 1;
	pr_info("UART open successfully!\n");

unlock_exit:
	mutex_unlock(&iacore->uart_lock);
	return ret;
}

static int iacore_uart_close_raw(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;

	mutex_lock(&iacore->uart_lock);

	pr_debug("UART users count: %d enter\n",
			atomic_read(&iacore->uart_users));

	if (IS_ERR_OR_NULL(iacore_uart->file)){
		pr_err("UART file error\n");
		goto unlock_exit;
	}

	if (atomic_read(&iacore->uart_users) < 1) {
		pr_err("UART is already closed.\n");
		atomic_set(&iacore->uart_users, 0);
		goto unlock_exit;
	}

	if (atomic_dec_return(&iacore->uart_users) > 0) {
		pr_info("UART is still used, users: %d\n",
			 atomic_read(&iacore->uart_users));
		goto unlock_exit;
	}

	iacore_uart_clock_control(iacore, UART_OP_CLK_OFF);

	iacore_uart->tty = NULL;
	filp_close(iacore_uart->file, NULL);
	iacore_uart->file = NULL;
	iacore->uart_ready = 0;

	pr_info("UART close successfully!\n");

unlock_exit:
	mutex_unlock(&iacore->uart_lock);
	return 0;
}

int iacore_uart_open(struct iacore_priv *iacore)
{
	int ret = 0;

	if (!current->fs) {
		ret = iacore_uart_thread_enable(iacore, true);
	} else {
		ret = iacore_uart_open_raw(iacore);
	}

	return ret;
}

int iacore_uart_close(struct iacore_priv *iacore)
{
	int ret = 0;

	if (!current->fs) {
		ret = iacore_uart_thread_enable(iacore, false);
	} else {
		ret = iacore_uart_close_raw(iacore);
	}

	return ret;
}

static int iacore_uart_wait(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int timeout;
	DECLARE_WAITQUEUE(wait, current);

	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}

	add_wait_queue(&iacore_uart->tty->read_wait, &wait);
	set_task_state(current, TASK_INTERRUPTIBLE);
	timeout = schedule_timeout(msecs_to_jiffies(50));

	set_task_state(current, TASK_RUNNING);
	remove_wait_queue(&iacore_uart->tty->read_wait, &wait);
	return timeout;
}

static int iacore_uart_config(struct iacore_priv *iacore)
{
	struct iacore_uart_device *iacore_uart = iacore->dev_data;
	int rc = 0;

	pr_info("\n");

	if (IS_ERR_OR_NULL(iacore_uart->tty)) {
		pr_err("tty is not available\n");
		return -EINVAL;
	}

	/* perform baudrate configuration */
	rc = iacore_configure_tty(iacore_uart->tty, iacore_uart->baudrate_vs,
				  UART_TTY_STOP_BITS);

	return rc;
}

struct ia_stream_device ia_uart_streamdev = {
	.open = iacore_uart_open,
	//.read = iacore_uart_read_internal,
	.read = iacore_uart_stream_read_internal,
	.close = iacore_uart_close,
	.wait = iacore_uart_wait,
	.config = iacore_uart_config,
};

int iacore_uart_thread_enable(struct iacore_priv *iacore, int enable)
{
	int ret = 0;
	int cmd = 0;

	if (!iacore || !iacore->uart_thread) {
		pr_err("Invalid null pointer\n");
		return -EINVAL;
	}

	mutex_lock(&iacore->uart_thread_mutex);

	if (enable)
		cmd = UART_OPEN;
	else
		cmd = UART_CLOSE;
	pr_info("cmd %d\n", cmd);
	atomic_set(&iacore->uart_thread_cond, cmd);
	smp_wmb();
	wake_up(&iacore->uart_wait_q);
	pr_info("wake up uart_wait_q\n");
	ret = wait_event_timeout(iacore->uart_compl_q,
		(atomic_read(&iacore->uart_thread_cond) <= 0),
		msecs_to_jiffies(UART_MORE_TIMEOUT));
	pr_info("wait_event_timeout for uart_compl_q\n");
	if (ret > 0)
		ret = atomic_read(&iacore->uart_thread_cond);
	else if(ret == 0)
		pr_err("wait timeout\n");

	mutex_unlock(&iacore->uart_thread_mutex);
	return ret;
}

static int ia_uart_thread_handler(void *ptr)
{
	struct iacore_priv *iacore = (struct iacore_priv *) ptr;
	int cmd, ret = 0;

	if (!iacore) {
		pr_err("Invalid null pointer\n");
		return -EINVAL;
	}

	set_freezable();
	do {
		wait_event_freezable(iacore->uart_wait_q,
			(cmd = atomic_read(&iacore->uart_thread_cond)) > 0);
		pr_info("Handled thread resumed\n");
		switch(cmd) {
			case UART_OPEN:
				ret = iacore_uart_open_raw(iacore);
				break;
			case UART_CLOSE:
				ret = iacore_uart_close_raw(iacore);
				break;
			default:
				break;
		}
		atomic_set(&iacore->uart_thread_cond, ret);
		smp_wmb();
		pr_info("Handled thread cmd %d, ret %d\n", cmd, ret);
		wake_up(&iacore->uart_compl_q);
		pr_info("Handled thread wake_up uart_compl_q\n");
	} while (!kthread_should_stop());

	return 0;
}

int ia_uart_kthread_init(struct iacore_priv *iacore)
{
	int ret = 0;

	if (!iacore) {
		pr_err("Invalid null pointer\n");
		return -EINVAL;
	}

	mutex_init(&iacore->uart_thread_mutex);
	atomic_set(&iacore->uart_thread_cond, 0);
	init_waitqueue_head(&iacore->uart_wait_q);
	init_waitqueue_head(&iacore->uart_compl_q);

	iacore->uart_thread = kthread_run(ia_uart_thread_handler,
			(void *)iacore, "iacore uart thread");
	if (IS_ERR_OR_NULL(iacore->uart_thread)) {
		pr_err("can't create iacore uart thread\n");
		iacore->uart_thread = NULL;
		ret = -ENOMEM;
	}
	pr_info("Created iacore uart thread\n");
	return ret;
}

void ia_uart_kthread_exit(struct iacore_priv *iacore)
{
	if (!iacore) {
		pr_err("Invalid null pointer\n");
		return;
	}

	if (iacore->uart_thread) {
		kthread_stop(iacore->uart_thread);
		pr_info("stopping stream kthread\n");
		iacore->uart_thread = NULL;
	}
}

MODULE_DESCRIPTION("ASoC IACORE driver");
MODULE_AUTHOR("Greg Clemson <gclemson@audience.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:iacore-codec");
