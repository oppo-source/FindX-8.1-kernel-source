/**
 * Copyright 2008-2013 OPPO Mobile Comm Corp., Ltd, All rights reserved.
 * VENDOR_EDIT:
 * File:device_info.c
 * ModuleName :devinfo
 * Author : wangjc
 * Date : 2013-10-23
 * Version :1.0 2.0
 * Description :add interface to get device information.
 * History :
   <version >  <time>  <author>  <desc>
   1.0                2013-10-23        wangjc        init
   2.0          2015-04-13  hantong modify as platform device  to support diffrent configure in dts
*/

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <soc/qcom/smem.h>
#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include "../../../fs/proc/internal.h"
#include <asm/uaccess.h>

#define DEVINFO_NAME "devinfo"
#define INFO_BUF_LEN 64

static int mainboard_res = 0;
static int ant_board_id = 0;
static int hight_version_type = 0;

static struct of_device_id devinfo_id[] = {
        {.compatible = "oppo-devinfo", },
        {},
};

struct devinfo_data {
    struct platform_device *devinfo;
    struct pinctrl     *pinctrl;
    struct pinctrl_state *hw_sub_gpio_sleep;
    int sub_hw_id1;
    int sub_hw_id2;
};

static struct proc_dir_entry *parent = NULL;

static void *device_seq_start(struct seq_file *s, loff_t *pos)
{
    static unsigned long counter = 0;
    if (*pos == 0) {
        return &counter;
    } else {
        *pos = 0;
        return NULL;
    }
}

static void *device_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    return NULL;
}

static void device_seq_stop(struct seq_file *s, void *v)
{
    return;
}

static int device_seq_show(struct seq_file *s, void *v)
{
    struct proc_dir_entry *pde = s->private;
    struct manufacture_info *info = pde->data;
    if (info) {
        seq_printf(s, "Device version:\t\t%s\nDevice manufacture:\t\t%s\n",
                     info->version, info->manufacture);
        if(info->fw_path)
            seq_printf(s, "Device fw_path:\t\t%s\n", info->fw_path);
    }
    return 0;
}

static struct seq_operations device_seq_ops = {
    .start = device_seq_start,
    .next = device_seq_next,
    .stop = device_seq_stop,
    .show = device_seq_show
};

static int device_proc_open(struct inode *inode, struct file *file)
{
    int ret = seq_open(file, &device_seq_ops);
    pr_err("caven %s is called\n", __func__);
    if (!ret) {
        struct seq_file *sf = file->private_data;
        sf->private = PDE(inode);
    }
    return ret;
}

static const struct file_operations device_node_fops = {
    .read =  seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
    .open = device_proc_open,
    .owner = THIS_MODULE,
};

int register_device_proc(char *name, char *version, char *manufacture)
{
    struct proc_dir_entry *d_entry;
    struct manufacture_info *info;

    if (!parent) {
        parent =  proc_mkdir("devinfo", NULL);
        if (!parent) {
            pr_err("can't create devinfo proc\n");
            return -ENOENT;
        }
    }

    info = kzalloc(sizeof*info, GFP_KERNEL);
    info->version = version;
    info->manufacture = manufacture;
    d_entry = proc_create_data(name, S_IRUGO, parent, &device_node_fops, info);
    if (!d_entry) {
        pr_err("create %s proc failed.\n", name);
        kfree(info);
        return -ENOENT;
    }
    return 0;
}

int register_devinfo(char *name, struct manufacture_info *info)
{
    struct proc_dir_entry *d_entry;
    if(!parent) {
        parent =  proc_mkdir ("devinfo", NULL);
        if(!parent) {
            pr_err("can't create devinfo proc\n");
            return -ENOENT;
        }
    }

    d_entry = proc_create_data (name, S_IRUGO, parent, &device_node_fops, info);
    if(!d_entry) {
        pr_err("create %s proc failed.\n", name);
        return -ENOENT;
    }
    return 0;
}

static void dram_type_show(void)
{
    #if 0
    struct manufacture_info dram_info;
    int *p = NULL;
    #if 0
    p  = (int *)smem_alloc2(SMEM_DRAM_TYPE, 4);
    #else
    p  = (int *)smem_alloc(SMEM_DRAM_TYPE, 4, 0, 0);
    #endif

    if (p) {
        switch (*p){
            case 0x01:
                dram_info.version = "K3QF4F40BM-FGCF FBGA";
                dram_info.manufacture = "SAMSUNG";
                break;
            case 0x06:
                dram_info.version = "H9CKNNNCPTMRPR FBGA";
                dram_info.manufacture = "HYNIX";
                break;
            default:
                dram_info.version = "unknown";
                dram_info.manufacture = "unknown";
        }
    }else{
        dram_info.version = "unknown";
        dram_info.manufacture = "unknown";
    }

    register_device_proc("ddr", dram_info.version, dram_info.manufacture);
    #endif
}

static void sub_mainboard_verify(struct devinfo_data *devinfo_data)
{
    int ret;
    int id1 = -1;
    int id2 = -1;
    static char temp_sub[INFO_BUF_LEN] = {0};
    struct device_node *np;
    struct manufacture_info audioboard_info;
    static int operator = OPERATOR_UNKOWN;

    if (!devinfo_data) {
        pr_err("devinfo_data is NULL\n");
        return;
    }
    np = devinfo_data->devinfo->dev.of_node;
    devinfo_data->sub_hw_id1 = of_get_named_gpio(np, "Hw,sub_hwid_1", 0);
    if (devinfo_data->sub_hw_id1 < 0) {
        pr_err("devinfo_data->sub_hw_id1 not specified\n");
    }
    devinfo_data->sub_hw_id2 = of_get_named_gpio(np, "Hw,sub_hwid_2", 0);
    if (devinfo_data->sub_hw_id2 < 0) {
        pr_err("devinfo_data->sub_hw_id2 not specified\n");
    }
    devinfo_data->pinctrl = devm_pinctrl_get(&devinfo_data->devinfo->dev);
    if (IS_ERR_OR_NULL(devinfo_data->pinctrl)) {
        pr_err("%s:%d Getting pinctrl handle failed\n", __func__, __LINE__);
        goto sub_mainboard_set;
    }
    devinfo_data->hw_sub_gpio_sleep = pinctrl_lookup_state(devinfo_data->pinctrl, "hw_sub_gpio_sleep");
    if (IS_ERR_OR_NULL(devinfo_data->hw_sub_gpio_sleep)) {
        pr_err("%s:%d Failed to get the suspend state pinctrl handle\n", __func__, __LINE__);
    }

    if (devinfo_data->sub_hw_id1 >= 0) {
        ret = gpio_request(devinfo_data->sub_hw_id1, "SUB_HW_ID1");
        if (ret) {
            pr_err("unable to request gpio [%d]\n", devinfo_data->sub_hw_id1);
        } else {
            id1 = gpio_get_value(devinfo_data->sub_hw_id1);
        }
    }
    if (devinfo_data->sub_hw_id2 >= 0) {
        ret = gpio_request(devinfo_data->sub_hw_id2, "SUB_HW_ID2");
        if (ret) {
            pr_err("unable to request gpio [%d]\n", devinfo_data->sub_hw_id2);
        } else {
            id2 = gpio_get_value(devinfo_data->sub_hw_id2);
        }
    }
sub_mainboard_set:
    audioboard_info.manufacture = temp_sub;
    audioboard_info.version ="Qcom";
    operator = get_Operator_Version();

    switch (get_project()) {
    case OPPO_17107:
    case OPPO_17127:
        pr_err("id1 = %d, id2 = %d\n", id1, id2);
        ant_board_id = id1*10;
        if ((id1 == 1) && (operator == OPERATOR_ALL_CHINA_CARRIER ||
            operator == OPERATOR_CHINA_MOBILE || operator == OPERATOR_FOREIGN)) {
            snprintf(audioboard_info.manufacture, INFO_BUF_LEN, "rf-match");
        } else if ((id1 == 0) && (operator == OPERATOR_FOREIGN)) {
            snprintf(audioboard_info.manufacture, INFO_BUF_LEN, "rf-match");
        } else {
            snprintf(audioboard_info.manufacture, INFO_BUF_LEN, "rf-unmatch");
        }
        break;
    default:
        snprintf(audioboard_info.manufacture, INFO_BUF_LEN, "rf-unmatch");
        break;
    }

    if (!IS_ERR_OR_NULL(devinfo_data->hw_sub_gpio_sleep)) {
        pinctrl_select_state(devinfo_data->pinctrl, devinfo_data->hw_sub_gpio_sleep);
    }

    register_device_proc("audio_mainboard", audioboard_info.version, audioboard_info.manufacture);
}

static void mainboard_verify(struct devinfo_data *devinfo_data)
{
    struct manufacture_info mainboard_info;
    int hw_opreator_version = 0;
    static char temp_manufacture[INFO_BUF_LEN] = {0};
    if (!devinfo_data) {
        pr_err("devinfo_data is NULL\n");
        return;
    }

    hw_opreator_version = get_Operator_Version();

    mainboard_info.manufacture = temp_manufacture;
    switch (get_PCB_Version()) {
    case HW_VERSION__10:
        mainboard_info.version ="10";
        snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-T0", hw_opreator_version);
        break;
    case HW_VERSION__11:
        mainboard_info.version = "11";
        snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-SA", hw_opreator_version);
        break;
    case HW_VERSION__12:
        mainboard_info.version = "12";
        snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-SA", hw_opreator_version);
        break;
    case HW_VERSION__13:
        mainboard_info.version = "13";
        snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-SB", hw_opreator_version);
        break;
    case HW_VERSION__14:
        mainboard_info.version = "14";
        snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-SC", hw_opreator_version);
        break;
    case HW_VERSION__15:
        mainboard_info.version = "15";
        snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-(T3-T4)", hw_opreator_version);
        break;
    default:
        mainboard_info.version = "UNKOWN";
        snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-UNKOWN", hw_opreator_version);
    }
    register_device_proc("mainboard", mainboard_info.version, mainboard_info.manufacture);
}

static ssize_t high_version_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;
    len = sprintf(page, "%s", hight_version_type? "Standard":"SuperVooc");

    if (len > *off) {
        len -= *off;
    }
    else
        len = 0;
    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

static struct file_operations high_version_fops = {
    .read = high_version_read_proc,
    .write = NULL,
};

static void high_version_show(struct devinfo_data *devinfo_data)
{
    int high_verion_gpio;
    struct pinctrl_state *high_active;
    struct pinctrl_state *high_sleep;
    int ret = 0;

    if (!devinfo_data) {
        pr_err("devinfo_data is NULL\n");
        return;
    }

    if (IS_ERR_OR_NULL(devinfo_data->pinctrl)) {
        pr_err("PinCtrl is NUll\n");
        return;
    }

    high_active = pinctrl_lookup_state(devinfo_data->pinctrl, "high_version_active");
    if (!IS_ERR_OR_NULL(high_active)) {
        pinctrl_select_state(devinfo_data->pinctrl, high_active);
    } else {
        pr_err("%s:%d Failed to get high_version_active\n", __func__, __LINE__);
        return;
    }

    high_verion_gpio = of_get_named_gpio(devinfo_data->devinfo->dev.of_node, "Hw,high_version", 0);

    if (high_verion_gpio >= 0) {
        ret = gpio_request(high_verion_gpio, "high_version");
        if (ret) {
            pr_err("unable to request gpio [%d]\n", high_verion_gpio);
        } else {
            hight_version_type = gpio_get_value(high_verion_gpio);
        }
    }

    high_sleep = pinctrl_lookup_state(devinfo_data->pinctrl, "high_version_sleep");;
    if (!IS_ERR_OR_NULL(high_sleep)) {
        pinctrl_select_state(devinfo_data->pinctrl, high_sleep);
    } else {
        pr_err("%s:%d Failed to get high_version_sleep\n", __func__, __LINE__);
    }

    proc_create("version", S_IRUGO, parent, &high_version_fops);
    return;
}

static ssize_t mainboard_resource_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;
    len = sprintf(page, "%d", mainboard_res);

    if (len > *off) {
        len -= *off;
    }
    else
        len = 0;
    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

struct file_operations mainboard_res_proc_fops = {
    .read = mainboard_resource_read_proc,
    .write = NULL,
};
/*#endif*/

static ssize_t ant_board_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;
    len = sprintf(page, "%d", ant_board_id);

    if (len > *off) {
        len -= *off;
    }
    else
        len = 0;
    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

struct file_operations ant_board_fops = {
    .read = ant_board_read_proc,
    .write = NULL,
};


static int devinfo_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct devinfo_data *devinfo_data = NULL;
    pr_err("this is project  %d \n", get_project());
    devinfo_data = kzalloc(sizeof(struct devinfo_data), GFP_KERNEL);
    if (devinfo_data == NULL) {
        pr_err("devinfo_data kzalloc failed\n");
        ret = -ENOMEM;
        return ret;
    }
    /*parse_dts*/
    devinfo_data->devinfo = pdev;
    /*end of parse_dts*/
    if (!parent) {
        parent =  proc_mkdir("devinfo", NULL);
        if (!parent) {
            pr_err("can't create devinfo proc\n");
            ret = -ENOENT;
        }
    }

    sub_mainboard_verify(devinfo_data);
    mainboard_verify(devinfo_data);
    dram_type_show();
    high_version_show(devinfo_data);
    proc_create("ant", S_IRUGO, parent, &ant_board_fops);
    return ret;
}

static int devinfo_remove(struct platform_device *dev)
{
    remove_proc_entry(DEVINFO_NAME, NULL);
    return 0;
}

static struct platform_driver devinfo_platform_driver = {
    .probe = devinfo_probe,
    .remove = devinfo_remove,
    .driver = {
        .name = DEVINFO_NAME,
        .of_match_table = devinfo_id,
    },
};

module_platform_driver(devinfo_platform_driver);

MODULE_DESCRIPTION("OPPO device info");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wangjc <wjc@oppo.com>");
