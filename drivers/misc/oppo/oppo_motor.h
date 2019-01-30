/************************************************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_motor.h
**
** Description:
**      Definitions for m1120 camera motor control layer.
**
** Version: 1.0
** Date created: 2018/01/14,20:27
** Author: Fei.Mo@PSW.BSP.Sensor
**
** --------------------------- Revision History: -------------------------------------
* <version>		<date>		<author>		<desc>
* Revision 1.0		2018/01/14	Fei.Mo@PSW.BSP.Sensor	Created
**************************************************************************************/
#ifndef __OPPO_MOTOR__H
#define __OPPO_MOTOR__H

#include <linux/alarmtimer.h>
#include <linux/version.h>

#define MOTOR_TAG                  "[oppo_motor] "
#define MOTOR_ERR(fmt, args...)    printk(KERN_ERR MOTOR_TAG" %s : "fmt,__FUNCTION__,##args)
#define MOTOR_LOG(fmt, args...)    printk(KERN_INFO MOTOR_TAG" %s : "fmt,__FUNCTION__,##args)

#define MOTOR_EVENT_TYPE		EV_KEY
#define MOTOR_EVENT_MANUAL_TO_UP	KEY_F13
#define MOTOR_EVENT_MANUAL_TO_DOWN	KEY_F14
#define MOTOR_EVENT_UP			KEY_F15
#define MOTOR_EVENT_UP_ABNORMAL		KEY_F16
#define MOTOR_EVENT_UP_NORMAL		KEY_F17
#define MOTOR_EVENT_DOWN		KEY_F18
#define MOTOR_EVENT_DOWN_ABNORMAL	KEY_F19
#define MOTOR_EVENT_DOWN_NORMAL		KEY_F20
#define DHALL_DETECT_RANGE_HIGH		(170)
#define DHALL_DETECT_RANGE_LOW		(-512)
#define MOTOR_RESET_TIMER		(500)//500ms
#define MOTOR_STOP_STAITC_POS_VALUE		(3)
#define MOTOR_STOP_STAITC_NEG_VALUE		(-3)
#define MOTOR_STOP_COMPENSATE_VALUE		(30)
#define MOTOR_STOP_RETARD_VALUE		(430)
#define MOTOR_IRQ_MONITOR_TIME		(20)//20ms
#define MOTOR_IRQ_MONITOR_COUNT		(8)


enum dhall_id {
	DHALL_0 = 0,
	DHALL_1,
};

enum wakelock_id {
	MOTOR_RUN_LOCK = 0,
	HALL_DATA_LOCK,
	POSITION_DETECT_LOCK,
	MAX_LOCK
};

enum dhall_detection_mode {
	DETECTION_MODE_POLLING = 0,
	DETECTION_MODE_INTERRUPT,
	DETECTION_MODE_INVALID,
};

enum {
	MOTOR_POWER_OFF = 0,
	MOTOR_POWER_ON,
};

enum motor_direction {
	MOTOR_DOWN = 0,
	MOTOR_UPWARD,
};

enum motor_type {
	MOTOR_UNKNOWN = 0,
	MOTOR_FI5,
	MOTOR_FI6,
};

enum motor_move_state {
	MOTOR_STOP,//never move after boot
	MOTOR_UPWARD_ING,
	MOTOR_DOWNWARD_ING,
	MOTOR_UPWARD_STOP,
	MOTOR_DOWNWARD_STOP
};

enum camera_position {
	UP_STATE,
	DOWN_STATE,
	MID_STATE,
};

enum motor_driver_mode {
	MOTOR_MODE_FULL = 0,
	MOTOR_MODE_1_2,
	MOTOR_MODE_1_4,
	MOTOR_MODE_1_8,
	MOTOR_MODE_1_16,
	MOTOR_MODE_1_32
};

enum motor_speed {
	MOTOR_SPEED0 = 0, //High
	MOTOR_SPEED1,
	MOTOR_SPEED2,
	MOTOR_SPEED3,
	MOTOR_SPEED4,
	MOTOR_SPEED5,
	MOTOR_SPEED6,
	MOTOR_SPEED7,
	MOTOR_SPEED8,
	MOTOR_SPEED9,
	MOTOR_SPEED10,
	MOTOR_SPEED11,
	MOTOR_SPEED12,
	MOTOR_SPEED13//LOW
};

typedef struct {
	short dhall_0_irq_position;
	short dhall_1_irq_position;
	short up_retard_hall0;
	short up_retard_hall1;
	short down_retard_hall0;
	short down_retard_hall1;
} cali_data_t;

typedef struct {
	short data0;
	short data1;
} dhall_data_t;

#define MOTOR_IOCTL_BASE			(0x89)
#define MOTOR_IOCTL_START_MOTOR			_IOW(MOTOR_IOCTL_BASE, 0x00, int)
#define MOTOR_IOCTL_STOP_MOTOR			_IOW(MOTOR_IOCTL_BASE, 0x01, int)
#define MOTOR_IOCTL_MOTOR_UPWARD		_IOW(MOTOR_IOCTL_BASE, 0x02, int)
#define MOTOR_IOCTL_MOTOR_DOWNWARD		_IOW(MOTOR_IOCTL_BASE, 0x03, int)
#define MOTOR_IOCTL_GET_POSITION		_IOR(MOTOR_IOCTL_BASE, 0x04, int)
#define MOTOR_IOCTL_SET_DIRECTION		_IOW(MOTOR_IOCTL_BASE, 0x05, int)
#define MOTOR_IOCTL_SET_SPEED			_IOW(MOTOR_IOCTL_BASE, 0x06, int)
#define MOTOR_IOCTL_SET_DELAY			_IOW(MOTOR_IOCTL_BASE, 0x07, int)
#define MOTOR_IOCTL_GET_DHALL_DATA		_IOR(MOTOR_IOCTL_BASE, 0x08, dhall_data_t)
#define MOTOR_IOCTL_SET_CALIBRATION		_IOW(MOTOR_IOCTL_BASE, 0x09, cali_data_t)
#define MOTOR_IOCTL_GET_INTERRUPT		_IOR(MOTOR_IOCTL_BASE, 0x0A, int)

#define MOTOR_STOP_TIMEOUT (12000) //80k


struct oppo_dhall_operations {
	int (*get_data) (unsigned int id, short *data);
	int (*set_detection_mode) (unsigned int id, u8 mode);
	int (*enable_irq) (unsigned int id, bool enable);
	int (*clear_irq) (unsigned int id);
	int (*get_irq_state) (unsigned int id);
	bool (*update_threshold) (unsigned int id, int position,short lowthd, short highthd);
	void (*dump_regs) (unsigned int id, u8* buf);
	int (*set_reg) (unsigned int id, int reg, int val);
	bool (*is_power_on) (unsigned int id);
};

struct oppo_motor_operations {
	int (*set_power) (int mode);
	int (*set_direction) (int dir);
	int (*set_working_mode) (int mode);
	int (*get_all_config)(int* config ,int count);
	int (*calculate_pwm_count) (int angle,int mode);
	int (*pwm_config) (int duty_ns, int period_ns);
	int (*pwm_enable) (void);
	int (*pwm_disable) (void);
	int (*get_motor_type) (void);
};

struct oppo_motor_chip {
	struct device *dev;
	struct pinctrl *pctrl;
	struct pinctrl_state *free_fall_state;
	struct input_dev *i_dev;
	struct workqueue_struct * manual2auto_wq;
	struct delayed_work	detect_work;
	struct work_struct	motor_work;
	struct work_struct	manual_position_work;
	struct delayed_work	up_work;
	struct delayed_work	down_work;
	struct hrtimer stop_timer;
	struct hrtimer speed_up_timer;
	struct hrtimer speed_down_timer;
	struct alarm reset_timer;
	struct notifier_block fb_notify;
	struct oppo_dhall_operations * dhall_ops;
	struct oppo_motor_operations * motor_ops;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock        suspend_lock;
#else
	struct wakeup_source	*suspend_ws;
#endif
	const char * d_name;
	const char * m_name;
	atomic_t	in_suspend;
	int		motor_started;
	int		motor_switch;
	bool		speed_changed;
	int		motor_start_speed;
	int		md_mode;
	int		md_speed;
	int		md_dir;
	int		md_L;
	int		speed_up_L;
	int		speed_up_pwm_count;
	int		speed_down_L;
	int		speed_down_pwm_count;
	int		pwm_count;
	int		motor_enable;
	int		position;
	int		last_position;
	int		detect_delay;
	int		detect_count;
	int		irq_enabled;
	unsigned long	pwm_duty;
	unsigned long	pwm_period;
	int		move_state;
	short		dhall_data0;
	short		dhall_data1;
	short		hall_0_irq_position;
	short		hall_1_irq_position;
	int		hall_0_irq_count;
	int		hall_1_irq_count;
	int		up_retard_hall0;
	int		up_retard_hall1;
	int		down_retard_hall0;
	int		down_retard_hall1;
	bool		led_on;
	bool		stop_timer_trigger;
	bool		speed_can_up;
	bool		speed_can_down;
	bool		hall_detect_switch;
	bool		is_fi_6_motor;
	bool		is_motor_test;
	bool		is_skip_pos_check;
	int        	manual2auto_up_switch;
	int        	manual2auto_down_switch;
	unsigned int	free_fall_gpio;
	int		irq;
	bool        irq_monitor_started;
	bool        is_irq_abnormal;
};

extern int oppo_register_dhall(const char * name, struct oppo_dhall_operations *ops);
extern int oppo_register_motor(const char * name, struct oppo_motor_operations *ops);
//dhall control api
extern int oppo_dhall_get_data(unsigned int id);
extern int oppo_dhall_set_detection_mode(unsigned int id,u8 mode);
extern int oppo_dhall_enable_irq (unsigned int id,bool enable);
extern int oppo_dhall_clear_irq (unsigned int id);
extern int oppo_dhall_irq_handler(unsigned int id);
extern int oppo_dhall_get_irq_state(unsigned int id);
extern void oppo_dhall_dump_regs(unsigned int id, u8* buf);
extern int oppo_dhall_set_reg(unsigned int id, int reg, int val);
extern bool oppo_dhall_update_threshold(unsigned int id, int position,short lowthd, short highthd);
extern bool oppo_dhall_is_power_on(void);
//motor control api
extern int oppo_motor_set_power (int mode);
extern int oppo_motor_set_direction (int dir);
extern int oppo_motor_set_working_mode (int mode);
extern int oppo_motor_calculate_pwm_count(int L, int mode);
extern int oppo_motor_pwm_config(int duty_ns, int period_ns);
extern int oppo_motor_pwm_enable(void);
extern int oppo_motor_pwm_disable(void);
extern int oppo_motor_get_all_config(int* config ,int count);
extern int oppo_get_motor_type(void);
#endif
