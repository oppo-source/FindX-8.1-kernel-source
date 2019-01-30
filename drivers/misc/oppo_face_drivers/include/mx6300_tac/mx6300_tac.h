/************************************************************************************
 ** File: - mx6300_tee_spi.h
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
 ************************************************************************************/
#ifndef MX6300_TEE_SPI_H
#define MX6300_TEE_SPI_H

#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/qpnp/qpnp-adc.h>

#include "face_ta_mx_driver_interface.h"

#define FACE_TA_NAME "seccamfacereg64"
#define TARGET_FACE_TA_MX_DRIVER     (5)
#define TARGET_FACE_TA_MX_FILE       (9)
#define SECCAM_OPPO_CMD_NO_ION_TAG 0x7FFF3001

#define MX_SPI_SBUF_SIZE            (1024)
#define MX_FILE_SBUF_SIZE           (500 * 1024)
#define MX6300_FIRMWARE_BUF_SIZE    (400 * 1024)

#define MX_DRIVER_RETRY_INTERVAL    (50)
#define MX_DRIVER_MAX_RETRY_TIMES   (20)

struct mx6300_dev_data {
    struct device *dev;

    int IRAVDD_gpio;
    int IRDVDD_gpio;
    int IRPWDN_gpio;
    int MX_VDDIO;
    int MX_DVDD1;
    int debug_gpio;
    int vio1p8_gpio;
    int vio1v8_gpio;
    int vdd0v9_gpio;
    int vdd1_0v9_gpio;
    int reset_gpio;
    int device_id;
};

typedef struct {
    uint32_t tag;
    face_ta_mx_driver_command_t cmd;
} face_qsee_msg_t;

typedef struct {
    uint32_t tag;
    face_ta_mx_driver_file_command_t cmd;
} face_qsee_mx_file_msg_t;

int mx6300_tac_start_stream(uint32_t width, uint32_t height, face_ta_mx_driver_stream_type_t type);
int mx6300_tac_stop_stream(void);
int mx6300_tac_wakeup(void);
int mx6300_tac_sleep(void);
int mx6300_tac_set_ntc(int32_t ir_tempature, int32_t ldmp_tempature);

#endif /* MX6300_TEE_SPI_H */
