/************************************************************************************
 ** File: - lm3643.c
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2018, OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **      lm3643 driver
 **
 ** Version: 1.0
 ** Date created: 18:03:11,12/05/2018
 ** Author: oujinrong@BSP.Fingerprint.Basic
 ** TAG: BSP.Fingerprint.Basic
 ** --------------------------- Revision History: --------------------------------
 **  <author>       <data>          <desc>
 ** oujinrong       2018/05/12      create file
 ** oujinrong       2018/06/04      flood led power will be controlled by mx6300
 ** oujinrong       2018/06/17      optimize the performance of powering on/off the flood led
 ************************************************************************************/

#define pr_fmt(fmt) "lm3643: " fmt

#include <linux/module.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define LM3643_DEV_NAME             "lm3643-device"
#define LM3643_CHRD_DRIVER_NAME     "lm3643-i2cdriver"
#define I2CDEV_MAJOR                (90) /* assigned */
#define N_I2C_MINORS                (128)  /* ... up to 256 */
#define LM3643_CLASS_NAME           "lm3643-i2c"
#define FLASH_NAME                  "lm3643"
#define LM3643_MAX_REGISTERS        (0x0F)

#define LM3643_FLASH_ON             (1)
#define LM3643_FLASH_OFF            (0)

#define TORCH_TEST                  (0)

struct lm3643_flash_driver {
    struct i2c_client   *client;
    struct device       *dev;
    dev_t               devt;
    struct list_head    device_entry;
    int                 hwen_gpio;
    struct regmap       *regmap;
};

static struct regmap_config lm3643_regmap = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = LM3643_MAX_REGISTERS,
};

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static DECLARE_BITMAP(minors, N_I2C_MINORS);

static struct lm3643_flash_driver *lm3643_flash = NULL;
static struct class *lm3643_flash_class;

volatile static int lm3643_enable = 0;

int lm3643_power_enable(int enable) {
    int ret = 0;
    if (NULL == lm3643_flash) {
        pr_err("flood led no device");
        return -ENODEV;
    }
    if (lm3643_enable != enable) {
        lm3643_enable = enable;
    }
    if (gpio_is_valid(lm3643_flash->hwen_gpio)) {
        gpio_direction_output(lm3643_flash->hwen_gpio, enable);
    }

    if (enable) {
        mdelay(5);
        #if TORCH_TEST
        /* test torch mode */
        regmap_write(lm3643_flash->regmap, 0x07, 0x0D);
        regmap_write(lm3643_flash->regmap, 0x08, 0x00);
        regmap_write(lm3643_flash->regmap, 0x05, 0xFF);
        regmap_write(lm3643_flash->regmap, 0x06, 0xFF);
        regmap_write(lm3643_flash->regmap, 0x01, 0x0A);
        pr_debug("%s torch mode\n", __func__);
        #else
        ret = regmap_write(lm3643_flash->regmap, 0x07, 0x0D);
        ret = regmap_write(lm3643_flash->regmap, 0x08, 0x00);
        ret = regmap_write(lm3643_flash->regmap, 0x03, 0x55);
        ret = regmap_write(lm3643_flash->regmap, 0x04, 0x55);
        ret = regmap_write(lm3643_flash->regmap, 0x01, 0x26);
        pr_debug("%s flash mode\n", __func__);
        #endif
        pr_err("%s flood turn on\n", __func__);
    } else {
        pr_err("%s flood turn off\n", __func__);
    }
    return ret;
}

static int lm3643_proc_open(struct inode *inode, struct file *file)
{
    int ret = 0;
    ret = lm3643_power_enable(LM3643_FLASH_ON);
    return ret;
}

static int lm3643_proc_close(struct inode *inode, struct file *file)
{
    int ret = 0;
    ret = lm3643_power_enable(LM3643_FLASH_OFF);
    return ret;
}

static const struct file_operations lm3643_flashs_fops = {
    .owner = THIS_MODULE,
    .open = lm3643_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = lm3643_proc_close,
};

static ssize_t lm3643_proc_write(struct file *filp, const char __user *buff,
    size_t len, loff_t *data)
{
    if (len > 8) {
        len = 8;
    }
    pr_err("%s do nothing", __func__);
    return len;
}

static ssize_t lm3643_proc_read(struct file *filp, char __user *buff,
                            size_t len, loff_t *data)
{
    char value[2] = {0};
    snprintf(value, sizeof(value), "%d", lm3643_enable);
    return simple_read_from_buffer(buff, len, data, value,1);
}

static const struct file_operations lm3643_fops = {
    .owner          = THIS_MODULE,
    .read           = lm3643_proc_read,
    .write          = lm3643_proc_write,
};


int lm3643_parse_dts(struct lm3643_flash_driver* lm3643_flash)
{
    int ret = 0;
    /*get lm3643 gpio resource*/
    lm3643_flash->hwen_gpio = of_get_named_gpio(lm3643_flash->client->dev.of_node, "lm3643,hwen-gpio", 0);
    if (gpio_is_valid(lm3643_flash->hwen_gpio)) {
        ret = gpio_request(lm3643_flash->hwen_gpio, "lm3643_hwen-gpio");
        if (ret) {
            pr_err("%s could not request lm3643 hwen gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s hwen_gpio not valid  gpio \n", __func__);
        return -EIO;
    }

    return ret;
}

static int msm_flash_lm3643_i2c_probe(struct i2c_client *client,
        const struct i2c_device_id *id)
{
    int ret = 0;
    int device_id = 0;
    unsigned long minor;
    struct proc_dir_entry *proc_entry;
    lm3643_flash = kzalloc(sizeof(*lm3643_flash), GFP_KERNEL);
    if (!lm3643_flash) {
        pr_err("%s failed to alloc memory\n", __func__);
        ret = -ENOMEM;
        goto exit_error;
    }
    lm3643_flash->client = client;
    lm3643_flash->dev = &client->dev;
    if (lm3643_parse_dts(lm3643_flash)) {
        pr_err("%s failed to parse dts. \n", __func__);
        ret =  -EINVAL;
        goto dev_clean;
    }
    if (gpio_is_valid(lm3643_flash->hwen_gpio)) {
        gpio_direction_output(lm3643_flash->hwen_gpio, 1);
        msleep(10);
    }

    lm3643_flash->regmap = devm_regmap_init_i2c(client, &lm3643_regmap);
    if (IS_ERR(lm3643_flash->regmap)) {
        ret = PTR_ERR(lm3643_flash->regmap);
        pr_err("fail to allocate register map: %d\n", ret);
        goto gpio_clean;
    }
    i2c_set_clientdata(client, lm3643_flash);
    regmap_read(lm3643_flash->regmap, 0x0c, &device_id);
    pr_err("%s device_id:0x%x addr:0x%x\n", __func__, device_id, client->addr);
    if (gpio_is_valid(lm3643_flash->hwen_gpio)) {
        gpio_direction_output(lm3643_flash->hwen_gpio, 0);
    }

    if ((device_id == 0x02) || (device_id == 0x04)) {
        mutex_lock(&device_list_lock);
        minor = find_first_zero_bit(minors, N_I2C_MINORS);
        pr_err("lm3643 minor:%lu\n", minor);
        if (minor < N_I2C_MINORS) {
            struct device *dev;
            lm3643_flash->devt = MKDEV(I2CDEV_MAJOR, minor);
            dev = device_create(lm3643_flash_class, &client->dev, lm3643_flash->devt, lm3643_flash, LM3643_DEV_NAME);
            ret = IS_ERR(dev) ? PTR_ERR(dev) : 0;
        } else {
            pr_err("%s no minor number available! minor:%ld\n", __func__, minor);
            ret = -ENODEV;
            mutex_unlock(&device_list_lock);
            goto gpio_clean;
        }
        if (ret == 0) {
            set_bit(minor, minors);
            list_add(&lm3643_flash->device_entry, &device_list);
        }
        proc_entry = proc_create_data("lm3643_enable", 0660, NULL, &lm3643_fops, NULL);
        if (proc_entry == NULL) {
            pr_err("[%s]: Error! Couldn't create lm3643_enable proc entry\n", __func__);
        }
        proc_entry = proc_create_data("flood_led_enable", 0660, NULL, &lm3643_fops, NULL);
        if (proc_entry == NULL) {
            pr_err("[%s]: Error! Couldn't create flood_led_enable proc entry\n", __func__);
        }
        mutex_unlock(&device_list_lock);
    } else {
        ret = -ENODEV;
        pr_err("[%s]: Read device_id failed!", __func__);
        goto gpio_clean;
    }

    pr_err("%s probe succeed.", __func__);
    return ret;
gpio_clean:
    gpio_free(lm3643_flash->hwen_gpio);
dev_clean:
    kfree(lm3643_flash);
    lm3643_flash = NULL;
exit_error:
    pr_err("%s probe failed, ret:%d\n", __func__, ret);
    return ret;
}

static int msm_flash_lm3643_i2c_remove(struct i2c_client *client)
{
    int ret = 0;
    if (lm3643_flash) {
        gpio_free(lm3643_flash->hwen_gpio);
        kfree(lm3643_flash);
        lm3643_flash = NULL;
    }
    return ret;
}

static const struct of_device_id lm3643_i2c_trigger_dt_match[] = {
    {.compatible = "lm3643"},
    {}
};

MODULE_DEVICE_TABLE(of, lm3643_i2c_trigger_dt_match);

static const struct i2c_device_id lm3643_i2c_id[] = {
    {FLASH_NAME , 0},
    { }
};

static struct i2c_driver lm3643_i2c_driver = {
    .id_table = lm3643_i2c_id,
    .probe  = msm_flash_lm3643_i2c_probe,
    .remove = msm_flash_lm3643_i2c_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = FLASH_NAME,
        .of_match_table = lm3643_i2c_trigger_dt_match,
    },
};

static int __init msm_flash_lm3643_init(void)
{
    int ret = 0;
    BUILD_BUG_ON(N_I2C_MINORS > 256);
    pr_debug("%s :%d entry  begin\n", __func__, ret);
    ret = register_chrdev(I2CDEV_MAJOR, LM3643_CHRD_DRIVER_NAME, &lm3643_flashs_fops);
    if (ret < 0) {
        pr_err("%s failed to register char device! ret:%d\n",__func__,ret);
        return ret;
    }
    lm3643_flash_class = class_create(THIS_MODULE, LM3643_CLASS_NAME);
    if (IS_ERR(lm3643_flash_class)) {
        unregister_chrdev(I2CDEV_MAJOR, lm3643_i2c_driver.driver.name);
        pr_err("%s failed to create class!\n",__func__);
        return PTR_ERR(lm3643_flash_class);
    }
    pr_debug("%s :%d creat class \n", __func__, ret);

    ret = i2c_register_driver(THIS_MODULE, &lm3643_i2c_driver);
    if (ret < 0) {
        class_destroy(lm3643_flash_class);
        unregister_chrdev(I2CDEV_MAJOR, lm3643_i2c_driver.driver.name);
        pr_err("%s failed to register i2c driver, ret:%d\n", __func__, ret);
    }
    return ret;
}

static void __exit msm_flash_lm3643_exit(void)
{
    i2c_del_driver(&lm3643_i2c_driver);
    class_destroy(lm3643_flash_class);
    unregister_chrdev(I2CDEV_MAJOR, lm3643_i2c_driver.driver.name);
    return;
}

module_init(msm_flash_lm3643_init);
module_exit(msm_flash_lm3643_exit);
MODULE_DESCRIPTION("lm3643 FLASH");
MODULE_LICENSE("GPL v2");
