/************************************************************************************
 ** File: - oppo_face_common.h
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2018 OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **          3d face compatibility configuration
 **
 ** Version: 1.0
 ** Date created: 18:03:11, 29/03/2018
 ** Author: Ziqing.guo@BSP.Fingerprint.Basic
 ** TAG: BSP.Fingerprint.Basic
 ** --------------------------- Revision History: --------------------------------
 **  <author>                 <data>                        <desc>
 **  Ziqing.guo             2018/03/30                   create the file
 ************************************************************************************/

#ifndef _OPPO_FACE_COMMON_H_
#define _OPPO_FACE_COMMON_H_

#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/qpnp/qpnp-adc.h>

#define FACE_TEMPATURE_INTERVAL (3000)

#define FACE_VOLTAGE_TO_DEVIDE (1800) /* 1.8v, in mv */
#define FACE_DIVIDOR_RESISTANCE_10K (10000) /* 10k ohm */
#define FACE_DIVIDOR_RESISTANCE_47K (47000) /* 47k ohm */

enum {
    FACE_OK,
    FACE_ERROR_GPIO,
    FACE_ERROR_GENERAL,
    FACE_ERROR_HW_VERSION,
};

typedef enum face_enrol_status {
    FACE_VOLT_LDMP_TEMPATURE = 1,
    FACE_VOLT_IR_TEMPATURE = 2,
    FACE_VOLT_LDMP_POWER = 3,
}face_volt_type_t;

struct face_data {
    struct device *dev;
    struct qpnp_vadc_chip	*pm8998_vadc_dev;
    int gpio_temp_ldmp;
    int gpio_temp_ir;
};

typedef struct {
    int ir_tempature;
    int ldmp_tempature;
} face_tempature_t;

typedef struct {
    int32_t T;
    uint32_t R;
}temp_table_t;

int face_get_ldmp_power_volt(int *volt);

#endif  /*_OPPO_FACE_COMMON_H_*/
