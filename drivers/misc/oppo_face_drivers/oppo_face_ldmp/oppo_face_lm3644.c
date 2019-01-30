/************************************************************************************
 ** File: - lm3644.c
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2018, OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **      lm3644 driver
 **
 ** Version: 1.0
 ** Date created: 18:03:11,06/05/2018
 ** Author: oujinrong@BSP.Fingerprint.Basic
 ** TAG: BSP.Fingerprint.Basic
 ** --------------------------- Revision History: --------------------------------
 **  <author>       <data>          <desc>
 ** oujinrong       2018/05/06      add brightness setting
 ** oujinrong       2018/05/08      do not enable watchdog
 ** oujinrong       2018/05/09      set max power to 546
 ** oujinrong       2018/05/14      set wdt to 50ms
 ** oujinrong       2018/05/28      enable watchdog
 ** oujinrong       2018/05/30      add hrtimer lock
 ** oujinrong       2018/06/04      ldmp power will be controlled by mx6300, fix coverity 63128
 ** oujinrong       2018/06/10      add read proc for reading ldmp power in module
 ** oujinrong       2018/06/17      optimize the performance of powering on/off the ldmp
 ** oujinrong       2018/06/19      add watchdog test
 ** oujinrong       2018/07/05      add doe security proc
 ** oujinrong       2018/07/08      pull down wdt gpio when stop
 ************************************************************************************/

#define pr_fmt(fmt) "lm3644: " fmt

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
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include "../include/mx6300_tac/mx6300_tac.h"

#define LM3644_DEV_NAME             "lm3644-device"
#define LM3644_CHRD_DRIVER_NAME     "lm3644-i2cdriver"
#define I2CDEV_MAJOR                (91) /* assigned */
#define N_I2C_MINORS                (128)  /* ... up to 256 */
#define LM3644_CLASS_NAME           "lm3644-i2c"
#define FLASH_NAME                  "lm3644"
#define LM3644_MAX_REGISTERS        (0x0F)

#define LM3644_FLASH_ON 1
#define LM3644_FLASH_OFF 0

#define TORCH_TEST 0

#define ENABLE_WATCHDOG 1

static struct hrtimer hrt;
static unsigned int wdt_interval = 50000;
static ktime_t ktime;

#define DEFAULT_BRIGHTNESS      (0x55)
#define POWER_THRESHOLD         (546)
#define LDMP_MIN_POWER          (450)
#define LDMP_MAX_POWER          (840)

static uint8_t ldmp_max_brightness = 0x0;
static uint16_t ldmp_max_power = 0;
static uint8_t current_brightness = DEFAULT_BRIGHTNESS;
static struct mutex wdt_lock;

static int doe_check_ok = 1;

extern int mx6300_tac_get_ldmp_power_data_from_module(uint32_t *eeprom_data);

struct lm3644_flash_driver {
    struct i2c_client                *client;
    struct device                    *dev;
    dev_t                            devt;
    struct list_head                 device_entry;
    int                              hwen_gpio;
    int                              ldo_en_gpio;
    int                              watchdog_gpio;
    int                              watchdog_en_gpio;
    struct regmap                    *regmap;
};

static struct regmap_config lm3644_regmap = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = LM3644_MAX_REGISTERS,
};

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static DECLARE_BITMAP(minors, N_I2C_MINORS);

static struct lm3644_flash_driver *lm3644_flash = NULL;
static struct class *lm3644_flash_class;

volatile static int lm3644_enable = 0;
static int watchdog_trigger_flag = 0;

static int lm3644_get_brightness_from_electricity(uint32_t electricity, uint8_t *brightness) {
    /* from uA to brightness code */
    if (NULL == brightness) {
        pr_err("%s, no brightness buffer.\n", __func__);
        return -EINVAL;
    }
    if (electricity < 11) {
        pr_err("%s, electricity value error\n", __func__);
        return -EINVAL;
    }
    *brightness = (uint8_t) ((electricity - 11) / 11725);
    return 0;
}

static int lm3644_get_electricity_from_brightness(uint8_t brightness, uint32_t *electricity) {
    /* from brightness code to uA */
    if (NULL == electricity) {
        pr_err("%s, no electricity buffer.\n", __func__);
        return -EINVAL;
    }
    *electricity = ((uint32_t)brightness * 11725) + 11;
    return 0;
}

static uint32_t lm3644_get_max_brightness(void) {
    int ret = 0;

    uint32_t eeprom_data = 0;
    uint16_t power = 0;
    uint8_t valid_data = 0;
    uint8_t checksum_from_module = 0;
    uint8_t checksum_from_power = 0;

    uint32_t max_electricity = 0;

    /* if has read the max_brightness from mx6300 */
    if (0 != ldmp_max_brightness) {
        return ldmp_max_brightness;
    }

    ret = mx6300_tac_get_ldmp_power_data_from_module(&eeprom_data);
    if (ret || (0 == eeprom_data)) {
        pr_err("%s, read max power failed, set to default brightness\n", __func__);
        return DEFAULT_BRIGHTNESS;
    }

    valid_data = (uint8_t) (eeprom_data >> 16) & 0xFF;
    power = (uint16_t) eeprom_data & 0xFFFF;

    /* checksum1 is read from eeprom */
    checksum_from_module = (uint8_t) (eeprom_data >> 24) & 0xFF;
    /* checksum2 is calculated with power data */
    checksum_from_power = (((power >> 8) & 0xFF) + (power & 0xFF)) % 255;

    pr_err("%s, read light power data 0x%x\n", __func__, eeprom_data);
    /* verify valid code */
    if (valid_data != 0x01) {
        pr_err("%s, power data not valid, valid_data = 0x%x\n", __func__, valid_data);
        return DEFAULT_BRIGHTNESS;
    }

    /* verify checksum */
    if (checksum_from_module != checksum_from_power) {
        pr_err("%s, verify checksum failed. checksum1 = 0x%x, checksum2 = 0x%x\n",
                __func__, checksum_from_module, checksum_from_power);
        return DEFAULT_BRIGHTNESS;
    }

    /* check that if the data is within limits */
    if ((power < LDMP_MIN_POWER) || (power > LDMP_MAX_POWER)) {
        pr_err("%s, power data not valid, power is %dmW\n",
                __func__, power);
        return DEFAULT_BRIGHTNESS;
    }

    /* caclulate the max electricity (uA) */
    ldmp_max_power = power;
    pr_err("%s, get max power %dmW\n", __func__, ldmp_max_power);
    if (ldmp_max_power < POWER_THRESHOLD) {
        ldmp_max_brightness = DEFAULT_BRIGHTNESS;
        return DEFAULT_BRIGHTNESS;
    }
    max_electricity = (POWER_THRESHOLD * 1000000) / ldmp_max_power;
    ret = lm3644_get_brightness_from_electricity(max_electricity, &ldmp_max_brightness);
    if (ret || (0 == ldmp_max_brightness)) {
        pr_err("%s, get max brightness failed, set to default brightness\n", __func__);
        return DEFAULT_BRIGHTNESS;
    }

    return ldmp_max_brightness;
}

static int lm3644_set_brightness(uint32_t brightness) {
    int ret = 0;
    uint8_t max_brightness = 0;
    /* get max brightness */
    max_brightness = lm3644_get_max_brightness();

    brightness = (brightness < max_brightness)? brightness: max_brightness;

    ret = regmap_write(lm3644_flash->regmap, 0x03, brightness);
    if (ret) {
        pr_err("%s, set regmap 0x03 failed. ret = %d\n", __func__, ret);
        return -EIO;
    }
    ret = regmap_write(lm3644_flash->regmap, 0x04, brightness);
    if (ret) {
        pr_err("%s, set regmap 0x04 failed. ret = %d\n", __func__, ret);
        return -EIO;
    }

    current_brightness = brightness;

    pr_err("%s, current brightness set to 0x%x, max_brightness 0x%x\n",
                __func__, current_brightness, max_brightness);

    return ret;
}

static int ldmp_wtd_trigger(void) {
    int kick_config = 0;
    kick_config = gpio_get_value(lm3644_flash->watchdog_gpio);
    gpio_direction_output(lm3644_flash->watchdog_gpio, (kick_config == 1) ? 0: 1);
    return 0;
}

static enum hrtimer_restart hrtimer_callback(struct hrtimer *hrt_ptr) {
    if (watchdog_trigger_flag) {
        /* restart when trigger flag is not 0 */
        ldmp_wtd_trigger();
        hrtimer_start(&hrt, ktime, HRTIMER_MODE_REL);
    } else {
        /* stop hrtimer when flag is 0, pull watchdog gpio down */
        gpio_direction_output(lm3644_flash->watchdog_gpio, 0);
        pr_debug("%s, watchdog stop trigger.\n", __func__);
    }
    return HRTIMER_NORESTART;
}

static int wdt_hrtimer_init(void) {
    hrtimer_init(&hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    hrt.function = hrtimer_callback;
    ktime = ktime_set(0, (wdt_interval % 1000000) * 1000);
    pr_err("wdt hrtimer init end");
    return 0;
}

static int wdt_hrtimer_start(void) {
    ldmp_wtd_trigger();
    hrtimer_start(&hrt, ktime, HRTIMER_MODE_REL);
    return 0;
}

static int wdt_hrtimer_stop(void) {
    hrtimer_cancel(&hrt);
    return 0;
}

int lm3644_power_enable(int enable) {
    int ret = 0;
    int device_id = 0;
    pr_err("lm3644_proc_open enter enable:%d \n", enable);
    if (NULL == lm3644_flash) {
        pr_err("ldmp no device");
        return -ENODEV;
    }
    if ((!doe_check_ok) && enable) {
        pr_err("doe check failed. do not power on lm3644");
        return 0;
    }
    if (lm3644_enable != enable) {
        lm3644_enable = enable;
    }

    if (enable) {
        /* add lock to ensure the previous hrtimer stop */
        mutex_lock(&wdt_lock);
        if (!watchdog_trigger_flag) {
            /* set trigger flag */
            watchdog_trigger_flag = 1;
            /* trigger watchdog */
            wdt_hrtimer_start();
        }
        mutex_unlock(&wdt_lock);

#if ENABLE_WATCHDOG
        /* ENABLE watchdog */
        pr_err("enable watchdog ic\n");
        if (gpio_is_valid(lm3644_flash->watchdog_en_gpio)) {
            gpio_direction_output(lm3644_flash->watchdog_en_gpio, 1);
        }
#endif

        /* power on */
        if (gpio_is_valid(lm3644_flash->ldo_en_gpio)) {
            gpio_direction_output(lm3644_flash->ldo_en_gpio, 1);
            mdelay(5);
        }
        if (gpio_is_valid(lm3644_flash->hwen_gpio)) {
            gpio_direction_output(lm3644_flash->hwen_gpio, 1);
        }

        mdelay(5);
        regmap_read(lm3644_flash->regmap, 0x0c, &device_id);
        pr_debug("%s get device_id:%d\n", __func__, device_id);
        #if TORCH_TEST
        regmap_write(lm3644_flash->regmap, 0x07, 0x0D);
        regmap_write(lm3644_flash->regmap, 0x08, 0x00);
        regmap_write(lm3644_flash->regmap, 0x05, 0xFF);
        regmap_write(lm3644_flash->regmap, 0x06, 0xFF);
        regmap_write(lm3644_flash->regmap, 0x01, 0x0A);
        pr_debug("%s torch mode\n",__func__);
        #else
        ret = regmap_write(lm3644_flash->regmap, 0x07, 0x0D);
        ret = regmap_write(lm3644_flash->regmap, 0x08, 0x00);
        lm3644_set_brightness(DEFAULT_BRIGHTNESS);
        ret = regmap_write(lm3644_flash->regmap, 0x01, 0x26);
        pr_debug("%s flash mode\n", __func__);
        #endif
        pr_err("%s laser turn on\n", __func__);
    } else {
        /* power down */
        if (gpio_is_valid(lm3644_flash->ldo_en_gpio)) {
            gpio_direction_output(lm3644_flash->ldo_en_gpio, 0);
        }

        if (gpio_is_valid(lm3644_flash->hwen_gpio)) {
            gpio_direction_output(lm3644_flash->hwen_gpio, 0);
        }

        /* disable watchdog */
        if (gpio_is_valid(lm3644_flash->watchdog_en_gpio)) {
            gpio_direction_output(lm3644_flash->watchdog_en_gpio, 0);
        }

        /* lock until the previous hrtimer stop */
        mutex_lock(&wdt_lock);
        /* stop triggering */
        wdt_hrtimer_stop();
        watchdog_trigger_flag = 0;
        gpio_direction_output(lm3644_flash->watchdog_gpio, 0);
        mutex_unlock(&wdt_lock);
        pr_err("%s laser turn off\n", __func__);
    }
    return ret;
}

static int lm3644_wdt_test(void) {
    int ret = 0;
    int device_id = 0;
    /* 1. wake up mx6300 */
    ret = mx6300_tac_wakeup();
    if (ret) {
        pr_err("%s wake up mx6300 failed", __func__);
        return ret;
    }
    /* 2. power on lm3644 */
    ret = lm3644_power_enable(1);
    if (ret) {
        pr_err("%s power on lm3644 failed", __func__);
        goto sleep_mx6300;
    }
    /* 3. read reg, expect read succeed */
    ret = regmap_read(lm3644_flash->regmap, 0x0c, &device_id);
    if (ret) {
        pr_err("%s reg read after power on lm3644 failed", __func__);
        goto poweroff_lm3644;
    }
    /* 4. stop trigger watchdog */
    ret = wdt_hrtimer_stop();
    if (ret) {
        pr_err("%s stop wdt hrtimer failed", __func__);
        goto poweroff_lm3644;
    }
    /* 5. wait at least 150ms for timeout */
    msleep(200);
    /* 6. read reg, expect read failed */
    ret = regmap_read(lm3644_flash->regmap, 0x0c, &device_id);
    if (!ret) {
        pr_err("%s reg read succeed after stop triggerring wdt. test failed!", __func__);
        ret = -EIO;
        goto poweroff_lm3644;
    }
    pr_err("%s reg read failed after stop triggerring wdt. test pass!\n", __func__);
    ret = 0;

poweroff_lm3644:
    /* 7. power off lm3644 */
    lm3644_power_enable(0);
sleep_mx6300:
    /* 8. sleep mx6300 */
    mx6300_tac_sleep();
    return ret;
}

static int lm3644_proc_open(struct inode *inode, struct file *file) {
    int ret = 0;
    ret= lm3644_power_enable(LM3644_FLASH_ON);
    return ret;
}

static int lm3644_proc_close(struct inode *inode, struct file *file) {
    int ret = 0;
    ret = lm3644_power_enable(LM3644_FLASH_OFF);
    return ret;
}

static const struct file_operations lm3644_flashs_fops = {
    .owner = THIS_MODULE,
    .open = lm3644_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = lm3644_proc_close,
};

static ssize_t lm3644_proc_write(struct file *filp, const char __user *buff,
    size_t len, loff_t *data) {
    if (len > 8) {
        len = 8;
    }
    pr_err("%s do nothing", __func__);
    return len;
}

static ssize_t lm3644_proc_read(struct file *filp, char __user *buff,
    size_t len, loff_t *data) {
    char value[2] = {0};
    snprintf(value, sizeof(value), "%d", lm3644_enable);
    return simple_read_from_buffer(buff, len, data, value, 1);
}

static const struct file_operations lm3644_fops = {
    .owner      = THIS_MODULE,
    .read       = lm3644_proc_read,
    .write      = lm3644_proc_write,
};

static ssize_t ldmp_doe_security_proc_write(struct file *filp, const char __user *buff,
    size_t len, loff_t *data) {
    char buf[8] = {0};
    if (len > 8) {
        len = 8;
    }
    if (copy_from_user(buf, (void __user *)buff, sizeof(buf))) {
        pr_err("proc write error.\n");
        return -EFAULT;
    }

    doe_check_ok = simple_strtoul(buf, NULL, 10);
    if (!doe_check_ok) {
        lm3644_power_enable(0);
        pr_err("%s doe check status set failed. do not allow to power on ldmp", __func__);
    }
    return len;
}

static const struct file_operations ldmp_doe_security_fops = {
    .owner      = THIS_MODULE,
    .write      = ldmp_doe_security_proc_write,
};


static ssize_t lm3644_lightpower_proc_read(struct file *filp, char __user *buff,
    size_t len, loff_t *data) {
    char value[8] = {0};
    lm3644_get_max_brightness();
    snprintf(value, sizeof(value), "%dmW", ldmp_max_power);
    return simple_read_from_buffer(buff, len, data, value, 8);
}

static const struct file_operations lm3644_lightpower_fops = {
    .owner      = THIS_MODULE,
    .read       = lm3644_lightpower_proc_read,
};

static ssize_t lm3644_electricity_proc_write(struct file *filp, const char __user *buff,
    size_t len, loff_t *data) {
    int ret = 0;
    char buf[8] = {0};
    /* electricity current (uA) */
    uint32_t electricity = 0;
    uint8_t brightness = 0;
    if (len > 8) {
        len = 8;
    }
    if (copy_from_user(buf, (void __user *)buff, sizeof(buf))) {
        pr_err("proc write error.\n");
        return -EFAULT;
    }

    /* get electricity (uA) from buffer */
    electricity = simple_strtoul(buf, NULL, 10);

    /* get brightness to set */
    ret = lm3644_get_brightness_from_electricity(electricity, &brightness);
    if (ret) {
        pr_err("%s, get brightness error.\n", __func__);
        goto err;
    }

    /* set brightness of lm3644 */
    if (lm3644_set_brightness(brightness)) {
        pr_err("%s, set electricity to %d uA failed.\n", __func__, electricity);
        goto err;
    }

err:
    return len;
}

static ssize_t lm3644_electricity_proc_read(struct file *filp, char __user *buff,
    size_t len, loff_t *data) {
    int ret = 0;
    char value[16] = {0};
    uint32_t electricity = 0;

    ret = lm3644_get_electricity_from_brightness(current_brightness, &electricity);
    if (ret) {
        pr_err("%s, get electricity error\n", __func__);
    }
    snprintf(value, sizeof(value), "%d uA", electricity);
    return simple_read_from_buffer(buff, len, data, value, 16);
}

static const struct file_operations lm3644_electricity_fops = {
    .owner      = THIS_MODULE,
    .read       = lm3644_electricity_proc_read,
    .write      = lm3644_electricity_proc_write,
};

static ssize_t lm3644_wdt_test_proc_read(struct file *filp, char __user *buff,
    size_t len, loff_t *data) {
    int ret = 0;
    char value[8] = {0};
    ret = lm3644_wdt_test();
    if (!ret) {
        snprintf(value, sizeof(value), "PASS");
    } else {
        snprintf(value, sizeof(value), "FAILED");
    }
    return simple_read_from_buffer(buff, len, data, value, strlen(value));
}

static const struct file_operations lm3644_wdt_test_fops = {
    .owner      = THIS_MODULE,
    .read       = lm3644_wdt_test_proc_read,
};

int lm3644_parse_dts(struct lm3644_flash_driver* lm3644_flash) {
    int ret = 0;
    /*get lm3644 gpio resource*/
    lm3644_flash->hwen_gpio = of_get_named_gpio(lm3644_flash->client->dev.of_node, "lm3644,hwen-gpio", 0);
    if (gpio_is_valid( lm3644_flash->hwen_gpio)) {
        ret = gpio_request(lm3644_flash->hwen_gpio, "lm3644_hwen-gpio");
        if (ret) {
            pr_err("%s could not request lm3644 hwen gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid gpio\n", __func__);
        return -EIO;
    }

    lm3644_flash->ldo_en_gpio = of_get_named_gpio(lm3644_flash->client->dev.of_node, "lm3644,ldo-en-gpio", 0);
    if (gpio_is_valid( lm3644_flash->ldo_en_gpio)) {
        ret = gpio_request(lm3644_flash->ldo_en_gpio, "lm3643-ldo-en-gpio");
        if (ret) {
            pr_err("%s could not request lm3644 ldo en gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid  gpio\n", __func__);
        return -EIO;
    }

    lm3644_flash->watchdog_gpio = of_get_named_gpio(lm3644_flash->client->dev.of_node, "lm3644,watchdog-gpio",0);
    if (gpio_is_valid(lm3644_flash->watchdog_gpio)) {
        ret = gpio_request(lm3644_flash->watchdog_gpio, "lm3644-watchdog-gpio");
        if (ret) {
            pr_err("%s could not request lm3644 ldo en gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid  gpio\n", __func__);
        return -EIO;
    }

    lm3644_flash->watchdog_en_gpio = of_get_named_gpio(lm3644_flash->client->dev.of_node, "lm3644,watchdog-en-gpio",0);
    if (gpio_is_valid(lm3644_flash->watchdog_en_gpio)) {
        ret = gpio_request(lm3644_flash->watchdog_en_gpio, "lm3644-watchdog-en-gpio");
        if (ret) {
            pr_err("%s could not request lm3644 watchdog en gpio\n", __func__);
            return ret;
        }
    } else {
        pr_err("%s not valid  gpio\n", __func__);
        return -EIO;
    }
    return ret;
}

static int msm_flash_lm3644_i2c_probe(struct i2c_client *client,
        const struct i2c_device_id *id) {
    int ret = 0 ;
    int device_id = 0;
    unsigned long minor;
    struct proc_dir_entry *proc_entry = NULL;

    lm3644_flash = kzalloc(sizeof(*lm3644_flash), GFP_KERNEL);
    if (!lm3644_flash) {
        pr_err("%s failed to alloc memory\n",__func__);
        ret = -ENOMEM;
        goto exit_error;
    }
    lm3644_flash->client = client;
    lm3644_flash->dev = &client->dev;
    if (lm3644_parse_dts(lm3644_flash)) {
        pr_err("%s failed to parse dts. \n",__func__);
        ret =  -EINVAL;
        goto exit_error;
    }
    if (gpio_is_valid(lm3644_flash->ldo_en_gpio)) {
        gpio_direction_output(lm3644_flash->ldo_en_gpio, 1);
        mdelay(10);
    }
    if (gpio_is_valid(lm3644_flash->hwen_gpio)) {
        gpio_direction_output(lm3644_flash->hwen_gpio, 1);
    }
    mdelay(20);
    lm3644_flash->regmap = devm_regmap_init_i2c(client, &lm3644_regmap);
    if (IS_ERR(lm3644_flash->regmap)) {
        ret = PTR_ERR(lm3644_flash->regmap);
        pr_err("fail to allocate register map: %d\n", ret);
        return ret;
    }
    i2c_set_clientdata(client, lm3644_flash);
    regmap_read(lm3644_flash->regmap, 0x0c, &device_id);
    pr_err("%s device_id:0x%x addr:0x%x\n", __func__, device_id, client->addr);
    if (gpio_is_valid(lm3644_flash->hwen_gpio)) {
        gpio_direction_output(lm3644_flash->hwen_gpio, 0);
    }
    if (gpio_is_valid(lm3644_flash->ldo_en_gpio)) {
        gpio_direction_output(lm3644_flash->ldo_en_gpio, 0);
    }
    proc_entry = proc_create_data( "lm3644_enable", 0660, NULL,&lm3644_fops, NULL);
    if (proc_entry == NULL) {
        pr_err("[%s]: Error! Couldn't create lm3644_enable proc entry\n", __func__);
    }
    proc_entry = proc_create_data( "ldmp_doe_security", 0660, NULL,&ldmp_doe_security_fops, NULL);
    if (proc_entry == NULL) {
        pr_err("[%s]: Error! Couldn't create ldmp_doe_security proc entry\n", __func__);
    }
    proc_entry = proc_create_data("lm3644_power", 0660, NULL, &lm3644_electricity_fops, NULL);
    if (proc_entry == NULL) {
        pr_err("[%s]: Error! Couldn't create lm3644_power proc entry\n", __func__);
    }
    proc_entry = proc_create_data("lm3644_lightpower", 0660, NULL, &lm3644_lightpower_fops, NULL);
    if (proc_entry == NULL) {
        pr_err("[%s]: Error! Couldn't create lm3644_lightpower proc entry\n", __func__);
    }
    proc_entry = proc_create_data("ldmp_wdt_test", 0660, NULL, &lm3644_wdt_test_fops, NULL);
    if (proc_entry == NULL) {
        pr_err("[%s]: Error! Couldn't create ldmp_wdt_test proc entry\n", __func__);
    }
    if ((device_id == 0x02) || (device_id == 0x04)) {
        mutex_lock(&device_list_lock);
        minor = find_first_zero_bit(minors, N_I2C_MINORS);
        pr_err("lm3644 minor:%lu\n",minor);
        if (minor < N_I2C_MINORS) {
            struct device *dev;
            lm3644_flash->devt = MKDEV(I2CDEV_MAJOR, minor);
            dev = device_create(lm3644_flash_class, &client->dev, lm3644_flash->devt,lm3644_flash, LM3644_DEV_NAME);
            ret = IS_ERR(dev) ? PTR_ERR(dev) : 0;
        }
        else {
            pr_err("%s no minor number available! minor:%ld\n",__func__,minor);
            ret = -ENODEV;
            mutex_unlock(&device_list_lock);
            goto res_clean;
        }
        if (ret == 0) {
            set_bit(minor, minors);
            list_add(&lm3644_flash->device_entry, &device_list);
            pr_debug("%s list_add\n",__func__);
        }
        mutex_unlock(&device_list_lock);
    } else {
        ret = -ENODEV;
        pr_err("[%s]: Read device_id failed!", __func__);
        goto res_clean;
    }

    /* pull down watchdog trigger gpio */
    if (gpio_is_valid(lm3644_flash->watchdog_gpio)) {
        ret = gpio_direction_output(lm3644_flash->watchdog_gpio, 0);
        if (ret) {
            pr_err("%s, set watchdog gpio failed.\n", __func__);
            goto res_clean;
        }
    }
    else {
        pr_err("%s, watchdog gpio invalid.\n", __func__);
        goto res_clean;
    }

    /* pull down watchdog enable gpio */
    if (gpio_is_valid(lm3644_flash->watchdog_en_gpio)) {
        ret = gpio_direction_output(lm3644_flash->watchdog_en_gpio, 0);
        if (ret) {
            pr_err("%s, set watchdog en gpio failed.\n", __func__);
            goto res_clean;
        }
    }
    else {
        pr_err("%s, watchdog en gpio invalid.\n", __func__);
        goto res_clean;
    }

    pr_err("%s probe succeed.", __func__);
    return ret;
res_clean:
    kfree(lm3644_flash);
    lm3644_flash = NULL;
exit_error:
    pr_err("%s probe failed, ret:%d\n", __func__, ret);
    return ret;
}

static int msm_flash_lm3644_i2c_remove(struct i2c_client *client) {
    int ret = 0;
    return ret;
}

static const struct of_device_id lm3644_i2c_trigger_dt_match[] = {
    {.compatible = "lm3644"},
    {}
};

MODULE_DEVICE_TABLE(of, lm3644_i2c_trigger_dt_match);

static const struct i2c_device_id lm3644_i2c_id[] = {
    { FLASH_NAME, 0 },
    { }
};

static struct i2c_driver lm3644_i2c_driver = {
    .id_table = lm3644_i2c_id,
    .probe  = msm_flash_lm3644_i2c_probe,
    .remove = msm_flash_lm3644_i2c_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = FLASH_NAME,
        .of_match_table = lm3644_i2c_trigger_dt_match,
    },
};

static int __init msm_flash_lm3644_init(void) {
    int ret = 0;
    BUILD_BUG_ON(N_I2C_MINORS > 256);
    ret = register_chrdev(I2CDEV_MAJOR, LM3644_CHRD_DRIVER_NAME, &lm3644_flashs_fops);
    if (ret < 0) {
        pr_err("%s failed to register char device! ret:%d\n",__func__,ret);
        return ret;
    }
    lm3644_flash_class = class_create(THIS_MODULE, LM3644_CLASS_NAME);
    if (IS_ERR(lm3644_flash_class)) {
        unregister_chrdev(I2CDEV_MAJOR, lm3644_i2c_driver.driver.name);
        pr_err("%s failed to create class!\n", __func__);
        return PTR_ERR(lm3644_flash_class);
    }
    ret = i2c_register_driver(THIS_MODULE, &lm3644_i2c_driver);
    if (ret < 0) {
        class_destroy(lm3644_flash_class);
        unregister_chrdev(I2CDEV_MAJOR, lm3644_i2c_driver.driver.name);
        pr_err("%s failed to register i2c driver, ret:%d\n", __func__, ret);
    }
    wdt_hrtimer_init();
    mutex_init(&wdt_lock);
    return ret;
}

static void __exit msm_flash_lm3644_exit(void) {
    mutex_destroy(&wdt_lock);
    i2c_del_driver(&lm3644_i2c_driver);
    class_destroy(lm3644_flash_class);
    unregister_chrdev(I2CDEV_MAJOR, lm3644_i2c_driver.driver.name);
    return;
}

module_init(msm_flash_lm3644_init);
module_exit(msm_flash_lm3644_exit);
MODULE_DESCRIPTION("lm3644 FLASH");
MODULE_LICENSE("GPL v2");
