/************************************************************************************
 ** File: - SDM660.LA.1.0\android\kernel\msm-4.4\drivers\soc\oppo\oppo_fp_common\oppo_fp_common.c
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **      secure_common compatibility configuration
 **
 ** Version: 1.0
 ** Date created: 18:03:11,02/11/2017
 ** Author: Ziqing.guo@Prd.BaseDrv
 **
 ** --------------------------- Revision History: --------------------------------
 **  <author>         <data>         <desc>
 **  Bin.Li         2017/11/17     create the file
 **  Bin.Li         2017/11/18     add for mt6771
 **  Ziqing.guo     2018/03/12     fix the problem for coverity CID 16731
 **  Hongdao.yu     2018/05/01     remove fp engineering mode
 **  Ping.Liu       2018/06/22     compatible with SDM670/SDM710.
 ************************************************************************************/

#include <linux/module.h>
#include <linux/proc_fs.h>

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6763 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6771
#include <sec_boot_lib.h>
#else
#include <soc/qcom/smem.h>
#endif

#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/string.h>
#include "../include/oppo_secure_common.h"

#define OEM_FUSE_OFF        "0"
#define OEM_FUSE_ON         "1"
#define UNKNOW_FUSE_VALUE   "unkown fuse"
#define FUSE_VALUE_LEN      15

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 660 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 845 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 670 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 710  /* sdm660, sdm845, sdm670, sdm710 */
#define OEM_SEC_BOOT_REG 0x780350
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x78019c
#define OEM_SEC_OVERRIDE_1_REG 0x7860C4
#define OEM_OVERRIDE_1_ENABLED_VALUE 0xffffffff
#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8953 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8976  /* msm8953, 8976pro */
#define OEM_SEC_BOOT_REG 0xA0154
#endif

static struct proc_dir_entry *oppo_secure_common_dir = NULL;
static char* oppo_secure_common_dir_name = "oppo_secure_common";
static struct secure_data *secure_data_ptr = NULL;
static char g_fuse_value[FUSE_VALUE_LEN] = UNKNOW_FUSE_VALUE ;

static secure_type_t get_secureType(void)
{
        secure_type_t secureType = SECURE_BOOT_UNKNOWN;

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 660 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 845 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 670 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 710  /* sdm660, sdm845, sdm670, sdm710 */

        void __iomem *oem_config_base;
        uint32_t secure_oem_config1 = 0;
        uint32_t secure_oem_config2 = 0;
        oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
        secure_oem_config1 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        dev_err(secure_data_ptr->dev,"secure_oem_config1 0x%x\n", secure_oem_config1);

        oem_config_base = ioremap(OEM_SEC_ENABLE_ANTIROLLBACK_REG, 4);
        secure_oem_config2 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        dev_err(secure_data_ptr->dev,"secure_oem_config2 0x%x\n", secure_oem_config2);

        if (secure_oem_config1 == 0) {
                secureType = SECURE_BOOT_OFF;
        } else if (secure_oem_config2 == 0) {
                secureType = SECURE_BOOT_ON_STAGE_1;
        } else {
                secureType = SECURE_BOOT_ON_STAGE_2;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8953 /*msm8953*/

        void __iomem *oem_config_base;
        uint32_t secure_oem_config1 = 0;
        oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
        secure_oem_config1 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        dev_err(secure_data_ptr->dev,"secure_oem_config1 0x%x\n", secure_oem_config1);

        if (secure_oem_config1 == 0) {
                secureType = SECURE_BOOT_OFF;
        } else if (0 == strcmp(g_fuse_value, OEM_FUSE_OFF)) {
                secureType = SECURE_BOOT_ON_STAGE_1;
        } else if (0 == strcmp(g_fuse_value, OEM_FUSE_ON)) {
                secureType = SECURE_BOOT_ON_STAGE_2;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8976 /*msm8976pro*/

        void __iomem *oem_config_base;
        uint32_t secure_oem_config1 = 0;
        oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
        secure_oem_config1 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        dev_err(secure_data_ptr->dev,"secure_oem_config1 0x%x\n", secure_oem_config1);

        if (secure_oem_config1 == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6763

        if (g_hw_sbcen == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6771

        if (g_hw_sbcen == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#endif

        return secureType;
}

static ssize_t secureType_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
        char page[256] = {0};
        int len = 0;
        secure_type_t secureType = get_secureType();

        len = sprintf(page, "%d", secureType);

        if (len > *off) {
                len -= *off;
        }
        else {
                len = 0;
        }
        if (copy_to_user(buf, page, (len < count ? len : count))) {
                return -EFAULT;
        }
        *off += len < count ? len : count;
        return (len < count ? len : count);
}

static ssize_t secureType_write_proc(struct file *filp, const char __user *buf,
                size_t count, loff_t *offp)
{
        size_t local_count;
        if (count <= 0) {
                return 0;
        }
        strcpy(g_fuse_value, UNKNOW_FUSE_VALUE);

        local_count = (FUSE_VALUE_LEN - 1) < count ? (FUSE_VALUE_LEN - 1) : count;
        if (copy_from_user(g_fuse_value , buf, local_count) != 0) {
                dev_err(secure_data_ptr->dev, "write oem fuse value fail\n");
                return -EFAULT;
        }
        g_fuse_value[local_count] = '\0';
        dev_info(secure_data_ptr->dev, "write oem fuse value = %s\n", g_fuse_value);
        return count;
}

static struct file_operations secureType_proc_fops = {
        .read = secureType_read_proc,
        .write = secureType_write_proc,
};

static ssize_t secureSNBound_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
        char page[256] = {0};
        int len = 0;
        secure_device_sn_bound_state_t secureSNBound_state = SECURE_DEVICE_SN_BOUND_UNKNOWN;

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 660 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 845 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 670 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 710  /* sdm660, sdm845, sdm670, sdm710 */

        void __iomem *oem_config_base;
        uint32_t secure_override1_config = 0;
        oem_config_base = ioremap(OEM_SEC_OVERRIDE_1_REG, 4);
        secure_override1_config = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        dev_info(secure_data_ptr->dev,"secure_override1_config 0x%x\n", secure_override1_config);

        if (get_secureType() == SECURE_BOOT_ON_STAGE_2 && secure_override1_config != OEM_OVERRIDE_1_ENABLED_VALUE) {
                secureSNBound_state = SECURE_DEVICE_SN_BOUND_OFF; /*secure stage2 devices not bind serial number*/
        } else {
                secureSNBound_state = SECURE_DEVICE_SN_BOUND_ON;  /*secure stage2 devices bind serial number*/
        }

#endif

        len = sprintf(page, "%d", secureSNBound_state);

        if (len > *off) {
                len -= *off;
        }
        else {
                len = 0;
        }

        if (copy_to_user(buf, page, (len < count ? len : count))) {
                return -EFAULT;
        }
        *off += len < count ? len : count;
        return (len < count ? len : count);
}


static struct file_operations secureSNBound_proc_fops = {
        .read = secureSNBound_read_proc,
};

static int secure_register_proc_fs(struct secure_data *secure_data)
{
        struct proc_dir_entry *pentry;

        /*  make the dir /proc/oppo_secure_common  */
        oppo_secure_common_dir =  proc_mkdir(oppo_secure_common_dir_name, NULL);
        if(!oppo_secure_common_dir) {
                dev_err(secure_data->dev,"can't create oppo_secure_common_dir proc\n");
                return SECURE_ERROR_GENERAL;
        }

        /*  make the proc /proc/oppo_secure_common/secureType  */
        pentry = proc_create("secureType", 0664, oppo_secure_common_dir, &secureType_proc_fops);
        if(!pentry) {
                dev_err(secure_data->dev,"create secureType proc failed.\n");
                return SECURE_ERROR_GENERAL;
        }

        /*  make the proc /proc/oppo_secure_common/secureSNBound  */
        pentry = proc_create("secureSNBound", 0444, oppo_secure_common_dir, &secureSNBound_proc_fops);
        if(!pentry) {
                dev_err(secure_data->dev,"create secureSNBound proc failed.\n");
                return SECURE_ERROR_GENERAL;
        }

        return SECURE_OK;
}

static int oppo_secure_common_probe(struct platform_device *secure_dev)
{
        int ret = 0;
        struct device *dev = &secure_dev->dev;
        struct secure_data *secure_data = NULL;

        secure_data = devm_kzalloc(dev,sizeof(struct secure_data), GFP_KERNEL);
        if (secure_data == NULL) {
                dev_err(dev,"secure_data kzalloc failed\n");
                ret = -ENOMEM;
                goto exit;
        }

        secure_data->dev = dev;
        secure_data_ptr = secure_data;

        ret = secure_register_proc_fs(secure_data);
        if (ret) {
                goto exit;
        }
        return SECURE_OK;

exit:

        if (oppo_secure_common_dir) {
                remove_proc_entry(oppo_secure_common_dir_name, NULL);
        }

        dev_err(dev,"secure_data probe failed ret = %d\n",ret);
        if (secure_data) {
                devm_kfree(dev, secure_data);
        }

        return ret;
}

static int oppo_secure_common_remove(struct platform_device *pdev)
{
        return SECURE_OK;
}

static struct of_device_id oppo_secure_common_match_table[] = {
        {   .compatible = "oppo,secure_common", },
        {}
};

static struct platform_driver oppo_secure_common_driver = {
        .probe = oppo_secure_common_probe,
        .remove = oppo_secure_common_remove,
        .driver = {
                .name = "oppo_secure_common",
                .owner = THIS_MODULE,
                .of_match_table = oppo_secure_common_match_table,
        },
};

static int __init oppo_secure_common_init(void)
{
        return platform_driver_register(&oppo_secure_common_driver);
}

static void __exit oppo_secure_common_exit(void)
{
        platform_driver_unregister(&oppo_secure_common_driver);
}

fs_initcall(oppo_secure_common_init);
module_exit(oppo_secure_common_exit)
