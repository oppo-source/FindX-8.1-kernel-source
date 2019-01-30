/**********************************************************************
** Copyright 2008-2013 OPPO Mobile Comm Corp., Ltd, All rights reserved.
** VENDOR_EDIT :                                                                                             
** File : - SDM660_8.0_LA_2.0\android\kernel\msm-4.4\include\soc\device_info.h
** ModuleName:devinfo
** Author : wangjc
** Version : 1.0
** Date : 2013-10-23
** Descriptio : add interface to get device information.
** History :
**                  <time>         <author>             <desc>
**                2013-10-23	    wangjc	            init
**********************************************************************/

#ifndef _DEVICE_INFO_H
#define _DEVICE_INFO_H


/*dram type*/
/*
enum{
        DRAM_TYPE0 = 0,
        DRAM_TYPE1,
        DRAM_TYPE2,
        DRAM_TYPE3,
        DRAM_UNKNOWN,
};
*/

enum{
        MAINBOARD_RESOURCE0 = 0,
        MAINBOARD_RESOURCE1 = 1,
        MAINBOARD_RESOURCE2 = 2,
};

struct manufacture_info {
        char *version;
        char *manufacture;
        char *fw_path;
};

int register_device_proc(char *name, char *version, char *manufacture);
int register_devinfo(char *name, struct manufacture_info *info);


#endif /*_DEVICE_INFO_H*/
