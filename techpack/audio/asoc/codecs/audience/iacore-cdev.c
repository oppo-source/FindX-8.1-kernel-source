/* iacore-cdev.c -- Character device interface.
 *
 * Author: Marc Butler <mbutler@audience.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This interface is intended to be used during integration and
 * development.
 *
 * Currently only a single node is registered.
 *
 * COMMAND CHARACTER DEVICE
 * ========================
 * Implements a modestly robust "lucky charms" macro format
 * parser. That allows user to cat macros directly into the device
 * node.
 *
 * The macro format is line oriented. Here are some perl style regexes
 * to describe the format:
 *
 * - Comments: semicolon to end of line.
 * ;.*$
 *
 * - Preset identifier only one per line.
 * ^\s+!Preset\s+id:\s+(\d+)\s*(;.*)
 *
 * - Commands appear as pairs of 16 bit hex values each prefixed with 0x.
 * 0x[0-9a-f]{1,4}\s+0x[0-9a-f]{1,4}
 *
 * STREAMING CHARACTER DEVICE
 * ==========================
 * The streaming character device implements an interface allowing the
 * unified driver to output streaming data via a connected HW interface.
 * This data may be consumed by open/read/close operations on the character
 * device.  The driver expects all streaming configuration to be set via
 * another method, for example the command character device, before the
 * streaming cdev is opened.
 *
 * In general, the streaming node performs the following operations:
 * - open(): prepare the HW interface for streaming (if needed)
 *           begin streaming via iacore cmd API call
 * - producer kthread: services the HW interface and writes data into
 *                     a circular buffer in kernel space
 * - poll(): implemented so clients can use poll/epoll/select
 * - read(): reads data out of the circular buffer and copies the
 *           data to user space
 * - release/close(): stop the producer thread, stop streaming,
 *		      closes the HW interface (if needed),
 *                    free all resources, discard stale data
 *
 * If userspace does not read the data from the character device fast
 * enough, the producer thread will consume and overwrite the oldest
 * data in the circular buffer first.
 */

#include "iaxxx.h"

#include <linux/circ_buf.h>
#include <linux/mm.h>
#include "iacore.h"
#include "iacore-raw.h"
#include "iacore-cdev.h"
#include "iacore-vs.h"


#define CDEV_COUNT			CDEV_MAX_DEV
#define IA_STREAMING_READ_TIMEOUT	4000
#define CB_SIZE				128 /* MUST be power-of-two */
#define READBUF_SIZE			128
#define PARSE_BUFFER_SIZE		PAGE_SIZE

/*
 * 1 complete command log == 50 bytes, total 300 commands
 *
 * 50 * 300 = 15000 == ~16k
 */
#define CMD_HISTORY_BUF_SIZ		(PAGE_SIZE * 4)


/* Streaming Device device internals */
#define IACORE_STREAMING_PRE_ALLOC_KMALLOC	1
#define IACORE_STREAMING_USE_READ_WAIT		0

#define IACORE_STREAMING_FAIL_RETRY		50

#define GET_CDEV_PRIV(iacore) \
	((!iacore || !iacore->cdev_priv) ? NULL : iacore->cdev_priv)

/* command character device internals */
enum parse_token {
	PT_NIL, PT_PRESET, PT_ID, PT_HEXWORD
};

struct stream_circ_buf {
	char *buf[CB_SIZE];
	int length[CB_SIZE];
	int head;
	int tail;
};

/* Character Devices:
 *  /dev/adnc0 - Command
 *  /dev/adnc-cvq - adnc cvq control interface
 *  /dev/adnc2 - Streaming
 *  /dev/iaraw - adnc raw interface
 *  /dev/adnc4 - Command History
 */
enum {
	CDEV_COMMAND,
	CDEV_CVQ,
	CDEV_STREAMING,
	CDEV_RAW,
	CDEV_CMD_HISTORY,
	CDEV_MAX_DEV,
};

#if (IACORE_STREAMING_PRE_ALLOC_KMALLOC == 1)
struct static_stream_circ_buf {
	char *buf[CB_SIZE];
	int head;
};

static struct static_stream_circ_buf static_stream_circ;

#endif

/* character device priv struct */
struct iacore_cdev_priv {
	struct cdev cdev_command;
	struct cdev cdev_cvq;
	struct cdev cdev_streaming;
	struct cdev cdev_raw;
	struct cdev cdev_cmd_history;

	struct iacore_priv *iacore;

	int cdev_major;
	int cdev_minor;
	struct class *cdev_class;
	struct device *devices[CDEV_COUNT];

	/* cdev_iacore-> */
	char *stream_read_page;
	int stream_read_off;

	struct stream_circ_buf stream_circ;
	//struct mutex stream_consumer_mutex;
	spinlock_t stream_consumer_spnlk;
	atomic_t cb_pages_out;

	struct timespec read_time;
	char readbuf[READBUF_SIZE];
	char *cmd_history_buf;

	char parse_buffer[PARSE_BUFFER_SIZE + sizeof(u32)];
	int parse_have;		/* Bytes currently in buffer. */
	int last_token;		/* Used to control parser state. */
	int (*parse_cb_preset)(void *, int);
	int (*parse_cb_cmd)(void *, u32);

	struct task_struct *stream_thread;
	wait_queue_head_t stream_in_q;
	struct completion start_strm_compl;
};

/* streaming character device internals */
static int streaming_producer(void *ptr);
static char *streaming_consume_page(struct iacore_cdev_priv *cdev_iacore,
				    int *length);

/* The extra space allows the buffer to be zero terminated even if the
 * last newline is also the last character.
 */
static int macro_preset_id(void *ctx, int id)
{
	pr_debug("ignored preset id = %i\n", id);
	return 0;
}

static int macro_cmd(void *ctx, u32 cmd)
{
	struct iacore_priv *iacore = (struct iacore_priv *)ctx;
	u32 resp;
	int rc;

	rc = iacore_cmd_locked(iacore, cmd, &resp);
	if (rc < 0) {
		pr_err("cmd (0x%08x) send fail %d\n", cmd, rc);
		return rc;
	}

	if (!(cmd & BIT(28))) {
		pr_debug("cmd=0x%08x Response:0x%08x\n", cmd,
				iacore->bus.last_response);
	} else {
		pr_debug("cmd=0x%08x\n", cmd);
	}
	return rc;
}

/* Line oriented parser that extracts tokens from the shared
 * parse_buffer.
 *
 * FIXME: Add callback mechanism to actually act on commands and preset ids.
 */
static int parse(void *cb_ctx)
{
	char *cur, *tok;
	u16 w;
	u32 cmd;
	int err;
	int id;
	struct iacore_priv *iacore = (struct iacore_priv *)cb_ctx;
	struct iacore_cdev_priv *cdev_iacore;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	cur = cdev_iacore->parse_buffer;
	cmd = 0;
	err = 0;
	while ((tok = strsep(&cur, " \t\r\n")) != cur) {
		/* Comments extend to eol. */
		if (*tok == ';') {
			while (*cur != 0 && *cur != '\n')
				cur++;
			continue;
		}

		switch (cdev_iacore->last_token) {
		case PT_NIL:
			if (*tok == '0' &&
			    sscanf(tok, "0x%hx", &w) == 1) {
				cdev_iacore->last_token = PT_HEXWORD;
				cmd = w << 16;
			} else if (strncasecmp("!Preset", tok, 7) == 0) {
				cdev_iacore->last_token = PT_PRESET;
			} else if (*tok != 0) {
				pr_debug("invalid token: '%s'\n", tok);
				err = -EINVAL;
				goto EXIT;
			}
			break;
		case PT_PRESET:
			if (strncasecmp(tok, "id:", 3) == 0) {
				cdev_iacore->last_token = PT_ID;
			} else {
				pr_debug("expecting 'id:' got '%s'\n", tok);
				err = -EINVAL;
				goto EXIT;
			}
			break;
		case PT_ID:
			if (sscanf(tok, "%d", &id) == 1) {
				cdev_iacore->parse_cb_preset(cb_ctx, id);
				cdev_iacore->last_token = PT_NIL;
			} else {
				pr_debug("expecting preset id: got '%s'\n",
					 tok);
				err = -EINVAL;
				goto EXIT;
			}
			break;
		case PT_HEXWORD:
			if (cdev_iacore->last_token == PT_HEXWORD &&
			    sscanf(tok, "0x%hx", &w) == 1) {
				cdev_iacore->parse_cb_cmd(cb_ctx, cmd | w);
				cdev_iacore->last_token = PT_NIL;
			} else {
				pr_debug("expecting hex word: got '%s'\n",
					 tok);
				err = -EINVAL;
				goto EXIT;
			}
			break;
		}
	}

EXIT:
	return err;
}

static int command_open(struct inode *inode, struct file *filp)
{
	struct iacore_priv *iacore;
	struct iacore_cdev_priv *cdev_iacore;
	int err = 0;
	unsigned major;
	unsigned minor;

	pr_debug("called");

	cdev_iacore = container_of((inode)->i_cdev, struct iacore_cdev_priv,
				   cdev_command);
	iacore = cdev_iacore->iacore;

	if (inode->i_cdev != &cdev_iacore->cdev_command) {
		pr_err("error bad cdev field\n");
		err = -ENODEV;
		goto OPEN_ERR;
	}

	major = imajor(inode);
	minor = iminor(inode);
	if ( (major != cdev_iacore->cdev_major) ||
		(minor >= CDEV_COUNT) ) {
		pr_err("no such device major=%u minor=%u\n",
			 major, minor);
		err = -ENODEV;
		goto OPEN_ERR;
	}

	IACORE_MUTEX_LOCK(&iacore->access_lock);
#ifdef FW_SLEEP_TIMER_TEST
	/* Disable the FW_SLEEP timer.
	 * NOTE : setup_fw_sleep_timer() never returns a value other
	 * than 0. The check is for any future updates.
	 */
	err = setup_fw_sleep_timer_unlocked(iacore, 0);
	if (err)
		pr_err("Stop FW_SLEEP timer failed\n");
#endif
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	filp->private_data = iacore;

	/* Initialize parser. */
	cdev_iacore->last_token = PT_NIL;
	cdev_iacore->parse_have = 0;
	cdev_iacore->parse_cb_preset = macro_preset_id;
	cdev_iacore->parse_cb_cmd = macro_cmd;
OPEN_ERR:
	return err;
}

static int command_release(struct inode *inode, struct file *filp)
{
	struct iacore_priv *iacore;
	iacore = (struct iacore_priv *)filp->private_data;
	return 0;
}

static loff_t command_llseek(struct file *filp, loff_t off, int whence)
{
	/*
	 * Only is lseek(fd, 0, SEEK_SET) to allow multiple calls to
	 * read().
	 */
	if (off != 0 || whence != SEEK_SET)
		return -ESPIPE;

	filp->f_pos = 0;
	return 0;
}

static ssize_t command_read(struct file *filp, char __user *buf,
			       size_t count, loff_t *f_pos)
{
	struct iacore_priv *iacore;
	u32 resp;
	size_t slen;
	int err;
	size_t cnt;
	struct iacore_cdev_priv *cdev_iacore;

	iacore = (struct iacore_priv *)filp->private_data;
	if (!iacore) {
		pr_err("Invalid iacore pointer");
		return -EINVAL;
	}

	err = cnt = 0;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	if (timespec_compare(&cdev_iacore->read_time,
			     &iacore->last_resp_time) != 0) {
		resp = iacore_resp(iacore);
		memcpy(&cdev_iacore->read_time, &iacore->last_resp_time,
		       sizeof(cdev_iacore->read_time));
		snprintf(cdev_iacore->readbuf, READBUF_SIZE,
			 "%li.%4li 0x%04hx 0x%04hx\n",
			 cdev_iacore->read_time.tv_sec,
			 cdev_iacore->read_time.tv_nsec,
			 resp >> 16, resp & 0xffff);
	}

	slen = strnlen(cdev_iacore->readbuf, READBUF_SIZE);
	if (*f_pos >= slen)
		goto OUT;	/* End of file. */

	slen -= *f_pos;
	cnt = min(count, slen);
	err = copy_to_user(buf, cdev_iacore->readbuf + *f_pos, cnt);
	if (err) {
		pr_err("copy_to_user fail %d\n", err);
		err = -EFAULT;
		goto OUT;
	}
	*f_pos += cnt;

OUT:
	return (err) ? err : cnt;
}

static ssize_t command_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *f_pos)
{
	struct iacore_priv *iacore;
	size_t used;
	int rem;
	const char __user *ptr;
	int err;
	struct iacore_cdev_priv *cdev_iacore;

	pr_debug("\n");

	iacore = (struct iacore_priv *)filp->private_data;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return 0;
	}

	err = 0;
	used = 0;
	ptr = buf;
	while (used < count) {
		int space, frag;
		char *data, *end;
		char last;

		space = PARSE_BUFFER_SIZE - cdev_iacore->parse_have;
		if (space == 0) {
			pr_debug("line too long - exhausted buffer\n");
			err = -EFBIG;
			goto OUT;
		}

		/* Top up the parsing buffer. */
		rem = count - used;
		frag = min(space, rem);
		data = cdev_iacore->parse_buffer + cdev_iacore->parse_have;
		pr_debug("copying fragment size = %i\n", frag);
		err  = copy_from_user(data, ptr, frag);
		if (err) {
			pr_debug("error copying user data\n");
			err = -EFAULT;
			goto OUT;
		}
		used += frag;

		/* Find the last newline and terminated the buffer
		 * there with 0 making a string.
		 */
		end = cdev_iacore->parse_buffer + cdev_iacore->parse_have +
		      frag - 1;
		while (*end != '\n' && end >= cdev_iacore->parse_buffer)
			end--;
		end += 1;
		last = *end;
		*end = 0;

		err = parse(iacore);
		if (err) {
			pr_debug("parsing error");
			err = -EINVAL;
			goto OUT;
		}

		*end = last;
		cdev_iacore->parse_have = data + frag - end;
		pr_debug("used = %zu parse_have = %i\n", used,
			 cdev_iacore->parse_have);
		if (cdev_iacore->parse_have > 0)
			memmove(cdev_iacore->parse_buffer, end,
				cdev_iacore->parse_have);
	}

	/*
	 * There are no obviously useful semantics for using file
	 * position: so don't increment.
	 */
OUT:
	return (err) ? err : count;
}

static const struct file_operations command_fops = {
	.owner = THIS_MODULE,
	.llseek = command_llseek,
	.read = command_read,
	.write = command_write,
	.open = command_open,
	.release = command_release
};

#if (IACORE_STREAMING_PRE_ALLOC_KMALLOC == 1)
static int streaming_static_page_alloc(struct iacore_cdev_priv *cdev_iacore)
{
	char *new_page;
	int i = 0, j;
	struct iacore_priv *iacore;

	iacore = cdev_iacore->iacore;

	pr_info("allocate mem pool with pool size %ld & block size %d\n", PAGE_SIZE, CB_SIZE);

	for (i = 0; i < CB_SIZE; i++) {
		new_page = (void *)get_zeroed_page(GFP_KERNEL);
		if (!new_page) {
			pr_err("new_page @ index %d failed\n", i);

			for (j = 0; j < i; j++) {
				ClearPageReserved(virt_to_page(static_stream_circ.buf[j]));
				free_page((unsigned long)static_stream_circ.buf[j]);
				static_stream_circ.buf[j] = NULL;
			}

			return -ENOMEM;
		}

		SetPageReserved(virt_to_page(new_page));
		static_stream_circ.buf[i] = new_page;
	}

	return 0;
}

static void streaming_static_page_free(struct iacore_cdev_priv *cdev_iacore)
{
	int i = 0;
	struct iacore_priv *iacore;

	iacore = cdev_iacore->iacore;

	for (i = 0; i < CB_SIZE; i++) {
		if (static_stream_circ.buf[i]) {
			ClearPageReserved(virt_to_page(static_stream_circ.buf[i]));
			free_page((unsigned long)static_stream_circ.buf[i]);
			static_stream_circ.buf[i] = NULL;
		}
	}
}
#endif

static char *streaming_page_alloc(struct iacore_cdev_priv *cdev_iacore)
{
	char *new_page;

#if (IACORE_STREAMING_PRE_ALLOC_KMALLOC == 1)
	int chead;

	chead = ACCESS_ONCE(static_stream_circ.head);
	new_page = static_stream_circ.buf[chead];
	static_stream_circ.head = ((chead + 1) & (CB_SIZE - 1));

#else
	new_page = kmalloc(PAGE_SIZE, GFP_KERNEL);
#endif
	if (new_page)
		atomic_inc(&cdev_iacore->cb_pages_out);
	return new_page;
}

static void streaming_page_free(struct iacore_cdev_priv *cdev_iacore,
				char *old_page)
{
	if (!old_page)
		return;
#if (IACORE_STREAMING_PRE_ALLOC_KMALLOC == 1)
	memset(old_page, 0, PAGE_SIZE);
#else
	kfree(old_page);
#endif
	atomic_dec(&cdev_iacore->cb_pages_out);
}

int streamdev_open(struct iacore_priv *iacore)
{
	int err;
	struct task_struct *stream_thread;
	struct iacore_cdev_priv *cdev_iacore;
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	bool is_perf_mode = false;

	pr_debug("\n");

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}
	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	/*
	* check for required function implementations
	* note that streamdev.wait is deliberately omitted
	*/
	if (!iacore->streamdev.read) {
		pr_err("streaming not configured\n");
		return -ENODEV;
	}

	pr_debug("streaming mutex lock killable\n");
	err = mutex_lock_killable(&iacore->streaming_mutex);
	if (err) {
		pr_err("did not get streaming lock: %d\n", err);
		err = -EBUSY;
		goto streamdev_mutex_lock_err;
	}

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	is_perf_mode = true;
#endif
	if (is_perf_mode && iacore_get_power_state(iacore) == VS_MODE) {
		pr_err("streaming over I2S interface\n");
	} else {
		if (iacore->streamdev.open) {
			err = iacore->streamdev.open(iacore);
			if (err) {
				pr_err("can't open streaming device = %d\n", err);
				goto streamdev_invalid_param_err;
			}
		}

		/* initialize stream buffer */
		//mutex_init(&cdev_iacore->stream_consumer_mutex);
		spin_lock_init(&cdev_iacore->stream_consumer_spnlk);
		memset(&cdev_iacore->stream_circ, 0,
			sizeof(cdev_iacore->stream_circ));

		cdev_iacore->stream_read_page = NULL;
		cdev_iacore->stream_read_off = 0;

		/* clears the old completion flag states if any */
		reinit_completion(&cdev_iacore->start_strm_compl);

		/* start thread to buffer streaming data */
		stream_thread = kthread_run(streaming_producer, (void *)
					iacore, "iacore stream thread");
		if (IS_ERR_OR_NULL(stream_thread)) {
			pr_err("can't create iacore streaming thread = %p\n",
				stream_thread);
			err = -ENOMEM;
			goto streamdev_thread_create_err;
		}
		cdev_iacore->stream_thread = stream_thread;
	}

	/* TODO: perform IO streaming if the device is not in VS_MODE */
	/* start bursting */

	IACORE_MUTEX_LOCK(&iacore->access_lock);
	iacore_disable_irq_nosync(iacore);
	pr_info("firmware is loaded in %s mode\n",
		 iacore_fw_state_str(iacore->fw_state));

	if ((iacore_get_power_state(iacore) == VS_MODE) &&
					(voice_sense->bargein_sts == true)) {
		err = iacore_change_state_unlocked(iacore, BARGEIN_BURSTING);
	} else if (iacore_get_power_state(iacore) == VS_MODE) {
		err = iacore_change_state_unlocked(iacore, VS_BURSTING);
	} else if (iacore_get_power_state(iacore) == BARGEIN_SLEEP) {
		err = iacore_change_state_unlocked(iacore, IO_BARGEIN_STREAMING);
	} else if (iacore_get_power_state(iacore) == PROXY_MODE) {
		pr_info("noting to do if in Proxy Mode\n");
		err = 0;
	} else {
		err = iacore_change_state_unlocked(iacore, IO_STREAMING);
	}
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	if (err) {
		pr_err("failed to turn on streaming\n");
		goto streamdev_set_streaming_err;
	}
	complete(&cdev_iacore->start_strm_compl);
	IACORE_MUTEX_UNLOCK(&iacore->streaming_mutex);
	return err;

streamdev_set_streaming_err:
	pr_err("stopping stream kthread\n");
	iacore_stop_streaming_thread(iacore);
streamdev_thread_create_err:
	if (iacore->streamdev.close)
		iacore->streamdev.close(iacore);
streamdev_invalid_param_err:
	pr_debug("streaming mutex unlock\n");
	IACORE_MUTEX_UNLOCK(&iacore->streaming_mutex);
streamdev_mutex_lock_err:
	pr_crit("exit");
	return err;
}

void iacore_stop_streaming_thread(struct iacore_priv *iacore)
{
	struct iacore_cdev_priv *cdev_iacore;
	cdev_iacore = GET_CDEV_PRIV(iacore);

	pr_debug("\n");

	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return;
	}

	/* stop streaming thread */
	if (cdev_iacore->stream_thread) {
		kthread_stop(cdev_iacore->stream_thread);
		pr_info("stopping stream kthread\n");
		cdev_iacore->stream_thread = NULL;
	}
}

int streamdev_stop_and_clean_thread(struct iacore_priv *iacore)
{
	char *page;
	int length;
	struct iacore_cdev_priv *cdev_iacore;
	cdev_iacore = GET_CDEV_PRIV(iacore);

	if (!cdev_iacore) {
		pr_err("Invalid private pointer");
		return -EINVAL;
	}

		/* ignore threadfn return value */
		pr_debug("stopping stream kthread\n");

	iacore_stop_streaming_thread(iacore);

	/* free any pages on the circular buffer */
	while ((page = streaming_consume_page(cdev_iacore, &length)))
		streaming_page_free(cdev_iacore, page);

	if (cdev_iacore->stream_read_page) {
		streaming_page_free(cdev_iacore,
				    cdev_iacore->stream_read_page);
		/* prevents double free */
		cdev_iacore->stream_read_page = NULL;
	}

	length = atomic_read(&cdev_iacore->cb_pages_out);
	if (length) {
		pr_err("Err. Some pages present (%d) during release\n",
								length);
		/* It is no a fatal error, recovery it */
		/* return -EIO; */
		atomic_set(&cdev_iacore->cb_pages_out, 0);
	}
	if (iacore->streamdev.close)
		iacore->streamdev.close(iacore);

	return 0;
}

int streamdev_release(struct iacore_priv *iacore)
{
	int err = 0;
	struct iacore_cdev_priv *cdev_iacore;
	bool is_perf_mode = false;

	pr_debug("called");

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer");
		return -EINVAL;
	}

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
	is_perf_mode = true;
#endif
	IACORE_MUTEX_LOCK(&iacore->streaming_mutex);

	if (is_perf_mode && iacore_get_power_state(iacore) == VS_BURSTING) {
		pr_err("streaming thread not running in perf mode\n");
	} else {
		//streamdev_stop_and_clean_thread(iacore);
	}

	/* stop streaming */
	IACORE_MUTEX_LOCK(&iacore->access_lock);
	pr_debug("firmware is loaded in %s mode\n",
		 iacore_fw_state_str(iacore->fw_state));

	if (iacore_get_power_state(iacore) == VS_BURSTING) {
		err = iacore_change_state_unlocked(iacore, VS_MODE);
	} else if (iacore_get_power_state(iacore) == BARGEIN_BURSTING) {
		err = iacore_change_state_unlocked(iacore, VS_MODE);
	} else if (iacore_get_power_state(iacore) == IO_BARGEIN_STREAMING) {
		err = iacore_change_state_unlocked(iacore, BARGEIN_SLEEP);
	} else if (iacore_get_power_state(iacore) == PROXY_MODE) {
		pr_info("stop and clean thread in Proxy Mode\n");
		streamdev_stop_and_clean_thread(iacore);
		err = 0;
	} else {
		err = iacore_change_state_unlocked(iacore, FW_SLEEP);
	}
	if (err) {
		pr_err("failed to turn off streaming: %d\n", err);
	}
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);
	pr_debug("streaming mutex unlock\n");
	IACORE_MUTEX_UNLOCK(&iacore->streaming_mutex);

	return err;
}

static int streaming_open(struct inode *inode, struct file *filp)
{
	struct iacore_priv *iacore;
	struct iacore_cdev_priv *cdev_iacore;
	struct iacore_voice_sense *voice_sense;
	unsigned major;
	unsigned minor;
	int err;
	u8 preset;

	pr_debug("\n");

	cdev_iacore = container_of((inode)->i_cdev, struct iacore_cdev_priv,
				   cdev_streaming);
	iacore = cdev_iacore->iacore;

	IACORE_MUTEX_LOCK(&iacore->access_lock);
#ifdef FW_SLEEP_TIMER_TEST
	/* Disable the FW_SLEEP timer.
	 * NOTE : setup_fw_sleep_timer() never returns a value other
	 * than 0. The check is for any future updates.
	 */
	err = setup_fw_sleep_timer_unlocked(iacore, 0);
	if (err)
		pr_err("FW_SLEEP timer stop failed\n");
#endif

	if ((iacore_get_power_state(iacore) == VS_BURSTING) ||
			(iacore_get_power_state(iacore) == BARGEIN_BURSTING)) {
		pr_err("Error, FW in Bursting state. returning\n");
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return 0;
	}

	voice_sense = GET_VS_PRIV(iacore);
	if (voice_sense) {
		preset = voice_sense->params.vq_preset & PRESET_MASK;
		if (preset == PRESET_VAD_ON_VQ_NO_BUFFERING) {
			pr_err("Streaming Fail. Preset = VQ + No Buffering\n");
			IACORE_MUTEX_UNLOCK(&iacore->access_lock);
			return -EINVAL;
		}
	}

	major = imajor(inode);
	minor = iminor(inode);
	if ( (major != cdev_iacore->cdev_major) ||
		(minor >= CDEV_COUNT) ) {
		pr_err("no such device major=%u minor=%u\n",
			major, minor);
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return -ENODEV;
	}

	if (inode->i_cdev != &cdev_iacore->cdev_streaming) {
		pr_err("error bad cdev field\n");
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return -ENODEV;
	}

	filp->private_data = iacore;

	if (iacore_check_and_reload_fw_unlocked(iacore)) {
		IACORE_MUTEX_UNLOCK(&iacore->access_lock);
		return -EINVAL;
	}

	err = iacore_pm_get_sync(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	if (err < 0) {
		pr_err("pm_get_sync failed: %d\n", err);
		return err;
	}

	err = streamdev_open(iacore);
	if (err)
		pr_err("error %d\n", err);

	return err;
}

static int streaming_release(struct inode *inode, struct file *filp)
{
	struct iacore_priv *iacore;
	struct iacore_voice_sense *voice_sense;
	struct iacore_cdev_priv *cdev_iacore;
	int err = 0;
	u8 preset;

	pr_debug("\n");

	iacore = (struct iacore_priv *)filp->private_data;

	voice_sense = GET_VS_PRIV(iacore);
	if (voice_sense) {
		preset = voice_sense->params.vq_preset & PRESET_MASK;
		if (preset == PRESET_VAD_ON_VQ_NO_BUFFERING) {
			pr_err("Streaming Fail. Preset = VQ + No Buffering\n");
			return -EINVAL;
		}
	}

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	if (inode->i_cdev != &cdev_iacore->cdev_streaming) {
		pr_err("bad cdev field\n");
		return -ENODEV;
	}

	err = streamdev_release(iacore);
	if (err)
		pr_err("error = %d\n", err);

	IACORE_MUTEX_LOCK(&iacore->access_lock);

	iacore_pm_put_autosuspend(iacore);
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	return err;
}

static char *streaming_consume_page(struct iacore_cdev_priv *cdev_iacore,
				    int *length)
{
	char *page = NULL;
	int chead, ctail;

	//mutex_lock(&cdev_iacore->stream_consumer_mutex);
	spin_lock(&cdev_iacore->stream_consumer_spnlk);

	chead = ACCESS_ONCE(cdev_iacore->stream_circ.head);
	ctail = cdev_iacore->stream_circ.tail;

	if (CIRC_CNT(chead, ctail, CB_SIZE) >= 1) {
		smp_read_barrier_depends();

		page = cdev_iacore->stream_circ.buf[ctail];
		*length = cdev_iacore->stream_circ.length[ctail];
		smp_mb();

		cdev_iacore->stream_circ.tail = (ctail + 1) & (CB_SIZE - 1);
	}

	//mutex_unlock(&cdev_iacore->stream_consumer_mutex);
	spin_unlock(&cdev_iacore->stream_consumer_spnlk);

	return page;
}

ssize_t streaming_read(struct file *filp, char __user *buf,
			      size_t count, loff_t *f_pos)
{
	struct iacore_priv *iacore;
	int user_pos = 0;
	int copy_len;
	int count_remain = count;
	unsigned long bytes_read = 0;
	static int length = PAGE_SIZE;
	int err;
	u8 preset;
	struct iacore_voice_sense *voice_sense;
	struct iacore_cdev_priv *cdev_iacore;

	pr_debug("\n");

	iacore = (struct iacore_priv *)filp->private_data;
	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	voice_sense = GET_VS_PRIV(iacore);
	if (voice_sense) {
		preset = voice_sense->params.vq_preset & PRESET_MASK;
		if (preset == PRESET_VAD_ON_VQ_NO_BUFFERING) {
			pr_err("Streaming Fail. Preset = VQ + No Buffering\n");
			return -EINVAL;
		}
	}

	mutex_lock(&iacore->streaming_mutex);
	/* read a page off of the circular buffer */
	if (!cdev_iacore->stream_read_page ||
	    cdev_iacore->stream_read_off == PAGE_SIZE) {
read_next_page:
		if (cdev_iacore->stream_read_page)
			streaming_page_free(cdev_iacore,
					    cdev_iacore->stream_read_page);

		cdev_iacore->stream_read_page = streaming_consume_page(
						cdev_iacore, &length);
		while (!cdev_iacore->stream_read_page) {
			err = wait_event_interruptible_timeout(
				cdev_iacore->stream_in_q,
				(cdev_iacore->stream_read_page =
				streaming_consume_page(cdev_iacore, &length)),
				msecs_to_jiffies(IA_STREAMING_READ_TIMEOUT));

			if (err == -ERESTARTSYS) {
				/* return short read or -EINTR */
				if (count - count_remain > 0)
					err = count - count_remain;
				else
					err = -EINTR;

				pr_err("wait event err %d\n", err);
				goto ERR_OUT;
			} else if (err == 0) {
				pr_err("wait event timeout\n");
				goto ERR_OUT;
			}
		}

		cdev_iacore->stream_read_off = 0;
	}

	while (count_remain > 0) {
		copy_len = min((int)count_remain, (int) length -
			cdev_iacore->stream_read_off);

		err = copy_to_user(buf + user_pos,
				   cdev_iacore->stream_read_page +
				   cdev_iacore->stream_read_off, copy_len);

		if (err) {
			pr_err("copy_to_user = %d\n", err);
			err = -EFAULT;
			goto ERR_OUT;
		}

		user_pos += copy_len;
		cdev_iacore->stream_read_off += copy_len;
		count_remain -= copy_len;
		bytes_read += copy_len;

		if (cdev_iacore->stream_read_off == PAGE_SIZE &&
		    count_remain > 0)
			goto read_next_page;

		if (length < PAGE_SIZE) {
			pr_err("size is less than PAGE_SIZE %d\n", length);
			break;
		}
	}

	mutex_unlock(&iacore->streaming_mutex);
	return bytes_read;

ERR_OUT:
	mutex_unlock(&iacore->streaming_mutex);
	return err;
}

static int streaming_producer(void *ptr)
{
	struct iacore_priv *iacore;
	char *buf;
	char *consume_page;
	int rlen = 0;		/* bytes read into buf buffer */
	int rlen_last = 0;	/* bytes read on last read call */
	int length;
	int chead, ctail;
	int data_ready = 1;
#if (IACORE_STREAMING_USE_READ_WAIT == 0)
	int retry = IACORE_STREAMING_FAIL_RETRY;
#endif
	unsigned long bytes_read = 0;
	struct iacore_cdev_priv *cdev_iacore;

	pr_debug("\n");

	iacore = (struct iacore_priv *) ptr;
	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	buf = streaming_page_alloc(cdev_iacore);
	if (!buf) {
		pr_err("Failed to allocate memory\n");
		return -ENOMEM;
	}

	if (!wait_for_completion_timeout(&cdev_iacore->start_strm_compl,
				msecs_to_jiffies(IA_STRT_STRM_CMD_TOUT))) {
		pr_err("start stream cmd wait timeout\n");
		streaming_page_free(cdev_iacore, buf);
		return -ETIMEDOUT;
	}

	/*
	 * loop while the thread isn't kthread_stop'd AND
	 * keep looping after the kthread_stop to throw away any data
	 * that may be in the UART receive buffer
	 */
	do {
		if (rlen == PAGE_SIZE) {
			chead = cdev_iacore->stream_circ.head;
			ctail = ACCESS_ONCE(cdev_iacore->stream_circ.tail);

			if (CIRC_SPACE(chead, ctail, CB_SIZE) < 1) {
				/* consume oldest slot */
				pr_info("last page of stream buffer\n");
				consume_page = streaming_consume_page(
						cdev_iacore, &length);
				if (consume_page)
					streaming_page_free(cdev_iacore,
							    consume_page);

				chead = cdev_iacore->stream_circ.head;
				ctail = ACCESS_ONCE(
					cdev_iacore->stream_circ.tail);
			}

			/* insert */
			cdev_iacore->stream_circ.buf[chead] = buf;
			cdev_iacore->stream_circ.length[chead] = rlen;
			smp_wmb(); /* commit data */
			cdev_iacore->stream_circ.head = ((chead + 1) &
							 (CB_SIZE - 1));

			/* awake any reader blocked in select, poll, epoll */
			wake_up_interruptible(&cdev_iacore->stream_in_q);

			buf = streaming_page_alloc(cdev_iacore);
			if (!buf) {
				pr_err("Failed to allocate memory\n");
				return -ENOMEM;
			}
			rlen = 0;
		}
		/* avoid massive log */

		rlen_last = iacore->streamdev.read(iacore, buf + rlen,
					PAGE_SIZE - rlen);

		if (rlen_last < 0) {
#if (IACORE_STREAMING_USE_READ_WAIT == 1)
			if (iacore->streamdev.wait) {
				data_ready = iacore->streamdev.wait(iacore);
				if (data_ready > 0) {

					rlen_last = iacore->streamdev.read(
							iacore, buf + rlen,
							PAGE_SIZE - rlen);

					if (rlen_last < 0) {
						pr_err("read err strmdev: %d\n",
							rlen_last);
					} else {
						rlen += rlen_last;
					}
				}
			} else {
				pr_err("read error on streamdev: %d\n",
					rlen_last);
			}
#else
			retry--;

			if (retry <= 0) {
				pr_debug("read error %d\n", rlen_last);
				usleep_range(IA_DELAY_1MS,
						IA_DELAY_1MS + 50);
				retry = IACORE_STREAMING_FAIL_RETRY;
			}

#endif
		} else {
			rlen += rlen_last;
#if (IACORE_STREAMING_USE_READ_WAIT == 0)
			retry = IACORE_STREAMING_FAIL_RETRY;
#endif
		}
	} while (!kthread_should_stop());

	pr_info("end capture streaming data\n");
	pr_info("data ready = %d\n", data_ready);
	pr_info("bytes_read = %ld\n", bytes_read);

	streaming_page_free(cdev_iacore, buf);

	return 0;
}

static unsigned int streaming_poll(struct file *filp, poll_table *wait)
{
	struct iacore_priv *iacore = filp->private_data;
	int chead, ctail;
	unsigned int mask = 0;
	struct iacore_cdev_priv *cdev_iacore;
	struct iacore_voice_sense *voice_sense;
	u8 preset;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	voice_sense = GET_VS_PRIV(iacore);
	if (voice_sense) {
		preset = voice_sense->params.vq_preset & PRESET_MASK;
		if (preset == PRESET_VAD_ON_VQ_NO_BUFFERING) {
			pr_err("Streaming Fail. Preset = VQ + No Buffering\n");
			return -EINVAL;
		}
	}

	poll_wait(filp, &cdev_iacore->stream_in_q, wait);

	chead = ACCESS_ONCE(cdev_iacore->stream_circ.head);
	ctail = cdev_iacore->stream_circ.tail;

	if (CIRC_CNT(chead, ctail, CB_SIZE) >= 1)
		mask |= POLLIN | POLLRDNORM; /* readable */

	return mask;
}

static const struct file_operations streaming_fops = {
	.owner = THIS_MODULE,
	.read = streaming_read,
	.open = streaming_open,
	.release = streaming_release,
	.poll = streaming_poll
};


struct iacore_macro cmd_hist[IA_MAX_CMD_HISTORY_COUNT] = { {0} };
int cmd_hist_index;

static int cmd_history_open(struct inode *inode, struct file *filp)
{
	struct iacore_cdev_priv *cdev_iacore;
	struct iacore_priv *iacore;
	int rc = 0;
	unsigned major;
	unsigned minor;
	int index, i, j = 0;
	struct timespec *ts;

	pr_debug("\n");

	cdev_iacore = container_of((inode)->i_cdev, struct iacore_cdev_priv,
				   cdev_cmd_history);
	iacore = cdev_iacore->iacore;

	if (inode->i_cdev != &cdev_iacore->cdev_cmd_history) {
		pr_err("error bad cdev field\n");
		rc = -ENODEV;
		goto out;
	}

	major = imajor(inode);
	minor = iminor(inode);
	if (major != cdev_iacore->cdev_major ||
	    minor >= CDEV_COUNT) {
		pr_err("no such device major=%u minor=%u\n", major, minor);
		rc = -ENODEV;
		goto out;
	}

	filp->private_data = iacore;

	cdev_iacore->cmd_history_buf = kmalloc(CMD_HISTORY_BUF_SIZ, GFP_KERNEL);
	if (!cdev_iacore->cmd_history_buf) {
		pr_err("buffer alloc failed\n");
		return -ENOMEM;
	}

	if (filp->f_flags == O_WRONLY)
		return 0;

	pr_debug("cmd_hist_index %d\n", cmd_hist_index);
	for (i = 0; i < IA_MAX_CMD_HISTORY_COUNT; i++) {
		index = i + cmd_hist_index;
		index %= IA_MAX_CMD_HISTORY_COUNT;
		if (cmd_hist[index].cmd) {
			ts = &cmd_hist[index].timestamp;
			j += snprintf(cdev_iacore->cmd_history_buf + j,
				      PAGE_SIZE,
				      "[%5lu.%03lu] ",
				      ts->tv_sec, ts->tv_nsec / (1000*1000));
			j += snprintf(cdev_iacore->cmd_history_buf + j,
				      PAGE_SIZE,
				      "0x%04x 0x%04x; ",
				      cmd_hist[index].cmd >> 16,
				      cmd_hist[index].cmd & 0xffff);
			if (cmd_hist[index].resp)
				j += snprintf(cdev_iacore->cmd_history_buf + j,
					      PAGE_SIZE,
					      "resp = 0x%04x 0x%04x; ",
					      cmd_hist[index].resp >> 16,
					      cmd_hist[index].resp & 0xffff);
			j += snprintf(cdev_iacore->cmd_history_buf + j,
				      PAGE_SIZE, "\n");

			if (j >= CMD_HISTORY_BUF_SIZ) {
				pr_info("len > BUF_SIZ, memory corruption hit\n");
				break;
			}
		}
	}

	iacore->cmd_history_size = j;

out:
	return rc;
}

static ssize_t cmd_history_read(struct file *filp, char __user *buf,
					size_t count, loff_t *f_pos)
{
	struct iacore_priv * const iacore
			= (struct iacore_priv *)filp->private_data;
	static int done, pos;
	unsigned int size;
	int rc;
	struct iacore_cdev_priv *cdev_iacore;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	if (done || !iacore->cmd_history_size) {
		pr_debug("done\n");
		done = 0;
		pos = 0;
		return 0;
	}

	size = iacore->cmd_history_size > count ?
			count : iacore->cmd_history_size;

	rc = copy_to_user(buf, cdev_iacore->cmd_history_buf + pos, size);
	if (rc) {
		pr_err("error in copy_to_user() %d\n", rc);
		return -EFAULT;
	}

	iacore->cmd_history_size -= size;
	pos += size;

	if (!iacore->cmd_history_size)
		done = 1;

	return	size;
}

static ssize_t cmd_history_write(struct file *filp, const char __user *user_buf,
				size_t count, loff_t *f_pos)
{
	int err;
	struct iacore_priv *iacore = (struct iacore_priv *)filp->private_data;
	struct iacore_cdev_priv *cdev_iacore;
	char buf[32];
	size_t buf_size;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	if (!cdev_iacore->cmd_history_buf) {
		pr_err("Invalid Command History buffer\n");
		return -EINVAL;
	}

	buf_size = min(count, (sizeof(buf)-1));
	err = copy_from_user(buf, user_buf, buf_size);
	if (err) {
		pr_err("copy_from_user err: %d\n", err);
		goto out;
	}

	buf[buf_size] = 0;
	pr_debug("requested - %s\n", buf);
	if (!strncmp(buf, "clear", 5)) {
		memset(cmd_hist, 0,  IA_MAX_CMD_HISTORY_COUNT *
						sizeof(cmd_hist[0]));
		cmd_hist_index = 0;
	} else {
		pr_err("Invalid command: %s", buf);
	}
out:
	return (err) ? err : count;

}

static int cmd_history_release(struct inode *inode, struct file *filp)
{
	struct iacore_priv * const iacore
			= (struct iacore_priv *)filp->private_data;
	struct iacore_cdev_priv *cdev_iacore;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	kfree(cdev_iacore->cmd_history_buf);
	cdev_iacore->cmd_history_buf = NULL;
	iacore->cmd_history_size = 0;
	return 0;
}

static const struct file_operations cmd_history_fops = {
	.owner = THIS_MODULE,
	.read = cmd_history_read,
	.write = cmd_history_write,
	.open = cmd_history_open,
	.release = cmd_history_release,
};

static int cvq_open(struct inode *inode, struct file *filp)
{
	struct iacore_priv *iacore;
	struct iacore_cdev_priv *cdev_iacore;

	cdev_iacore = container_of((inode)->i_cdev, struct iacore_cdev_priv,
				   cdev_cvq);
	iacore = cdev_iacore->iacore;

	filp->private_data = iacore;

	return 0;
}

unsigned int cvq_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct iacore_priv * const iacore
			= (struct iacore_priv *)filp->private_data;
	unsigned int mask = 0;

	pr_debug("enter\n");
	poll_wait(filp, &iacore->cvq_wait_queue, wait);

	pr_debug("poll waited\n");
	if (iacore->iacore_cv_kw_detected == true) {
		pr_info("kw detected\n");
		iacore->iacore_cv_kw_detected = false;
		mask |= POLLIN | POLLRDNORM;
	}
	pr_debug("leave\n");

	return mask;
}

static const struct file_operations cvq_fops = {
	.owner = THIS_MODULE,
	.open = cvq_open,
	.poll = cvq_poll,
	.unlocked_ioctl	= ia_cvq_ioctl,
	.compat_ioctl	= ia_cvq_compat_ioctl,
};

static int raw_open(struct inode *inode, struct file *filp)
{
	struct iacore_priv *iacore;
	struct iacore_cdev_priv *cdev_iacore;
#ifdef FW_SLEEP_TIMER_TEST
	int err;
#endif
	cdev_iacore = container_of((inode)->i_cdev, struct iacore_cdev_priv,
				   cdev_raw);
	iacore = cdev_iacore->iacore;

	pr_info("\n");

	IACORE_MUTEX_LOCK(&iacore->access_lock);
#ifdef FW_SLEEP_TIMER_TEST
	/* Disable the FW_SLEEP timer.
	 * NOTE : setup_fw_sleep_timer() never returns a value other
	 * than 0. The check is for any future updates.
	 */
	err = setup_fw_sleep_timer_unlocked(iacore, 0);
	if (err)
		pr_err("Stop FW_SLEEP timer failed\n");
#endif
	IACORE_MUTEX_UNLOCK(&iacore->access_lock);

	filp->private_data = iacore;

	iacore->raw_buffered_read_sts = false;

	INC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
	err = iacore_datablock_open(iacore);
	if (err)
		pr_err("open failed %d\n", err);
	return err;
}

static int raw_release(struct inode *inode, struct file *filp)
{
	struct iacore_priv * const iacore
			= (struct iacore_priv *)filp->private_data;

	iacore->raw_buffered_read_sts = false;
	iacore_datablock_close(iacore);
	DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore);
	return 0;
}

static const struct file_operations raw_fops = {
	.owner = THIS_MODULE,
	.open = raw_open,
	.read = raw_read,
	.write = raw_write,
	.unlocked_ioctl	= raw_ioctl,
	.release = raw_release,
};

static int create_cdev(struct iacore_priv *iacore, struct cdev *cdev,
		       const struct file_operations *fops, unsigned int index,
		       char *cdev_name)
{
	struct iacore_cdev_priv *cdev_iacore;
	struct device *dev;
	int devno;
	int err;

	cdev_iacore = GET_CDEV_PRIV(iacore);
	if (!cdev_iacore) {
		pr_err("Invalid private pointer\n");
		return -EINVAL;
	}

	devno = MKDEV(cdev_iacore->cdev_major, cdev_iacore->cdev_minor + index);
	cdev_init(cdev, fops);
	cdev->owner = THIS_MODULE;
	err = cdev_add(cdev, devno, 1);
	if (err) {
		pr_err("failed to add cdev=%d error: %d\n",
			index, err);
		return err;
	}

	if (cdev_name == NULL) {
		dev = device_create(cdev_iacore->cdev_class, NULL, devno, NULL,
				    IACORE_CDEV_NAME "%d",
				    cdev_iacore->cdev_minor + index);
	} else {
		dev = device_create(cdev_iacore->cdev_class, NULL, devno, NULL,
				    cdev_name,
				    cdev_iacore->cdev_minor + index);
	}

	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		pr_err("device_create cdev=%d failed: %d\n",
			index, err);
		cdev_del(cdev);
		return err;
	}
	cdev_iacore->devices[index] = dev;

	return 0;
}

static void cdev_destroy(struct iacore_cdev_priv *cdev_iacore,
			 struct cdev *cdev, int index)
{
	int devno;
	devno = MKDEV(cdev_iacore->cdev_major, cdev_iacore->cdev_minor + index);
	device_destroy(cdev_iacore->cdev_class, devno);
	cdev_del(cdev);
}

int iacore_cdev_init(struct iacore_priv *iacore)
{
	struct iacore_cdev_priv *cdev_iacore;
	int err;
	dev_t dev;

	pr_info("enter.\n");

	cdev_iacore = devm_kzalloc(iacore->dev, sizeof(struct iacore_cdev_priv),
				   GFP_KERNEL);

	if (!cdev_iacore) {
		pr_err("private data allocation failed\n");
		return -ENOMEM;
	}

	iacore->cdev_priv = cdev_iacore;
	cdev_iacore->iacore = iacore;
	/* initialize to required setup values */
	cdev_iacore->cdev_major = cdev_iacore->cdev_minor = 0;

	/* reserve character device */
	err = alloc_chrdev_region(&dev, cdev_iacore->cdev_minor, CDEV_COUNT,
				  IACORE_CDEV_NAME);
	if (err) {
		pr_err("unable to allocate char dev = %d\n", err);
		goto err_chrdev;
	}

	cdev_iacore->cdev_major = MAJOR(dev);
	pr_info("char dev major = %d\n", cdev_iacore->cdev_major);

	/* register device class */
	cdev_iacore->cdev_class = class_create(THIS_MODULE, IACORE_CDEV_NAME);
	if (IS_ERR(cdev_iacore->cdev_class)) {
		err = PTR_ERR(cdev_iacore->cdev_class);
		pr_err("unable to create %s class = %d\n",
			IACORE_CDEV_NAME, err);
		goto err_class;
	}

	err = create_cdev(iacore, &cdev_iacore->cdev_raw, &raw_fops, CDEV_RAW,
			  IACORE_RAW_DEV_NAME);
	if (err)
		goto err_raw;

	err = create_cdev(iacore, &cdev_iacore->cdev_cvq, &cvq_fops, CDEV_CVQ,
			  IACORE_CVQ_DEV_NAME);
	if (err)
		goto err_cvq;
	err = create_cdev(iacore, &cdev_iacore->cdev_command, &command_fops,
			  CDEV_COMMAND, NULL);
	if (err)
		goto err_command;

	pr_debug("command cdev initialized.\n");

	err = create_cdev(iacore, &cdev_iacore->cdev_streaming, &streaming_fops,
			  CDEV_STREAMING, NULL);
	if (err)
		goto err_streaming;

	pr_debug("streaming cdev initialized.\n");

#if (IACORE_STREAMING_PRE_ALLOC_KMALLOC == 1)
	streaming_static_page_alloc(cdev_iacore);
#endif

	err = create_cdev(iacore, &cdev_iacore->cdev_cmd_history,
			  &cmd_history_fops, CDEV_CMD_HISTORY, NULL);
	if (err)
		goto err_cmd_history;

	pr_debug("cmd_history cdev initialized.\n");

	init_waitqueue_head(&cdev_iacore->stream_in_q);
	init_completion(&cdev_iacore->start_strm_compl);
	atomic_set(&cdev_iacore->cb_pages_out, 0);

	pr_info("leave.\n");

	return err;

err_cmd_history:
#if (IACORE_STREAMING_PRE_ALLOC_KMALLOC == 1)
	streaming_static_page_free(cdev_iacore);
#endif
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_streaming, CDEV_STREAMING);
err_streaming:
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_command, CDEV_COMMAND);
err_command:
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_cvq, CDEV_CVQ);
err_cvq:
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_raw, CDEV_RAW);
err_raw:
	class_destroy(cdev_iacore->cdev_class);
err_class:
	unregister_chrdev_region(MKDEV(cdev_iacore->cdev_major,
				       cdev_iacore->cdev_minor), CDEV_COUNT);
err_chrdev:
	devm_kfree(iacore->dev, cdev_iacore);
	iacore->cdev_priv = NULL;

	pr_err("setup failure: no cdevs available!\n");
	return err;
}

void iacore_cdev_cleanup(struct iacore_priv *iacore)
{
	struct iacore_cdev_priv *cdev_iacore = iacore->cdev_priv;

	if (!cdev_iacore)
		return;

#if (IACORE_STREAMING_PRE_ALLOC_KMALLOC == 1)
	streaming_static_page_free(cdev_iacore);
#endif

	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_cmd_history,
							CDEV_CMD_HISTORY);
	iacore_stop_streaming_thread(iacore);
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_streaming, CDEV_STREAMING);
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_command, CDEV_COMMAND);
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_cvq, CDEV_CVQ);
	cdev_destroy(cdev_iacore, &cdev_iacore->cdev_raw, CDEV_RAW);

	class_destroy(cdev_iacore->cdev_class);
	unregister_chrdev_region(MKDEV(cdev_iacore->cdev_major,
				       cdev_iacore->cdev_minor), CDEV_COUNT);

	devm_kfree(iacore->dev, cdev_iacore);

	iacore->cdev_priv = NULL;

}
