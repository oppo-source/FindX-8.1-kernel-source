/************************************************************************************
** File: - kernel/msm-4.9/techpack/audio/asoc/codecs/as6313/as6313.h
** VENDOR_EDIT
** Copyright (C), 2018-2020, OPPO Mobile Comm Corp., Ltd
**
** Description:
**     add driver for audio switch as6313
** Version: 1.0
** --------------------------- Revision History: --------------------------------
**      <author>                       <date>                  <desc>
** Le.Li@PSW.MM.AudioDriver    08/09/2018           creat this file
************************************************************************************/

#ifndef __AS6313_H
#define __AS6313_H

//AS6313 addr
#define AS6313_SLAVE_ADDR 0x0d

//AS6313 ioctl CMD
#define AS_IOCTL_SET_MODE    0x100     	//set MODE
#define AS_IOCTL_GET_MODE    0x101		//get MODE
#define AS_IOCTL_SET_IRQ     0x102		//irq SET

//MODE
enum {
	AS_MODE_INVALID = -1,
	AS_MODE_USB = 0,           			//USB
	AS_MODE_OMTP,						//OMTP
	AS_MODE_CTIA,						//CTIA
	AS_MODE_TRS,						//TRS
	AS_MODE_UART,						//UART
	AS_MODE_AUX,						//AUX
	AS_MODE_OFF,						//OFF
	AS_MODE_MAX
};

//irq seting
enum {
	AS_IRQ_ENABLE = 0,          		//enable irq
	AS_IRQ_DISABLE,             		//disable irq
	AS_IRQ_MAX
};

//debug
enum {
	AS_DBG_TYPE_MODE = 0,     			//mode
	AS_DBG_TYPE_IRQ,					//irq
	AS_DBG_TYPE_REG0,					//reg0
	AS_DBG_TYPE_REG1,					//reg1
	AS_DBG_TYPE_MAX
};

#endif
