/************************************************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_motor.c
**
** Description:
**      Definitions for m1120 camera motor control layer.
**
** Version: 1.0
** Date created: 2018/01/14,20:27
** Author: Fei.Mo@PSW.BSP.Sensor
**
** --------------------------- Revision History: ------------------------------------
* <version>		<date>		<author>		<desc>
* Revision 1.0		2018/01/14	Fei.Mo@PSW.BSP.Sensor	Created
**************************************************************************************/
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <linux/hrtimer.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
#include <linux/wakelock.h>
#endif
#include <soc/oppo/oppo_project.h>
#include "oppo_motor.h"
#include "oppo_motor_notifier.h"

static struct oppo_motor_chip * g_the_chip = NULL;
static DEFINE_MUTEX(motor_running_mutex);
static DEFINE_MUTEX(position_detect_mutex);

static void motor_run_work(struct work_struct *work);
static void oppo_motor_reset_check(struct oppo_motor_chip *chip);

int oppo_register_dhall(const char * name,struct oppo_dhall_operations *ops)
{
	if (!g_the_chip) {
		struct oppo_motor_chip *chip = kzalloc(sizeof(struct oppo_motor_chip), GFP_KERNEL);
		if (!chip) {
			MOTOR_ERR("kzalloc err \n");
			return -ENOMEM;
		}
		g_the_chip = chip;
	}

	if (!g_the_chip->dhall_ops) {
		if (ops) {
			g_the_chip->dhall_ops = ops;
			g_the_chip->d_name = name;
		}  else {
			MOTOR_ERR("dhall_ops NULL \n");
			return -EINVAL;
		}
	} else {
		MOTOR_ERR("dhall_ops has been register \n");
		return -EINVAL;
	}
	return 0;
}

int oppo_register_motor(const char * name,struct oppo_motor_operations *ops)
{
	if (!g_the_chip) {
		struct oppo_motor_chip *chip = kzalloc(sizeof(struct oppo_motor_chip), GFP_KERNEL);
		if (!chip) {
			MOTOR_ERR("kzalloc err \n");
			return -ENOMEM;
		}
		g_the_chip = chip;
	}

	if (!g_the_chip->motor_ops) {
		if (ops) {
			g_the_chip->motor_ops = ops;
			g_the_chip->m_name = name;
		} else {
			MOTOR_ERR("motor_ops NULL \n");
			return -EINVAL;
		}
	} else {
		MOTOR_ERR("motor_ops has been register \n");
		return -EINVAL;
	}

	return 0;
}

static int oppo_input_dev_init(struct oppo_motor_chip *chip)
{
	struct input_dev *dev;
	int err;

	dev = input_allocate_device();
	if (!dev) {
		MOTOR_ERR("input_dev null \n");
		return -ENOMEM;
	}
	dev->name = "motor";
	dev->id.bustype = BUS_I2C;

	set_bit(MOTOR_EVENT_TYPE, dev->evbit);
	set_bit(MOTOR_EVENT_MANUAL_TO_UP, dev->keybit);
	set_bit(MOTOR_EVENT_MANUAL_TO_DOWN, dev->keybit);
	set_bit(MOTOR_EVENT_UP, dev->keybit);
	set_bit(MOTOR_EVENT_UP_ABNORMAL, dev->keybit);
	set_bit(MOTOR_EVENT_UP_NORMAL, dev->keybit);
	set_bit(MOTOR_EVENT_DOWN, dev->keybit);
	set_bit(MOTOR_EVENT_DOWN_ABNORMAL, dev->keybit);
	set_bit(MOTOR_EVENT_DOWN_NORMAL, dev->keybit);

	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		return err;
	}

	chip->i_dev = dev;

	return 0;
}

static void report_positon_state_manual_to_up(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_MANUAL_TO_UP, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_MANUAL_TO_UP, 0);
	input_sync(chip->i_dev);
}

static void report_positon_state_manual_to_down(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_MANUAL_TO_DOWN, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_MANUAL_TO_DOWN, 0);
	input_sync(chip->i_dev);
}

static void report_positon_state_up(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_UP, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_UP, 0);
	input_sync(chip->i_dev);
}

static void report_positon_state_up_abnormal(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_UP_ABNORMAL, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_UP_ABNORMAL, 0);
	input_sync(chip->i_dev);
}

static void report_positon_state_up_normal(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_UP_NORMAL, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_UP_NORMAL, 0);
	input_sync(chip->i_dev);
}

static void report_positon_state_down(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_DOWN, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_DOWN, 0);
	input_sync(chip->i_dev);
}

static void report_positon_state_down_abnormal(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_DOWN_ABNORMAL, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_DOWN_ABNORMAL, 0);
	input_sync(chip->i_dev);
}

static void report_positon_state_down_normal(struct oppo_motor_chip *chip)
{
	input_report_key(chip->i_dev, MOTOR_EVENT_DOWN_NORMAL, 1);
	input_sync(chip->i_dev);
	input_report_key(chip->i_dev, MOTOR_EVENT_DOWN_NORMAL, 0);
	input_sync(chip->i_dev);
}

//should use in irq handle
int oppo_dhall_enable_irq (unsigned int id, bool enable)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->enable_irq) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->dhall_ops->enable_irq(id,enable);
	}
}

int oppo_dhall_clear_irq (unsigned int id)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->enable_irq) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->dhall_ops->clear_irq(id);
	}
}

int oppo_dhall_get_data(unsigned int id)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->get_data) {
  		return -EINVAL;
 	} else {
 		if (id == DHALL_0)
 			return g_the_chip->dhall_ops->get_data(id, &g_the_chip->dhall_data0);
 		else if (id == DHALL_1)
 			return g_the_chip->dhall_ops->get_data(id, &g_the_chip->dhall_data1);
 		else
 			return -EINVAL;
	}
}

bool oppo_dhall_update_threshold(unsigned int id, int position, short lowthd, short highthd)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->update_threshold) {
  		return false;
 	} else {
 		return g_the_chip->dhall_ops->update_threshold(id,position,lowthd,highthd);
	}
}

int oppo_dhall_set_detection_mode(unsigned int id, u8 mode)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->set_detection_mode) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->dhall_ops->set_detection_mode(id, mode);
	}
}

int oppo_dhall_get_irq_state(unsigned int id)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->get_irq_state) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->dhall_ops->get_irq_state(id);
	}
}

void oppo_dhall_dump_regs(unsigned int id, u8* buf)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->dump_regs) {
  		return ;
 	} else {
 		g_the_chip->dhall_ops->dump_regs(id,buf);
	}
}

int oppo_dhall_set_reg(unsigned int id, int reg, int val)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->set_reg) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->dhall_ops->set_reg(id,reg,val);
	}
}

bool oppo_dhall_is_power_on(void)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->dhall_ops->is_power_on) {
  		return false;
 	} else {
 	    if (g_the_chip->dhall_ops->is_power_on(DHALL_0) || g_the_chip->dhall_ops->is_power_on(DHALL_1))
 		    return true;
 		else
 		    return false;
	}

}

int oppo_motor_set_power (int mode)
{
	if (!g_the_chip || !g_the_chip->dhall_ops || !g_the_chip->motor_ops->set_power) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->motor_ops->set_power(mode);
	}
}

int oppo_motor_set_direction (int dir)
{
	if (!g_the_chip || !g_the_chip->motor_ops->set_direction) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->motor_ops->set_direction(dir);
	}
}

int oppo_motor_set_working_mode (int mode)
{
	if (!g_the_chip || !g_the_chip->motor_ops || !g_the_chip->motor_ops->set_working_mode) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->motor_ops->set_working_mode(mode);
	}

}

int oppo_motor_calculate_pwm_count(int L, int mode)
{
	if (!g_the_chip || !g_the_chip->motor_ops || !g_the_chip->motor_ops->calculate_pwm_count) {
  		return 0;
 	} else {
 		return g_the_chip->motor_ops->calculate_pwm_count(L, mode);
	}

}

int oppo_motor_pwm_config(int duty_ns, int period_ns)
{
	if (!g_the_chip || !g_the_chip->motor_ops || !g_the_chip->motor_ops->pwm_config) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->motor_ops->pwm_config(duty_ns, period_ns);
	}

}

int oppo_motor_pwm_enable(void)
{
	if (!g_the_chip || !g_the_chip->motor_ops || !g_the_chip->motor_ops->pwm_enable) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->motor_ops->pwm_enable();
	}

}

int oppo_motor_pwm_disable(void)
{
	if (!g_the_chip || !g_the_chip->motor_ops || !g_the_chip->motor_ops->pwm_disable) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->motor_ops->pwm_disable();
	}

}

int oppo_motor_get_all_config(int* config ,int count)
{
	if (!g_the_chip || !g_the_chip->motor_ops || !g_the_chip->motor_ops->get_all_config) {
  		return -EINVAL;
 	} else {
 		return g_the_chip->motor_ops->get_all_config(config,count);
	}

}

int oppo_get_motor_type(void)
{
	if (!g_the_chip || !g_the_chip->motor_ops || !g_the_chip->motor_ops->get_motor_type) {
  		return MOTOR_UNKNOWN;
 	} else {
 		return g_the_chip->motor_ops->get_motor_type();
	}

}

static void oppo_motor_notify_state(unsigned long val)
{
    if (val < MOTOR_UP_EVENT || val > MOTOR_BLOCK_EVENT)
        return;

    motor_notifier_call_chain(val);
}

static void oppo_motor_awake_init(struct oppo_motor_chip *chip)
{
	if (!chip)
		return;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND, "motor wakelock");
#else
	chip->suspend_ws = wakeup_source_register("motor wakelock");
#endif
}

static void oppo_motor_set_awake(struct oppo_motor_chip *chip, int id ,bool awake)
{
	static int awake_count = 0;
	static int wakelock_holder = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if (id >= MAX_LOCK)
		return;

	if (awake && !awake_count) {
		if (!(wakelock_holder & (1 << id))) {//this holder have not this lock now
			wakelock_holder |= (1 << id);
			awake_count ++;
			wake_lock(&chip->suspend_lock);
			MOTOR_LOG("awake\n");
		}
	} else if (!awake && (awake_count == 1)) {//only one lock hold
		if (wakelock_holder & (1 << id)) {
			wakelock_holder &= ~(1 << id);
			awake_count = 0;
			wake_unlock(&chip->suspend_lock);
			MOTOR_LOG("relax\n");
		}
	} else if (!awake) {
		if (awake_count != 0) {
			if (wakelock_holder & (1 << id)) {
				awake_count --;
				wakelock_holder &= ~(1 << id);
			}
		}
	} else {
		if (!(wakelock_holder & (1 << id))) {//this holder have not this lock now
			awake_count ++;
			wakelock_holder |= (1 << id);
		}
	}
	//MOTOR_LOG("awake_count %d wakelock_holder %d\n",awake_count,wakelock_holder);
#else
	if (!chip->suspend_ws)
		return;

	if (id >= MAX_LOCK)
		return;

	if (awake && !awake_count) {
		if (!(wakelock_holder & (1 << id))) {//this holder have not this lock now
			wakelock_holder |= (1 << id);
			awake_count ++;
			__pm_stay_awake(chip->suspend_ws);
			MOTOR_LOG("awake\n");
		}
	} else if (!awake && (awake_count == 1)) {//only one lock hold
		if (wakelock_holder & (1 << id)) {
			wakelock_holder &= ~(1 << id);
			awake_count = 0;
			__pm_relax(chip->suspend_ws);
			MOTOR_LOG("relax\n");
		}
	} else if (!awake) {
		if (awake_count != 0) {
			if (wakelock_holder & (1 << id)) {
				awake_count --;
				wakelock_holder &= ~(1 << id);
			}
		}
	} else {
		if (!(wakelock_holder & (1 << id))) {//this holder have not this lock now
			awake_count ++;
			wakelock_holder |= (1 << id);
		}
	}
	//MOTOR_LOG("awake_count %d wakelock_holder %d\n",awake_count,wakelock_holder);
#endif
}

//note:work in irq context
int oppo_dhall_irq_handler(unsigned int id)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return -EINVAL;
	}

	if (id == DHALL_0) {
		g_the_chip->hall_0_irq_count ++;
		queue_delayed_work(g_the_chip->manual2auto_wq, &g_the_chip->up_work, 0);
	} else if (id == DHALL_1) {
		g_the_chip->hall_1_irq_count ++;
		queue_delayed_work(g_the_chip->manual2auto_wq, &g_the_chip->down_work, 0);
	}
	return 0;
}

static void oppo_set_md_mode_para(int md_mode)
{
	int mode = 0;
	static int mode_pre = -1;

	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return ;
	}

	if ((md_mode >= MOTOR_MODE_FULL) && (md_mode <= MOTOR_MODE_1_32)) {
		if (mode_pre != md_mode) {
			mode_pre = md_mode;
			g_the_chip->md_mode = md_mode;
		} else {
			return;//working mode not change
		}
	}

	switch (g_the_chip->md_mode) {
		case MOTOR_MODE_FULL:
			oppo_motor_set_working_mode(MOTOR_MODE_FULL);
			mode = 1;
			break;

		case MOTOR_MODE_1_16:
			oppo_motor_set_working_mode(MOTOR_MODE_1_16);
			mode = 16;
			break;
		case MOTOR_MODE_1_32:
			oppo_motor_set_working_mode(MOTOR_MODE_1_32);
			mode = 32;
			break;
		default:
			oppo_motor_set_working_mode(MOTOR_MODE_1_32);
			mode = 32;
			break;
	}
	g_the_chip->pwm_count = oppo_motor_calculate_pwm_count(g_the_chip->md_L, mode);
	g_the_chip->pwm_count = g_the_chip->pwm_count * 32 / 20;
	g_the_chip->speed_up_pwm_count = oppo_motor_calculate_pwm_count(g_the_chip->speed_up_L, mode);
	g_the_chip->speed_down_pwm_count = oppo_motor_calculate_pwm_count(g_the_chip->speed_down_L, mode);
	MOTOR_LOG("pwm_count %d,md_L %d, speed_up_pwm_count %d, speed_down_pwm_count %d ,mdmode %d\n",
			g_the_chip->pwm_count, g_the_chip->md_L,
			g_the_chip->speed_up_pwm_count,g_the_chip->speed_down_pwm_count, mode);
}

static void oppo_set_speed_para(int speed)
{
	long long period_ns = 0;
	unsigned long duty_ns = 0;
	static int speed_pre = -1;

	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return ;
	}

	if ((speed >= MOTOR_SPEED0) && (speed <= MOTOR_SPEED13)) {
		if (speed_pre != speed) {
			speed_pre = speed;
			g_the_chip->md_speed = speed;
		} else {
			return;//speed not change
		}
	}
	switch (g_the_chip->md_speed) {
	case MOTOR_SPEED0:
		switch (g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 12000;// 80KHZ.
			break;
		default:
			period_ns = 12000;
			break;
		}
		break;
	case MOTOR_SPEED1:
		switch (g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 15500;// 64KHZ
			break;
		default:
			period_ns = 15500;
			break;
		}
		break;
	case MOTOR_SPEED2:
		switch (g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 19531;// 51.2HZ
			break;
		default:
			period_ns = 19531;
			break;
		}
		break;
	case MOTOR_SPEED3:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 26041;// 38.4KHZ
			break;

		default:
			period_ns = 26041;
			break;
		}
		break;
	case MOTOR_SPEED4:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 31250;// 32KHZ
			break;

		default:
			period_ns = 31250;
			break;
		}
		break;
	case MOTOR_SPEED5:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 39062;// 25.6KHZ
			break;

		default:
			period_ns = 39062;
			break;
		}
		break;

	case MOTOR_SPEED6:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 44642;// 22.4KHZ
			break;

		default:
			period_ns = 44642;
			break;
		}
		break;

	case MOTOR_SPEED7:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 52083;// 19.2KHZ
			break;

		default:
			period_ns = 52083;
			break;
		}
		break;

	case MOTOR_SPEED8:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 60240;// 16.6KHZ
			break;

		default:
			period_ns = 60240;
			break;
		}
		break;
	case MOTOR_SPEED9:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 78125;// 12.8KHZ
			break;

		default:
			period_ns = 78125;
			break;
		}
		break;
	case MOTOR_SPEED10:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 104166;// 9.6KHZ
			break;

		default:
			period_ns = 104166;
			break;
		}
		break;
	case MOTOR_SPEED11:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 156250;// 6.4KHZ
			break;

		default:
			period_ns = 156250;
			break;
		}
		break;
	case MOTOR_SPEED12:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 312500;// 3.2KHZ
			break;

		default:
			period_ns = 312500;
			break;
		}
		break;
			break;
	case MOTOR_SPEED13:
		switch(g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 625000;// 1.6KHZ
			break;

		default:
			period_ns = 625000;
			break;
		}
		break;


	default:
		switch (g_the_chip->md_mode) {
		case MOTOR_MODE_1_32:
			period_ns = 60240;// 16.6KHZ

		default:
			period_ns = 60240;
			break;
		}
		break;
	}
	duty_ns = (unsigned long)(period_ns/2);
	g_the_chip->pwm_duty = duty_ns;
	g_the_chip->pwm_period = period_ns;
}

static void oppo_set_direction_para(int direction)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return ;
	}

	if (direction >= 0)
		g_the_chip->md_dir = !!direction;
}

static void oppo_set_motor_move_state(int move_state)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return ;
	}

	if ((move_state >= MOTOR_STOP) && (move_state <= MOTOR_DOWNWARD_STOP)) {
		g_the_chip->move_state = move_state;
	}
}

static void oppo_parameter_init(struct oppo_motor_chip *chip)
{
	atomic_set(&chip->in_suspend, 0);
	chip->position = DOWN_STATE;
	chip->move_state = MOTOR_STOP;
	chip->motor_switch = 1;//switch open defaut
	chip->hall_0_irq_count = 0;
	chip->hall_1_irq_count = 0;
	chip->hall_0_irq_position = DHALL_DETECT_RANGE_HIGH;
	chip->hall_1_irq_position = DHALL_DETECT_RANGE_HIGH;
	chip->up_retard_hall0 = 0;
	chip->up_retard_hall1 = MOTOR_STOP_RETARD_VALUE;
	chip->down_retard_hall0 = MOTOR_STOP_RETARD_VALUE;
	chip->down_retard_hall1 = 0;
	chip->detect_delay = 10;//10ms
	chip->md_L = 87;//8.7mm
	chip->speed_up_L = 5;//0.5mm
	chip->speed_down_L = 2;//2mm
	chip->hall_detect_switch = true;
	chip->manual2auto_down_switch = 1;
	chip->manual2auto_up_switch = 0;
	chip->is_motor_test = 0;
	chip->is_skip_pos_check = 0;
	chip->irq_monitor_started = false;
	chip->is_irq_abnormal = false;

	if (oppo_get_motor_type() == MOTOR_FI5) { // fi 5 motor
		chip->is_fi_6_motor = false;
		chip->speed_can_down = true;
		chip->speed_can_up = true;
	} else {
		chip->is_fi_6_motor = true;
		chip->speed_can_down = true;
		chip->speed_can_up = false;
	}

	oppo_dhall_update_threshold(DHALL_0, DOWN_STATE, 511, 511);
	oppo_dhall_update_threshold(DHALL_1, DOWN_STATE, 511, 511);
	oppo_dhall_set_detection_mode(DHALL_0,DETECTION_MODE_INTERRUPT);
	oppo_dhall_set_detection_mode(DHALL_1,DETECTION_MODE_INTERRUPT);

	oppo_set_md_mode_para(MOTOR_MODE_1_32);

	if (chip->is_fi_6_motor)
		oppo_set_speed_para(MOTOR_SPEED8);
	else
		oppo_set_speed_para(MOTOR_SPEED4);

	oppo_set_direction_para(MOTOR_UPWARD);

	oppo_motor_awake_init(chip);

	MOTOR_LOG("is_fi_6_motor %d \n",chip->is_fi_6_motor);

}

static int oppo_motor_run_check(struct oppo_motor_chip *chip)
{
	if (chip->is_skip_pos_check) {
		MOTOR_ERR("skip_pos_check \n");
		return 1;
	}

	if ((chip->position == UP_STATE) && (chip->md_dir == MOTOR_UPWARD)) {
		MOTOR_LOG("has been in up_state, return false\n");
		return 0;
	} else if ((chip->position == DOWN_STATE) && (chip->md_dir == MOTOR_DOWN)) {
		MOTOR_LOG("has been in down_state, return false\n");
		return 0;
	}
	MOTOR_ERR("oppo_motor_run_check ok\n");
	return 1;
}

static void oppo_motor_control(int on ,int speed ,int direction)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return;
	}

	if (on) {
		if (g_the_chip->motor_switch == 0) {
			return;
		}
		if (!g_the_chip->motor_started) {
			g_the_chip->motor_enable = 1;
			oppo_set_speed_para(speed);
			oppo_set_direction_para(direction);
			if (oppo_motor_run_check(g_the_chip)) {
				schedule_work(&g_the_chip->motor_work);
			}
		}
	} else {
		if (g_the_chip->motor_started) {
			g_the_chip->motor_enable = 0;
			schedule_work(&g_the_chip->motor_work);
		}
	}
}

static void oppo_motor_start(void)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return;
	}

	oppo_motor_control(1, g_the_chip->md_speed, g_the_chip->md_dir);
}

static void oppo_motor_stop(void)
{
	int on = 0;
	int speed = 0;
	int direction = 0;

	oppo_motor_control(on, speed, direction);
}

static void oppo_motor_change_speed(int speed)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return;
	}

	oppo_set_speed_para(speed);

	mutex_lock(&motor_running_mutex);
	if (g_the_chip->motor_started) {
		oppo_motor_pwm_disable();
		oppo_motor_pwm_config(g_the_chip->pwm_duty, g_the_chip->pwm_period);
		oppo_motor_pwm_enable();
	}
	mutex_unlock(&motor_running_mutex);
}

static void oppo_motor_upward(void)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return;
	}

	oppo_motor_control(1, g_the_chip->md_speed, MOTOR_UPWARD);
}

static void oppo_motor_downward(void)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return;
	}

	oppo_motor_control(1, g_the_chip->md_speed, MOTOR_DOWN);
}
static irqreturn_t oppo_free_fall_detect_handler(int irq, void *dev_id)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip NULL \n");
		return -EINVAL;
	}
	MOTOR_LOG("call \n");

	disable_irq_nosync(g_the_chip->irq);
	oppo_motor_downward();
	enable_irq(g_the_chip->irq);

	return IRQ_HANDLED;
}

static void oppo_motor_free_fall_register(struct oppo_motor_chip * chip)
{
	struct device_node *np = NULL;
	int rc = 0;
	np = chip->dev->of_node;

	chip->pctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR(chip->pctrl)) {
		MOTOR_ERR("failed to get pinctrl\n");
		return;
	};

	chip->free_fall_state = pinctrl_lookup_state(chip->pctrl, "free_fall_input");
	if (IS_ERR(chip->free_fall_state)) {
		rc = PTR_ERR(chip->free_fall_state);
		MOTOR_ERR("pinctrl_lookup_state, err:%d\n", rc);
		return;
	};

	pinctrl_select_state(chip->pctrl,chip->free_fall_state);
	chip->free_fall_gpio = of_get_named_gpio(np, "motor,irq-gpio", 0);

	if (!gpio_is_valid(chip->free_fall_gpio)) {
		MOTOR_LOG("qcom,hall-power-gpio gpio not specified\n");
	} else {
		rc = gpio_request(chip->free_fall_gpio, "motor-irq-gpio");
		if (rc)
			MOTOR_LOG("request free_fall_gpio gpio failed, rc=%d\n",rc);

		rc = gpio_direction_input(chip->free_fall_gpio);
		msleep(50);
		chip->irq = gpio_to_irq(chip->free_fall_gpio);

		if (request_irq(chip->irq, &oppo_free_fall_detect_handler, IRQ_TYPE_EDGE_RISING, "free_fall", NULL)) {
			MOTOR_ERR("IRQ LINE NOT AVAILABLE!!\n");
			return;
		}
		irq_set_irq_wake(chip->irq, 1);
	}

	MOTOR_ERR("GPIO %d irq:%d \n",chip->free_fall_gpio, chip->irq);
}

static enum hrtimer_restart motor_stop_timer_func(struct hrtimer *hrtimer)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return HRTIMER_NORESTART;
	}

	MOTOR_LOG("call \n");
	g_the_chip->stop_timer_trigger = 1;

	oppo_motor_stop();
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart motor_speed_up_timer_func(struct hrtimer *hrtimer)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return HRTIMER_NORESTART;
	}

	MOTOR_LOG("call \n");

	mod_delayed_work(system_highpri_wq, &g_the_chip->detect_work, 0);

	return HRTIMER_NORESTART;
}

static enum alarmtimer_restart motor_reset_timer_func(struct alarm *alrm, ktime_t now)
{
	if (!g_the_chip) {
		MOTOR_LOG("g_the_chip null \n ");
		return HRTIMER_NORESTART;
	}

	MOTOR_LOG("hall_0_irq_count %d hall_1_irq_count %d \n",g_the_chip->hall_0_irq_count,g_the_chip->hall_1_irq_count);

	if ((g_the_chip->hall_0_irq_count >= MOTOR_IRQ_MONITOR_COUNT) ||
		(g_the_chip->hall_1_irq_count >= MOTOR_IRQ_MONITOR_COUNT)) {
		MOTOR_LOG("irq abnormal,reset \n");
		g_the_chip->is_irq_abnormal = true;
		g_the_chip->motor_switch = 0;
		g_the_chip->hall_0_irq_count = 0;
		g_the_chip->hall_1_irq_count = 0;
	}
	g_the_chip->irq_monitor_started = false;

	return ALARMTIMER_NORESTART;
}

static void oppo_motor_irq_monitor(struct oppo_motor_chip *chip)
{
	if (!chip->irq_monitor_started) {
		MOTOR_LOG("start \n");
		chip->irq_monitor_started = true;
		alarm_start_relative(&chip->reset_timer,
					ktime_set(MOTOR_IRQ_MONITOR_TIME / 1000, (MOTOR_IRQ_MONITOR_TIME % 1000) * 1000000));
	}
}

#ifdef CONFIG_DRM_MSM
static int fb_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	int blank;
	struct msm_drm_notifier *evdata = data;

	if (!g_the_chip) {
		return 0;
	}

	if (!evdata || (evdata->id != 0))
		return 0;

	if (event == MSM_DRM_EARLY_EVENT_BLANK) {
		blank = *(int *)(evdata->data);
		if (blank == MSM_DRM_BLANK_UNBLANK) {
			g_the_chip->led_on = true;
			MOTOR_LOG("led_on %d\n",g_the_chip->led_on);
		} else if (blank == MSM_DRM_BLANK_POWERDOWN) {
			g_the_chip->led_on = false;
			MOTOR_LOG("led_on %d\n",g_the_chip->led_on);
		} else {
			MOTOR_LOG("receives wrong data EARLY_BLANK:%d\n", blank);
		}
	}

	return 0;
}
#else
static int fb_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	int blank;
	struct fb_event *evdata = data;

	if (!g_the_chip) {
		return 0;
	}

	if (evdata && evdata->data) {
		if (event == FB_EVENT_BLANK) {
			blank = *(int *)evdata->data;
			if (blank == FB_BLANK_UNBLANK) {
				g_the_chip->led_on = true;
				MOTOR_LOG("led_on %d\n",g_the_chip->led_on);
			} else if (blank == FB_BLANK_POWERDOWN) {
				g_the_chip->led_on = false;
				MOTOR_LOG("led_on %d\n",g_the_chip->led_on);
			}
		}
	}
	return 0;
}
#endif /*CONFIG_DRM_MSM*/

static void motor_run_work(struct work_struct *work)
{
	struct oppo_motor_chip *chip = container_of(work, struct oppo_motor_chip, motor_work);
	unsigned long intsecond = 0;
	unsigned long nsecond = 0;
	long long value = 0;
	int ret = 0;

	mutex_lock(&motor_running_mutex);

	if (chip->motor_enable && (chip->motor_started == 0)) {
		MOTOR_ERR("start motor\n");

		oppo_motor_set_awake(chip,MOTOR_RUN_LOCK,true);
		oppo_motor_set_power(MOTOR_POWER_ON);
		oppo_motor_set_direction(chip->md_dir);

		if (!g_the_chip->is_motor_test) {
			if (chip->md_dir == MOTOR_UPWARD)
				oppo_dhall_update_threshold(DHALL_0, DOWN_STATE, 511, 511);
			else if (chip->md_dir == MOTOR_DOWN)
				oppo_dhall_update_threshold(DHALL_1, DOWN_STATE, 511, 511);
		}

		ret = oppo_motor_pwm_config(chip->pwm_duty, chip->pwm_period);
		if (ret < 0) {
			MOTOR_ERR("pwm_config fail ret =  %d \n",ret);
			chip->motor_started = 0;
			mutex_unlock(&motor_running_mutex);
			return;
		} else {
			MOTOR_ERR("pwm_config success ret = %d \n",ret);
		}

		ret = oppo_motor_pwm_enable();
		if (ret < 0) {
			MOTOR_ERR("pwm_enable fail ret = %d \n",ret);
			chip->motor_started = 0;
			mutex_unlock(&motor_running_mutex);
			return;
		} else {
			if (chip->md_dir == MOTOR_UPWARD) {
				report_positon_state_up(chip);
				oppo_set_motor_move_state(MOTOR_UPWARD_ING);
			} else if (chip->md_dir == MOTOR_DOWN) {
				report_positon_state_down(chip);
				oppo_set_motor_move_state(MOTOR_DOWNWARD_ING);
			}

			chip->motor_started = 1;
			MOTOR_ERR("pwm_enable success ret = %d \n",ret);
		}

		//calculate when the motor should stop
		value = chip->pwm_count * MOTOR_STOP_TIMEOUT;//chip->pwm_period;
		nsecond = do_div(value, 1000000000);//value = value/1000000000 ,nsecond = value % 1000000000
		intsecond = (unsigned long) value;

		MOTOR_ERR("time value = %llu nsecond = %lu intsecond = %lu.\n", value ,nsecond ,intsecond);
		hrtimer_start(&chip->stop_timer, ktime_set(intsecond, nsecond),HRTIMER_MODE_REL);

		//calculate when the motor should speed up
		value = chip->speed_up_pwm_count * chip->pwm_period;
		nsecond = do_div(value, 1000000000);//value = value/1000000000 ,nsecond = value % 1000000000
		intsecond = (unsigned long) value;

		MOTOR_ERR("time value = %llu nsecond = %lu intsecond = %lu.\n", value ,nsecond ,intsecond);
		hrtimer_start(&chip->speed_up_timer, ktime_set(intsecond, nsecond),HRTIMER_MODE_REL);

	} else if (!chip->motor_enable && (chip->motor_started == 1)) {
		MOTOR_ERR("stop motor\n");

		oppo_motor_pwm_disable();
		oppo_motor_set_power(MOTOR_POWER_OFF);

		chip->motor_started = 0;

		if (chip->move_state == MOTOR_UPWARD_ING) {
			oppo_set_motor_move_state(MOTOR_UPWARD_STOP);

			oppo_dhall_get_data(DHALL_1);
			if (chip->dhall_data1 > chip->hall_1_irq_position) {
				chip->position = UP_STATE;
				report_positon_state_up_normal(chip);
				MOTOR_ERR("POS_NORMAL, hall_1_irq_count %d\n",chip->hall_1_irq_count);
				chip->hall_1_irq_count = 0;
				oppo_motor_notify_state(MOTOR_UP_EVENT);
				oppo_dhall_update_threshold(DHALL_1,UP_STATE,-512,chip->hall_1_irq_position);
			} else {
				chip->position = MID_STATE;
				report_positon_state_up_abnormal(chip);
				MOTOR_ERR("POS_ABNORMAL %d %d\n",chip->dhall_data1 ,chip->hall_1_irq_position);
				oppo_motor_notify_state(MOTOR_BLOCK_EVENT);
			}
		} else if (chip->move_state == MOTOR_DOWNWARD_ING) {
			oppo_set_motor_move_state(MOTOR_DOWNWARD_STOP);

			oppo_dhall_get_data(DHALL_0);
			if (chip->dhall_data0 > chip->hall_0_irq_position) {
				chip->position = DOWN_STATE;
				report_positon_state_down_normal(chip);
				MOTOR_ERR("POS_NORMAL, hall_0_irq_count %d\n",chip->hall_0_irq_count);
				chip->hall_0_irq_count = 0;
				oppo_motor_notify_state(MOTOR_DOWN_EVENT);
				oppo_dhall_update_threshold(DHALL_0,DOWN_STATE,-512,chip->hall_0_irq_position);
			} else {
				chip->position = MID_STATE;
				report_positon_state_down_abnormal(chip);
				MOTOR_ERR("POS_ABNORMAL %d %d\n",chip->dhall_data0 ,chip->hall_0_irq_position);
				oppo_motor_notify_state(MOTOR_BLOCK_EVENT);
			}
		}

		if (!chip->stop_timer_trigger)
			hrtimer_cancel(&chip->stop_timer);
		else
			chip->stop_timer_trigger = 0;
		//set motor starting speed after motor stop
		if (chip->speed_can_up || chip->speed_can_down) {
			if (chip->is_fi_6_motor)
				oppo_set_speed_para(MOTOR_SPEED8);
			else
				oppo_set_speed_para(MOTOR_SPEED4);
		}

		oppo_motor_set_awake(chip,MOTOR_RUN_LOCK,false);
	}

	mutex_unlock(&motor_running_mutex);
}

static void manual_to_auto_up_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
  	struct oppo_motor_chip *chip = container_of(dwork, struct oppo_motor_chip, up_work);

	oppo_motor_set_awake(chip,HALL_DATA_LOCK,true);//dont sleep
	oppo_motor_irq_monitor(chip);

	if (atomic_read(&chip->in_suspend)) {
		MOTOR_ERR("in_suspend delay 20 ms \n");
		queue_delayed_work(chip->manual2auto_wq, &chip->up_work, msecs_to_jiffies(20));
		return;
	}

	oppo_dhall_update_threshold(DHALL_0,MID_STATE,511,511);

	if (chip->manual2auto_up_switch) {
		if (!chip->motor_started) {
			report_positon_state_manual_to_up(chip);
			oppo_motor_upward();
		}
	} else {
		if (!g_the_chip->is_motor_test)
			schedule_work(&chip->manual_position_work);
	}

	oppo_dhall_clear_irq(DHALL_0);

	if (!chip->is_irq_abnormal)
		oppo_dhall_enable_irq(DHALL_0,true);

	oppo_motor_set_awake(chip,HALL_DATA_LOCK,false);
}

static void manual_to_auto_down_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
  	struct oppo_motor_chip *chip = container_of(dwork, struct oppo_motor_chip, down_work);

	oppo_motor_set_awake(chip,HALL_DATA_LOCK,true);//dont sleep
	oppo_motor_irq_monitor(chip);

	if (atomic_read(&chip->in_suspend)) {
		MOTOR_ERR("in_suspend delay 20 ms \n");
		queue_delayed_work(chip->manual2auto_wq,&chip->down_work,msecs_to_jiffies(20));
		return;
	}

	oppo_dhall_update_threshold(DHALL_1,MID_STATE,511,511);

	if (chip->manual2auto_down_switch) {
		if (!chip->motor_started) {
			report_positon_state_manual_to_down(chip);
			oppo_motor_downward();
		}
	}

	oppo_dhall_clear_irq(DHALL_1);

	if (!chip->is_irq_abnormal)
		oppo_dhall_enable_irq(DHALL_1,true);

	oppo_motor_set_awake(chip,HALL_DATA_LOCK,false);

}

static void position_detect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
  	struct oppo_motor_chip *chip = container_of(dwork, struct oppo_motor_chip, detect_work);
  	int hall_delta_pre = 0;
  	int hall_delta = 0;
  	int delta_d = 0;
  	int speed_down = 0;
  	int speed_up = 0;
  	int should_stop_count = 0;

	mutex_lock(&position_detect_mutex);
	oppo_motor_set_awake(chip,POSITION_DETECT_LOCK,true);//should not be sleep during detecting

    while (1) {
    	//speed up
    	if (speed_up == 0) {
    		speed_up = 1;
    		if (chip->speed_can_up) {
    			if (chip->is_fi_6_motor)
    				oppo_motor_change_speed(MOTOR_SPEED8);
    			else
    				oppo_motor_change_speed(MOTOR_SPEED0);
    		}
    	}

    	oppo_dhall_get_data(DHALL_0);
    	oppo_dhall_get_data(DHALL_1);

    	hall_delta = chip->dhall_data0 - chip->dhall_data1;
    	delta_d = hall_delta - hall_delta_pre;
    	hall_delta_pre = hall_delta;

    	MOTOR_ERR("dhall_0 data:%d,dhall_1 data:%d ,hall_delta:%d,delta_d: %d\n",
    			chip->dhall_data0,chip->dhall_data1,hall_delta,delta_d);

    	//speed down , shoud fix magnetic noise
    	if ((chip->dhall_data0 - chip->dhall_data1) > (chip->down_retard_hall0 - chip->down_retard_hall1)) {
    		if ((chip->move_state == MOTOR_DOWNWARD_ING) && (speed_down == 0)) {
    		   		speed_down = 1;
    		   		MOTOR_ERR("downward change_speed speed_can_down:%d\n",chip->speed_can_down);
    		   		if (chip->speed_can_down) {
    		   			if (chip->is_fi_6_motor)
    						oppo_motor_change_speed(MOTOR_SPEED11);
    					else
    						oppo_motor_change_speed(MOTOR_SPEED11);
    				}
    		}
    	} else if ((chip->dhall_data1 - chip->dhall_data0) > (chip->up_retard_hall1 - chip->up_retard_hall0)) {
    	   	if ((chip->move_state == MOTOR_UPWARD_ING) && (speed_down == 0)) {
    		   		speed_down = 1;
    		   		MOTOR_ERR("upward change_speed speed_can_down:%d\n",chip->speed_can_down);
    		   		if (chip->speed_can_down) {
    		   			if (chip->is_fi_6_motor)
    						oppo_motor_change_speed(MOTOR_SPEED11);
    					else
    						oppo_motor_change_speed(MOTOR_SPEED11);
    				}
    		}
    	}

    	//stop motor
    	if ((delta_d >= MOTOR_STOP_STAITC_NEG_VALUE) && (delta_d <= MOTOR_STOP_STAITC_POS_VALUE)) {
    		should_stop_count ++;
    		if (should_stop_count >= 2) {
    			should_stop_count = 0;
    			hall_delta_pre = 0;
    			speed_down = 0;
    			speed_up = 0;
    			if (chip->hall_detect_switch)
    				oppo_motor_stop();
    			oppo_motor_set_awake(chip,POSITION_DETECT_LOCK,false);
    			break;
    		}
    	} else {
    		should_stop_count = 0;
    		//schedule_delayed_work(&chip->detect_work, msecs_to_jiffies(chip->detect_delay));
    	}
    	msleep(chip->detect_delay);
    }
	mutex_unlock(&position_detect_mutex);

}

static void manual_position_detect_work(struct work_struct *work)
{
	struct oppo_motor_chip *chip = container_of(work, struct oppo_motor_chip, manual_position_work);
	int hall_delta_pre = 0;
  	int hall_delta = 0;
  	int delta_d = 0;
  	int should_stop_count = 0;

	mutex_lock(&position_detect_mutex);
	oppo_motor_set_awake(chip,POSITION_DETECT_LOCK,true);//should not be sleep during detecting
	while (1) {
		oppo_dhall_get_data(DHALL_0);
		oppo_dhall_get_data(DHALL_1);

		hall_delta = chip->dhall_data0 - chip->dhall_data1;
		delta_d = hall_delta - hall_delta_pre;
		hall_delta_pre = hall_delta;

		MOTOR_ERR("dhall_0 data:%d,dhall_1 data:%d ,hall_delta:%d,delta_d: %d\n",
				chip->dhall_data0,chip->dhall_data1,hall_delta,delta_d);

		//stop motor
    	if ((delta_d >= MOTOR_STOP_STAITC_NEG_VALUE) && (delta_d <= MOTOR_STOP_STAITC_POS_VALUE)) {
    		should_stop_count ++;
    		if (should_stop_count >= 2) {
    			should_stop_count = 0;
    			hall_delta_pre = 0;
			if (chip->dhall_data1 > chip->hall_1_irq_position) {
				chip->position = UP_STATE;
				oppo_motor_notify_state(MOTOR_UP_EVENT);
				oppo_dhall_update_threshold(DHALL_1,UP_STATE,-512,chip->hall_1_irq_position);
			} else {
				chip->position = MID_STATE;
			}
			report_positon_state_manual_to_up(chip);
			MOTOR_ERR("up_retard_hall1 %d\n",chip->up_retard_hall1);
    			oppo_motor_set_awake(chip,POSITION_DETECT_LOCK,false);
    			break;
    		}
    	} else {
    		should_stop_count = 0;
    		//schedule_delayed_work(&chip->detect_work, msecs_to_jiffies(chip->detect_delay));
    	}
		msleep(chip->detect_delay);
	}
	mutex_unlock(&position_detect_mutex);
}

static void oppo_motor_reset_check(struct oppo_motor_chip *chip)
{
    oppo_dhall_get_data(DHALL_0);

    MOTOR_LOG("hall0 data %d  hall_0_irq_position %d \n",chip->dhall_data0,chip->hall_0_irq_position);

    if (chip->dhall_data0 < chip->hall_0_irq_position) {
        MOTOR_LOG("reset motor \n");
        oppo_motor_downward();
    }

}

//user space interface
static ssize_t motor_direction_store(struct device *pdev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	unsigned long direction = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (g_the_chip->motor_started)
		return count;

	if (sscanf(buf, "%lu", &direction) == 1) {
		oppo_set_direction_para(direction);
	}

	return count;;
}

static ssize_t motor_direction_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", g_the_chip->md_dir);
}

static ssize_t motor_speed_store(struct device *pdev, struct device_attribute *attr,
			   const char *buff, size_t count)
{

	unsigned long speed = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (g_the_chip->motor_started)
		return count;

	if (sscanf(buff, "%lu", &speed) == 1) {
		oppo_set_speed_para(speed);
	}

	MOTOR_LOG("speed:%d\n", g_the_chip->md_speed);

	return count;
}

static ssize_t motor_speed_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", g_the_chip->md_speed);
}

static ssize_t motor_mode_store(struct device *pdev, struct device_attribute *attr,
			   const char *buff, size_t count)
{
	int mdmode = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (g_the_chip->motor_started)
		return count;

	if (sscanf(buff, "%d", &mdmode) == 1) {
		MOTOR_LOG("mdmode = %d\n", mdmode);
		oppo_set_md_mode_para(mdmode);
	}
	return count;
}

static ssize_t  motor_mode_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", g_the_chip->md_mode);
}

static ssize_t motor_enable_store(struct device *pdev, struct device_attribute *attr,
					const char *buff, size_t count)
{
	unsigned long enable = 0;

	if (sscanf(buff, "%lu", &enable) == 1) {
		if (enable) {
			MOTOR_ERR("oppo_motor_start\n");
			oppo_motor_start();
		} else {
			oppo_motor_stop();
		}
	}
	return count;
}

static ssize_t motor_change_speed_store(struct device *pdev, struct device_attribute *attr,
						const char *buff, size_t count)
{
	unsigned long speed = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%lu", &speed) == 1) {
		oppo_motor_change_speed(speed);
	}
	return count;
}

static ssize_t motor_sw_switch_store(struct device *pdev, struct device_attribute *attr,
						const char *buff, size_t count)
{
	unsigned long sw_switch = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%lu", &sw_switch) == 1) {
		g_the_chip->motor_switch = sw_switch;
	}
	return count;
}

static ssize_t  motor_sw_switch_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);;
	}
	return snprintf(buf, PAGE_SIZE, "%d\n",g_the_chip->motor_switch);
}

static ssize_t motor_speed_change_switch_store(struct device *pdev, struct device_attribute *attr,
						const char *buff, size_t count)
{
	unsigned long sw_switch = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%lu", &sw_switch) == 1) {
		if (sw_switch == 0) {
			g_the_chip->speed_can_down = false;
			g_the_chip->speed_can_up = false;
		} else if (sw_switch == 1) {
			g_the_chip->speed_can_down = true;
		} else if (sw_switch == 2) {
			g_the_chip->speed_can_up = true;
		} else {
			g_the_chip->speed_can_down = true;
			g_the_chip->speed_can_up = true;
		}
	}
	return count;
}

static ssize_t  motor_speed_change_switch_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);;
	}
	return snprintf(buf, PAGE_SIZE, "%d,%d\n",g_the_chip->speed_can_down,g_the_chip->speed_can_up);
}

static ssize_t dhall_detect_switch_store(struct device *pdev, struct device_attribute *attr,
						const char *buff, size_t count)
{
	unsigned long sw_switch = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%lu", &sw_switch) == 1) {
		if (sw_switch)
			g_the_chip->hall_detect_switch = true;
		else
			g_the_chip->hall_detect_switch = false;
	}
	return count;
}

static ssize_t  dhall_detect_switch_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);;
	}
	return snprintf(buf, PAGE_SIZE, "%d\n",g_the_chip->hall_detect_switch);
}

static ssize_t  motor_all_config_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int config[6] = {0};

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);;
	}

	oppo_motor_get_all_config(config, 6);

	return snprintf(buf, PAGE_SIZE, "config {sleep:%d step:%d m0:%d m1:%d vref:%d dir:%d}\n",
			config[0],config[1],config[2],
			config[3],config[4],config[5]);
}

static ssize_t  motor_position_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int config[6] = {0};

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", g_the_chip->position);
}

static ssize_t  motor_test_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);;
	}

	MOTOR_LOG("is_motor_test %d\n",g_the_chip->is_motor_test);

	return snprintf(buf, PAGE_SIZE, "%d\n", g_the_chip->is_motor_test);
}

static ssize_t motor_test_store(struct device *pdev, struct device_attribute *attr,
						const char *buff, size_t count)
{
	int test = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%d", &test) == 1) {
		g_the_chip->is_motor_test = test;
	}
	MOTOR_LOG("test %d ,is_motor_test %d\n",test,g_the_chip->is_motor_test);
	return count;
}

static ssize_t dhall_data_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
	}

	oppo_dhall_get_data(DHALL_0);
	oppo_dhall_get_data(DHALL_1);

	MOTOR_ERR("dhall0 data %d dhall1 data %d \n",g_the_chip->dhall_data0,g_the_chip->dhall_data1);

	return sprintf(buf, "%d,%d\n",g_the_chip->dhall_data0,g_the_chip->dhall_data1);
}

static ssize_t dhall_all_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 _buf[1024] = {0};

	oppo_dhall_dump_regs(DHALL_0, _buf);

	return sprintf(buf, "%s\n", _buf);
}

static ssize_t motor_move_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n", -1);
	}
	return sprintf(buf, "%d\n", g_the_chip->move_state);
}

static ssize_t dhall_irq_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n%d\n",-1,-1);
	}
	return sprintf(buf, "%d,%d\n",g_the_chip->hall_0_irq_count, g_the_chip->hall_1_irq_count);
}

static ssize_t motor_manual2auto_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int manual2auto_switch = 0;

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n",0);
	}

	if ((g_the_chip->manual2auto_up_switch == 1) || (g_the_chip->manual2auto_down_switch == 1))
		manual2auto_switch = 1;

	return sprintf(buf, "%d\n",manual2auto_switch);
}

static ssize_t motor_manual2auto_switch_store(struct device *pdev, struct device_attribute *attr,
						const char *buff, size_t count)
{
	int data[1] = {0};

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%d", &data[0]) == 1) {
		g_the_chip->manual2auto_down_switch = data[0];
		//g_the_chip->manual2auto_up_switch = data[0];
		MOTOR_ERR("data[%d]\n",data[0]);
		if (data[0] == 1) {
		    if (g_the_chip->position == UP_STATE) {
		        oppo_dhall_update_threshold(DHALL_1,MID_STATE,
		                 DHALL_DETECT_RANGE_LOW,g_the_chip->hall_1_irq_position);
		    } else if (g_the_chip->position == DOWN_STATE) {
		        oppo_dhall_update_threshold(DHALL_0,MID_STATE,
		                DHALL_DETECT_RANGE_LOW,g_the_chip->hall_0_irq_position);
		    }
			g_the_chip->is_skip_pos_check = 0;
		} else {
			g_the_chip->is_skip_pos_check = 1;
		}
	} else {
		MOTOR_ERR("fail\n");
	}
	return count;
}

static ssize_t dhall_calibration_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n%d\n",-1,-1);
	}
	return sprintf(buf, "%d,%d,%d,%d,%d,%d\n",g_the_chip->hall_0_irq_position, g_the_chip->hall_1_irq_position,
	                                     g_the_chip->down_retard_hall0, g_the_chip->down_retard_hall1,
	                                     g_the_chip->up_retard_hall0, g_the_chip->up_retard_hall1);
}

static ssize_t dhall_calibration_store(struct device *pdev, struct device_attribute *attr,
						const char *buff, size_t count)
{
	int data[6] = {0};

	if (!g_the_chip) {
		MOTOR_ERR("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%d,%d,%d,%d,%d,%d", &data[0],&data[1],&data[2],&data[3],&data[4],&data[5]) == 6) {
		if (data[0] > 0)
			g_the_chip->hall_0_irq_position = data[0];
		if (data[1] > 0)
			g_the_chip->hall_1_irq_position = data[1];
		if (data[2] > 0)//near hall1
		    g_the_chip->down_retard_hall0 = data[2];
		if (data[3] > 0)//near hall2
		    g_the_chip->down_retard_hall1 = data[3];
		if (data[4] > 0)//far hall1
		    g_the_chip->up_retard_hall0 = data[4];
		if (data[5] > 0)//far hall2
		    g_the_chip->up_retard_hall1 = data[5];
		MOTOR_ERR("data[%d %d %d %d %d %d]\n",data[0],data[1],data[2],data[3],data[4],data[5]);

		oppo_dhall_update_threshold(DHALL_0, DOWN_STATE, DHALL_DETECT_RANGE_LOW, g_the_chip->hall_0_irq_position);
	} else {
		MOTOR_ERR("fail\n");
	}
	return count;

}

static DEVICE_ATTR(direction, S_IRUGO | S_IWUSR, motor_direction_show, motor_direction_store);
static DEVICE_ATTR(speed,   S_IRUGO | S_IWUSR, motor_speed_show, motor_speed_store);
static DEVICE_ATTR(mode,    S_IRUGO | S_IWUSR, motor_mode_show, motor_mode_store);
static DEVICE_ATTR(enable,  S_IRUGO | S_IWUSR, NULL, motor_enable_store);
static DEVICE_ATTR(change_speed,   S_IRUGO | S_IWUSR,NULL, motor_change_speed_store);
static DEVICE_ATTR(move_state,    S_IRUGO | S_IWUSR, motor_move_state_show,NULL);
static DEVICE_ATTR(config,   S_IRUGO | S_IWUSR, motor_all_config_show,NULL);
static DEVICE_ATTR(sw_switch,    S_IRUGO | S_IWUSR, motor_sw_switch_show,motor_sw_switch_store);
static DEVICE_ATTR(speed_change_switch,    S_IRUGO | S_IWUSR, motor_speed_change_switch_show,motor_speed_change_switch_store);
static DEVICE_ATTR(position,   S_IRUGO | S_IWUSR, motor_position_show,NULL);
static DEVICE_ATTR(manual2auto_switch,   S_IRUGO | S_IWUSR, motor_manual2auto_switch_show,motor_manual2auto_switch_store);
static DEVICE_ATTR(motor_test,   S_IRUGO | S_IWUSR, motor_test_show,motor_test_store);
static DEVICE_ATTR(hall_data,   S_IRUGO | S_IWUSR,dhall_data_show,NULL);
static DEVICE_ATTR(hall_reg,   S_IRUGO | S_IWUSR,dhall_all_reg_show,NULL);
static DEVICE_ATTR(hall_irq_count,   S_IRUGO | S_IWUSR,dhall_irq_count_show,NULL);
static DEVICE_ATTR(hall_calibration,   S_IRUGO | S_IWUSR,dhall_calibration_show,dhall_calibration_store);
static DEVICE_ATTR(hall_detect_switch,   S_IRUGO | S_IWUSR,dhall_detect_switch_show,dhall_detect_switch_store);

static struct attribute * __attributes[] = {
	&dev_attr_direction.attr,
	&dev_attr_speed.attr,
	&dev_attr_mode.attr,
	&dev_attr_enable.attr,
	&dev_attr_change_speed.attr,
	&dev_attr_move_state.attr,
	&dev_attr_config.attr,
	&dev_attr_sw_switch.attr,
	&dev_attr_speed_change_switch.attr,
	&dev_attr_position.attr,
	&dev_attr_manual2auto_switch.attr,
	&dev_attr_motor_test.attr,
	&dev_attr_hall_data.attr,
	&dev_attr_hall_reg.attr,
	&dev_attr_hall_irq_count.attr,
	&dev_attr_hall_calibration.attr,
	&dev_attr_hall_detect_switch.attr,
	NULL
};

static struct attribute_group __attribute_group = {
	.attrs = __attributes
};

/*-----misc-------*/
static int motor_open( struct inode* inode, struct file* file)
{
	return nonseekable_open(inode, file);
}

static int motor_release( struct inode* inode, struct file* file)
{
	file->private_data = NULL;
	return 0;
}

static long motor_ioctl(struct file* file, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	int data = 0;
	short raw = 0;
	void __user *u_arg = (void __user *)arg;
	cali_data_t  cali_data;
	dhall_data_t dhall_data;

	if (NULL == g_the_chip) {
		MOTOR_ERR("g_the_chip null!!\n");
		return -EFAULT;
	}

	if(_IOC_DIR(cmd) & _IOC_READ) {
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	} else if (_IOC_DIR(cmd) & _IOC_WRITE) {
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}
	if (err) {
		MOTOR_ERR("access error: %08X \n", cmd);
		return -EFAULT;
	}

	switch (cmd) {
	case MOTOR_IOCTL_START_MOTOR:
		if (copy_from_user(&data, u_arg, sizeof(data))) {
			MOTOR_ERR("start motor fail \n");
			return -EFAULT;
		}

		if (data)
			oppo_motor_start();;
		break;

	case MOTOR_IOCTL_STOP_MOTOR:
		if (copy_from_user(&data, u_arg, sizeof(data))) {
			MOTOR_ERR("stop motor fail \n");
			return -EFAULT;
		}

		if (data)
			oppo_motor_stop();
		break;

	case MOTOR_IOCTL_MOTOR_UPWARD:
		if (copy_from_user(&data, u_arg, sizeof(data))) {
			MOTOR_ERR("motor upward fail \n");
			return -EFAULT;
		}

		if (data)
			oppo_motor_upward();;
		break;

	case MOTOR_IOCTL_MOTOR_DOWNWARD:
		if (copy_from_user(&data, u_arg, sizeof(data))) {
			MOTOR_ERR("motor downward fail \n");
			return -EFAULT;
		}

		if (data)
			oppo_motor_downward();
		break;

	case MOTOR_IOCTL_GET_POSITION:
		if (u_arg == NULL) {
			MOTOR_ERR("get position fail0 \n");
			break;
		}

		data = g_the_chip->position;
		if (copy_to_user(u_arg, &data, sizeof(data))) {
			MOTOR_ERR("get position fail1 \n");
			return -EFAULT;
		};
		break;

	case MOTOR_IOCTL_SET_DIRECTION:
		if (copy_from_user(&data, u_arg, sizeof(data))) {
			MOTOR_ERR("set direction fail \n");
			return -EFAULT;
		}

		oppo_set_direction_para(data);
		break;

	case MOTOR_IOCTL_SET_SPEED:
		if (copy_from_user(&data, u_arg, sizeof(data))) {
			MOTOR_ERR("set speed fail \n");
			return -EFAULT;
		}

		oppo_set_speed_para(data);
		break;

	case MOTOR_IOCTL_SET_DELAY:
	case MOTOR_IOCTL_GET_DHALL_DATA:
		if (u_arg == NULL) {
			MOTOR_ERR("get dhall data fail0 \n");
			break;
		}

		oppo_dhall_get_data(DHALL_0);
		oppo_dhall_get_data(DHALL_1);
		dhall_data.data0 = g_the_chip->dhall_data0;
		dhall_data.data1 = g_the_chip->dhall_data1;

		if (copy_to_user(u_arg, &dhall_data, sizeof(dhall_data))) {
			MOTOR_ERR("get position fail1 \n");
			return -EFAULT;
		};
	case MOTOR_IOCTL_SET_CALIBRATION:
		if (copy_from_user(&cali_data, u_arg, sizeof(cali_data))) {
			MOTOR_ERR("set calibration fail \n");
			return -EFAULT;
		}
		g_the_chip->hall_0_irq_position  = cali_data.dhall_0_irq_position;
		g_the_chip->hall_1_irq_position  = cali_data.dhall_1_irq_position;
		g_the_chip->down_retard_hall0   = cali_data.down_retard_hall0;
		g_the_chip->down_retard_hall1 = cali_data.down_retard_hall1;
		g_the_chip->up_retard_hall0 = cali_data.up_retard_hall0;
		g_the_chip->up_retard_hall1 = cali_data.up_retard_hall1;
		break;
	case MOTOR_IOCTL_GET_INTERRUPT:
	default:
		return -ENOTTY;
	}

	return err;
}

static struct file_operations motor_fops = {
	.owner = THIS_MODULE,
	.open = motor_open,
	.unlocked_ioctl = motor_ioctl,
	.release = motor_release,
};

static struct miscdevice motor_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "motor",
	.fops = &motor_fops,
};

static int motor_platform_probe(struct platform_device *pdev)
{
	struct oppo_motor_chip *chip = NULL;
	struct proc_dir_entry *dir = NULL;
	int err = 0;

	MOTOR_LOG("call \n");

	if (!g_the_chip) {
		chip = kzalloc(sizeof(struct oppo_motor_chip), GFP_KERNEL);
		if (!chip) {
			MOTOR_ERR("kzalloc err \n");
			return -ENOMEM;
		}
		g_the_chip = chip;
	} else {
		chip = g_the_chip;
	}

	chip->dev = &pdev->dev;

	if (!chip->dhall_ops) {
		MOTOR_ERR("no dhall available \n");
		goto fail;
	}

	if (!chip->motor_ops) {
		MOTOR_ERR("no motor driver available \n");
		goto fail;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &__attribute_group);
	if(err) {
		MOTOR_ERR("sysfs_create_group was failed(%d) \n", err);
		goto sysfs_create_fail;
	}

	err = misc_register(&motor_device);
	if (err) {
		MOTOR_ERR("misc_register was failed(%d)", err);
		goto misc_fail;
	}

	err = oppo_input_dev_init(chip);
	if (err < 0) {
		MOTOR_ERR("oppo_input_dev_init fail \n");
		goto input_fail;
	}

	chip->manual2auto_wq = create_singlethread_workqueue("manual2auto_wq");
	if (!chip->manual2auto_wq) {
	    MOTOR_ERR("manual2auto_wq NULL \n");
		goto input_fail;
	}

	if ((is_project(OPPO_17107) && (get_PCB_Version() >= HW_VERSION__13)) ||
		(is_project(OPPO_17127) && (get_PCB_Version() >= HW_VERSION__11)))
	    oppo_motor_free_fall_register(chip);

	oppo_parameter_init(chip);

	hrtimer_init(&chip->stop_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	chip->stop_timer.function = motor_stop_timer_func;

	hrtimer_init(&chip->speed_up_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	chip->speed_up_timer.function = motor_speed_up_timer_func;

	INIT_WORK(&chip->motor_work, motor_run_work);
	INIT_WORK(&chip->manual_position_work,manual_position_detect_work);

	alarm_init(&chip->reset_timer, ALARM_BOOTTIME, motor_reset_timer_func);

	INIT_DELAYED_WORK(&chip->detect_work, position_detect_work);
	INIT_DELAYED_WORK(&chip->up_work, manual_to_auto_up_work);
	INIT_DELAYED_WORK(&chip->down_work, manual_to_auto_down_work);

	chip->fb_notify.notifier_call = fb_notifier_callback;
	#ifdef CONFIG_DRM_MSM
	msm_drm_register_client(&chip->fb_notify);
	#else
	fb_register_client(&chip->fb_notify);
	#endif

	oppo_motor_reset_check(g_the_chip);

	MOTOR_LOG("success. \n");
	return 0;

input_fail:
	misc_deregister(&motor_device);
misc_fail:
	sysfs_remove_group(&pdev->dev.kobj, &__attribute_group);
sysfs_create_fail:
fail:
	kfree(chip);
	g_the_chip = NULL;
	MOTOR_LOG("fail \n");
	return -EINVAL;
}

static int motor_platform_remove(struct platform_device *pdev)
{
	if (g_the_chip) {
		sysfs_remove_group(&pdev->dev.kobj, &__attribute_group);
		misc_deregister(&motor_device);
		input_unregister_device(g_the_chip->i_dev);
        input_free_device(g_the_chip->i_dev);
		kfree(g_the_chip);
		g_the_chip = NULL;
	}
	return 0;
}

static int motor_platform_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (g_the_chip) {
		atomic_set(&g_the_chip->in_suspend, 1);
	}
	return 0;
}

static int motor_platform_resume(struct platform_device *pdev)
{
	if (g_the_chip) {
		atomic_set(&g_the_chip->in_suspend, 0);
	}
	return 0;
}
static void motor_platform_shutdown(struct platform_device *pdev)
{
	if (g_the_chip) {
		oppo_motor_reset_check(g_the_chip);
	}
	return;
}


static const struct of_device_id of_motor_match[] = {
	{ .compatible = "oppo-motor"},
	{},
};
MODULE_DEVICE_TABLE(of, of_motor_match);

static struct platform_driver motor_platform_driver = {
	.probe		= motor_platform_probe,
	.remove		= motor_platform_remove,
	.suspend	= motor_platform_suspend,
	.resume		= motor_platform_resume,
	.shutdown   = motor_platform_shutdown,
	.driver		= {
		.name	= "oppo_motor",
		.of_match_table = of_motor_match,
	},
};

static int __init motor_platform_init(void)
{
	MOTOR_LOG("call \n");

	platform_driver_register(&motor_platform_driver);
	return 0;
}

late_initcall(motor_platform_init);
MODULE_DESCRIPTION("camera motor platform driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mofei@oppo.com");
