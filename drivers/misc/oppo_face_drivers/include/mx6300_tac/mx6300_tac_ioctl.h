/************************************************************************************
 ** File: - mx6300_ioc.h
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2018 OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **          mx6300 ioc for access from userspace
 **
 ** Version: 1.0
 ** Date created: 18:03:11, 11/04/2018
 ** Author: oujinrong@BSP.Fingerprint.Basic
 ** TAG: BSP.Fingerprint.Basic
 ** --------------------------- Revision History: --------------------------------
 **  <author>               <data>                      <desc>
 **  oujinrong              2018/04/11                  create the file
 ************************************************************************************/

#ifndef MX6300_TAC_IOCTL_H
#define MX6300_TAC_IOCTL_H

#define MX6300_IOCTL_READDATA_MAXSIZE (256)
#define MX6300_IOCTL_WRITEDATA_MAXSIZE (2 * 1024)

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t type;
} mx6300_ioctl_stream_data_t;

typedef uint8_t mx6300_ioctl_set_enable_data_t;

typedef uint32_t mx6300_ioctl_set_status_data_t;

typedef struct {
    uint16_t offset;
    uint8_t data[MX6300_IOCTL_READDATA_MAXSIZE];
} mx6300_ioctl_read_data_t;

typedef struct {
    uint16_t offset;
    uint8_t data[16];
} mx6300_ioctl_write_eeprom_data_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint8_t data[MX6300_IOCTL_WRITEDATA_MAXSIZE];
} mx6300_ioctl_write_large_data_t;

typedef struct {
    int32_t tir;
    int32_t tlaser;
} mx6300_ioctl_ntc_data_t;

typedef struct {
    uint32_t addr;
    uint32_t data;
} mx6300_ioctl_reg_data_t;

#define FACE_TAC_CMD_MAGIC 0x5D
#define FACE_TAC_CMD_MAXNR 34

/* stream control */
#define FACE_TAC_CMD_IOCTL_STOP_STREAM        _IO(FACE_TAC_CMD_MAGIC, 1)
#define FACE_TAC_CMD_IOCTL_START_STREAM       _IOW(FACE_TAC_CMD_MAGIC, 2, mx6300_ioctl_stream_data_t)

/* set enable or not */
#define FACE_TAC_CMD_IOCTL_SET_FLOOD_LED      _IOW(FACE_TAC_CMD_MAGIC, 3, mx6300_ioctl_set_enable_data_t)
#define FACE_TAC_CMD_IOCTL_SET_LASER          _IOW(FACE_TAC_CMD_MAGIC, 4, mx6300_ioctl_set_enable_data_t)
#define FACE_TAC_CMD_IOCTL_SET_AE             _IOW(FACE_TAC_CMD_MAGIC, 5, mx6300_ioctl_set_enable_data_t)
#define FACE_TAC_CMD_IOCTL_SET_SLEEP_WAKEUP   _IOW(FACE_TAC_CMD_MAGIC, 6, mx6300_ioctl_set_enable_data_t)

/* set status */
#define FACE_TAC_CMD_IOCTL_SET_EXP            _IOW(FACE_TAC_CMD_MAGIC, 7, mx6300_ioctl_set_status_data_t)
#define FACE_TAC_CMD_IOCTL_SET_HTS            _IOW(FACE_TAC_CMD_MAGIC, 8, mx6300_ioctl_set_status_data_t)
#define FACE_TAC_CMD_IOCTL_SET_VTS            _IOW(FACE_TAC_CMD_MAGIC, 9, mx6300_ioctl_set_status_data_t)
#define FACE_TAC_CMD_IOCTL_SET_FPS            _IOW(FACE_TAC_CMD_MAGIC, 10, mx6300_ioctl_set_status_data_t)

/* read message */
#define FACE_TAC_CMD_IOCTL_GET_IR_SENSOR_ID   _IOR(FACE_TAC_CMD_MAGIC, 10, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_IR_OTP_ID      _IOR(FACE_TAC_CMD_MAGIC, 11, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_LDMP_ID        _IOR(FACE_TAC_CMD_MAGIC, 12, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_LDMP_SN        _IOR(FACE_TAC_CMD_MAGIC, 13, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_FW_VER         _IOR(FACE_TAC_CMD_MAGIC, 14, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_LIB_VER        _IOR(FACE_TAC_CMD_MAGIC, 15, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_EXP            _IOR(FACE_TAC_CMD_MAGIC, 16, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_LASER          _IOR(FACE_TAC_CMD_MAGIC, 17, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_FLOOD_LED      _IOR(FACE_TAC_CMD_MAGIC, 18, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_GET_IR_SENSOR_AE   _IOR(FACE_TAC_CMD_MAGIC, 19, mx6300_ioctl_read_data_t)

/* load firmware or config */
#define FACE_TAC_CMD_IOCTL_WRITE_FW           _IO(FACE_TAC_CMD_MAGIC, 20)
#define FACE_TAC_CMD_IOCTL_WRITE_CONFIG       _IO(FACE_TAC_CMD_MAGIC, 21)

/* EEPROM */
#define FACE_TAC_CMD_IOCTL_WRITE_EEPROM       _IOW(FACE_TAC_CMD_MAGIC, 22, mx6300_ioctl_write_eeprom_data_t)
#define FACE_TAC_CMD_IOCTL_READ_EEPROM        _IOWR(FACE_TAC_CMD_MAGIC, 23, mx6300_ioctl_read_data_t)

/* NTC */
#define FACE_TAC_CMD_IOCTL_NTC_SVC            _IOR(FACE_TAC_CMD_MAGIC, 24, mx6300_ioctl_ntc_data_t)

#define FACE_TAC_CMD_IOCTL_GET_LASER_PULSE    _IOR(FACE_TAC_CMD_MAGIC, 25, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_SET_LASER_PULSE    _IOW(FACE_TAC_CMD_MAGIC, 26, mx6300_ioctl_set_status_data_t)

#define FACE_TAC_CMD_IOCTL_STORE_LARGE_DATA   _IOW(FACE_TAC_CMD_MAGIC, 27, mx6300_ioctl_write_large_data_t)

/* AUTH_CMD */
#define FACE_TAC_CMD_IOCTL_GET_AUTH_DATA      _IOR(FACE_TAC_CMD_MAGIC, 28, mx6300_ioctl_read_data_t)
#define FACE_TAC_CMD_IOCTL_VERIFY_AUTH_DATA   _IOR(FACE_TAC_CMD_MAGIC, 29, mx6300_ioctl_write_large_data_t)

#define FACE_TAC_CMD_IOCTL_RELEASE_TA         _IO(FACE_TAC_CMD_MAGIC, 30)

#define FACE_TAC_CMD_IOCTL_POST_INIT          _IO(FACE_TAC_CMD_MAGIC, 31)

#define FACE_TAC_CMD_IOCTL_REG_READ           _IOWR(FACE_TAC_CMD_MAGIC, 32, mx6300_ioctl_reg_data_t)
#define FACE_TAC_CMD_IOCTL_REG_WRITE          _IOW(FACE_TAC_CMD_MAGIC, 33, mx6300_ioctl_reg_data_t)

#define FACE_TAC_CMD_IOCTL_WRITE_STEREO       _IO(FACE_TAC_CMD_MAGIC, 34)

#endif /* MX6300_TAC_IOCTL_H */
