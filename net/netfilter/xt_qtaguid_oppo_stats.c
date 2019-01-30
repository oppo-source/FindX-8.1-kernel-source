/***********************************************************
** Copyright (C), 2008-2018, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - xt_qtaguid_oppo_stats.c
** Description: Add a proc node to support the function that make
**              a backup of netstat before sleep
**
** Version: 1.0
** Date : 2018/01/11
** Author: Yunqing.Zeng@BSP.Power.Basic
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** zengyunqing 2018/01/11 1.0 build this module
****************************************************************/

#include <linux/file.h>
#include <linux/inetdevice.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define QTAGUID_STATS_BUFFER_SIZE (1024*100)
#define QTAGUID_STATS_BUFFER_RESERVED (10)
struct qtaguid_stats_desc {
	bool enabled;
	char *buf_base;
	unsigned int buf_size;
	unsigned int wr_offset;
	struct mutex buf_mlock;
};

struct proc_dir_entry * get_xt_qtaguid_procdir(void);
static struct qtaguid_stats_desc net_stats;
static struct proc_dir_entry *remote_xt_qtaguid_procdir;

static struct proc_dir_entry *xt_qtaguid_statsbackup_file;
static unsigned int proc_statsbackup_perms = S_IRUGO;

static int net_snapshot_proc_show(struct seq_file *m, void *v)
{
	mutex_lock(&net_stats.buf_mlock);
	if(net_stats.enabled) {
		seq_printf(m, "%s", net_stats.buf_base);
	} else {
		seq_printf(m, "%s", "init buffer error\n");
	}
	mutex_unlock(&net_stats.buf_mlock);
	return 0;
}

static char restart_string[] = "OPPO_MARK_RESTART";
static char command_string_user[sizeof(restart_string)];
static ssize_t net_snapshot_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	size_t cp_count = 0, cmd_real_len = 0;
	unsigned long ret_count = 0, real_write = 0;

	mutex_lock(&net_stats.buf_mlock);
	if(count == sizeof(restart_string)) {
		memset(command_string_user, 0, sizeof(command_string_user));
		cmd_real_len = sizeof(command_string_user) < count ? sizeof(command_string_user) : count ;
		if(!copy_from_user(command_string_user, buffer, cmd_real_len)) {
			if(!memcmp(restart_string, command_string_user, cmd_real_len - 1)) {
				//pr_info("%s: wr_offset restart\n", __func__);
				net_stats.wr_offset = 0;
				snprintf(net_stats.buf_base, net_stats.buf_size, "restart state\n");
				mutex_unlock(&net_stats.buf_mlock);
				return count;
			}
		} else {
			pr_info("%s: warning line%d\n", __func__, __LINE__);
		}
	}

	cp_count = count < net_stats.buf_size - net_stats.wr_offset ? count : net_stats.buf_size - net_stats.wr_offset;
	if(cp_count < 0)
		cp_count = 0;

	ret_count = copy_from_user(&net_stats.buf_base[net_stats.wr_offset], buffer, cp_count);
	if(ret_count >= 0) {
		real_write = cp_count - ret_count;
	}

	net_stats.wr_offset += real_write;
	if(net_stats.wr_offset > net_stats.buf_size) {
		net_stats.wr_offset = net_stats.buf_size;
	}

	net_stats.buf_base[net_stats.wr_offset] = '\0';
	mutex_unlock(&net_stats.buf_mlock);
	return count;
}

static int net_snapshot_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, net_snapshot_proc_show, PDE_DATA(inode));
}


static const struct file_operations net_snapshot_proc_fops = {
	.open		= net_snapshot_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= net_snapshot_proc_write,
};


static int __init xt_qtaguid_oppo_stats_init(void)
{
	mutex_init(&net_stats.buf_mlock);

	remote_xt_qtaguid_procdir = get_xt_qtaguid_procdir();
	if(!remote_xt_qtaguid_procdir) {
		pr_err("%s: failed to get remote_xt_qtaguid_procdir\n", __func__);
		goto xt_qtaguid_procdir_null;
	}

	net_stats.buf_base = (char*)kzalloc(QTAGUID_STATS_BUFFER_SIZE, GFP_KERNEL);
	if(!net_stats.buf_base) {
		pr_err("%s: malloc buffer failed\n", __func__);
		goto xt_qtaguid_buffer_error;
	} else {
		net_stats.buf_size = QTAGUID_STATS_BUFFER_SIZE - QTAGUID_STATS_BUFFER_RESERVED;
		snprintf(net_stats.buf_base, net_stats.buf_size, "init_state\n");
		net_stats.wr_offset = 0;
		net_stats.enabled = true;
	}

	xt_qtaguid_statsbackup_file = proc_create_data("stats_oppobackup", proc_statsbackup_perms,
						          remote_xt_qtaguid_procdir, &net_snapshot_proc_fops, NULL);
	if (!xt_qtaguid_statsbackup_file) {
		pr_err("%s: failed to create xt_qtaguid/stats_oppobackup file\n", __func__);
		goto xt_qtaguid_statsbackup_error;
	}

	return 0;

xt_qtaguid_statsbackup_error:
	kfree(net_stats.buf_base);

xt_qtaguid_buffer_error:

xt_qtaguid_procdir_null:

	return 0;
}

late_initcall(xt_qtaguid_oppo_stats_init);