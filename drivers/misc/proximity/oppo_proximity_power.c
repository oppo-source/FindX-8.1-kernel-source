/************************************************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_proximity_power.c
**
** Description:
**      Definitions for m1120 motor driver ic proximity_power.
**
** Version: 1.0
** Date created: 2018/01/14,20:27
** Author: Fei.Mo@PSW.BSP.Sensor
**
** --------------------------- Revision History: ------------------------------------
* <version>		<date>		<author>		<desc>
* Revision 1.0		2018/01/14	Fei.Mo@PSW.BSP.Sensor	Created
**************************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/qpnp/pwm.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/qpnp/pin.h>
#include <soc/oppo/oppo_project.h>

#include "oppo_proximity_power.h"

static struct oppo_proximity_chip *g_the_chip = NULL;

static void proximity_power_parse_dts(struct oppo_proximity_chip * chip)
{
	struct device_node *np = chip->dev->of_node;
	int rc = 0;

	chip->pctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR(chip->pctrl)) {
		printk("failed to get pinctrl\n");
	};

	chip->power_gpio = of_get_named_gpio(np, "qcom,power_gpio", 0);
	if (!gpio_is_valid(chip->power_gpio)) {
		printk("power_gpio gpio not specified\n");
	} else {
		rc = gpio_request(chip->power_gpio, "proximity_power_gpio");
		if (rc)
			printk("request proximity_power_gpio failed, rc=%d\n",rc);
	}

	printk("%d \n",chip->power_gpio);
}

static int proximity_power_config(struct oppo_proximity_chip * chip ,int config)
{
	int ret = 0;

	if (IS_ERR(chip->pctrl)) {
		ret = PTR_ERR(chip->pctrl);
		printk("failed to get pinctrl\n");
		return ret;
	};

	if (config == GPIO_LOW) {
		chip->power_state = pinctrl_lookup_state(chip->pctrl, "power_gpio_low");
		if (IS_ERR(chip->power_state)) {
			ret = PTR_ERR(chip->power_state);
			printk("pinctrl_lookup_state, err:%d\n", ret);
			return ret;
		};

		pinctrl_select_state(chip->pctrl,chip->power_state);

	} else {
		chip->power_state = pinctrl_lookup_state(chip->pctrl, "power_gpio_high");
		if (IS_ERR(chip->power_state)) {
			ret = PTR_ERR(chip->power_state);
			printk("pinctrl_lookup_state, err:%d\n", ret);
			return ret;
		};

		pinctrl_select_state(chip->pctrl,chip->power_state);
	}

	return 0;
}

static int proximity_power_hardware_init(struct oppo_proximity_chip * chip)
{
	int ret = 0;

	ret = proximity_power_config(chip,GPIO_LOW);
	if (ret < 0){
		printk("proximity_power_hardware_init %d \n",ret);
		return -EINVAL;
	}

	return 0;
}

static ssize_t oppo_proximity_power_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		printk("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", gpio_get_value(g_the_chip->power_gpio));
}

static ssize_t oppo_proximity_power_store(struct device *pdev, struct device_attribute *attr,
			   const char *buff, size_t count)
{
	int enable = -1;

	if (!g_the_chip) {
		printk("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%d", &enable) == 1) {
		if (enable == 0)
			proximity_power_config(g_the_chip,GPIO_LOW);
		else if (enable == 1)
			proximity_power_config(g_the_chip,GPIO_HIGH);

		printk("[oppo_proximity_power_store] enable %d",enable);
	}

	return count;
}

static DEVICE_ATTR(enable,   S_IRUGO | S_IWUSR, oppo_proximity_power_show, oppo_proximity_power_store);

static struct attribute * __attributes[] = {
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group __attribute_group = {
	.attrs = __attributes
};

static int proximity_power_platform_probe(struct platform_device *pdev)
{
	struct oppo_proximity_chip *chip = NULL;
	int ret = 0;

	printk("call\n");

	chip = devm_kzalloc(&pdev->dev,sizeof(struct oppo_proximity_chip), GFP_KERNEL);
	if (!chip) {
		printk("kernel memory alocation was failed");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;

	proximity_power_parse_dts(chip);

	ret = proximity_power_hardware_init(chip);
	if (ret < 0){
		printk("proximity_power_hardware_init %d \n",ret);
		return -EINVAL;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &__attribute_group);
	if(ret) {
		printk("sysfs_create_group was failed(%d) \n", ret);
		return -EINVAL;
	}

	g_the_chip = chip;

	printk("success \n");
	return 0;
}

static int proximity_power_platform_remove(struct platform_device *pdev)
{
	if (g_the_chip) {
		gpio_free(g_the_chip->power_gpio);
		kfree(g_the_chip);
		g_the_chip = NULL;
	}
	return 0;
}

static const struct of_device_id of_drv_match[] = {
	{ .compatible = "proximity_power"},
	{},
};
MODULE_DEVICE_TABLE(of, of_motor_match);

static struct platform_driver _driver = {
	.probe		= proximity_power_platform_probe,
	.remove		= proximity_power_platform_remove,
	.driver		= {
		.name	= "proximity_power",
		.of_match_table = of_drv_match,
	},
};

static int __init proximity_power_init(void)
{
	printk("call\n");
	platform_driver_register(&_driver);
	return 0;
}

static void __exit proximity_power_exit(void)
{
	printk("call\n");
}

module_init(proximity_power_init);
module_exit(proximity_power_exit);
MODULE_DESCRIPTION("proximity power control driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mofei@oppo.com");

