/************************************************************************************
 ** File: - oppo_face_common.c
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2018, OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **      face_common compatibility configuration
 **
 ** Version: 1.0
 ** Date created: 18:03:11,28/03/2018
 ** Author: Ziqing.guo@BSP.Fingerprint.Basic
 ** TAG: BSP.Fingerprint.Basic
 ** --------------------------- Revision History: --------------------------------
 **  <author>          <data>         <desc>
 ** Ziqing.guo       2018/03/28     create the file
 ** Ziqing.guo       2018/03/29     Add temperature sensor for ldmp && ir
 ** Ziqing.guo       2018/04/17     Remove request for GPIO11 && GPIO21
 ** oujinrong        2018/05/11     adapt to EVT2 and DVT machine with 10K ohm resistance
 ** oujinrong        2018/05/13     add ldmp voltage read
 ** oujinrong        2018/05/30     add proc to read real tempature
 ** Ziqing.guo       2018/06/10     Fix coverity error
 ************************************************************************************/

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <soc/qcom/smem.h>
#include <soc/oppo/oppo_project.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <soc/oppo/oppo_project.h>
#include "../include/oppo_face_common.h"
#include "../include/mx6300_tac/mx6300_tac.h"

static struct face_data *face_data_ptr = NULL;
static struct proc_dir_entry *oppo_face_common_dir = NULL;
static char* oppo_face_common_dir_name = "oppo_face_common";

static face_tempature_t last_temp = {-99999, -99999};
extern temp_table_t temp_table_47K[331];
static int pull_resist = FACE_DIVIDOR_RESISTANCE_10K;

static int face_get_resistance(uint32_t volt, uint32_t * resist) {
    *resist = (pull_resist * volt) / (FACE_VOLTAGE_TO_DEVIDE - volt);

    return FACE_OK;
}

static int face_get_temperature(uint32_t resist, int32_t * tempature) {
    int table_size = sizeof(temp_table_47K)/ sizeof(temp_table_47K[0]);
    int index = 0;

    for(index = 0; index < table_size; index++) {
        if(resist >= temp_table_47K[index].R) {
            *tempature = temp_table_47K[index].T;
            return 0;
        }
    }

    return FACE_OK;
}

static int face_convert_temperature(uint32_t volt, int32_t * tempature) {
    uint32_t resist;

    face_get_resistance(volt, &resist);
    face_get_temperature(resist, tempature);

    return FACE_OK;
}

static int face_gpio_parse_dts(struct face_data *face_data)
{
    int ret = FACE_OK;
    struct device *dev;
    struct device_node *np;

    if (!face_data || !(face_data->dev)) {
        ret = -FACE_ERROR_GENERAL;
        goto out;
    }

    dev = face_data->dev;
    np = dev->of_node;

    if (of_find_property(np, "qcom,pm8998face-vadc", NULL)) {
        face_data->pm8998_vadc_dev = qpnp_get_vadc(face_data->dev, "pm8998face");
        dev_info(dev, "%s\n", __func__);
        if (IS_ERR(face_data->pm8998_vadc_dev)) {
            ret = PTR_ERR(face_data->pm8998_vadc_dev);
            face_data->pm8998_vadc_dev = NULL;
            if (ret != -EPROBE_DEFER)
                dev_err(dev, "Couldn't get vadc ret=%d\n", ret);
            else {
                dev_err(dev, "Couldn't get vadc, try again...\n");
                goto out;
            }
        }
    }

out:
    return ret;
}

static int face_get_adc_value(face_volt_type_t face_temp_type, int* value)
{
    int ret = 0;
    struct device *dev = face_data_ptr->dev;
    struct qpnp_vadc_result results;
    enum qpnp_vadc_channels channel;

    if (face_temp_type == FACE_VOLT_LDMP_TEMPATURE) {
        channel = P_MUX6_1_1;
    } else if (face_temp_type == FACE_VOLT_IR_TEMPATURE) {
        channel = P_MUX8_1_1;
    } else if (face_temp_type == FACE_VOLT_LDMP_POWER) {
        /* This machin is EVT or before */
        if (is_project(OPPO_17107) && (get_PCB_Version() <= 2)) {
            dev_err(dev, "face unable to read volt from machine before EVT2\n");
            *value = 0;
            return -FACE_ERROR_HW_VERSION;
        } else {
            channel = P_MUX4_1_1;
        }
    } else {
        ret = -FACE_ERROR_GENERAL;
        goto out;
    }
    ret = qpnp_vadc_read(face_data_ptr->pm8998_vadc_dev, channel, &results);
    if (ret) {
        dev_err(dev, "face unable to read pm8998_vadc_dev ret = %d\n", ret);
        ret = -FACE_ERROR_GENERAL;
        goto out;
    }

    *value = (int)results.physical / 1000;
    dev_info(dev, "face_temp_volt: %d\n", *value);

out:
    return ret;
}

static int get_tempature(face_volt_type_t type, int *tempature) {
    int ret = FACE_OK;
    struct device *dev = NULL;
    uint32_t volt = 0;

    if (NULL == face_data_ptr) {
        pr_err("%s, no device\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }
    dev = face_data_ptr->dev;

    if (NULL == tempature) {
        dev_err(dev, "%s, tempature buffer is NULL.\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    ret = face_get_adc_value(type, &volt);
    if (ret) {
        dev_err(dev, "%s, read tempature failed", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    face_convert_temperature(volt, tempature);
err:
    return ret;
}

static int get_current_tempature(int *ir_tempature, int *ldmp_tempature) {
    int ret = FACE_OK;
    struct device *dev = NULL;
    uint32_t ir_value = 0, ldmp_value = 0;

    if (NULL == face_data_ptr) {
        pr_err("%s, no device\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }
    dev = face_data_ptr->dev;

    if ((NULL == ir_tempature) || (NULL == ldmp_tempature)) {
        pr_err("%s, tempature buffer is NULL.\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    ret = face_get_adc_value(FACE_VOLT_IR_TEMPATURE, &ir_value);
    if (ret) {
        pr_err("%s, read ir tempature failed", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    ret = face_get_adc_value(FACE_VOLT_LDMP_TEMPATURE, &ldmp_value);
    if (ret) {
        pr_err("%s, read ldmp tempature failed", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    face_convert_temperature(ir_value, ir_tempature);
    face_convert_temperature(ldmp_value, ldmp_tempature);
    dev_info(dev, "%s, ir tempature %d, ldmp tempature %d",
            __func__, *ir_tempature, *ldmp_tempature);
err:
    return ret;
}

static int face_update_tempature(void) {
    int ret = FACE_OK;
    int ir_tempature = 0;
    int ldmp_tempature = 0;
    struct device *dev = NULL;

    if (NULL == face_data_ptr) {
        pr_err("%s, no device\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }
    dev = face_data_ptr->dev;

    dev_info(dev, "%s start\n", __func__);

    ret = get_current_tempature(&ir_tempature, &ldmp_tempature);
    if (ret) {
        dev_err(dev, "%s, failed to get tempature\n", __func__);
        goto err;
    }
    /* tempature has changed, need to tell mx6300 to set tempature */
    if ((abs(last_temp.ir_tempature - ir_tempature) > FACE_TEMPATURE_INTERVAL) ||
            (abs(last_temp.ldmp_tempature - ldmp_tempature) > FACE_TEMPATURE_INTERVAL)) {
        ret = mx6300_tac_set_ntc(ir_tempature, ldmp_tempature);
        if (ret) {
            dev_err(dev, "%s, failed to set temp in mx6300.\n", __func__);
            goto err;
        }
        last_temp.ir_tempature = ir_tempature;
        last_temp.ldmp_tempature = ldmp_tempature;
    }

    dev_info(dev, "%s, succeed\n", __func__);
err:
    return ret;
}

int face_enable_ntc_monitoring(uint8_t enable) {
    int ret = FACE_OK;
    struct device *dev = NULL;

    if (NULL == face_data_ptr) {
        pr_err("%s, no device\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }
    dev = face_data_ptr->dev;

    if (enable) {
        /* get current tempature, if needed, update mx6300 */
        ret = face_update_tempature();
        if (ret) {
            dev_err(dev, "%s, failed to update tempature\n", __func__);
            goto err;
        }
    } else {
        /* reset the tempature */
        last_temp.ir_tempature = -99999;
        last_temp.ldmp_tempature = -99999;
    }
err:
    return ret;
}

int face_get_ldmp_power_volt(int *volt) {
    struct device *dev = face_data_ptr->dev;
    int ret = face_get_adc_value(FACE_VOLT_LDMP_POWER, volt);
    if (ret) {
        dev_err(dev, "ldmp power unable to read pm8998_vadc_dev ret = %d\n", ret);
        *volt = 0;
        return ret;
    }
    /* voltage is divided by 3 */
    *volt *= 3;
    return FACE_OK;
}

static ssize_t face_temp_ldmp_read_proc(struct file *file, char __user *buf,
        size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;
    int face_temp_volt = 0;

    struct device *dev = face_data_ptr->dev;
    int ret = face_get_adc_value(FACE_VOLT_LDMP_TEMPATURE, &face_temp_volt);
    if (ret) {
        dev_err(dev, "temp ldmp unable to read pm8998_vadc_dev ret = %d\n", ret);
        return 0;
    }

    dev_dbg(dev, "face_temp_ldmp_volt: %d\n", face_temp_volt);

    len = sprintf(page, "%d", face_temp_volt);

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

static ssize_t face_temp_ir_read_proc(struct file *file, char __user *buf,
        size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;
    int face_temp_volt = 0;

    struct device *dev = face_data_ptr->dev;
    int ret = face_get_adc_value(FACE_VOLT_IR_TEMPATURE, &face_temp_volt);
    if (ret) {
        dev_err(dev, "temp ir unable to read pm8998_vadc_dev ret = %d\n", ret);
        return 0;
    }

    dev_dbg(dev, "face_temp_ir_volt: %d\n", face_temp_volt);

    len = sprintf(page, "%d", face_temp_volt);

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

static ssize_t face_volt_ldmp_power_read_proc(struct file *file, char __user *buf,
        size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;
    int face_ldmp_power_volt = 0;

    struct device *dev = face_data_ptr->dev;
    int ret = face_get_ldmp_power_volt(&face_ldmp_power_volt);
    if (ret) {
        dev_err(dev, "ldmp power unable to read pm8998_vadc_dev ret = %d\n", ret);
        return 0;
    }

    dev_dbg(dev, "face_ldmp_power_volt: %d\n", face_ldmp_power_volt);

    len = sprintf(page, "%d", face_ldmp_power_volt);

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

static ssize_t face_tempature_ir_sensor_power_read_proc(
        struct file *file, char __user *buf,
        size_t count, loff_t *off)
{
    int ret = 0;
    char page[256] = {0};
    int len = 0;
    int ir_tempature = 0;

    struct device *dev = face_data_ptr->dev;

    dev_info(dev, "%s start\n", __func__);

    ret = get_tempature(FACE_VOLT_IR_TEMPATURE, &ir_tempature);
    if (ret) {
        dev_err(dev, "%s, failed to get tempature\n", __func__);
        len = sprintf(page, "%d", -999999);
        goto err;
    }

    dev_dbg(dev, "face_temp_ir_ir_sensor_tempature: %d\n", ir_tempature);

    len = sprintf(page, "%d", ir_tempature);
err:
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

static ssize_t face_tempature_ldmp_power_read_proc(
        struct file *file, char __user *buf,
        size_t count, loff_t *off)
{
    int ret = 0;
    char page[256] = {0};
    int len = 0;
    int ldmp_tempature = 0;

    struct device *dev = face_data_ptr->dev;

    dev_info(dev, "%s start\n", __func__);

    ret = get_tempature(FACE_VOLT_LDMP_TEMPATURE, &ldmp_tempature);
    if (ret) {
        dev_err(dev, "%s, failed to get tempature\n", __func__);
        len = sprintf(page, "%d", -999999);
        goto err;
    }


    dev_dbg(dev, "face_temp_ldmp_tempature: %d\n", ldmp_tempature);

    len = sprintf(page, "%d", ldmp_tempature);
err:
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

static struct file_operations face_temp_ldmp_proc_fops = {
    .read = face_temp_ldmp_read_proc,
};

static struct file_operations face_temp_ir_proc_fops = {
    .read = face_temp_ir_read_proc,
};

static struct file_operations face_volt_ldmp_power_proc_fops = {
    .read = face_volt_ldmp_power_read_proc,
};

static struct file_operations face_tempature_ldmp_power_proc_fops = {
    .read = face_tempature_ldmp_power_read_proc,
};

static struct file_operations face_tempature_ir_sensor_power_proc_fops = {
    .read = face_tempature_ir_sensor_power_read_proc,
};

static ssize_t ntc_enable_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data) {
    char buf[8] = {0};
    int ret = 0;
    uint8_t enable_ntc = 0;
    struct device *dev = NULL;

    if (NULL == face_data_ptr) {
        pr_err("%s, no device\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }
    dev = face_data_ptr->dev;

    if (len > 8) {
        len = 8;
    }
    if (copy_from_user(buf, (void __user *)buff, sizeof(buf))) {
        dev_err(dev, "proc write error.\n");
        return -EFAULT;
    }
    enable_ntc = simple_strtoul(buf, NULL, 10);
    ret = face_enable_ntc_monitoring(enable_ntc);
    if (ret < 0) {
        dev_err(dev, "%s flash write failed %d\n", __func__, __LINE__);
    }
err:
    return len;
}

static const struct file_operations ntc_enable_fops = {
    .owner      = THIS_MODULE,
    .write      = ntc_enable_proc_write,
};

static int face_register_proc_fs(struct face_data *face_data)
{
    struct proc_dir_entry *pentry;

    /*  make the dir /proc/oppo_face_common  */
    oppo_face_common_dir =  proc_mkdir(oppo_face_common_dir_name, NULL);
    if(!oppo_face_common_dir) {
        dev_err(face_data->dev,"can't create oppo_face_common_dir proc\n");
        return FACE_ERROR_GENERAL;
    }

    /*  make the proc /proc/oppo_face_common/face_temp1  */
    pentry = proc_create("face_temp_ldmp", 0444, oppo_face_common_dir, &face_temp_ldmp_proc_fops);
    if(!pentry) {
        dev_err(face_data->dev,"create face temp ldmp proc failed.\n");
        return FACE_ERROR_GENERAL;
    }

    /*  make the proc /proc/oppo_face_common/face_temp2  */
    pentry = proc_create("face_temp_ir", 0444, oppo_face_common_dir, &face_temp_ir_proc_fops);
    if(!pentry) {
        dev_err(face_data->dev,"create face temp ir proc failed.\n");
        return FACE_ERROR_GENERAL;
    }

    /*  make the proc /proc/oppo_face_common/face_ldmp_power  */
    pentry = proc_create("face_ldmp_power", 0444, oppo_face_common_dir, &face_volt_ldmp_power_proc_fops);
    if(!pentry) {
        dev_err(face_data->dev,"create face ldmp power volt proc failed.\n");
        return FACE_ERROR_GENERAL;
    }

    pentry = proc_create("face_ntc_svc", 0660, oppo_face_common_dir, &ntc_enable_fops);
    if(!pentry) {
        dev_err(face_data->dev,"create face ntc svc proc failed.\n");
        return FACE_ERROR_GENERAL;
    }

    pentry = proc_create("face_ldmp_tempature", 0444,
            oppo_face_common_dir, &face_tempature_ldmp_power_proc_fops);
    if(!pentry) {
        dev_err(face_data->dev,"create face ldmp tempature proc failed.\n");
        return FACE_ERROR_GENERAL;
    }
    pentry = proc_create("face_ir_sensor_tempature", 0444,
            oppo_face_common_dir, &face_tempature_ir_sensor_power_proc_fops);
    if(!pentry) {
        dev_err(face_data->dev,"create face ir sensor tempature proc failed.\n");
        return FACE_ERROR_GENERAL;
    }

    return FACE_OK;
}

static int oppo_face_common_probe(struct platform_device *face_dev)
{
    int ret = 0;
    struct device *dev = &face_dev->dev;
    struct face_data *face_data = NULL;

    face_data = devm_kzalloc(dev, sizeof(struct face_data), GFP_KERNEL);
    if (face_data == NULL) {
        dev_err(dev, "face_data kzalloc failed\n");
        ret = -ENOMEM;
        goto exit;
    }

    face_data->dev = dev;
    face_data_ptr = face_data;
    ret = face_gpio_parse_dts(face_data);
    if (ret) {
        goto exit;
    }

    ret = face_register_proc_fs(face_data);
    if (ret) {
        goto exit;
    }

    /* EVT pull resist 47K ohm */
    if (is_project(OPPO_17107) && (get_PCB_Version() == 2)) {
        pull_resist = FACE_DIVIDOR_RESISTANCE_47K;
        dev_dbg(dev, "%s is EVT machine, pull resistance 47kohm\n", __func__);
    }
    /* EVT2 + DVT pull resist 10K ohm */
    else {
        pull_resist = FACE_DIVIDOR_RESISTANCE_10K;
        dev_dbg(dev, "%s is not EVT machine, pull resistance 10kohm", __func__);
    }

    dev_info(dev, "oppo_face_common probe ok ret = %d\n", ret);
    return FACE_OK;

exit:

    if (oppo_face_common_dir) {
        remove_proc_entry(oppo_face_common_dir_name, NULL);
    }

    dev_err(dev, "face_data probe failed ret = %d\n", ret);
    if (face_data) {
        devm_kfree(dev, face_data);
    }

    return ret;
}

static int oppo_face_common_remove(struct platform_device *pdev)
{
    return FACE_OK;
}

static struct of_device_id oppo_face_common_match_table[] = {
    {   .compatible = "oppo,face-common", },
    {}
};

static struct platform_driver oppo_face_common_driver = {
    .probe = oppo_face_common_probe,
    .remove = oppo_face_common_remove,
    .driver = {
        .name = "oppo_face_common",
        .owner = THIS_MODULE,
        .of_match_table = oppo_face_common_match_table,
    },
};

module_platform_driver(oppo_face_common_driver);
