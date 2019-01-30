/************************************************************************************
 ** File: - mx6300_spi_tac.c
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2018 OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **          mx6300 spi APIs export from TEE
 **
 ** Version: 1.0
 ** Date created: 18:03:11, 08/04/2018
 ** Author: oujinrong@BSP.Fingerprint.Basic
 ** TAG: BSP.Fingerprint.Basic
 ** --------------------------- Revision History: --------------------------------
 **  <author>               <data>                      <desc>
 **  oujinrong              2018/04/08                  create the file
 **  oujinrong              2018/04/23                  add sleep mode
 **  oujinrong              2018/04/24                  not power on when ftm
 **  oujinrong              2018/05/02                  add mes auth
 **  oujinrong              2018/05/07                  add post init
 **  oujinrong              2018/05/31                  divide mx driver into normal command and file command
 **  oujinrong              2018/06/04                  control flash light when wake.
 **  oujinrong              2018/06/11                  add ldmp sn read
 **  oujinrong              2018/06/12                  shutdown ta after send command
 **  oujinrong              2018/06/12                  add stereo load
 **  oujinrong              2018/06/13                  add wake up retry
 **  oujinrong              2018/07/08                  add power down
 **  oujinrong              2018/07/29                  add power down on shutdown
 ************************************************************************************/

#define pr_fmt(fmt) "mx6300: " fmt

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <soc/qcom/smem.h>
#include <soc/oppo/oppo_project.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include "qseecom_kernel.h"

#include "../include/oppo_face_common.h"
#include "../include/mx6300_tac/mx6300_tac_ioctl.h"
#include "../include/mx6300_tac/mx6300_tac.h"

#include "boot_mode.h"

static struct mx6300_dev_data *mx6300_dev_ptr = NULL;
static struct qseecom_handle *mx_spi_handle = NULL;
static struct qseecom_handle *mx_file_handle = NULL;

static struct mutex mx_spi_sbuf_lock;
static struct mutex mx_file_sbuf_lock;

static uint8_t fw_buf[MX6300_FIRMWARE_BUF_SIZE] = {0};
static uint32_t fw_buf_size = 0;

extern int aw36413_power_enable(int enable);
extern int lm3643_power_enable(int enable);
extern int lm3644_power_enable(int enable);

static int mx6300_config_single_vreg(struct device *dev,struct regulator *reg_ptr, int config) {
    int ret = 0;
    if (!dev || !reg_ptr) {
        pr_err("%s: get failed NULL parameter\n", __func__);
        goto vreg_get_fail;
    }

    if (config) {
        pr_debug("%s enable\n", __func__);
        ret = regulator_enable(reg_ptr);
        if (ret < 0) {
            pr_err("%s regulator_enable failed\n", __func__);
            goto vreg_unconfig;
        }
    }
    else {
        pr_debug("%s disable\n", __func__);
        ret = regulator_disable(reg_ptr);
        if (ret < 0) {
            pr_err("%s regulator_enable failed\n", __func__);
            goto vreg_unconfig;
        }
        regulator_put(reg_ptr);
        reg_ptr = NULL;
    }
    return 0;

vreg_unconfig:
    if (regulator_count_voltages(reg_ptr) > 0) {
        regulator_set_load(reg_ptr, 0);
    }

vreg_get_fail:
    pr_err("%s vreg_get_fail\n", __func__);
    return -ENODEV;
}

static int mx6300_parse_dts(struct mx6300_dev_data* mx6300_dev) {
    int ret = 0;
    struct device_node *dev_node = mx6300_dev->dev->of_node;

    mx6300_dev->debug_gpio = of_get_named_gpio(dev_node, "mx6300,MX_tstmode-gpio", 0);
    if (gpio_is_valid(mx6300_dev->debug_gpio)) {
        ret = gpio_request(mx6300_dev->debug_gpio, "mx6300-debug-gpio");
        if(ret) {
            pr_err("%s could not request debug gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid debug gpio:%d\n", __func__, mx6300_dev->debug_gpio);
        return -EIO;
    }

    mx6300_dev->vio1p8_gpio = of_get_named_gpio(dev_node, "mx6300,MX_VISP_VDDIO_1V8-gpio", 0);
    if (gpio_is_valid(mx6300_dev->vio1p8_gpio)) {
        ret = gpio_request(mx6300_dev->vio1p8_gpio, "mx6300-VISP_VDDIO_1V8-gpio");
        if(ret) {
            pr_err("%s could not request VISP_VDDIO_1V8 gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid VISP_VDDIO_1V8 gpio\n", __func__);
        return -EIO;
    }

    mx6300_dev->vio1v8_gpio = of_get_named_gpio(dev_node,"mx6300,MX_VDDIO_1V8-gpio", 0);
    if (gpio_is_valid(mx6300_dev->vio1v8_gpio)) {
        ret = gpio_request(mx6300_dev->vio1v8_gpio, "mx6300-MX_VDDIO_1V8-gpio");
        if(ret) {
            pr_err("%s could not request MX_VDDIO_1V8 gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid MX_VDDIO_1V8 gpio\n", __func__);
        return -EIO;
    }

    mx6300_dev->vdd0v9_gpio = of_get_named_gpio(dev_node,"mx6300,MX_VISP_VDD_0V9-gpio",0);
    if (gpio_is_valid(mx6300_dev->vdd0v9_gpio)) {
        ret = gpio_request(mx6300_dev->vdd0v9_gpio, "mx6300_VISP_VDD_0V9-gpio");
        if(ret) {
              pr_err("%s could not request VISP_VDD_0V9 gpio\n", __func__);
              return ret;
        }
    } else {
        pr_err("%s not valid VISP_VDD_0V9 gpio\n", __func__);
        return -EIO;
    }

    mx6300_dev->vdd1_0v9_gpio = of_get_named_gpio(dev_node, "mx6300,MX_VISP_VDD1_0V9-gpio", 0);
    if (gpio_is_valid(mx6300_dev->vdd1_0v9_gpio)) {
        ret = gpio_request(mx6300_dev->vdd1_0v9_gpio, "mx6300-VISP_VDD1_0V9-gpio");
        if(ret) {
              pr_err("%s could not request VISP_VDD1_0V9 gpio\n", __func__);
              return ret;
        }
    } else {
        pr_err("%s not valid VISP_VDD1_0V9 gpio\n", __func__);
        return -EIO;
    }

    mx6300_dev->reset_gpio = of_get_named_gpio(dev_node, "mx6300,MX_reset-gpio", 0);
    if (gpio_is_valid(mx6300_dev->reset_gpio)) {
        ret = gpio_request(mx6300_dev->reset_gpio, "mx6300_reset-gpio");
        if(ret) {
            pr_err("%s could not request reset gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid reset gpio\n", __func__);
        return -EIO;
    }

    mx6300_dev->IRAVDD_gpio = of_get_named_gpio(dev_node, "mx6300,IR_ACAMD_2V8-gpio", 0);
    if (gpio_is_valid(mx6300_dev->IRAVDD_gpio)) {
        ret = gpio_request(mx6300_dev->IRAVDD_gpio, "mx6300_IR_ACAMD_2V8-gpio");
        if(ret) {
              pr_err("%s could not request IR_ACAMD_2V8-gpio gpio\n", __func__);
              return ret;
        }
    } else {
        pr_err("%s not valid IR_ACAMD_2V8-gpio gpio\n", __func__);
        return -EIO;
    }

    mx6300_dev->IRDVDD_gpio = of_get_named_gpio(dev_node, "mx6300,IR_VCAMD_1V2-gpio", 0);
    if (gpio_is_valid(mx6300_dev->IRDVDD_gpio)) {
        ret = gpio_request(mx6300_dev->IRDVDD_gpio, "mx6300-IR_VCAMD_1V2-gpio");
        if(ret) {
              pr_err("%s could not request IR_VCAMD_1V2-gpio gpio\n", __func__);
              return ret;
        }
    } else {
        pr_err("%s not valid IR_VCAMD_1V2-gpio gpio\n", __func__);
        return -EIO;
    }

    mx6300_dev->IRPWDN_gpio = of_get_named_gpio(dev_node, "mx6300,IR_PWDN-gpio", 0);
    if (gpio_is_valid(mx6300_dev->IRPWDN_gpio)) {
        ret = gpio_request(mx6300_dev->IRPWDN_gpio, "IR_PWDN-gpio");
        if(ret) {
              pr_err("%s could not request IR_PWDN-gpio\n", __func__);
              return ret;
        }
    } else {
        pr_err("%s not valid IR_PWDN-gpio\n", __func__);
        return -EIO;
    }

    pr_err("%s parser dt ok.\n", __func__);
    return ret;
}

static int mx6300_power_on(struct mx6300_dev_data* mx6300_dev) {
    int ret = 0;
    struct regulator *reg_ptr;
    int boot_mode = get_boot_mode();

    reg_ptr = regulator_get(mx6300_dev->dev, "mx6300_IR_CAMIO");
    mx6300_config_single_vreg(mx6300_dev->dev, reg_ptr, 1);

    if (boot_mode == MSM_BOOT_MODE__FACTORY) {
        pr_err("FTM mode %s only power UP CAM_VIO\n", __func__);
        return 0;
    }

    if (gpio_is_valid(mx6300_dev->vdd0v9_gpio)) {
            ret = gpio_direction_output(mx6300_dev->vdd0v9_gpio, 1);
            if (ret) {
                pr_err("%s vdd0v9_gpio on failed\n", __func__);
                return -EIO;
            }
        }
        else {
            pr_err("%s vdd0v9_gpio invalid\n", __func__);
            return -EIO;
    }
    msleep(1);
    if (gpio_is_valid(mx6300_dev->vio1v8_gpio)) {
        ret = gpio_direction_output(mx6300_dev->vio1v8_gpio, 1);
        if (ret) {
            pr_err("%s vio1v8_gpio on failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s vio1v8_gpio invalid\n", __func__);
        return -EIO;
    }

    msleep(1);

    if(gpio_is_valid(mx6300_dev->vdd1_0v9_gpio)) {
        ret = gpio_direction_output(mx6300_dev->vdd1_0v9_gpio, 1);
        if (ret) {
            pr_err("%s vdd1_0v9_gpio on failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s vdd1_0v9_gpio invalid\n", __func__);
        return -EIO;
    }

    if (gpio_is_valid(mx6300_dev->vio1p8_gpio)) {
        ret = gpio_direction_output(mx6300_dev->vio1p8_gpio, 1);
        if (ret) {
            pr_err("%s vio1p8_gpio on failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s vio1p8_gpio invalid\n", __func__);
        return -EIO;
    }

    if (gpio_is_valid(mx6300_dev->debug_gpio)) {
        ret = gpio_direction_output(mx6300_dev->debug_gpio, 0);
        if (ret) {
            pr_err("%s debug_gpio on failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s debug_gpio invalid\n", __func__);
        return -EIO;
    }

    if (gpio_is_valid(mx6300_dev->IRAVDD_gpio)) {
        ret = gpio_direction_output(mx6300_dev->IRAVDD_gpio, 1);
        if (ret) {
            pr_err("%s IRAVDD_gpio on failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s IRAVDD_gpio invalid\n", __func__);
        return -EIO;
    }

    if (gpio_is_valid(mx6300_dev->IRDVDD_gpio)) {
        ret = gpio_direction_output(mx6300_dev->IRDVDD_gpio, 1);
        if (ret) {
            pr_err("%s IRDVDD_gpio on failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s IRDVDD_gpio invalid\n", __func__);
        return -EIO;
    }
    msleep(1);
    if (gpio_is_valid(mx6300_dev->IRPWDN_gpio)) {
        ret = gpio_direction_output(mx6300_dev->IRPWDN_gpio, 1);
        if (ret) {
            pr_err("%s IRPWDN_gpio on failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s IRPWDN_gpio invalid\n", __func__);
        return -EIO;
    }

    msleep(50);
    if (gpio_is_valid(mx6300_dev->reset_gpio)) {
        ret = gpio_direction_output(mx6300_dev->reset_gpio, 1);
        if (ret) {
            pr_err("%s reset_gpio set high failed\n", __func__);
            return -EIO;
        }
        msleep(10);
        ret = gpio_direction_output(mx6300_dev->reset_gpio, 0);
        if (ret) {
            pr_err("%s reset_gpio set low failed\n", __func__);
            return -EIO;
        }
    }
    else {
        pr_err("%s reset_gpio invalid\n", __func__);
        return -EIO;
    }

    pr_err("%s ok\n", __func__);
    return 0;
}

static void mx6300_power_down(struct mx6300_dev_data* mx6300_dev) {
    int ret = 0;
    if (mx6300_dev == NULL) {
        return;
    }

    if (gpio_is_valid(mx6300_dev->IRPWDN_gpio)) {
        ret = gpio_direction_output(mx6300_dev->IRPWDN_gpio, 0);
        if (ret) {
            pr_err("%s IRPWDN_gpio on failed\n", __func__);
        }
    } else {
        pr_err("%s IRPWDN_gpio invalid\n", __func__);
    }
    msleep(1);
    if (gpio_is_valid(mx6300_dev->IRDVDD_gpio)) {
        ret = gpio_direction_output(mx6300_dev->IRDVDD_gpio, 0);
        if (ret) {
            pr_err("%s IRDVDD_gpio on failed\n", __func__);
        }
    } else {
        pr_err("%s IRDVDD_gpio invalid\n", __func__);
    }
    if (gpio_is_valid(mx6300_dev->IRAVDD_gpio)) {
        ret = gpio_direction_output(mx6300_dev->IRAVDD_gpio, 0);
        if (ret) {
            pr_err("%s IRAVDD_gpio on failed\n", __func__);
        }
    } else {
        pr_err("%s IRAVDD_gpio invalid\n", __func__);
    }

    if (gpio_is_valid(mx6300_dev->vio1p8_gpio)) {
        ret = gpio_direction_output(mx6300_dev->vio1p8_gpio, 0);
        if (ret) {
            pr_err("%s vio1p8_gpio on failed\n", __func__);
        }
    } else {
        pr_err("%s vio1p8_gpio invalid\n", __func__);
    }

    if(gpio_is_valid(mx6300_dev->vdd1_0v9_gpio)) {
        ret = gpio_direction_output(mx6300_dev->vdd1_0v9_gpio, 0);
        if (ret) {
            pr_err("%s vdd1_0v9_gpio on failed\n", __func__);
        }
    } else {
        pr_err("%s vdd1_0v9_gpio invalid\n", __func__);
    }

    msleep(1);
    if (gpio_is_valid(mx6300_dev->vio1v8_gpio)) {
        ret = gpio_direction_output(mx6300_dev->vio1v8_gpio, 0);
        if (ret) {
            pr_err("%s vio1v8_gpio on failed\n", __func__);
        }
    } else {
        pr_err("%s vio1v8_gpio invalid\n", __func__);
    }

    if (gpio_is_valid(mx6300_dev->vdd0v9_gpio)) {
        ret = gpio_direction_output(mx6300_dev->vdd0v9_gpio, 0);
        if (ret) {
            pr_err("%s vdd0v9_gpio on failed\n", __func__);
        }
    } else {
        pr_err("%s vdd0v9_gpio invalid\n", __func__);
    }

    pr_err("%s ok\n", __func__);
}

static face_ta_mx_driver_command_t *create_spi_command(int32_t command_id) {
    face_ta_mx_driver_command_t *cmd = NULL;
    cmd = (face_ta_mx_driver_command_t *)\
        kmalloc(sizeof(face_ta_mx_driver_command_t), GFP_KERNEL);
    if (cmd == NULL) {
        return NULL;
    }
    memset(cmd, 0, sizeof(face_ta_mx_driver_command_t));

    /* fill in the header */
    cmd->header.target = TARGET_FACE_TA_MX_DRIVER;
    cmd->header.command = command_id;
    return cmd;
}

static face_ta_mx_driver_file_command_t *create_file_command(int32_t command_id) {
    face_ta_mx_driver_file_command_t *cmd = NULL;
    cmd = (face_ta_mx_driver_file_command_t *)\
        kmalloc(sizeof(face_ta_mx_driver_file_command_t), GFP_KERNEL);
    if (cmd == NULL) {
        return NULL;
    }
    memset(cmd, 0, sizeof(face_ta_mx_driver_file_command_t));

    /* fill in the header */
    cmd->header.target = TARGET_FACE_TA_MX_FILE;
    cmd->header.command = command_id;
    return cmd;
}

static int mx6300_tac_send_spi_cmd(face_ta_mx_driver_command_t *cmd,
                                uint8_t *read_buf, uint32_t size) {
    int ret = FACE_OK;
    int *response = NULL;
    face_qsee_msg_t *msg_ptr = NULL;

    mutex_lock(&mx_spi_sbuf_lock);

    if (mx_spi_handle == NULL) {
        ret = qseecom_start_app(&mx_spi_handle,
                                FACE_TA_NAME,
                                MX_SPI_SBUF_SIZE);
        if (ret < 0) {
            pr_err("%s, open facereg ta failed.\n", __func__);
            mx_spi_handle = NULL;
            ret = -FACE_ERROR_GENERAL;
            goto err;
        }
    }

    /* fill the command in sbuf */
    msg_ptr = (face_qsee_msg_t *) mx_spi_handle->sbuf;
    msg_ptr->tag = SECCAM_OPPO_CMD_NO_ION_TAG;
    memcpy(&msg_ptr->cmd, cmd, sizeof(face_ta_mx_driver_command_t));

    response = (int *) (mx_spi_handle->sbuf + sizeof(face_qsee_msg_t));

    ret = qseecom_send_command(mx_spi_handle,
        msg_ptr, sizeof(face_qsee_msg_t),
        response, sizeof(int32_t));
    if (ret) {
        pr_err("%s, qseecom send command failed\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    ret = *response;
    if (ret != FACE_OK) {
        pr_err("%s, response failed. rsp = %d\n", __func__, ret);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    ret = msg_ptr->cmd.mx_driver_cmd.response;
    if (ret != FACE_OK) {
        pr_err("%s, TA handle command response failed. rsp = %d\n", __func__, ret);
        goto err;
    }

    if (read_buf != NULL && size != 0) {
        memcpy(read_buf, msg_ptr->cmd.read_cmd.data, size);
    }
err:
    if (mx_spi_handle != NULL) {
        qseecom_shutdown_app(&mx_spi_handle);
        mx_spi_handle = NULL;
    }
    mutex_unlock(&mx_spi_sbuf_lock);
    return ret;
}

static int mx6300_tac_send_file_cmd(face_ta_mx_driver_file_command_t *cmd) {
    int ret = FACE_OK;
    int *response = NULL;
    face_qsee_mx_file_msg_t *msg_ptr = NULL;

    mutex_lock(&mx_file_sbuf_lock);

    if (mx_file_handle == NULL) {
        ret = qseecom_start_app(&mx_file_handle,
                                FACE_TA_NAME,
                                MX_FILE_SBUF_SIZE);
        if (ret < 0) {
            pr_err("%s, open facereg ta failed.\n", __func__);
            mx_file_handle = NULL;
            goto err;
        }
    }

    /* fill the command in sbuf */
    msg_ptr = (face_qsee_mx_file_msg_t *) mx_file_handle->sbuf;
    msg_ptr->tag = SECCAM_OPPO_CMD_NO_ION_TAG;
    memcpy(&msg_ptr->cmd, cmd, sizeof(face_ta_mx_driver_file_command_t));

    response = (int *) (mx_file_handle->sbuf + sizeof(face_qsee_mx_file_msg_t));

    ret = qseecom_send_command(mx_file_handle,
        msg_ptr, sizeof(face_qsee_mx_file_msg_t),
        response, sizeof(int32_t));
    if (ret) {
        pr_err("%s, qseecom send command failed\n", __func__);
        goto err;
    }

    ret = (*response);
    if (ret != FACE_OK) {
        pr_err("%s, qseecom send command response failed, ret = %d\n", __func__, ret);
        ret = -FACE_ERROR_GENERAL;
        goto err;
    }

    ret = msg_ptr->cmd.mx_driver_cmd.response;
    if (ret != FACE_OK) {
        pr_err("%s, TA handle command response failed. rsp = %d\n", __func__, ret);
        goto err;
    }

err:
    if (mx_file_handle != NULL) {
        qseecom_shutdown_app(&mx_file_handle);
        mx_file_handle = NULL;
    }
    mutex_unlock(&mx_file_sbuf_lock);
    return ret;
}

static int mx6300_tac_send_start_stream_cmd(uint32_t width, uint32_t height,
                            face_ta_mx_driver_stream_type_t type) {
    int ret = FACE_OK;
    face_ta_mx_driver_command_t *cmd = NULL;

    cmd = create_spi_command(FACE_TA_MX_DRIVER_START_STREAM);
    if (cmd == NULL) {
        pr_err("%s, create new command failed", __func__);
        return -FACE_ERROR_GENERAL;
    }

    cmd->start_cmd.stream_data.width = width;
    cmd->start_cmd.stream_data.height = height;
    cmd->start_cmd.stream_data.type = type;

    ret = mx6300_tac_send_spi_cmd(cmd, NULL, 0);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
        goto exit;
    }

    pr_err("%s, start stream succeed.\n", __func__);
exit:
    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

static int mx6300_tac_send_stop_stream_cmd(void) {
    int ret = FACE_OK;
    face_ta_mx_driver_command_t *cmd = NULL;

    cmd = create_spi_command(FACE_TA_MX_DRIVER_STOP_STREAM);
    if (cmd == NULL) {
        pr_err("%s, create new command failed", __func__);
        return -FACE_ERROR_GENERAL;
    }

    ret = mx6300_tac_send_spi_cmd(cmd, NULL, 0);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
        goto exit;
    }

    pr_err("%s, stop stream succeed.\n", __func__);
exit:
    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

static int mx6300_tac_send_enable_cmd(face_ta_mx_driver_cmd_t cmd_type,
                                        uint8_t on) {
    int ret = FACE_OK;
    face_ta_mx_driver_command_t *cmd = NULL;
    pr_debug("%s cmd type : %d", __func__, (int)cmd_type);

    cmd = create_spi_command(cmd_type);
    if (cmd == NULL) {
        pr_err("%s, create new command failed\n", __func__);
        return -FACE_ERROR_GENERAL;
    }

    cmd->set_enable_cmd.on = on;
    ret = mx6300_tac_send_spi_cmd(cmd, NULL, 0);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
    }

    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

static int mx6300_tac_send_status_cmd(face_ta_mx_driver_cmd_t cmd_type,
                                        uint32_t status) {
    int ret = FACE_OK;
    face_ta_mx_driver_command_t *cmd = NULL;
    pr_debug("%s cmd type : %d", __func__, (int)cmd_type);

    cmd = create_spi_command(cmd_type);
    if (cmd == NULL) {
        pr_err("%s, create new command failed\n", __func__);
        return -FACE_ERROR_GENERAL;
    }

    cmd->set_status_cmd.status = status;
    ret = mx6300_tac_send_spi_cmd(cmd, NULL, 0);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
    }

    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

static int mx6300_tac_send_read_cmd(face_ta_mx_driver_cmd_t cmd_type,
                    uint8_t *buf, uint32_t size, uint16_t offset, uint32_t addr) {
    int ret = FACE_OK;
    face_ta_mx_driver_command_t *cmd = NULL;
    pr_debug("%s cmd type : %d", __func__, (int)cmd_type);

    cmd = create_spi_command(cmd_type);
    if (cmd == NULL) {
        pr_err("%s, create new command failed\n", __func__);
        return -FACE_ERROR_GENERAL;
    }

    if (buf == NULL) {
        pr_err("%s, input a NULL buffer\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    if (size > MX6300_READDATA_MAXSIZE) {
        pr_err("%s, cannot read data with size %d\n",
                        __func__, size);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    cmd->read_cmd.offset = offset;
    cmd->read_cmd.addr = addr;

    ret = mx6300_tac_send_spi_cmd(cmd, buf, size);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
    }

exit:
    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

static int mx6300_tac_send_write_cmd(face_ta_mx_driver_cmd_t cmd_type,
                    uint8_t *buf, uint32_t size, uint16_t offset, uint32_t addr) {
    int ret = FACE_OK;
    face_ta_mx_driver_command_t *cmd = NULL;
    pr_debug("%s cmd type : %d", __func__, (int)cmd_type);

    cmd = create_spi_command(cmd_type);
    if (cmd == NULL) {
        pr_err("%s, create new command failed", __func__);
        return -FACE_ERROR_GENERAL;
    }

    if (buf == NULL) {
        pr_err("%s, input a NULL buffer\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    if (size > MX6300_WRITEDATA_MAXSIZE) {
        pr_err("%s, cannot write data with size %d\n",
                        __func__, size);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    cmd->write_cmd.offset = offset;
    cmd->write_cmd.addr = addr;
    cmd->write_cmd.size = size;
    memcpy(cmd->write_cmd.data, buf, size);
    ret = mx6300_tac_send_spi_cmd(cmd, NULL, 0);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
    }

exit:
    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

static int mx6300_tac_send_write_file_cmd(face_ta_mx_driver_cmd_t cmd_type,
                                            uint8_t *buf, uint32_t size) {
    int ret = FACE_OK;
    face_ta_mx_driver_file_command_t *cmd = NULL;
    pr_debug("%s cmd type : %d", __func__, (int)cmd_type);

    cmd = create_file_command(cmd_type);
    if (cmd == NULL) {
        pr_err("%s, create new command failed", __func__);
        return -FACE_ERROR_GENERAL;
    }

    if (buf == NULL) {
        pr_err("%s, input a NULL buffer\n", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    if (size > MX6300_FILEDATA_MAXSIZE) {
        pr_err("%s, cannot write data with size %d\n",
                        __func__, size);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    cmd->write_file_cmd.size = size;
    memcpy(cmd->write_file_cmd.data, buf, size);
    ret = mx6300_tac_send_file_cmd(cmd);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
    }

exit:
    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

static int mx6300_tac_send_ntc_cmd(int32_t tir, int32_t tlaser) {
    int ret = FACE_OK;
    face_ta_mx_driver_command_t *cmd = NULL;

    cmd = create_spi_command(FACE_TA_MX_DRIVER_SET_NTC_SVC);
    if (cmd == NULL) {
        pr_err("%s, create new command failed", __func__);
        return -FACE_ERROR_GENERAL;
    }

    cmd->ntc_cmd.tir = tir;
    cmd->ntc_cmd.tlaser = tlaser;
    ret = mx6300_tac_send_spi_cmd(cmd, NULL, 0);
    if (ret) {
        pr_err("%s, send command failed\n", __func__);
    }

    if (cmd) {
        kfree(cmd);
    }
    return ret;
}

int mx6300_tac_wakeup(void) {
    int ret = 0;
    int retry_count = 0;
    if ((mx6300_dev_ptr == NULL) ||
        (mx6300_dev_ptr->dev == NULL)) {
        pr_err("%s, no device", __func__);
        return -FACE_ERROR_GENERAL;
    }

    for (retry_count = 0; retry_count < MX_DRIVER_MAX_RETRY_TIMES; retry_count++) {
        /* pull mx6300 wake-gpio and send wake up SPI command in TA*/
        ret = mx6300_tac_send_enable_cmd(FACE_TA_MX_DRIVER_SET_SLEEP_WAKEUP, 0);
        if (ret) {
            pr_err("%s, failed to send wakeup command, retry %d times\n", __func__, retry_count + 1);
            ret = -FACE_ERROR_GENERAL;
            msleep(MX_DRIVER_RETRY_INTERVAL);
        } else {
            break;
        }
    }
    if (ret) {
        pr_err("%s, finally failed to wake up mx6300\n", __func__);
        goto exit;
    }

    pr_debug("%s, wake up mx6300 succeed\n", __func__);
exit:
    return ret;
}

int mx6300_tac_sleep(void) {
    int ret = 0;
    if ((mx6300_dev_ptr == NULL) ||
        (mx6300_dev_ptr->dev == NULL)) {
        pr_err("%s, no device", __func__);
        return -FACE_ERROR_GENERAL;
    }

    msleep(5);

    /* sleep mx6300 */
    ret = mx6300_tac_send_enable_cmd(FACE_TA_MX_DRIVER_SET_SLEEP_WAKEUP, 1);
    if (ret) {
        pr_err("%s, failed to sleep mx6300\n",
            __func__);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    pr_debug("%s, sleep mx6300 succeed\n", __func__);
exit:
    return ret;
}

static int mx6300_tac_power_on_laser(int enable) {
    int ret = 0;
    ret = lm3644_power_enable(enable);
    if (!ret) {
        pr_debug("%s, power %s lm3644.\n", __func__, (enable)? "on": "off");
        goto exit;
    }
    pr_err("%s, failed to power %s laser.\n", __func__, (enable)? "on": "off");
exit:
    return ret;
}

static int mx6300_tac_power_enable_flood_led(int enable) {
    int ret = 0;

    ret = lm3643_power_enable(enable);
    if (!ret) {
        pr_debug("%s, power %s lm3643.\n", __func__, (enable)? "on": "off");
        goto exit;
    }

    ret = aw36413_power_enable(enable);
    if (!ret) {
        pr_debug("%s, power %s aw36413.\n", __func__, (enable)? "on": "off");
        goto exit;
    }

    pr_err("%s, power %s laser failed.\n", __func__, (enable)? "on": "off");
exit:
    return ret;
}

int mx6300_tac_start_stream(uint32_t width, uint32_t height,
                            face_ta_mx_driver_stream_type_t type) {
    int ret = FACE_OK;

    /* wakeup mx6300 */
    ret = mx6300_tac_wakeup();
    if (ret) {
        pr_err("%s, mx6300 tac wake up failed.", __func__);
        return -FACE_ERROR_GENERAL;
    }

    ret = mx6300_tac_power_enable_flood_led(1);
    if (ret) {
        pr_err("%s, mx6300 tac power on flood led failed", __func__);
        goto turnoff_laser;
    }

    ret = mx6300_tac_power_on_laser(1);
    if (ret) {
        pr_err("%s, mx6300 tac power on laser failed", __func__);
        goto exit;
    }

    msleep(2);
    ret = mx6300_tac_send_stop_stream_cmd();
    if (ret) {
        pr_err("%s, mx6300 stop stream failed when start stream", __func__);
    }

    ret = mx6300_tac_send_start_stream_cmd(width, height, type);
    if (ret) {
        pr_err("%s, start stream failed\n", __func__);
        goto turnoff_flood_led;
    }

    pr_err("%s, start stream succeed.\n", __func__);
    return ret;

turnoff_flood_led:
    mx6300_tac_power_enable_flood_led(0);
turnoff_laser:
    mx6300_tac_power_on_laser(0);
exit:
    return ret;
}

int mx6300_tac_stop_stream(void) {
    int ret = FACE_OK;

    ret = mx6300_tac_send_stop_stream_cmd();
    if (ret) {
        pr_err("%s, mx6300 tac stop stream failed", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    ret = mx6300_tac_power_on_laser(0);
    if (ret) {
        pr_err("%s, mx6300 tac power on laser failed", __func__);
    }

    ret = mx6300_tac_power_enable_flood_led(0);
    if (ret) {
        pr_err("%s, mx6300 tac power on flood led failed", __func__);
    }

    /* sleep mx6300 */
    ret = mx6300_tac_sleep();
    if (ret) {
        pr_err("%s, mx6300 tac sleep failed.", __func__);
        ret = -FACE_ERROR_GENERAL;
        goto exit;
    }

    pr_err("%s, stop stream succeed.\n", __func__);

exit:
    return ret;
}

int mx6300_tac_get_ldmp_power_data_from_module(uint32_t *eeprom_data) {
    int ret = FACE_OK;

    mx6300_ioctl_read_data_t read_data;
    read_data.offset = 0;

    if (NULL == eeprom_data) {
        pr_err("%s, input buffer is NULL.", __func__);
        return -EINVAL;
    }

    ret = mx6300_tac_send_read_cmd(FACE_TA_MX_DRIVER_GET_LDMP_LIGHT_POWER,
            read_data.data, MX6300_EEPROMDATA_MAXSIZE, read_data.offset, 0);
    if (ret) {
        pr_err("%s, TA read ldmp light power failed\n", __func__);
        ret = -EIO;
        *eeprom_data = 0;
        goto err;
    }

    memcpy(eeprom_data, read_data.data, sizeof(uint32_t));
err:
    return ret;
}

int mx6300_tac_set_ntc(int32_t ir_tempature, int32_t ldmp_tempature) {
    int ret = FACE_OK;

    ret = mx6300_tac_send_ntc_cmd(ir_tempature, ldmp_tempature);
    if (ret) {
        pr_err("%s, TA set ntc failed\n", __func__);
        return -EIO;
    }
    pr_debug("%s, set ntc succeed\n", __func__);
    return ret;
}

static int mx6300_tac_handle_ioc_sleep_wake_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                                        uint32_t cmd, uint64_t arg) {
    int ret = FACE_OK;
    mx6300_ioctl_set_enable_data_t sleep;

    ret = copy_from_user(&sleep, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, copy from user failed\n",
                __func__);
        return -EIO;
    }

    if (sleep) {
        ret = mx6300_tac_sleep();
        if (ret) {
            pr_err("%s, mx6300 sleep fail.", __func__);
            goto exit;
        }
        pr_debug("%s, handle sleep cmd succeed\n", __func__);
    }
    else {
        ret = mx6300_tac_wakeup();
        if (ret) {
            pr_err("%s, mx6300 sleep fail.", __func__);
            goto exit;
        }
        pr_debug("%s, handle wakeup cmd succeed\n", __func__);
    }

exit:
    return ret;
}

static int mx6300_tac_handle_ioc_enable_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                uint32_t cmd, uint64_t arg, const char *target_name) {
    int ret = FACE_OK;
    mx6300_ioctl_set_enable_data_t en_data;

    ret = copy_from_user(&en_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, set_%s_cmd copy from user failed\n",
                __func__, target_name);
        return -EIO;
    }

    ret = mx6300_tac_send_enable_cmd(ta_cmd_type, en_data);
    if (ret) {
        pr_err("%s, TA set %s status failed\n",
                __func__, target_name);
        return -EIO;
    }

    pr_debug("%s, handle cmd set %s to %d succeed\n",
                __func__, target_name, en_data);
    return ret;
}

static int mx6300_tac_handle_ioc_status_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                uint32_t cmd, uint64_t arg, const char *target_name) {
    int ret = FACE_OK;
    mx6300_ioctl_set_status_data_t status_data;

    ret = copy_from_user(&status_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, set_%s_cmd copy from user failed\n",
                __func__, target_name);
        return -EIO;
    }

    ret = mx6300_tac_send_status_cmd(ta_cmd_type, status_data);
    if (ret) {
        pr_err("%s, TA set %s status failed\n",
                __func__, target_name);
        return -EIO;
    }

    pr_debug("%s, handle cmd set %s to %d succeed\n",
                __func__, target_name, status_data);
    return ret;
}

static int mx6300_tac_handle_ioc_read_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                uint32_t cmd, uint64_t arg, const char *target_name) {
    int ret = FACE_OK;
    mx6300_ioctl_read_data_t read_data;

    ret = mx6300_tac_send_read_cmd(ta_cmd_type, read_data.data,
                    MX6300_IOCTL_READDATA_MAXSIZE, 0, 0);
    if (ret) {
        pr_err("%s, TA read %s failed\n",
                __func__, target_name);
        return -EIO;
    }

    ret = copy_to_user((void *) arg, &read_data, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, read_%s_cmd copy to user failed\n",
                __func__, target_name);
        return -EIO;
    }

    pr_debug("%s, handle cmd read %s succeed\n",
                __func__, target_name);
    return ret;
}

static int mx6300_tac_handle_ioc_store_fwconfig_cmd(uint32_t cmd, uint64_t arg) {
    int ret = FACE_OK;
    mx6300_ioctl_write_large_data_t *write_data = NULL;
    write_data = kmalloc(sizeof(mx6300_ioctl_write_large_data_t), GFP_KERNEL);
    if (write_data == NULL) {
        pr_err("%s, store fwconfig, kmalloc failed\n", __func__);
        return -EIO;
    }
    ret = copy_from_user(write_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, store fw/config command copy from user failed\n", __func__);
        ret = -EIO;
        goto err;
    }

    if ((write_data->offset + write_data->size) > MX6300_FIRMWARE_BUF_SIZE) {
        pr_err("%s, offset + size is over 0x%x",
                    __func__, MX6300_FIRMWARE_BUF_SIZE);
        ret = -EINVAL;
        goto err;
    }

    memcpy(&fw_buf[write_data->offset], write_data->data, write_data->size);
    fw_buf_size = ((write_data->offset + write_data->size) > fw_buf_size) ?
                        (write_data->offset + write_data->size): fw_buf_size;
    pr_debug("%s, store_buf_size %d, input offset %d, input size %d\n",
            __func__, fw_buf_size, write_data->offset, write_data->size);
    kfree(write_data);
    return ret;
err:
    pr_err("%s, handle cmd store fw/config failed\n", __func__);
    kfree(write_data);
    return ret;
}

static int mx6300_tac_handle_ioc_write_fwconfig_cmd(face_ta_mx_driver_cmd_t cmd_type,
                                                const char *target_name) {
    int ret = FACE_OK;
    if (fw_buf_size > MX6300_FILEDATA_MAXSIZE) {
        pr_err("%s, write %s failed. buffer size %d is too large\n",
                        __func__, target_name, fw_buf_size);
    }
    ret = mx6300_tac_send_write_file_cmd(cmd_type, fw_buf, fw_buf_size);
    if (ret) {
        pr_err("%s, TA write %s failed\n", __func__, target_name);
        return -EIO;
    }

    /* reset buf size */
    fw_buf_size = 0;

    pr_debug("%s, handle cmd write %s succeed\n", __func__, target_name);
    return ret;
}

static int mx6300_tac_handle_ioc_eeprom_read_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                                            uint32_t cmd, uint64_t arg) {
    int ret = FACE_OK;
    mx6300_ioctl_read_data_t read_data;
    ret = copy_from_user(&read_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, read_eeprom_cmd copy from user failed\n", __func__);
        return -EIO;
    }

    ret = mx6300_tac_send_read_cmd(ta_cmd_type, read_data.data,
                    MX6300_EEPROMDATA_MAXSIZE, read_data.offset, 0);
    if (ret) {
        pr_err("%s, TA read eeprom at offset 0x%x failed\n",
                    __func__, read_data.offset);
        return -EIO;
    }

    ret = copy_to_user((void *) arg, &read_data, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, read_eeprom_cmd copy to user failed\n", __func__);
        return -EIO;
    }

    pr_debug("%s, handle cmd read eeprom succeed\n", __func__);
    return ret;
}

static int mx6300_tac_handle_ioc_eeprom_write_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                                            unsigned int cmd, unsigned long arg) {
    int ret = FACE_OK;
    mx6300_ioctl_write_eeprom_data_t write_data;

    ret = copy_from_user(&write_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, write_eeprom_cmd copy from user failed\n", __func__);
        return -EIO;
    }

    ret = mx6300_tac_send_write_cmd(ta_cmd_type, write_data.data,
                    MX6300_EEPROMDATA_MAXSIZE, write_data.offset, 0);
    if (ret) {
        pr_err("%s, TA write eeprom at offset 0x%x failed\n",
                    __func__, write_data.offset);
        return -EIO;
    }

    pr_debug("%s, handle cmd write eeprom succeed\n", __func__);
    return ret;
}

static int mx6300_tac_handle_ioc_reg_read_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                                            uint32_t cmd, uint64_t arg) {
    int ret = FACE_OK;
    mx6300_ioctl_reg_data_t reg_data;
    ret = copy_from_user(&reg_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, read_eeprom_cmd copy from user failed\n", __func__);
        return -EIO;
    }

    ret = mx6300_tac_send_read_cmd(ta_cmd_type, (uint8_t *)&reg_data.data,
                                    sizeof(uint32_t), 0, reg_data.addr);
    if (ret) {
        pr_err("%s, TA read reg failed\n", __func__);
        return -EIO;
    }

    ret = copy_to_user((void *) arg, &reg_data, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, read_reg_cmd copy to user failed\n", __func__);
        return -EIO;
    }

    pr_debug("%s, handle cmd read reg succeed\n", __func__);
    return ret;
}

static int mx6300_tac_handle_ioc_reg_write_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                                            unsigned int cmd, unsigned long arg) {
    int ret = FACE_OK;
    mx6300_ioctl_reg_data_t reg_data;

    ret = copy_from_user(&reg_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, write_reg_cmd copy from user failed\n", __func__);
        return -EIO;
    }

    ret = mx6300_tac_send_write_cmd(ta_cmd_type, (uint8_t *)&reg_data.data,
                    sizeof(uint32_t), 0, reg_data.addr);
    if (ret) {
        pr_err("%s, TA write reg failed\n", __func__);
        return -EIO;
    }

    pr_debug("%s, handle cmd write reg succeed\n", __func__);
    return ret;
}


static int mx6300_tac_handle_ioc_ntc_set_cmd(face_ta_mx_driver_cmd_t ta_cmd_type,
                                        unsigned int cmd, unsigned long arg) {
    int ret = FACE_OK;
    mx6300_ioctl_ntc_data_t ntc_data;

    ret = copy_from_user(&ntc_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, set_ntc_cmd copy from user failed\n", __func__);
        return -EIO;
    }

    ret = mx6300_tac_send_ntc_cmd(ntc_data.tir, ntc_data.tlaser);
    if (ret) {
        pr_err("%s, TA set ntc failed\n", __func__);
        return -EIO;
    }
    pr_debug("%s, handle cmd write ntc succeed\n", __func__);
    return ret;
}

static int mx6300_tac_handle_ioc_gen_auth_cmd(unsigned int cmd, unsigned long arg) {
    int ret = FACE_OK;
    mx6300_ioctl_read_data_t read_data;

    ret = mx6300_tac_send_read_cmd(FACE_TA_MX_DRIVER_GEN_AUTH_DATA,
                    read_data.data, MX6300_AUTH_DATA_MAX_SIZE, 0, 0);
    if (ret) {
        pr_err("%s, TA gen auth buff failed\n", __func__);
        return -EIO;
    }

    ret = copy_to_user((void *) arg, &read_data, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, gen auth data cmd copy to user failed\n", __func__);
        return -EIO;
    }
    pr_debug("%s, handle cmd gen auth data succeed\n", __func__);
    return ret;
}

static int mx6300_tac_handle_ioc_verify_auth_data_cmd(unsigned int cmd, unsigned long arg) {
    int ret = FACE_OK;
    mx6300_ioctl_write_large_data_t *write_data = NULL;

    write_data = kmalloc(sizeof(mx6300_ioctl_write_large_data_t), GFP_KERNEL);
    if (write_data == NULL) {
        pr_err("%s, kmalloc failed\n", __func__);
        return -EIO;
    }

    ret = copy_from_user(write_data, (void *) arg, _IOC_SIZE(cmd));
    if (ret) {
        pr_err("%s, copy from user failed\n", __func__);
        ret = -EIO;
        goto exit;
    }

    if (write_data->size < MX6300_AUTH_DATA_MAX_SIZE) {
        pr_err("%s, signature size error\n", __func__);
        ret = -EIO;
        goto exit;
    }

    ret = mx6300_tac_send_write_cmd(FACE_TA_MX_DRIVER_VERIFY_AUTH_DATA,
                    write_data->data, MX6300_AUTH_DATA_MAX_SIZE, 0, 0);
    if (ret) {
        pr_err("%s, TA verify signature failed\n", __func__);
        ret = -EIO;
        goto exit;
    }

    pr_debug("%s, handle cmd verify auth data succeed\n", __func__);
exit:
    kfree(write_data);
    return ret;
}

static ssize_t ldmp_sn_proc_read(struct file *filp, char __user *buff,
    size_t len, loff_t *data) {
    int ret = 0;
    char value[18] = {0};
    ret = mx6300_tac_wakeup();
    if (ret) {
        pr_err("%s, mx6300 wakeup failed\n", __func__);
        snprintf(value, sizeof(value), "ERROR wake");
        return simple_read_from_buffer(buff, len, data, value, strlen(value));
    }
    ret = mx6300_tac_send_read_cmd(FACE_TA_MX_DRIVER_GET_LDMP_SN, value, 18, 0, 0);
    if (ret) {
        pr_err("%s, TA read ldmp sn failed\n", __func__);
        snprintf(value, sizeof(value), "ERROR read");
        goto err;
    }

    snprintf(value, sizeof(value), "%s", value);
err:
    mx6300_tac_sleep();
    return simple_read_from_buffer(buff, len, data, value, strlen(value));
}

static const struct file_operations ldmp_sn_fops = {
    .owner      = THIS_MODULE,
    .read       = ldmp_sn_proc_read,
};

int mx6300_tac_open(struct inode *inode, struct file *filp) {
    return FACE_OK;
}

int mx6300_tac_release(struct inode *inode, struct file *filp) {
    fw_buf_size = 0;
    return FACE_OK;
}

long mx6300_tac_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    int ret = FACE_OK;
    mx6300_ioctl_stream_data_t stream_data;

    pr_debug("%s, handle command [0x%x]\n", __func__, cmd);
    /* cmd legacy check */
    if (_IOC_TYPE(cmd) != FACE_TAC_CMD_MAGIC) {
        pr_err("%s, unknown command magic 0x%x\n", __func__, _IOC_TYPE(cmd));
        return -EINVAL;
    }
    if (_IOC_NR(cmd) > FACE_TAC_CMD_MAXNR) {
        pr_err("%s, unknown command nr %d, max nr is %d\n",
                    __func__, _IOC_NR(cmd), FACE_TAC_CMD_MAXNR);
        return -EINVAL;
    }

    /* access check */
    if (_IOC_DIR(cmd) & _IOC_READ) {
        ret = !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));
        if (ret) {
            pr_err("%s, cmd [0x%x] need READ access authority to address [0x%lx]\n",
                    __func__, cmd, arg);
            return -EINVAL;
        }
    }
    if (_IOC_DIR(cmd) & _IOC_WRITE) {
        ret = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));
        if (ret) {
            pr_err("%s, cmd [0x%x] need WRITE access authority to address [0x%lx]\n",
                    __func__, cmd, arg);
            return -EINVAL;
        }
    }

    switch (cmd) {
        case FACE_TAC_CMD_IOCTL_STOP_STREAM:
            {
                ret = mx6300_tac_send_stop_stream_cmd();
                if (ret) {
                    pr_err("%s, TA stop stream failed\n", __func__);
                }
                pr_err("%s, ioctl stop stream end\n", __func__);
                break;
            }
        case FACE_TAC_CMD_IOCTL_START_STREAM:
            {
                ret = copy_from_user(&stream_data, (void *) arg, _IOC_SIZE(cmd));
                if (ret) {
                    pr_err("%s, start_stream_cmd copy from user failed\n", __func__);
                    ret = -EIO;
                    break;
                }
                ret = mx6300_tac_send_start_stream_cmd(
                    stream_data.width,
                    stream_data.height,
                    stream_data.type);
                if (ret) {
                    pr_err("%s, TA start stream failed\n", __func__);
                }
                pr_err("%s, ioctl start stream end\n", __func__);
                break;
            }

        case FACE_TAC_CMD_IOCTL_SET_FLOOD_LED:
            {
                ret = mx6300_tac_handle_ioc_enable_cmd(
                    FACE_TA_MX_DRIVER_SET_FLOOD_LED,
                    cmd, arg, "flood_led");
                break;
            }
        case FACE_TAC_CMD_IOCTL_SET_LASER:
            {
                ret = mx6300_tac_handle_ioc_enable_cmd(
                    FACE_TA_MX_DRIVER_SET_LASER,
                    cmd, arg, "laser");
                break;
            }
        case FACE_TAC_CMD_IOCTL_SET_AE:
            {
                ret = mx6300_tac_handle_ioc_enable_cmd(
                    FACE_TA_MX_DRIVER_SET_ENABLE_AE,
                    cmd, arg, "AE");
                break;
            }
        case FACE_TAC_CMD_IOCTL_SET_SLEEP_WAKEUP:
            {
                ret = mx6300_tac_handle_ioc_sleep_wake_cmd(
                    FACE_TA_MX_DRIVER_SET_SLEEP_WAKEUP,
                    cmd, arg);
                break;
            }

        case FACE_TAC_CMD_IOCTL_SET_EXP:
            {
                ret = mx6300_tac_handle_ioc_status_cmd(
                    FACE_TA_MX_DRIVER_SET_EXP,
                    cmd, arg, "exp");
                break;
            }
        case FACE_TAC_CMD_IOCTL_SET_HTS:
            {
                ret = mx6300_tac_handle_ioc_status_cmd(
                    FACE_TA_MX_DRIVER_SET_HTS,
                    cmd, arg, "hts");
                break;
            }
        case FACE_TAC_CMD_IOCTL_SET_VTS:
            {
                ret = mx6300_tac_handle_ioc_status_cmd(
                    FACE_TA_MX_DRIVER_SET_VTS,
                    cmd, arg, "vts");
                break;
            }
        case FACE_TAC_CMD_IOCTL_SET_FPS:
            {
                ret = mx6300_tac_handle_ioc_status_cmd(
                    FACE_TA_MX_DRIVER_SET_FPS,
                    cmd, arg, "fps");
                break;
            }

        case FACE_TAC_CMD_IOCTL_GET_IR_SENSOR_ID:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_IR_SENSOR_ID,
                    cmd, arg, "sensor_id");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_IR_OTP_ID:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_IR_OTP_ID,
                    cmd, arg, "ir_otp_id");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_LDMP_ID:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_LDMP_ID,
                    cmd, arg, "ldmp_id");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_LDMP_SN:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_LDMP_SN,
                    cmd, arg, "ldmp_sn");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_FW_VER:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_FW_VER,
                    cmd, arg, "firmware_version");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_LIB_VER:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_LIB_VER,
                    cmd, arg, "lib_version");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_EXP:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_EXP,
                    cmd, arg, "exp_time");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_LASER:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_LASER,
                    cmd, arg, "laser_status");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_FLOOD_LED:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_FLOOD_LED,
                    cmd, arg, "flood_led_status");
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_IR_SENSOR_AE:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_IR_SENSOR_AE,
                    cmd, arg, "ir_sensor_ae");
                break;
            }

        case FACE_TAC_CMD_IOCTL_WRITE_FW:
            {
                ret = mx6300_tac_handle_ioc_write_fwconfig_cmd(
                    FACE_TA_MX_DRIVER_LOAD_FIRMWARE,
                    "firmware");
                break;
            }
        case FACE_TAC_CMD_IOCTL_WRITE_CONFIG:
            {
                ret = mx6300_tac_handle_ioc_write_fwconfig_cmd(
                    FACE_TA_MX_DRIVER_LOAD_CONFIG,
                    "config");
                break;
            }
        case FACE_TAC_CMD_IOCTL_WRITE_STEREO:
            {
                ret = mx6300_tac_handle_ioc_write_fwconfig_cmd(
                    FACE_TA_MX_DRIVER_LOAD_STEREO,
                    "stereo");
                break;
            }

        case FACE_TAC_CMD_IOCTL_WRITE_EEPROM:
            {
                ret = mx6300_tac_handle_ioc_eeprom_write_cmd(
                    FACE_TA_MX_DRIVER_EEPROM_WRITE,
                    cmd, arg);
                break;
            }
        case FACE_TAC_CMD_IOCTL_READ_EEPROM:
            {
                ret = mx6300_tac_handle_ioc_eeprom_read_cmd(
                    FACE_TA_MX_DRIVER_EEPROM_READ,
                    cmd, arg);
                break;
            }

        case FACE_TAC_CMD_IOCTL_NTC_SVC:
            {
                ret = mx6300_tac_handle_ioc_ntc_set_cmd(
                    FACE_TA_MX_DRIVER_SET_NTC_SVC,
                    cmd, arg);
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_LASER_PULSE:
            {
                ret = mx6300_tac_handle_ioc_read_cmd(
                    FACE_TA_MX_DRIVER_GET_LASER_PULSEWIDTH,
                    cmd, arg, "laser_pulsewidth");
                break;
            }
        case FACE_TAC_CMD_IOCTL_SET_LASER_PULSE:
            {
                ret = mx6300_tac_handle_ioc_status_cmd(
                    FACE_TA_MX_DRIVER_SET_LASER_PULSEWIDTH,
                    cmd, arg, "laser_pulsewidth");
                break;
            }
        case FACE_TAC_CMD_IOCTL_STORE_LARGE_DATA:
            {
                ret = mx6300_tac_handle_ioc_store_fwconfig_cmd(cmd, arg);
                break;
            }
        case FACE_TAC_CMD_IOCTL_GET_AUTH_DATA:
            {
                ret = mx6300_tac_handle_ioc_gen_auth_cmd(cmd, arg);
                break;
            }
        case FACE_TAC_CMD_IOCTL_VERIFY_AUTH_DATA:
            {
                ret = mx6300_tac_handle_ioc_verify_auth_data_cmd(cmd, arg);
                break;
            }
        case FACE_TAC_CMD_IOCTL_RELEASE_TA:
            {
                mutex_lock(&mx_spi_sbuf_lock);
                if (mx_spi_handle != NULL) {
                    qseecom_shutdown_app(&mx_spi_handle);
                    mx_spi_handle = NULL;
                    pr_debug("%s, qsee mx spi handle not null, release it.\n", __func__);
                }
                mutex_unlock(&mx_spi_sbuf_lock);

                mutex_lock(&mx_file_sbuf_lock);
                if (mx_file_handle != NULL) {
                    qseecom_shutdown_app(&mx_file_handle);
                    mx_file_handle = NULL;
                    printk(KERN_DEBUG
                        "%s, qsee mx file handle not null, release it.\n",
                        __func__);
                }
                mutex_unlock(&mx_file_sbuf_lock);
                ret = FACE_OK;
                break;
            }
        case FACE_TAC_CMD_IOCTL_POST_INIT:
            {
                ret = mx6300_tac_send_enable_cmd(FACE_TA_MX_DRIVER_POST_INIT, 0);
                break;
            }
        case FACE_TAC_CMD_IOCTL_REG_READ:
            {
                ret = mx6300_tac_handle_ioc_reg_read_cmd(FACE_TA_MX_DRIVER_REG_READ, cmd, arg);
                break;
            }
        case FACE_TAC_CMD_IOCTL_REG_WRITE:
            {
                ret = mx6300_tac_handle_ioc_reg_write_cmd(FACE_TA_MX_DRIVER_REG_WRITE, cmd, arg);
                break;
            }
        default:
            {
                pr_err("%s, unknown command 0x%x\n", __func__, cmd);
                ret = -EINVAL;
            }
    }
    return ret;
}

long mx6300_tac_ioctl_compat(struct file *filp, unsigned int cmd, unsigned long arg) {
    pr_debug("%s, handle command [0x%x]\n", __func__, cmd);
    return mx6300_tac_ioctl(filp, cmd, arg);
}

static int mx6300_probe(struct platform_device *pdev) {
    int ret = FACE_OK;
    struct device *dev = &pdev->dev;
    struct mx6300_dev_data *mx6300_dev = NULL;
    struct proc_dir_entry *proc_entry = NULL;

    mx6300_dev = devm_kzalloc(dev,sizeof(struct mx6300_dev_data), GFP_KERNEL);
    if (mx6300_dev == NULL) {
        pr_err("%s kernel memory alocation was failed", __func__);
        ret = -ENOMEM;
        goto exit;
    }
    memset(mx6300_dev, 0, sizeof(struct mx6300_dev_data));

    mx6300_dev->dev = dev;

    ret = mx6300_parse_dts(mx6300_dev);
    if (ret) {
        pr_err("%s failed to parse dts.\n", __func__);
        ret = -EINVAL;
        goto exit;
    }

    ret = mx6300_power_on(mx6300_dev);
    if (ret) {
        pr_err("%s failed to power on mx6300.\n", __func__);
        ret = -EINVAL;
        goto clean_up_gpio;
    }
    mx6300_dev_ptr = mx6300_dev;

    proc_entry = proc_create_data("ldmp_sn", 0444, NULL, &ldmp_sn_fops, NULL);
    if (proc_entry == NULL) {
        pr_err("[%s]: Error! Couldn't create ldmp_sn proc entry\n", __func__);
    }

    mutex_init(&mx_spi_sbuf_lock);
    mutex_init(&mx_file_sbuf_lock);

    pr_info("mx6300 probe ok ret = %d\n", ret);
    return FACE_OK;

clean_up_gpio:
    if (mx6300_dev) {
        gpio_free(mx6300_dev->IRAVDD_gpio);
        gpio_free(mx6300_dev->IRDVDD_gpio);
        gpio_free(mx6300_dev->IRPWDN_gpio);
        gpio_free(mx6300_dev->MX_VDDIO);
        gpio_free(mx6300_dev->MX_DVDD1);
        gpio_free(mx6300_dev->debug_gpio);
        gpio_free(mx6300_dev->vio1p8_gpio);
        gpio_free(mx6300_dev->vio1v8_gpio);
        gpio_free(mx6300_dev->vdd0v9_gpio);
        gpio_free(mx6300_dev->vdd1_0v9_gpio);
        gpio_free(mx6300_dev->reset_gpio);
        devm_kfree(dev, mx6300_dev);
        mx6300_dev = NULL;
        mx6300_dev_ptr = NULL;
        pr_err("free mx6300 gpio end\n");
    }
exit:
    pr_err("face_data probe failed ret = %d\n", ret);
    return ret;
}

static int mx6300_remove(struct platform_device *pdev) {
    if (mx6300_dev_ptr) {
        mx6300_power_down(mx6300_dev_ptr);
        gpio_free(mx6300_dev_ptr->IRAVDD_gpio);
        gpio_free(mx6300_dev_ptr->IRDVDD_gpio);
        gpio_free(mx6300_dev_ptr->IRPWDN_gpio);
        gpio_free(mx6300_dev_ptr->MX_VDDIO);
        gpio_free(mx6300_dev_ptr->MX_DVDD1);
        gpio_free(mx6300_dev_ptr->debug_gpio);
        gpio_free(mx6300_dev_ptr->vio1p8_gpio);
        gpio_free(mx6300_dev_ptr->vio1v8_gpio);
        gpio_free(mx6300_dev_ptr->vdd0v9_gpio);
        gpio_free(mx6300_dev_ptr->vdd1_0v9_gpio);
        gpio_free(mx6300_dev_ptr->reset_gpio);
        devm_kfree(&pdev->dev, mx6300_dev_ptr);
        mx6300_dev_ptr = NULL;
        pr_info("free mx6300 gpio end\n");
    }
    mutex_destroy(&mx_spi_sbuf_lock);
    mutex_destroy(&mx_file_sbuf_lock);
    return FACE_OK;
}

static void mx6300_shutdown(struct platform_device *pdev) {
    if (mx6300_dev_ptr) {
        mx6300_power_down(mx6300_dev_ptr);
        pr_info("mx6300 shutdown end\n");
    }
}

static const struct file_operations mx6300_tac_fops = {
  .owner = THIS_MODULE,
  .open = mx6300_tac_open,
  .release = mx6300_tac_release,
  .unlocked_ioctl = mx6300_tac_ioctl,
  .compat_ioctl = mx6300_tac_ioctl_compat,
};

static struct miscdevice mx6300_tac_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "mx6300_tac",
    .fops = &mx6300_tac_fops,
};

static struct of_device_id mx6300_match_table[] = {
    {   .compatible = "oppo,face-mx6300", },
    {}
};

static struct platform_driver mx6300_driver = {
    .probe = mx6300_probe,
    .remove = mx6300_remove,
    .shutdown = mx6300_shutdown,
    .driver = {
        .name = "mx6300",
        .owner = THIS_MODULE,
        .of_match_table = mx6300_match_table,
    },
};

static int __init mx6300_init(void) {
    int ret = 0;
    pr_debug("%s, init start\n", __func__);

    ret = misc_register(&mx6300_tac_dev);
    if (ret) {
        pr_err("%s, misc_register failed\n", __func__);
        return ret;
    }
    pr_debug("%s, misc_register succeed\n", __func__);

    ret = platform_driver_register(&mx6300_driver);
    if (ret) {
        pr_err("%s, platform_driver_register failed\n", __func__);
        return ret;
    }
    pr_debug("%s, mx6300 driver register succeed\n", __func__);
    return FACE_OK;
}

static void __exit mx6300_exit(void) {
    misc_deregister(&mx6300_tac_dev);
    platform_driver_unregister(&mx6300_driver);
    if (mx_spi_handle) {
        qseecom_shutdown_app(&mx_spi_handle);
        mx_spi_handle = NULL;
    }
    if (mx_file_handle) {
        qseecom_shutdown_app(&mx_file_handle);
        mx_file_handle = NULL;
    }
}

module_init(mx6300_init);
module_exit(mx6300_exit);

MODULE_DESCRIPTION("mx6300 API for using spi");
MODULE_AUTHOR("oujinrong <oujinrong@oppo.com>");
MODULE_LICENSE("GPL v2");

