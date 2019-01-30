/************************************************************************************
** File: - kernel/msm-4.9/techpack/audio/asoc/codecs/as6313/as6313.c
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include "as6313.h"

#define I2C_RETRY_DELAY		5 /* ms */
#define I2C_RETRIES		5
#define FSA4480_ERR_I2C	-1
#define NAME "as6313"

static struct i2c_client *as6313_client;
struct as6313_dev {
	struct	i2c_client	*client;
	int					mode;
	spinlock_t			lock;
};
struct as6313_dev *as6313_priv;

static int as6313_i2c_read(u8 reg, u8 *value)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
			.flags = 0,
			.len = 1,
			.buf = &reg,
		},
		{
			.flags = I2C_M_RD,
			.len = 1,
			.buf = value,
		},
	};

	if (!as6313_client) {
		pr_err("%s: as6313 not work!!!!", __func__);
		return -EINVAL;
	}
	msgs[0].addr = as6313_client->addr;
	msgs[1].addr = as6313_client->addr;
	do {
		err = i2c_transfer(as6313_client->adapter, msgs,
					ARRAY_SIZE(msgs));
		if (err != ARRAY_SIZE(msgs)) {
			msleep_interruptible(I2C_RETRY_DELAY);
		}
	} while ((err != ARRAY_SIZE(msgs)) && (++tries < I2C_RETRIES));

	if (err != ARRAY_SIZE(msgs)) {
		dev_err(&as6313_client->dev, "read transfer error %d\n", err);
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

extern int oppo_dynamic_switch_i2c_100KHz(struct i2c_adapter *adap);
extern int oppo_restore_old_i2c_speed(struct i2c_adapter *adap);

static int as6313_i2c_write(u8 reg, u8 value)
{
	int err;
	int tries = 0;
	u8 buf[2] = {0, 0};

	struct i2c_msg msgs[] = {
		{
			.flags = 0,
			.len = 2,
			.buf = buf,
		},
	};

	if (!as6313_client) {
		pr_err("%s: as6313 not work!!!!", __func__);
		return -EINVAL;
	}

	msgs[0].addr = as6313_client->addr;
	buf[0] = reg;
	buf[1] = value;

	err = oppo_dynamic_switch_i2c_100KHz(as6313_client->adapter);

	do {
		err = i2c_transfer(as6313_client->adapter, msgs,
				ARRAY_SIZE(msgs));

		if (err != ARRAY_SIZE(msgs)) {
			msleep_interruptible(I2C_RETRY_DELAY);
		}
	} while ((err != ARRAY_SIZE(msgs)) && (++tries < I2C_RETRIES));

	oppo_restore_old_i2c_speed(as6313_client->adapter);

	if (err != ARRAY_SIZE(msgs)) {
		dev_err(&as6313_client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static void io_write_reg0(int data)
{
	if (as6313_priv == NULL) {
		pr_err("%s: as6313_priv == NULL \n", __func__);
	}

	as6313_i2c_write(0x00, data);
}

static void io_write_reg1(int data)
{
	as6313_i2c_write(0x01, data);
}

static void io_disable_irq(void) {
	io_write_reg1(1);
}

static void io_irq_clear(void) {
	io_write_reg1(0);
}

static int io_read_reg0(void)
{
	u8 data;

	as6313_i2c_read(0x00, &data);

	return data;
}


static int io_read_reg1(void)
{
	u8 data;

	as6313_i2c_read(0x01, &data);

	return data;
}

//===============Applications=================//
int as6313_get_reg0(void) { return io_read_reg0(); }

int as6313_get_reg1(void) { return io_read_reg1(); }

void as6313_set_reg0(int data) { io_write_reg0(data & 7); }

void as6313_set_reg1(int data) { io_write_reg1(data & 1); }

void as6313_USB(void) {
	pr_err("%s: as6313 set usb!!!!", __func__);
	io_write_reg0(0);
}   //back to USB mode

void as6313_OMTP(void)			//enter OMTP mode
{
	pr_err("%s: as6313 set OMTP!!!!", __func__);
	io_write_reg0(7);
	io_write_reg0(1);
}

void as6313_CTIA(void)			//enter CTIA mode
{
	pr_err("%s: as6313 set CTIA!!!!", __func__);
	io_write_reg0(7);
	io_write_reg0(2);
}

void as6313_TRS(void)			// enter TRS mode
{
	io_write_reg0(7);
	io_write_reg0(3);
}

void as6313_UART(void)			//enter UART mode
{
	io_write_reg0(7);
	io_write_reg0(5);
}

void as6313_AUX(void)			//enter AUX mode
{
	io_write_reg0(7);
	io_write_reg0(6);
}

void as6313_OFF(void)			//enter OFF mode
{
	io_write_reg0(7);
}

void as6313_disable_irq(void)	//disable IRQ
{
	io_disable_irq();
}

void as6313_clear_irq(void)		//clear IRQ
{
	io_irq_clear();
}

static int as6313_set_mode(int mode)
{
	pr_info("set mode %d\n", mode);
	switch (mode) {
	case AS_MODE_USB:
		as6313_USB();
		break;
	case AS_MODE_OMTP:
		as6313_OMTP();
		break;
	case AS_MODE_CTIA:
		as6313_CTIA();
		break;
	case AS_MODE_TRS:
		as6313_TRS();
		break;
	case AS_MODE_UART:
		as6313_UART();
		break;
	case AS_MODE_AUX:
		as6313_AUX();
		break;
	case AS_MODE_OFF:
		as6313_OFF();
		break;
	default:
		pr_warn("Invalid mode %d\n", mode);
		return -1;
	}
	as6313_priv->mode = mode;
	return 0;
}

static int as6313_handle_irq(int oper)
{
	pr_info("handle irq %d\n", oper);
	switch (oper) {
	case AS_IRQ_ENABLE:
		as6313_clear_irq();
		break;
	case AS_IRQ_DISABLE:
		as6313_disable_irq();
		break;
	default:
		pr_warn("Invalid oper %d\n", oper);
		return -1;
	}
	return 0;
}

static int as6313_get_mode(void)
{
	int value = as6313_get_reg0();
	if (value > -1 && value <= 7) {
		int mode = value & 0x07;
		switch (mode) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
			as6313_priv->mode = mode;
			break;
		case 0x05:
		case 0x06:
		case 0x07:
			as6313_priv->mode = mode - 1;
			break;
		default:
			break;
		}
	}
	return as6313_priv->mode;
}

static int as6313_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int as6313_close(struct inode *inode, struct file *file)
{
	return 0;
}

static long  as6313_ioctl(struct file *filep, unsigned int cmd,
                unsigned long arg)
{
	long err = 0;
	int mode;
	void __user *argp = (void __user *)arg;

	switch(cmd) {
	case AS_IOCTL_SET_MODE:
		err = as6313_set_mode(arg);
		break;
	case AS_IOCTL_GET_MODE:
		mode = as6313_get_mode();
		err = copy_to_user(argp, &mode, sizeof(int));
		break;
	case AS_IOCTL_SET_IRQ:
		err = as6313_handle_irq(arg);
		break;
	default:
		pr_warn("Invalid cmd 0x%03x\n", cmd);
		break;
	}
	return err;
}

static ssize_t sysfs_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf, u32 type)
{
	int value = 0;

	switch (type) {
	case AS_DBG_TYPE_MODE:
		value = as6313_get_mode();
		break;
	case AS_DBG_TYPE_IRQ:
		value = as6313_get_reg1() & 0x01;
		break;
	case AS_DBG_TYPE_REG0:
		value = as6313_get_reg0();
		break;
	case AS_DBG_TYPE_REG1:
		value = as6313_get_reg1();
		break;
	default:
		pr_warn("%s: invalid type %d\n", __func__, type);
		break;
	}
	return sprintf(buf, "0x%04x\n", value);
}

static ssize_t sysfs_set(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count, u32 type)
{
	int err;
	unsigned long value;

	err = kstrtoul(buf, 10, &value);

	if (err) {
		pr_warn("%s: get data of type %d failed\n", __func__, type);
		return err;
	}

	pr_info("%s: set type %d, data %ld\n", __func__, type, value);
	switch (type) {
	case AS_DBG_TYPE_MODE:
		as6313_set_mode(value);
		break;
	case AS_DBG_TYPE_IRQ:
		as6313_handle_irq(value);
		break;
	case AS_DBG_TYPE_REG0:
		as6313_set_reg0(value);
		break;
	case AS_DBG_TYPE_REG1:
		as6313_set_reg1(value);
		break;
	default:
		pr_warn("%s: invalid type %d\n", __func__, type);
		break;
	}
	return count;
}

#define AS6313_DEVICE_SHOW(_name, _type) static ssize_t \
show_##_name(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
{ \
	return sysfs_show(dev, attr, buf, _type); \
}

#define AS6313_DEVICE_SET(_name, _type) static ssize_t \
set_##_name(struct device *dev, \
			 struct device_attribute *attr, \
			 const char *buf, size_t count) \
{ \
	return sysfs_set(dev, attr, buf, count, _type); \
}

#define AS6313_DEVICE_SHOW_SET(name, type) \
AS6313_DEVICE_SHOW(name, type) \
AS6313_DEVICE_SET(name, type) \
static DEVICE_ATTR(name, S_IWUSR | S_IRUGO, show_##name, set_##name);

AS6313_DEVICE_SHOW_SET(as6313_mode, AS_DBG_TYPE_MODE);
AS6313_DEVICE_SHOW_SET(as6313_handle_irq, AS_DBG_TYPE_IRQ);
AS6313_DEVICE_SHOW_SET(as6313_reg0, AS_DBG_TYPE_REG0);
AS6313_DEVICE_SHOW_SET(as6313_reg1, AS_DBG_TYPE_REG1);

static struct attribute *as6313_attrs[] = {
	&dev_attr_as6313_mode.attr,
	&dev_attr_as6313_handle_irq.attr,
	&dev_attr_as6313_reg0.attr,
	&dev_attr_as6313_reg1.attr,
	NULL
};

static const struct attribute_group as6313_group = {
	.attrs = as6313_attrs,
};

static struct file_operations as6313_ops = {
	.owner			= THIS_MODULE,
	.open			= as6313_open,
	.release		= as6313_close,
	.unlocked_ioctl	= as6313_ioctl,
};

static struct miscdevice as6313_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name =	"as_sbu",
	.fops = &as6313_ops,
};

static int as6313_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int err;

	pr_err("%s: register as6313 driver probe\n", __func__);

	if (client == NULL) {
		pr_err("%s: client == NULL\n", __func__);
		return -EIO;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "check_functionality failed\n");
		pr_err("%s: i2c_check_functionality fail\n", __func__);
		return -EIO;
	}

	as6313_priv = kzalloc(sizeof(*as6313_priv), GFP_KERNEL);
	if (as6313_priv == NULL) {
		err = -ENOMEM;
		pr_err("as6313_priv kzalloc is invalid\n");
		return err;
	}
	as6313_priv->client = client;
	as6313_priv->mode = AS_MODE_USB;
	spin_lock_init(&as6313_priv->lock);

	/* set global variables */
	as6313_client = client;
	as6313_USB();

	err = misc_register(&as6313_dev);
	if(err) {
		pr_err("%s: register as6313 driver as misc device error\n", __func__);
		goto exit_init;
	}

	dev_set_drvdata(&client->dev, as6313_priv);
	err = sysfs_create_group(&as6313_dev.this_device->kobj, &as6313_group);
	pr_err("%s: end err = %d\n", __func__, err);
exit_init:
	return err;
}

EXPORT_SYMBOL_GPL(as6313_USB);
EXPORT_SYMBOL_GPL(as6313_CTIA);
EXPORT_SYMBOL_GPL(as6313_OMTP);

static int __exit as6313_remove(struct i2c_client *client)
{
	sysfs_remove_group(&as6313_dev.this_device->kobj, &as6313_group);
	misc_deregister(&as6313_dev);
	kfree(as6313_priv);

	return 0;
}

static const struct i2c_device_id as6313_id[] = {
	{NAME, 0},
	{},
};

static const struct of_device_id as6313_match[] = {
	{ .compatible = "oppo,as6313" },
	{},
};

MODULE_DEVICE_TABLE(i2c, as6313_id);

static struct i2c_driver as6313_driver = {
	.driver = {
		.name = NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(as6313_match),
	},
	.probe = as6313_probe,
	.remove = as6313_remove,
	.id_table = as6313_id,
};

static int __init as6313_init(void)
{
	pr_err("Enter %s\n", __func__);

	return i2c_add_driver(&as6313_driver);
}

static void __exit as6313_exit(void)
{
	pr_err("Enter %s\n", __func__);

	i2c_del_driver(&as6313_driver);

	return;
}

module_init(as6313_init);
module_exit(as6313_exit);

MODULE_DESCRIPTION("as6313 driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("OPPO");
