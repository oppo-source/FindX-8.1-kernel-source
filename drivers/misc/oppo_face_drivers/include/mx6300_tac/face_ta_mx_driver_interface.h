/************************************************************************************
 ** File: - trustzone_images\core\securemsm\trustzone\qsapps\facereg\interface\face_ta_driver_interface.h
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2018, OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **      Face TA (orbecc driver interface)
 **
 ** Version: 1.0
 ** Date created: 17:40:11,03/04/2018
 ** Author: oujinrong@BSP.Fingerprint.Basic
 ** TAG: BSP.Fingerprint.Basic
 **
 ** --------------------------- Revision History: --------------------------------
 **     <author>        <data>          <desc>
 **     oujinrong       2018/04/02      create the file
 **     oujinrong       2018/05/02      add mes auth
 **     oujinrong       2018/05/31      devide command into normal command and large command
 ************************************************************************************/

#ifndef INCLUSION_GUARD_FACE_TA_DRIVER_INTERFACE
#define INCLUSION_GUARD_FACE_TA_DRIVER_INTERFACE

#include "face_ta_interface.h"
#include <stdbool.h>

#define MX6300_READDATA_MAXSIZE (256)
#define MX6300_WRITEDATA_MAXSIZE (256)
#define MX6300_FILEDATA_MAXSIZE (400 * 1024)

#define MX6300_EEPROMDATA_MAXSIZE (16)
#define MX6300_AUTH_DATA_MAX_SIZE (256)

typedef enum mx_stream_type {
    MX6300_STREAM_TYPE_IR_IMAGE = 1,
    MX6300_STREAM_TYPE_IR_PATTERN = 2,
    MX6300_STREAM_TYPE_DEPTH_IMAGE = 3,
} face_ta_mx_driver_stream_type_t;

typedef struct face_ta_mx_driver_stream_data {
    uint32_t width;
    uint32_t height;
    face_ta_mx_driver_stream_type_t type;
} face_ta_mx_driver_stream_data_t;

typedef struct {
    face_ta_cmd_header_t header;
    int32_t response;
} face_ta_mx_driver_simple_command_t;

typedef struct {
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    face_ta_mx_driver_stream_data_t stream_data;
} face_ta_mx_driver_stream_command_t;

typedef struct {
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    uint8_t on;
} face_ta_mx_driver_set_enable_command_t;

typedef struct {
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    uint32_t status;
} face_ta_mx_driver_set_status_command_t;

typedef struct {
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    uint16_t offset;
    uint32_t addr;
    uint8_t data[MX6300_READDATA_MAXSIZE];
} face_ta_mx_driver_read_command_t;

typedef struct {
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    uint32_t offset;
    uint32_t addr;
    uint32_t size;
    uint8_t data[MX6300_WRITEDATA_MAXSIZE];
} face_ta_mx_driver_write_command_t;

typedef struct {
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    int32_t tir;
    int32_t tlaser;
} face_ta_mx_driver_ntc_command_t;

typedef struct {
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    uint32_t size;
    uint8_t data[MX6300_FILEDATA_MAXSIZE];
} face_ta_mx_driver_file_write_command_t;

typedef union {
    face_ta_cmd_header_t header;
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    face_ta_mx_driver_stream_command_t start_cmd;
    face_ta_mx_driver_set_enable_command_t set_enable_cmd;
    face_ta_mx_driver_set_status_command_t set_status_cmd;
    face_ta_mx_driver_read_command_t read_cmd;
    face_ta_mx_driver_write_command_t write_cmd;
    face_ta_mx_driver_ntc_command_t ntc_cmd;
} face_ta_mx_driver_command_t;

typedef union {
    face_ta_cmd_header_t header;
    face_ta_mx_driver_simple_command_t mx_driver_cmd;
    face_ta_mx_driver_file_write_command_t write_file_cmd;
} face_ta_mx_driver_file_command_t;

typedef enum {
    FACE_TA_MX_DRIVER_STOP_STREAM = 4001,
    FACE_TA_MX_DRIVER_START_STREAM,

    /* for factory use */
    FACE_TA_MX_DRIVER_SET_FLOOD_LED,
    FACE_TA_MX_DRIVER_SET_LASER,
    FACE_TA_MX_DRIVER_SET_ENABLE_AE,
    FACE_TA_MX_DRIVER_SET_SLEEP_WAKEUP,
    FACE_TA_MX_DRIVER_SET_EXP,
    FACE_TA_MX_DRIVER_SET_HTS,
    FACE_TA_MX_DRIVER_SET_VTS,
    FACE_TA_MX_DRIVER_SET_FPS,

    FACE_TA_MX_DRIVER_GET_IR_SENSOR_ID,
    FACE_TA_MX_DRIVER_GET_IR_OTP_ID,
    FACE_TA_MX_DRIVER_GET_LDMP_ID,
    FACE_TA_MX_DRIVER_GET_LDMP_SN,
    FACE_TA_MX_DRIVER_GET_EXP,
    FACE_TA_MX_DRIVER_GET_LASER,
    FACE_TA_MX_DRIVER_GET_FLOOD_LED,
    FACE_TA_MX_DRIVER_GET_IR_SENSOR_AE,

    FACE_TA_MX_DRIVER_GET_FW_VER,
    FACE_TA_MX_DRIVER_GET_LIB_VER,

    FACE_TA_MX_DRIVER_LOAD_FIRMWARE,
    FACE_TA_MX_DRIVER_LOAD_CONFIG,
    FACE_TA_MX_DRIVER_LOAD_STEREO,

    FACE_TA_MX_DRIVER_SET_NTC_SVC,

    FACE_TA_MX_DRIVER_EEPROM_WRITE,
    FACE_TA_MX_DRIVER_EEPROM_READ,

    FACE_TA_MX_DRIVER_GET_LASER_PULSEWIDTH,
    FACE_TA_MX_DRIVER_SET_LASER_PULSEWIDTH,

    FACE_TA_MX_DRIVER_GEN_AUTH_DATA,
    FACE_TA_MX_DRIVER_VERIFY_AUTH_DATA,

    FACE_TA_MX_DRIVER_GET_LDMP_LIGHT_POWER,

    FACE_TA_MX_DRIVER_POST_INIT,

    FACE_TA_MX_DRIVER_REG_READ,
    FACE_TA_MX_DRIVER_REG_WRITE,

    FACE_TA_MX_DRIVER_DL_FILE_GET_VERIFY_RESULT,
    FACE_TA_MX_DRIVER_DL_HASH_INIT,
    FACE_TA_MX_DRIVER_DL_HASH_UNINIT,
} face_ta_mx_driver_cmd_t;

#endif /* INCLUSION_GUARD_FACE_TA_DRIVER_INTERFACE */

