#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>

#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/poll.h>
#include <linux/delay.h>
//#include <linux/wakelock.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <linux/power_supply.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>
#include <linux/ktime.h>
#include <linux/regulator/consumer.h>
#include "cam_debug_util.h"
#include "boot_mode.h"
#include <linux/proc_fs.h>

#include "mx6300-spi.h"

#ifndef OPPO_MX6300_TEE_SUPPORT
/* oujinrong@BSP.Fingerprint.Basic 2018/04/17, add to disable REE SPI of mx6300 */
#include "tiger_bin.h"
#define MX6300_DEV_NAME             "mx6000-device"
#define MX6300_CHRD_DRIVER_NAME     "mx6300-spidriver"
#define MX6300_CLASS_NAME           "mx6300-spi"
#define SPIDEV_MAJOR                155 /* assigned */
#define N_SPI_MINORS                33  /* ... up to 256 */
#define SR_WIP 1 /* Write in progress */
#define SR_WEL 2 /* Write enable latch */
static struct mx6300_dev *g_dev = NULL;
static DECLARE_BITMAP(minors, N_SPI_MINORS);

#define MX6300_DEFAULT_DEBUG   (0x1<<0)
#define MX6300_INFO_DEBUG       (0x1<<1)
#define mx6300_debug(level, fmt, args...) do{ \
    if(level > 1) {\
    pr_err("mx6300 " fmt, ##args); \
    } \
}while(0)

#define FUNC_ENTRY()  mx6300_debug(MX6300_INFO_DEBUG, "%s, E\n", __func__)
#define FUNC_EXIT()   mx6300_debug(MX6300_INFO_DEBUG,"%s, X\n", __func__)


static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static uint8_t mx6300_output_mode = 0;
static struct class *mx6300_spi_class;
typedef void (*mx6300_mode_func)(void);
mx6300_mode_func mode_call[4] = {NULL};
uint32_t fval=0x000151B0;
typedef enum res {
    RES_400_640 = 0,
    RES_640_400,
    RES_800_1280,
    RES_1280_800,
} res_t;

void mx6x_reg_read(uint32_t reg, uint32_t *val){
    unsigned char T_val[4];
    mx6300_spi_read(g_dev,reg,4,T_val);
    *val = T_val[0]|T_val[1]<<8|T_val[2]<<16|T_val[3]<<24;
    CAM_DBG(CAM_SENSOR, "mx6x_reg_read reg:0x%x val:0x%x\n", reg, *val);
    return;
}
void mx6x_reg_write(uint32_t reg, uint32_t val){
     unsigned char T_val[4];
     T_val[0] = ((val)&0xFF);
     T_val[1] = ((val>>8)&0xFF);
     T_val[2] = ((val>>16)&0xFF);
     T_val[3] = ((val>>24)&0xFF);
     mx6300_spi_write(g_dev,reg,4,T_val);
     CAM_DBG(CAM_SENSOR, "mx6x_reg_write reg:0x%x val:0x%x\n", reg, val);
    return;
}

void mx6x_laser_onoff(uint8_t on)
{
    uint32_t val=0, addr;
    uint8_t  offset=0xD8;

    addr=fval+offset;
    mx6x_reg_read(addr,&val);
    if(on)
    {
        val=(val&~(0xff))|0x01;
        mx6x_reg_write(addr,val);
    }
    else
    {
        val=(val&~(0xff))|0x00;
        mx6x_reg_write(addr,val);
    }
}

void mx6x_flood_led_onoff(uint8_t on)
{
    uint32_t val=0, addr;
    uint8_t  offset=0xD9;

    addr=fval+offset;
    mx6x_reg_read(addr,&val);

    if(on)
    {
        val=(val&~(0xff00))|0x0100;
        mx6x_reg_write(addr, val);
    }
    else
    {
        val=(val&~(0xff00))|0x0000;
        mx6x_reg_write(addr,val);
    }
}


void mx6x_ir_stream_1280X800(void)
{
    uint32_t reg=0 ;
    unsigned char val[4];
    uint8_t  offset=0x18;

    reg = fval+offset;
    mx6300_spi_read(g_dev,reg,4,val);
    val[0] = 0x02;
    mx6300_spi_write(g_dev,reg,4,val);
}

void mx6x_stop_stream(void)
{
    uint32_t reg = 0;
    uint32_t val = 0;
    uint8_t  offset=0x18;

    reg = fval+offset;
    mx6x_reg_read(reg,&val);
    val=(val&~(0xff))|0x00;
    mx6x_reg_write(reg,val);
    mx6x_laser_onoff(0);
    mx6x_flood_led_onoff(0);
}

void mx6x_ir_stream(void)
{
    uint32_t reg = 0;
    uint32_t val = 0;
    uint8_t  offset=0x18;

    reg = fval+offset;
    mx6x_reg_read(reg,&val);
    val=(val&~(0xff))|0x02;
    mx6x_reg_write(reg,val);
    mx6x_laser_onoff(1);
    mx6x_flood_led_onoff(1);
}

void mx6x_depth_stream(void)
{
    uint32_t reg = 0;
    uint32_t val = 0;
    uint8_t  offset=0x18, offset1=0x38;

    reg = fval+offset1;
    mx6x_reg_read(reg,&val);
    val=(val&~(0xff))|0x00;
    mx6x_reg_write(reg,val);

    reg = fval+offset;
    mx6x_reg_read(reg,&val);
    val=(val&~(0xff))|0x01;
    mx6x_reg_write(reg,val);
    mx6x_laser_onoff(1);
    mx6x_flood_led_onoff(0);
}

void mx6x_depth_load_param(void)
{
    uint32_t reg=0 ,val=0;
    uint8_t  offset=0x2C;

    reg = fval+offset;
    mx6x_reg_read(reg,&val);
    val=(val&~(0x100))|0x100;
    mx6x_reg_write(reg,val);

}

void mx6x_depth_res_set(res_t res)
{
    uint32_t reg = 0;
    uint32_t val = 0;
    uint32_t value=0;

    switch(res)
    {
        case RES_800_1280:
            {
                CAM_DBG(CAM_SENSOR, "mx6x_depth_res_set 800_1280\n");

                reg = fval+0x30;
                mx6x_reg_read(reg,&val);
                value=0x12000000|(val&0xFFFF);
                mx6x_reg_write(reg,value);

                reg = fval+0x34;
                mx6x_reg_read(reg,&val);
                value=(value&0xFFFF)|(val&0xFFFF0000);
                mx6x_reg_write(reg,value);

            }
            break;

        case RES_400_640:
            {
                CAM_DBG(CAM_SENSOR, "mx6x_depth_res_set 400_640\n");

                reg = fval+0x30;
                mx6x_reg_read(reg,&val);
                value=0x13000000|(val&0xFFFF);
                mx6x_reg_write(reg,value);

                reg = fval+0x34;
                mx6x_reg_read(reg,&val);
                value=(value&0xFFFF)|(val&0xFFFF0000);
                mx6x_reg_write(reg,value);
            }
            break;
        default:
            CAM_DBG(CAM_SENSOR, "mx6x_depth_res_set default\n");
            break;
    }
    mx6x_depth_load_param();
}

void mx6x_ir_load_param(void)
{
    uint32_t reg = 0;
    uint32_t val = 0;
    uint8_t  offset=0x54;

    reg = fval+offset;
    mx6x_reg_read(reg,&val);
    val=(val&~(0x100))|0x100;
    mx6x_reg_write(reg,val);

}

void mx6x_ir_res_set(res_t res)
{
    uint32_t reg = 0;
    uint32_t val = 0;
    uint32_t value=0;

    switch(res)
    {
        case RES_1280_800:
            {
                CAM_DBG(CAM_SENSOR, "mx6x_ir_res_set RES_1280_800\n");

                reg = fval+0x58;
                mx6x_reg_read(reg,&val);
                value=0x10000000|(val&0xFFFF);
                mx6x_reg_write(reg,value);

                reg = fval+0x5C;
                mx6x_reg_read(reg,&val);
                value=(value&0xFFFF)|(val&0xFFFF0000);
                mx6x_reg_write(reg,value);
            }
            break;

        case RES_640_400:
            {
                CAM_DBG(CAM_SENSOR, "mx6x_ir_res_set RES_640_400\n");

                reg = fval+0x58;
                mx6x_reg_read(reg,&val);
                value=0x11000000|(val&0xFFFF);
                mx6x_reg_write(reg,value);

                reg = fval+0x5C;
                mx6x_reg_read(reg,&val);
                value=(value&0xFFFF)|(val&0xFFFF0000);
                mx6x_reg_write(reg,value);
            }
            break;
        default:
            CAM_DBG(CAM_SENSOR, "mx6x_ir_res_set default\n");
            break;
    }
    mx6x_ir_load_param();
}

void mx6x_ir_stream_1280_800(void)
{
    mx6x_stop_stream();
    mx6x_ir_res_set(RES_1280_800);
    mx6x_ir_stream();
}

void mx6x_ir_stream_640_400(void)
{
    mx6x_stop_stream();
    mx6x_ir_res_set(RES_640_400);
    mx6x_ir_stream();
}

void mx6x_depth_stream_800_1280(void)
{
    mx6x_stop_stream();
    mx6x_depth_res_set(RES_800_1280);
    mx6x_depth_stream();
}

void mx6x_depth_stream_400_640(void)
{
    mx6x_stop_stream();
    mx6x_depth_res_set(RES_400_640);
    mx6x_depth_stream();
}


int cam_sensor_ctl_mxmodule(int cmd, int parm)
{
    CAM_DBG(CAM_SENSOR, "%s---- cmd:%d parm:%d g_dev:%p----\n", __func__, cmd, parm, g_dev);
    switch (cmd) {
    case MX_MSG_INIT:
        break;
    case MX_MSG_START:
        if (g_dev != NULL) {
            mx6300_reset(g_dev);
            if(mode_call[mx6300_output_mode])
                mode_call[mx6300_output_mode]();
            else
                mx6x_ir_stream_1280X800();
        }
        break;
    case MX_MSG_STOP:
        if (g_dev != NULL) {
            mx6x_stop_stream();
        }
        break;
    case MX_MSG_CONFIG:
        mx6300_output_mode = parm;
        break;
    case MX_MSG_PROBE:
        break;
    default:
        break;
    }
    return 0;
}

int mx6300_parse_dts(struct mx6300_dev* mx6300_dev)
{
    int ret = 0;
    FUNC_ENTRY();
    /*get mx6300 debug mode resource*/
    mx6300_dev->debug_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,MX_tstmode-gpio",0);
    if (gpio_is_valid(mx6300_dev->debug_gpio)) {
        ret = gpio_request(mx6300_dev->debug_gpio, "mx6300-debug-gpio");
        if(ret) {
            pr_err("%s could not request debug gpio/n",__func__);
           // return ret;
        }
    } else {
        pr_err("%s not valid debug gpio:%d\n",__func__, mx6300_dev->debug_gpio);
       // return -EIO;
    }

    CAM_DBG(CAM_SENSOR, "%s debug gpio:%d/n",__func__, mx6300_dev->debug_gpio);
    /*get mx6300 vio1p8 resource*/
    mx6300_dev->vio1p8_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,MX_VISP_VDDIO_1V8-gpio",0);
    if (gpio_is_valid(mx6300_dev->vio1p8_gpio)) {
        ret = gpio_request(mx6300_dev->vio1p8_gpio, "mx6300-VISP_VDDIO_1V8-gpio");
        if(ret) {
            pr_err("%s could not request VISP_VDDIO_1V8 gpio\n",__func__);
            return ret;
        }
    } else {
        pr_err("%s not valid VISP_VDDIO_1V8 gpio\n",__func__);
        return -EIO;
    }

    /*get mx6300 vio1p8 resource*/
    mx6300_dev->vio1v8_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,MX_VDDIO_1V8-gpio",0);
    if (gpio_is_valid(mx6300_dev->vio1v8_gpio)) {
        ret = gpio_request(mx6300_dev->vio1v8_gpio, "mx6300-MX_VDDIO_1V8-gpio");
        if(ret) {
            pr_err("%s could not request MX_VDDIO_1V8 gpio\n",__func__);
            return ret;
        }
    } else {
        pr_err("%s not valid MX_VDDIO_1V8 gpio\n",__func__);
        return -EIO;
    }

    /*get mx6300 vdd0v9 resource*/
    mx6300_dev->vdd0v9_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,MX_VISP_VDD_0V9-gpio",0);
    if (gpio_is_valid(mx6300_dev->vdd0v9_gpio)) {
        ret = gpio_request(mx6300_dev->vdd0v9_gpio, "mx6300_VISP_VDD_0V9-gpio");
        if(ret) {
              pr_err("%s could not request VISP_VDD_0V9 gpio\n",__func__);
              return ret;
        }
    } else {
        pr_err("%s not valid VISP_VDD_0V9 gpio\n",__func__);
        return -EIO;
    }

    /*get mx6300 vdd1_0v9 resource*/
    mx6300_dev->vdd1_0v9_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,MX_VISP_VDD1_0V9-gpio",0);
    if (gpio_is_valid(mx6300_dev->vdd1_0v9_gpio)) {
        ret = gpio_request(mx6300_dev->vdd1_0v9_gpio, "mx6300-VISP_VDD1_0V9-gpio");
        if(ret) {
              pr_err("%s could not request VISP_VDD1_0V9 gpio\n",__func__);
              return ret;
        }
    } else {
        pr_err("%s not valid VISP_VDD1_0V9 gpio\n",__func__);
        return -EIO;
    }

    /*get wake resource*/
    mx6300_dev->wake_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,MX_wake-gpio",0);
    if (gpio_is_valid(mx6300_dev->wake_gpio)) {
        ret = gpio_request(mx6300_dev->wake_gpio, "mx6300_wake-gpio");
        if(ret) {
              pr_err("%s could not request wake gpio\n",__func__);
              return ret;
        }
    } else {
        pr_err("%s not valid wake gpio\n",__func__);
        return -EIO;
    }

    /*get reset resource*/
    mx6300_dev->reset_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,MX_reset-gpio",0);
    if (gpio_is_valid(mx6300_dev->reset_gpio)) {
        ret = gpio_request(mx6300_dev->reset_gpio, "mx6300_reset-gpio");
        if(ret) {
            pr_err("%s could not request reset gpio\n",__func__);
            //return ret;
        }
    } else {
        pr_err("%s not valid reset gpio\n",__func__);
        return -EIO;
    }

     /*get IR camera vdd0v9 resource*/
    mx6300_dev->IRAVDD_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,IR_ACAMD_2V8-gpio",0);
    if (gpio_is_valid(mx6300_dev->IRAVDD_gpio)) {
        ret = gpio_request(mx6300_dev->IRAVDD_gpio, "mx6300_IR_ACAMD_2V8-gpio");
        if(ret) {
              pr_err("%s could not request IR_ACAMD_2V8-gpio gpio\n",__func__);
              return ret;
        }
    } else {
        pr_err("%s not valid IR_ACAMD_2V8-gpio gpio\n",__func__);
        return -EIO;
    }

    /*get IR camera vdd1_0v9 resource*/
    mx6300_dev->IRDVDD_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,IR_VCAMD_1V2-gpio",0);
    if (gpio_is_valid(mx6300_dev->IRDVDD_gpio)) {
        ret = gpio_request(mx6300_dev->IRDVDD_gpio, "mx6300-IR_VCAMD_1V2-gpio");
        if(ret) {
              pr_err("%s could not request IR_VCAMD_1V2-gpio gpio\n",__func__);
              return ret;
        }
    } else {
        pr_err("%s not valid IR_VCAMD_1V2-gpio gpio\n",__func__);
        return -EIO;
    }

    /*get IR camera resource*/
    mx6300_dev->IRPWDN_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,IR_PWDN-gpio",0);
    if (gpio_is_valid(mx6300_dev->IRPWDN_gpio)) {
        ret = gpio_request(mx6300_dev->IRPWDN_gpio, "IR_PWDN-gpio");
        if(ret) {
              pr_err("%s could not request IR_PWDN-gpio\n",__func__);
              return ret;
        }
    } else {
        pr_err("%s not valid IR_PWDN-gpio\n",__func__);
        return -EIO;
    }

    /*get IR camera resource*/
    /*mx6300_dev->LDMP_gpio = of_get_named_gpio(mx6300_dev->spi->dev.of_node,"mx6300,LDMP-gpio",0);
    if (gpio_is_valid(mx6300_dev->LDMP_gpio)) {
        ret = gpio_request(mx6300_dev->LDMP_gpio, "mx6300_LDMP-gpio");
        if(ret) {
            pr_err("%s could not request LDMP-gpio gpio\n",__func__);
            return ret;
        }
    } else {
        pr_err("%s not valid LDMP-gpio gpio\n",__func__);
        return -EIO;
    }*/
    CAM_DBG(CAM_SENSOR, "%s parser dt ok.\n",__func__);
    FUNC_EXIT();
    return ret;
}

void mx6300_cleanup(struct mx6300_dev* mx6300_dev)
{
    CAM_DBG(CAM_SENSOR, "mx6300 %s\n",__func__);
    /*
    if (gpio_is_valid(mx6300_dev->spi_gpio)) {
        gpio_free(mx6300_dev->spi_gpio);
        pr_err("%s remove spi_gpio success\n",__func__);
    }
    if (gpio_is_valid(mx6300_dev->spi_gpio_1)) {
        gpio_free(mx6300_dev->spi_gpio_1);
        pr_err("%s remove spi_gpio_1 success\n",__func__);
    }
    if (gpio_is_valid(mx6300_dev->dvdd_gpio)) {
        gpio_free(mx6300_dev->dvdd_gpio);
        pr_err("%s remove dvdd_gpio success\n",__func__);
    }
    if (gpio_is_valid(mx6300_dev->avdd_gpio)) {
        gpio_free(mx6300_dev->avdd_gpio);
        pr_err("%s remove avdd_gpio success\n",__func__);
    }
    if (gpio_is_valid(mx6300_dev->vcc3p3_gpio)) {
        gpio_free(mx6300_dev->vcc3p3_gpio);
        pr_err("%s remove vcc3p3_gpio success\n",__func__);
    }
    if (gpio_is_valid(mx6300_dev->wake_gpio)) {
        gpio_free(mx6300_dev->wake_gpio);
        pr_err("%s remove wake_gpio success\n",__func__);
    }
    if (gpio_is_valid(mx6300_dev->reset_gpio)) {
        gpio_free(mx6300_dev->reset_gpio);
        pr_err("%s remove reset_gpio success\n",__func__);
    }
    */
    return;
}

int mx6300_config_single_vreg(struct device *dev,struct regulator *reg_ptr, int config)
{
    int rc = 0;
    if (!dev || !reg_ptr) {
        pr_err("%s: get failed NULL parameter\n", __func__);
        goto vreg_get_fail;
    }


    if (config) {
        pr_err("%s enable\n", __func__);
        rc = regulator_enable(reg_ptr);
        if (rc < 0) {
            pr_err("%s regulator_enable failed\n", __func__);
            goto vreg_unconfig;
        }
    } else {
        pr_err("%s disable\n", __func__);
        rc = regulator_disable(reg_ptr);
            if (rc < 0) {
        pr_err("%s regulator_enable failed\n", __func__);
        goto vreg_unconfig;
        }
        regulator_put(reg_ptr);
        reg_ptr = NULL;
    }
    return 0;

vreg_unconfig:
    if(regulator_count_voltages(reg_ptr) > 0) {
        regulator_set_load(reg_ptr, 0);
    }

vreg_get_fail:
    pr_err("%s vreg_get_fail\n", __func__);
    return -ENODEV;
}

int mx6300_power_on(struct mx6300_dev* mx6300_dev)
{
//    struct regulator *reg_ptr;
    struct regulator *reg_ptr;
    int boot_mode = get_boot_mode();

    reg_ptr = regulator_get(&mx6300_dev->spi->dev, "mx6300_IR_CAMIO");
    mx6300_config_single_vreg(&mx6300_dev->spi->dev,reg_ptr, 1);

    /*add by hongbo.dai@Camera.Drv, 20180330 for fix AT test*/
    if (boot_mode == MSM_BOOT_MODE__FACTORY) {
        pr_err("FTM mode %s only power UP CAM_VIO\n",__func__);
        return 0;
    }

    if(gpio_is_valid(mx6300_dev->IRAVDD_gpio)) {
        gpio_direction_output(mx6300_dev->IRAVDD_gpio, 1);
    }else{
        pr_err("%s---- IRAVDD_gpio on failed ----\n",__func__);
    }

    if(gpio_is_valid(mx6300_dev->IRDVDD_gpio)) {
        int ret = 0;
        ret = gpio_direction_output(mx6300_dev->IRDVDD_gpio, 1);
        pr_err("%s---- IRDVDD_gpio on ret %d ----\n",__func__, ret);
    }else{
        pr_err("%s---- IRDVDD_gpio on failed ----\n",__func__);
    }

    if(gpio_is_valid(mx6300_dev->vio1v8_gpio)) {
        int ret = 0;
        ret = gpio_direction_output(mx6300_dev->vio1v8_gpio, 1);
        pr_err("%s---- vio1v8_gpio on ret %d ----\n",__func__, ret);
    }else{
        pr_err("%s---- vio1v8_gpio on failed ----\n",__func__);
    }

    if(gpio_is_valid(mx6300_dev->IRPWDN_gpio)) {
        gpio_direction_output(mx6300_dev->IRPWDN_gpio, 1);
    }else{
        pr_err("%s---- IRPWDN_gpio on failed ----\n",__func__);
    }

    //if(gpio_is_valid(mx6300_dev->LDMP_gpio)) {
    //    gpio_direction_output(mx6300_dev->LDMP_gpio, 1);
    //}else{
    //    pr_err("%s---- LDMP_gpio on failed ----\n",__func__);
    //}

    if(gpio_is_valid(mx6300_dev->vio1p8_gpio)) {
        gpio_direction_output(mx6300_dev->vio1p8_gpio, 1);
    }else{
        pr_err("%s---- vio1p8_gpio on failed ----\n",__func__);
    }


    if(gpio_is_valid(mx6300_dev->vdd0v9_gpio)) {
        gpio_direction_output(mx6300_dev->vdd0v9_gpio, 1);
    }else{
        pr_err("%s---- vdd0v9_gpio on failed ----\n",__func__);
    }

    if(gpio_is_valid(mx6300_dev->vdd1_0v9_gpio)) {
        gpio_direction_output(mx6300_dev->vdd1_0v9_gpio, 1);
    }else{
        pr_err("%s---- vdd1_0v9_gpio on failed ----\n",__func__);
    }

    if(gpio_is_valid(mx6300_dev->debug_gpio)) {
        gpio_direction_output(mx6300_dev->debug_gpio, 0);
    }else{
        pr_err("%s---- debug_gpio on failed ----\n",__func__);
    }

    if(gpio_is_valid(mx6300_dev->wake_gpio)) {
        gpio_direction_output(mx6300_dev->wake_gpio, 0);
    }else{
        pr_err("%s---- wake_gpio on failed ----\n",__func__);
    }

    msleep(50);
    if(gpio_is_valid(mx6300_dev->reset_gpio)) {
        gpio_direction_output(mx6300_dev->reset_gpio, 1);
        msleep(10);
    }else{
        pr_err("%s---- reset_gpio on failed ----\n",__func__);
    }

    if(gpio_is_valid(mx6300_dev->reset_gpio)) {
        gpio_direction_output(mx6300_dev->reset_gpio, 0);
    }else{
        pr_err("%s---- reset_gpio on failed ----\n",__func__);
    }

    mx6300_dev->isPowerOn = true;
    msleep(100);
    CAM_DBG(CAM_SENSOR, "---- power on ok ----\n");
    return 0;
}

void mx6300_reset(struct mx6300_dev* mx6300_dev) {
    unsigned char data[4] = {0};
    unsigned device_id = 0;
    uint32_t val;
    if (gpio_is_valid(mx6300_dev->reset_gpio)) {
        gpio_direction_output(mx6300_dev->reset_gpio, 1);
        msleep(20);
    }
    if (gpio_is_valid(mx6300_dev->reset_gpio)) {
        gpio_direction_output(mx6300_dev->reset_gpio, 0);
        msleep(100);
    }
    mx6300_spi_read_block(mx6300_dev, 0xc0, 4, data);
    val = data[0]|data[1]<<8|data[2]<<16|data[3]<<24;

    //CAM_DBG(CAM_SENSOR, "mx6300_reset val = {0x%x}----", val);
    if (val&0x000f0000) {
        fval = val;
    }
    mx6300_spi_read_block(mx6300_dev, fval, 4, data);
    device_id = data[1]|data[0]<<8;
    //CAM_DBG(CAM_SENSOR, "mx6300_reset data = {0x%x}----", device_id);
}

int mx6300_power_off(struct mx6300_dev* mx6300_dev)
{
    mx6300_reset(mx6300_dev);
    return 0;
}

void mx6300_spi_setup(struct mx6300_dev *mx6300_dev, int max_speed_hz)
{

    mx6300_dev->spi->mode = SPI_MODE_0;
    mx6300_dev->spi->max_speed_hz = max_speed_hz;
    mx6300_dev->spi->bits_per_word = 8;
    spi_setup(mx6300_dev->spi);
}

void mx6300_spi_write_enable(struct mx6300_dev *mx6300_dev, int enable)
{
    //unsigned char val = enable ? MX6300_CMD_WRITE_ENABLE : MX6300_CMD_WRITE_DISABLE;
    //spi_write(mx6300_dev->spi, &val, 1);
}

static int mx6300_spi_check_id(struct mx6300_dev *mx6300_dev)
{
    struct spi_message msg;
    int ret = 0;
    unsigned char buf[] = {0,0};
    unsigned char tx_buf[MX6300_CMD_SIZE] = {MX6300_CMD_READ_ID,0,0,0};
    unsigned device_id = 0;
    struct spi_transfer xfer[] = {
        {
         .tx_buf = tx_buf,
         .delay_usecs = 1,
         .len = MX6300_CMD_SIZE,
        },
        {
         .rx_buf = buf,
         .delay_usecs = 1,
         .len = MX6300_ID_SIZE,
        }
    };
    /*send mx6300 command to device.*/
    mx6300_spi_write_enable(mx6300_dev,0);
    spi_message_init(&msg);
    spi_message_add_tail(&xfer[0], &msg);
    spi_message_add_tail(&xfer[1], &msg);
    msleep(10);
    ret = spi_sync(mx6300_dev->spi, &msg);
    msleep(10);
    device_id = buf[0] << 8 | buf [1];
    mx6300_dev->device_id = device_id;
    CAM_DBG(CAM_SENSOR, "readed device_id:0X%02x  expected_id:0X%02x\n",device_id,MX6300_CMD_DEVICE_ID);
    return (device_id == MX6300_CMD_DEVICE_ID);
}

int mx6300_spi_read_sr(struct mx6300_dev *mx6300_dev)
{
    ssize_t retval;
    u8 code = MX6300_CMD_CHECK_BUSY;
    u8 val;

    retval = spi_write_then_read(mx6300_dev->spi, &code, 1, &val, 1);
    if (retval < 0) {
        dev_err(&mx6300_dev->spi->dev, "error %d reading SR\n", (int) retval);
        return retval;
    }

    return val;
}

static int mx6300_wait_till_ready(struct mx6300_dev *flash)
{
    int count = 0;
    int sr = 0;
    return 0;

    for (count = 0; count < MX6300_MAX_WAIT_COUNT; count++) {
        if ((sr = mx6300_spi_read_sr(flash)) < 0) {
            break;
        } else if (!(sr & SR_WIP)) {
            return 0;
        }
        /* REVISIT sometimes sleeping would be best */
    }
    CAM_DBG(CAM_SENSOR, "in: count = %d\n", count);
    return 1;
}


void mx6300_spi_test(struct mx6300_dev *mx6300_dev)
{
    int i = 0;
    int count = 0;
    int data_len = 150;
    int addr = MX6300_FLASH_ADDR;
    int addr2 = 0;
    unsigned char *write_data = kzalloc(100,GFP_KERNEL);
    unsigned char* buf = kzalloc(data_len,GFP_KERNEL);
    memset(write_data,0x09,100);
    FUNC_ENTRY();

    mx6300_erase_sector(mx6300_dev,addr,4096);

    /*read test*/
    msleep(20);
    if(buf) {
        count = mx6300_spi_read(mx6300_dev,addr,data_len,buf);
        pr_err("%s readed :%d bytes\n",__func__,count);
    } else {
        pr_err("%s malloc failed.\n",__func__);
        goto clean;
    }
    for(i = 0;i < data_len; i++) {
        pr_err("%s addr:0X%04x  data:0X%02x\n",__func__,addr+i,*(buf+i));
    }

    /*write test*/
    msleep(20);
    mx6300_erase_sector(mx6300_dev,addr,4096);
    msleep(20);
    count = mx6300_spi_write(mx6300_dev,MX6300_FLASH_ADDR,100,write_data);
    CAM_DBG(CAM_SENSOR, "%s written :%d bytes\n",count);

    /*read test again*/
    memset(buf,0,data_len);
    count = mx6300_spi_read(mx6300_dev,addr,data_len,buf);
    CAM_DBG(CAM_SENSOR, "%s readed :%d bytes\n",count);
    for(i = 0;i < data_len; i++) {
        pr_err("%s addr:0X%04x  data:0X%02x\n",__func__,addr+i,*(buf+i));
    }

    /*read test from 0X00 again*/
    memset(buf,0,data_len);
    count = mx6300_spi_read(mx6300_dev,addr2,data_len,buf);
    CAM_DBG(CAM_SENSOR, "mx6300 %s readed :%d bytes\n",count);
    /*print*/
    for(i = 0;i < data_len; i++) {
        pr_err("%s addr:0X%04x  data:0X%02x\n",__func__,addr2+i,*(buf+i));
    }

clean:
    kfree(buf);
    kfree(write_data);
    buf = NULL;
    write_data = NULL;
    FUNC_EXIT();
    return;
}

static int mx6300_open(struct inode *inode, struct file *filp)
{
    struct mx6300_dev *mx6300_dev = NULL;
    int    ret = 0;
    FUNC_ENTRY();
    mutex_lock(&device_list_lock);
    list_for_each_entry(mx6300_dev, &device_list, device_entry) {
        if(mx6300_dev->devt == inode->i_rdev) {
            pr_err("%s devices found\n",__func__);
            break;
        }
    }
    filp->private_data = mx6300_dev;
    mutex_unlock(&device_list_lock);

    if (mx6300_dev && !mx6300_dev->isPowerOn) {
        ret = mx6300_power_on(mx6300_dev);
        CAM_DBG(CAM_SENSOR, "called power on,ret:%d\n", ret);
    }

//    ret = mx6300_erase_sector(mx6300_dev,MX6300_FLASH_ADDR,);
//    pr_err("%s erased flash sector,ret:%d\n",__func__,ret);
    FUNC_EXIT();
    return ret;
}

static int mx6300_release(struct inode *inode, struct file *filp)
{
    struct mx6300_dev *mx6300_dev = NULL;
    int ret = 0;
    FUNC_ENTRY();
    mutex_lock(&device_list_lock);
    mx6300_dev = filp->private_data;
    filp->private_data = NULL;
    mutex_unlock(&device_list_lock);
    if (mx6300_dev) {
        ret = mx6300_power_off(mx6300_dev);
        CAM_DBG(CAM_SENSOR, "called power off,ret:%d\n",ret);
    }
    FUNC_EXIT();
    return ret;
}
int mx6300_spi_read(struct mx6300_dev *mx6300_dev,int addr,int read_len,unsigned char *data)
{
      int read_count=0;
      int j = 0;
      int ret=0;
      int read_times = read_len / MX6300_FLASH_BLOCK_SIZE;
      int last_size = read_len % MX6300_FLASH_BLOCK_SIZE;

      CAM_DBG(CAM_SENSOR, "mx6300_spi_read : 0x%08x , 0x%08x\n", addr, read_len);
      for (j = 0; j < read_times; j++) {
          ret = mx6300_spi_read_block(mx6300_dev, addr + read_count, MX6300_FLASH_BLOCK_SIZE, data + read_count);
          read_count += ret;
      }
      if (last_size>0) {
          ret = mx6300_spi_read_block(mx6300_dev, addr + read_count, last_size, data + read_count);
          read_count += ret;
      }
      return read_count;
}
/*int mx6x_spi_block_read(mx6x_spi_slave *dev_ptr,uint32_t addr,uint8_t *data_ptr,int data_size)
{
    int ret=0;

    uint8_t *tx=(uint8_t *)malloc(data_size+6);
    uint8_t *rx=(uint8_t *)malloc(data_size+6);
    memset(tx,0,data_size+6);
    memset(rx,0,data_size+6);
    tx[0]=MX6X_SPI_FAST_READ_CMD;
    tx[1]=addr[2];
    tx[2]=addr[1];
    tx[3]=addr[0];
    tx[4]=0x00;
    tx[5]=0x00;

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = data_size+6,
        .delay_usecs = dev_ptr->delay,
        .speed_hz = dev_ptr->speed,
        .bits_per_word = dev_ptr->bits,
    };

    ret = ioctl(dev_ptr->fd, SPI_IOC_MESSAGE(1), &tr);
    memcpy(data_ptr,&rx[6],data_size);


    free(rx);
    free(tx);

    return ret;

}*/
int mx6300_spi_read_block(struct mx6300_dev *mx6300_dev,int addr,int read_len,unsigned char *data)
{
     struct spi_message msg;
     int ret = 0;
     unsigned char *tx=(unsigned char *)kmalloc(read_len+6, GFP_KERNEL);
     unsigned char  *rx=(unsigned char *)kmalloc(read_len+6, GFP_KERNEL);

     struct spi_transfer xfer = {
         .tx_buf = tx,
         .rx_buf = rx,
         .len = read_len+6,
         .delay_usecs = MX6300_SPI_WR_UDELAY,
         .speed_hz = MX6300_SPI_SPEED,
         .bits_per_word = 8,
     };

     memset(tx,0,read_len+6);
     memset(rx,0,read_len+6);
     tx[0] = MX6300_CMD_READ_BYTES;
     tx[1] = (u8)(addr>>16)&0XFF;
     tx[2] = (u8)(addr>>8)&0XFF;
     tx[3] = (u8)(addr&0XFF);
     tx[4] = 0x00;
     tx[5] = 0x00;



     mx6300_spi_write_enable(mx6300_dev,0);
     spi_message_init(&msg);
     spi_message_add_tail(&xfer, &msg);
     if (mx6300_wait_till_ready(mx6300_dev)) {
        pr_err("%s  %d not ready,return -1\n",__func__,__LINE__);
        return -1;
     }
     ret = spi_sync(mx6300_dev->spi, &msg);
     memcpy(data, &rx[6], read_len);

     kfree(rx);
     kfree(tx);

     //pr_err("%s addr:0X%04x cmd:0X%02x read_len:%d ret:%d\n",__func__,addr,tx_buf[0],(msg.actual_length - MX6300_CMD_SIZE),ret);
     return (msg.actual_length - 6);
}


#if 0
int mx6300_spi_read_block(struct mx6300_dev *mx6300_dev,int addr,int read_len,unsigned char *data)
{
     struct spi_message msg;
     int ret = 0;
     unsigned char tx_buf[MX6300_CMD_SIZE] = {MX6300_CMD_READ_BYTES,
                                (u8)(addr>>16)&0XFF,
                                (u8)(addr>>8)&0XFF,
                                (u8)(addr&0XFF),0x00,0x00};
     /*struct spi_transfer xfer[] = {
         {
             .tx_buf = tx_buf,
             .len = MX6300_CMD_SIZE,
             .delay_usecs = MX6300_SPI_WR_UDELAY,
             .speed_hz = MX6300_SPI_SPEED
         },
         {
             .rx_buf = data,
             .len = read_len,
             .delay_usecs = MX6300_SPI_WR_UDELAY,
             .speed_hz = MX6300_SPI_SPEED
         }
     };*/


     struct spi_transfer xfer = {
         .tx_buf = (unsigned long)tx_buf,
         .rx_buf = (unsigned long)data,
         .len = read_len+6,
         .delay_usecs = MX6300_SPI_WR_UDELAY,
         .speed_hz = MX6300_SPI_SPEED,
         .bits_per_word = 8,
     };

     mx6300_spi_write_enable(mx6300_dev,0);
     spi_message_init(&msg);
     /*spi_message_add_tail(&xfer[0], &msg);
     spi_message_add_tail(&xfer[1], &msg);*/
     spi_message_add_tail(&xfer, &msg);
     if (mx6300_wait_till_ready(mx6300_dev)) {
        pr_err("%s  %d not ready,return -1\n",__func__,__LINE__);
        return -1;
     }
     ret = spi_sync(mx6300_dev->spi, &msg);
     //pr_err("%s addr:0X%04x cmd:0X%02x read_len:%d ret:%d\n",__func__,addr,tx_buf[0],(msg.actual_length - MX6300_CMD_SIZE),ret);
     return (msg.actual_length - MX6300_CMD_SIZE);
}
#endif
static ssize_t mx6300_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct mx6300_dev *mx6300_dev = filp->private_data;
    int ret = 0;
    int addr = *f_pos;
    unsigned char *read_buffer = kzalloc(count, GFP_KERNEL);
    FUNC_ENTRY();
    if (!read_buffer){
        pr_err("%s failed to alloc memory\n",__func__);
        ret = -ENOMEM;
        goto exit;
    }

    CAM_DBG(CAM_SENSOR, "count:%ld, f_pos:%d\n", count,(int)*f_pos);
    if (count > MX6300_BUFFER_MAX || count == 0) {
        pr_err("%s invalid read count\n", __func__);
        ret = EINVAL;
        goto exit;
    }

    mutex_lock(&mx6300_dev->rw_lock);
   // msleep(100);
    ret = mx6300_spi_read(mx6300_dev, addr, count, read_buffer);
    if(ret > 0) {
        int missing = 0;
        missing = copy_to_user(buf, read_buffer, ret);
        pr_err("%s read_count:%d missing%d\n",__func__,ret,missing);
        if(missing == ret) {
            ret = -EFAULT;
        }
    } else {
        pr_err("%s failed to read data from SPI device,ret:%d\n",__func__,ret);
        ret = -EFAULT;
    }
    mutex_unlock(&mx6300_dev->rw_lock);

exit:
    if(read_buffer) {
        kfree(read_buffer);
        read_buffer = NULL;
    }
    FUNC_EXIT();
    return ret;
}
int mx6300_erase_sector_4K(struct mx6300_dev * mx6300_dev,int addr)
{
    int ret = 0;
    unsigned char tx_buf[MX6300_CMD_SIZE];

    tx_buf[0] = MX6300_CMD_ERASE_4K;
    tx_buf[1] = (u8)(addr>>16)&0XFF;
    tx_buf[2] = (u8)(addr>>8)&0XFF;
    tx_buf[3] = (u8)(addr)&0XFF;
    if (mx6300_wait_till_ready(mx6300_dev)) {
        pr_err("%s  %d not ready,return -1\n",__func__,__LINE__);
        return -1;
    }
    mx6300_spi_write_enable(mx6300_dev,1);
    ret = spi_write(mx6300_dev->spi, tx_buf, 4);
    CAM_DBG(CAM_SENSOR, "ret:%d\n",ret);
    return ret;
}

int mx6300_erase_sector_32K(struct mx6300_dev * mx6300_dev,int addr)
{
    int ret = 0;
    unsigned char tx_buf[MX6300_CMD_SIZE];

    tx_buf[0] = MX6300_CMD_ERASE_32K;
    tx_buf[1] = (u8)(addr>>16)&0XFF;
    tx_buf[2] = (u8)(addr>>8)&0XFF;
    tx_buf[3] = (u8)(addr)&0XFF;
    if (mx6300_wait_till_ready(mx6300_dev)) {
        pr_err("%s  %d not ready,return -1\n",__func__,__LINE__);
        return -1;
    }
    mx6300_spi_write_enable(mx6300_dev,1);
    ret = spi_write(mx6300_dev->spi, tx_buf, 4);
    CAM_DBG(CAM_SENSOR, "ret:%d\n",ret);
    return ret;
}

int mx6300_erase_sector_64K(struct mx6300_dev * mx6300_dev,int addr)
{
    int ret = 0;
    unsigned char tx_buf[MX6300_CMD_SIZE];

    tx_buf[0] = MX6300_CMD_ERASE_64K;
    tx_buf[1] = (u8)(addr>>16)&0XFF;
    tx_buf[2] = (u8)(addr>>8)&0XFF;
    tx_buf[3] = (u8)(addr)&0XFF;
    if (mx6300_wait_till_ready(mx6300_dev)) {
        pr_err("%s  %d not ready,return -1\n",__func__,__LINE__);
        return -1;
    }
    mx6300_spi_write_enable(mx6300_dev,1);
    ret = spi_write(mx6300_dev->spi, tx_buf, 4);
    CAM_DBG(CAM_SENSOR, "ret:%d\n", ret);
    return ret;
}

/*guohui.tan@Camera,2017/10/30  add for MX6300 SPI driver*/
int mx6300_erase_sector(struct mx6300_dev * mx6300_dev,int addr, int erase_len)
{
    int ret = 0;
    int already_erase_size = 0;
    if (erase_len <= 0) {
        ret = -1;
        pr_err("%s  %d ret:%d\n",__func__,__LINE__,ret);
        return ret;
    }
    while (erase_len - already_erase_size > 0) {
        if (erase_len - already_erase_size >= SIZE_64K) {
            mx6300_erase_sector_64K(mx6300_dev,addr + already_erase_size);
            already_erase_size += SIZE_64K;
        } else if (erase_len - already_erase_size >= SIZE_32K){
            mx6300_erase_sector_32K(mx6300_dev,addr + already_erase_size);
            already_erase_size += SIZE_32K;
        } else {
            mx6300_erase_sector_4K(mx6300_dev,addr + already_erase_size);
            already_erase_size += SIZE_4K;
        }
        pr_err("%s  %d, already_erase_size= %d K \n",__func__,__LINE__,already_erase_size/1024);
    }
    CAM_DBG(CAM_SENSOR, "ret:%d\n", ret);
    return ret;
}

int mx6300_spi_write(struct mx6300_dev *mx6300_dev,int addr,int write_len,unsigned char *data)
{
      int write_count=0;
      int j = 0;
      int ret=0;
      int write_times = write_len / MX6300_FLASH_BLOCK_SIZE;
      int last_size = write_len % MX6300_FLASH_BLOCK_SIZE;

      CAM_DBG(CAM_SENSOR, "mx6300_spi_write : write_times:%d, last_size:%d, addr=0x%08x ,write_len= 0x%08x\n", write_times, last_size, addr, write_len);
      for (j = 0; j < write_times; j++) {
          ret = mx6300_spi_write_block(mx6300_dev, addr + write_count, MX6300_FLASH_BLOCK_SIZE, data + write_count);
          udelay(1000);
          write_count += ret;
      }
      if (last_size > 0) {
          ret = mx6300_spi_write_block(mx6300_dev, addr + write_count, last_size, data + write_count);
          udelay(1000);
          write_count += ret;
      }
      return write_count;
}

int mx6300_spi_write_block(struct mx6300_dev *mx6300_dev,int addr,int write_len,unsigned char *data)
{
     struct spi_message msg;
     int ret = 0;
     unsigned char tx_buf[MX6300_CMD_SIZE];
     struct spi_transfer xfer []= {
        {
             .tx_buf = tx_buf,
             .len = MX6300_CMD_SIZE,
             .delay_usecs = MX6300_SPI_WR_UDELAY,
             .speed_hz = MX6300_SPI_SPEED
        },
        {
             .tx_buf = data,
             .len = write_len,
             .delay_usecs = MX6300_SPI_WR_UDELAY,
             .speed_hz = MX6300_SPI_SPEED
        }
     };
     tx_buf[0] = MX6300_CMD_WRITE_BYTES;
     tx_buf[1] = (u8)(addr>>16)&0XFF;
     tx_buf[2] = (u8)(addr>>8)&0XFF;
     tx_buf[3] = (u8)(addr)&0XFF;

     spi_message_init(&msg);
     spi_message_add_tail(&xfer[0], &msg);
     spi_message_add_tail(&xfer[1], &msg);

     ret = spi_sync(mx6300_dev->spi, &msg);
     //pr_err("%s cmd:0X%02x addr:0X%04x ret:%d,write_len:%d data:0X%02x\n",__func__,tx_buf[0],addr,ret,(msg.actual_length - MX6300_CMD_SIZE),data[0]);
     return (msg.actual_length - MX6300_CMD_SIZE);
}

loff_t mx6300_llseek(struct file *filp, loff_t offset, int whence)
{
    struct mx6300_dev *mx6300_dev = filp->private_data;
    loff_t newpos;
    mutex_lock(&mx6300_dev->rw_lock);
    switch(whence) {
      case 0: /* SEEK_SET */
            newpos = offset;
            break;

      case 1: /* SEEK_CUR */
            newpos = filp->f_pos + offset;
            break;

      default: /* can't happen */
            return -EINVAL;
    }
    if (newpos < 0) return -EINVAL;
    filp->f_pos = newpos;
    mutex_unlock(&mx6300_dev->rw_lock);
    return newpos;

}

static ssize_t mx6300_write(struct file *filp, const char __user *buf,
    size_t count, loff_t *f_pos)
{
    struct mx6300_dev *mx6300_dev = filp->private_data;
    int ret = 0;
    int addr = *f_pos;
    unsigned char *write_buffer = kzalloc(count,GFP_KERNEL);
    FUNC_ENTRY();
    if (!write_buffer){
        pr_err("%s failed to alloc memory\n",__func__);
        ret = -ENOMEM;
        goto exit;
    }

    pr_err("%s count:%ld, f_pos:%d\n", __func__,count,(int)*f_pos);
    if (count > MX6300_BUFFER_MAX || count == 0) {
        pr_err("%s invalid read count\n", __func__);
        ret = EINVAL;
        goto exit;
    }

    mutex_lock(&mx6300_dev->rw_lock);

    //msleep(10);
    ret = mx6300_erase_sector(mx6300_dev,addr,count);
    pr_err("%s erased flash sector,ret:%d\n",__func__,ret);
 //   msleep(500);
    ret = copy_from_user(write_buffer, buf, count);
    if (ret == 0) {
        pr_err("%s %ld bytes copyed from_user, ret%d\n",__func__, count, ret);
        ret = mx6300_spi_write(mx6300_dev, addr, count, write_buffer);
        pr_err("%s writtend %d bytes \n",__func__,ret);
    } else {
        pr_err("%s failed to write data through SPI bus\n",__func__);
        ret = -EFAULT;
    }
    mutex_unlock(&mx6300_dev->rw_lock);

exit:
    if(write_buffer) {
        kfree(write_buffer);
        write_buffer = NULL;
    }
    FUNC_EXIT();
    return ret;
}

static int mx6300_probe(struct spi_device *spi)
{
    struct mx6300_dev *mx6300_dev = NULL;
    int    ret = -EINVAL ;
    unsigned long minor;
    mx6300_dev = kzalloc(sizeof(*mx6300_dev), GFP_KERNEL);
    FUNC_ENTRY();
    if (!mx6300_dev){
        pr_err("%s failed to alloc memory\n",__func__);
        ret = -ENOMEM;
        goto exit_error;
    }

    /* Initialize the driver data */
    mx6300_dev->spi = spi;

    mutex_init(&mx6300_dev->buf_lock);
    mutex_init(&mx6300_dev->rw_lock);
    INIT_LIST_HEAD(&mx6300_dev->device_entry);

    mx6300_dev->debug_gpio = -EINVAL;
    mx6300_dev->vio1p8_gpio = -EINVAL;
    mx6300_dev->vdd0v9_gpio = -EINVAL;
    mx6300_dev->vdd1_0v9_gpio = -EINVAL;
    mx6300_dev->wake_gpio = -EINVAL;
    mx6300_dev->vio1v8_gpio = -EINVAL;
    mx6300_dev->reset_gpio = -EINVAL;

    if (mx6300_parse_dts(mx6300_dev)) {
        pr_err("%s failed to parse dts. \n",__func__);
        ret =  -EINVAL;
        goto res_clean;
    }

    mx6300_power_on(mx6300_dev);

    mutex_lock(&device_list_lock);
    minor = find_first_zero_bit(minors, N_SPI_MINORS);
    CAM_DBG(CAM_SENSOR, "mx6300 minor:%lu\n", minor);
    if (minor < N_SPI_MINORS) {
        struct device *dev;
        mx6300_dev->devt = MKDEV(SPIDEV_MAJOR, minor);
        dev = device_create(mx6300_spi_class, &spi->dev, mx6300_dev->devt,mx6300_dev, MX6300_DEV_NAME);
        ret = IS_ERR(dev) ? PTR_ERR(dev) : 0;
    } else {
        pr_err("%s no minor number available! minor:%ld\n",__func__,minor);
        ret = -ENODEV;
        goto res_clean;
    }

    if(ret == 0) {
        set_bit(minor, minors);
        list_add(&mx6300_dev->device_entry, &device_list);
        pr_err("%s list_add\n",__func__);
    }
    mutex_unlock(&device_list_lock);

    if (1) {
    if (ret == 0) {
        spi_set_drvdata(spi, mx6300_dev);
        pr_err("%s setting device configuration.\n",__func__);
        mx6300_spi_setup(mx6300_dev, 1000*1000);
    }

    if(mx6300_spi_check_id(mx6300_dev)) {
        unsigned char *write_buffer = kzalloc(sizeof(tiger_1_8_37),GFP_KERNEL);
        CAM_DBG(CAM_SENSOR, "check id OK\n");
        if(write_buffer){
            memcpy(write_buffer,tiger_1_8_37,sizeof(tiger_1_8_37));
            mx6300_spi_write(mx6300_dev, 0, sizeof(tiger_1_8_37), write_buffer);
        }
        msleep(200);
        mx6300_power_off(mx6300_dev);
        mode_call[0] = mx6x_ir_stream_1280_800;//mx6x_ir_stream_1280X800;
        mode_call[1] = mx6x_ir_stream_640_400;
        mode_call[2] = mx6x_depth_stream_800_1280;
        mode_call[3] = mx6x_depth_stream_400_640;
        if(write_buffer) {
        kfree(write_buffer);
        write_buffer = NULL;
    }
    } else {
        pr_err("%s check id FAIL !!!\n",__func__);
        goto del_device;
    }
    }
    CAM_DBG(CAM_SENSOR, "probe Ok end.\n");
    //mx6300_spi_test(mx6300_dev);
    //mx6300_power_off(mx6300_dev);
    g_dev = mx6300_dev;
    FUNC_EXIT();
    return ret;

del_device:
    mutex_lock(&device_list_lock);
    list_del(&mx6300_dev->device_entry);
    clear_bit(MINOR(mx6300_dev->devt), minors);
    device_destroy(mx6300_spi_class, mx6300_dev->devt);
    mutex_unlock(&device_list_lock);

res_clean:
    //mx6300_cleanup(mx6300_dev);
    kfree(mx6300_dev);

exit_error:
    pr_err("%s probe failed, ret:%d\n",__func__,ret);
    return ret;
}

static int mx6300_remove(struct spi_device *spi)
{
    struct mx6300_dev *mx6300_dev = spi_get_drvdata(spi);
    FUNC_ENTRY();
    mx6300_dev->spi = NULL;
    spi_set_drvdata(spi, NULL);

    mutex_lock(&device_list_lock);
    list_del(&mx6300_dev->device_entry);
    device_destroy(mx6300_spi_class, mx6300_dev->devt);
    clear_bit(MINOR(mx6300_dev->devt), minors);
   // mx6300_cleanup(mx6300_dev);
    kfree(mx6300_dev);
    g_dev = NULL;
    mutex_unlock(&device_list_lock);
    FUNC_EXIT();
    return 0;
}

static const struct file_operations mx6300_fops = {
    .owner =    THIS_MODULE,
    .write =    mx6300_write,
    .read =    mx6300_read,
    .llseek =   mx6300_llseek,
    .open =    mx6300_open,
    .release =    mx6300_release,
};


volatile static int mx6300_gpio_enable = 0;
#define WAKE_GPIO_MASK  0x0F
#define RESET_GPIO_MASK 0xF0

static ssize_t mx6300_write_gpio(struct file *filp, const char __user *buff,
    size_t len, loff_t *data)
{
    char buf[8] = {0};
    int enable = 0;
    if (len > 8)
        len = 8;
    if (copy_from_user(buf, (void __user *)buff, sizeof(buf))) {
        pr_err("proc write error.\n");
        return -EFAULT;
    }
    enable = simple_strtoul(buf, NULL, 0);
    if (g_dev != NULL) {
        if ((mx6300_gpio_enable & WAKE_GPIO_MASK) != (enable & WAKE_GPIO_MASK)) {
            if (gpio_is_valid(g_dev->wake_gpio) && ((enable & WAKE_GPIO_MASK) == 1)) {
                gpio_direction_output(g_dev->wake_gpio, 1);
                mdelay(3);
                gpio_direction_output(g_dev->wake_gpio, 0);
            }
        }
        if ((mx6300_gpio_enable & RESET_GPIO_MASK) != (enable & RESET_GPIO_MASK)) {
            if (gpio_is_valid(g_dev->reset_gpio)) {
                gpio_direction_output(g_dev->reset_gpio, (enable & RESET_GPIO_MASK));
            }
        }
        pr_err("mx6300_write_gpio: mx6300_gpio_enable:0x%x=>0x%x\n", mx6300_gpio_enable, enable);
        mx6300_gpio_enable = enable;
    }
    return len;
}

static ssize_t mx6300_read_gpio(struct file *filp, char __user *buff,
                            size_t len, loff_t *data)
{
    char value[2] = {0};
    snprintf(value, sizeof(value), "%d", mx6300_gpio_enable);
    return simple_read_from_buffer(buff, len, data, value,1);
}


static const struct file_operations mx6300_gpio_fops = {
    .owner =    THIS_MODULE,
    .write =    mx6300_write_gpio,
    .read =    mx6300_read_gpio,
};


static struct of_device_id mx6300_match_table[] = {
        { .compatible = "orbbec,mx6300",},
        { },
};

static struct spi_driver mx6300_spi_driver = {
    .driver = {
        .name =    "mx6300",
        .owner =    THIS_MODULE,
        .of_match_table = mx6300_match_table,
    },
    .probe =    mx6300_probe,
    .remove =    mx6300_remove,
};

static int __init mx6300_init(void)
{
    int ret = 0;
    struct proc_dir_entry *proc_entry = NULL;
    FUNC_ENTRY();
    BUILD_BUG_ON(N_SPI_MINORS > 256);
    ret = register_chrdev(SPIDEV_MAJOR, MX6300_CHRD_DRIVER_NAME, &mx6300_fops);
    if (ret < 0) {
        pr_err("%s failed to register char device! ret:%d\n",__func__,ret);
        FUNC_EXIT();
        return ret;
    }
    proc_entry = proc_create_data( "mx6300_gpio_fops", 0666, NULL,&mx6300_gpio_fops, NULL);
    if (proc_entry == NULL) {
        ret = -ENOMEM;
        pr_err("[%s]: Error! Couldn't create mx6300_gpio_fops proc entry\n", __func__);
    }

    mx6300_spi_class = class_create(THIS_MODULE, MX6300_CLASS_NAME);
    if (IS_ERR(mx6300_spi_class)) {
        unregister_chrdev(SPIDEV_MAJOR, mx6300_spi_driver.driver.name);
        pr_err("%s failed to create class!\n",__func__);
        FUNC_EXIT();
        return PTR_ERR(mx6300_spi_class);
    }

    ret = spi_register_driver(&mx6300_spi_driver);
    CAM_DBG(CAM_SENSOR, "spi_register_driver,ret:%d \n", ret);
    if (ret < 0) {
        class_destroy(mx6300_spi_class);
        unregister_chrdev(SPIDEV_MAJOR, mx6300_spi_driver.driver.name);
        pr_info("%s failed to register SPI driver, ret:%d\n",__func__,ret);
    }
    FUNC_EXIT();
    return ret;
}

static void __exit mx6300_exit(void)
{
    FUNC_ENTRY();
    spi_unregister_driver(&mx6300_spi_driver);
    class_destroy(mx6300_spi_class);
    unregister_chrdev(SPIDEV_MAJOR, mx6300_spi_driver.driver.name);
    FUNC_EXIT();
    return;
}

module_init(mx6300_init);
module_exit(mx6300_exit);
#else

#include "mx6300_tac/mx6300_tac.h"

static uint8_t mx6300_output_mode = 0;
#define MX6300_STREAM_MODE_SIZE 6

const face_ta_mx_driver_stream_data_t stream_mode_config[MX6300_STREAM_MODE_SIZE] = {
    /* IR Image mode: this mode will open ir stream and flood led */
    /* 1280 x 800 IR mode */
    {
        .width = 1280,
        .height = 800,
        .type = MX6300_STREAM_TYPE_IR_IMAGE,
    },

    /* 640 x 400 IR mode */
    {
        .width = 640,
        .height = 400,
        .type = MX6300_STREAM_TYPE_IR_IMAGE,
    },

    /* Depth Image mode: this mode will open depth stream and laser */
    /* 800 x 1280 depth mode */
    {
        .width = 800,
        .height = 1280,
        .type = MX6300_STREAM_TYPE_DEPTH_IMAGE,
    },

    /* 400 x 640 depth mode */
    {
        .width = 400,
        .height = 640,
        .type = MX6300_STREAM_TYPE_DEPTH_IMAGE,
    },

    /* IR pattern mode: this mode will open IR stream and laser */
    /* 1280 x 800 IR pattern mode */
    {
        .width = 1280,
        .height = 800,
        .type = MX6300_STREAM_TYPE_IR_PATTERN,
    },

    /* 640 x 400 IR pattern mode */
    {
        .width = 640,
        .height = 400,
        .type = MX6300_STREAM_TYPE_IR_PATTERN,
    }
};

int cam_sensor_ctl_mxmodule(int cmd, int parm) {
    int ret = 0;
    printk(KERN_INFO "%s, get cmd %d, parm %d\n", __func__, cmd, parm);
    switch (cmd) {
        case MX_MSG_INIT:
            {
                break;
            }
        case MX_MSG_START:
            {
                if(mx6300_output_mode < MX6300_STREAM_MODE_SIZE) {
                    ret = mx6300_tac_start_stream(
                        stream_mode_config[mx6300_output_mode].width,
                        stream_mode_config[mx6300_output_mode].height,
                        stream_mode_config[mx6300_output_mode].type);
                }
                else {
                    ret = mx6300_tac_start_stream(
                        stream_mode_config[0].width,
                        stream_mode_config[0].height,
                        stream_mode_config[0].type);
                }
                break;
            }
        case MX_MSG_STOP:
            {
                ret = mx6300_tac_stop_stream();
                break;
            }
        case MX_MSG_CONFIG:
            {
                if (parm >= 0 && parm < MX6300_STREAM_MODE_SIZE) {
                    mx6300_output_mode = parm;
                }
                break;
            }
        case MX_MSG_PROBE:
            {
                break;
            }
        default:
            {
                printk(KERN_DEBUG "%s, unknown command.\n", __func__);
                break;
            }
    }
    return ret;
}
#endif /* OPPO_MX6300_TEE_SUPPORT */

MODULE_AUTHOR("wangdeyong@oppo.com");
MODULE_DESCRIPTION("mx6300 SPI device interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi-mx6300");

