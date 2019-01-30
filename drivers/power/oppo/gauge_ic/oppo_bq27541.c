/************************************************************************************
** File:  \\192.168.144.3\Linux_Share\12015\ics2\development\mediatek\custom\oppo77_12015\kernel\battery\battery
** VENDOR_EDIT
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
**
** Description:
**          for dc-dc sn111008 charg
**
** Version: 1.0
** Date created: 21:03:46, 05/04/2012
** Author: Fanhong.Kong@ProDrv.CHG
**
** --------------------------- Revision History: ------------------------------------------------------------
* <version>           <date>                <author>                             <desc>
* Revision 1.0        2015-06-22        Fanhong.Kong@ProDrv.CHG           Created for new architecture
************************************************************************************************************/
#ifdef CONFIG_OPPO_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <asm/unaligned.h>

#include <linux/xlog.h>
#include <mt-plat/mtk_gpio.h>
#include <upmu_common.h>
#include <linux/irqchip/mtk-eic.h>
#include <linux/power_supply.h>

#include <linux/wakelock.h>
#include <linux/gpio.h>

#include <mt-plat/battery_meter.h>
#include <mt-plat/charging.h>
#include <mt-plat/battery_common.h>
#include <soc/oppo/device_info.h>

#else
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <soc/oppo/device_info.h>

#endif

#include "oppo_sha1_hmac.h"
#ifdef OPPO_SHA1_HMAC
#include <linux/random.h>
#endif
#include "../oppo_charger.h"
#include "../oppo_gauge.h"
#include "../oppo_vooc.h"
#include "oppo_bq27541.h"

static struct chip_bq27541 *gauge_ic = NULL;
static DEFINE_MUTEX(bq27541_i2c_access);

/**********************************************************
  *
  *   [I2C Function For Read/Write bq27541]
  *
  *********************************************************/
int bq27541_read_i2c(int cmd, int *returnData)
{
        if (!gauge_ic->client) {
                pr_err(" gauge_ic->client NULL, return\n");
                return 0;
        }
        if (cmd == BQ27541_BQ27411_CMD_INVALID) {
                return 0;
        }

        mutex_lock(&bq27541_i2c_access);
        //gauge_ic->client->timing = 300;
        *returnData = i2c_smbus_read_word_data(gauge_ic->client, cmd);

        mutex_unlock(&bq27541_i2c_access);
        /*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData) ;*/
        if (*returnData < 0) {
                return 1;
        } else {
                return 0;
        }
}

int bq27541_i2c_txsubcmd(int cmd, int writeData)
{
        if (!gauge_ic->client) {
                pr_err(" gauge_ic->client NULL, return\n");
                return 0;
        }
        if (cmd == BQ27541_BQ27411_CMD_INVALID) {
                return 0;
        }

        mutex_lock(&bq27541_i2c_access);
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
        gauge_ic->client->timing = 300;
#endif
#endif
        i2c_smbus_write_word_data(gauge_ic->client, cmd, writeData);
        mutex_unlock(&bq27541_i2c_access);
        return 0;
}

static int bq27541_write_i2c_block(u8 cmd, u8 length, u8 *writeData)
{
		if (!gauge_ic->client) {
				pr_err(" gauge_ic->client NULL, return\n");
				return 0;
		}
		if (cmd == BQ27541_BQ27411_CMD_INVALID) {
				return 0;
		}

		mutex_lock(&bq27541_i2c_access);
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
		gauge_ic->client->timing = 300;
#endif
#endif
		i2c_smbus_write_i2c_block_data(gauge_ic->client, cmd, length, writeData);
		mutex_unlock(&bq27541_i2c_access);
		return 0;

}


static int bq27541_read_i2c_block(u8 cmd, u8 length, u8 *returnData)
{
	if(!gauge_ic->client) {
		pr_err(" gauge_ic->client NULL,return\n");
		return 0;
	}
	if(cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;

    mutex_lock(&bq27541_i2c_access);
#ifdef CONFIG_OPPO_CHARGER_MTK
    gauge_ic->client->timing = 300;
#endif
    i2c_smbus_read_i2c_block_data(gauge_ic->client, cmd, length, returnData);
    
    mutex_unlock(&bq27541_i2c_access); 
	//pr_err(" cmd = 0x%x, returnData = 0x%x\r\n",cmd,*returnData)  ; 
	return 0;
}


static int bq27541_read_i2c_onebyte(u8 cmd, u8 *returnData)
{
        if (!gauge_ic->client) {
                pr_err(" gauge_ic->client NULL, return\n");
                return 0;
        }
        if (cmd == BQ27541_BQ27411_CMD_INVALID) {
                return 0;
        }

        mutex_lock(&bq27541_i2c_access);
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
        gauge_ic->client->timing = 300;
#endif
#endif
        *returnData = i2c_smbus_read_byte_data(gauge_ic->client, cmd);

        mutex_unlock(&bq27541_i2c_access);
        /*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData) ; */
        if (*returnData < 0) {
                return 1;
        } else {
                return 0;
        }
}

static int bq27541_i2c_txsubcmd_onebyte(u8 cmd, u8 writeData)
{
        if (!gauge_ic->client) {
                pr_err(" gauge_ic->client NULL, return\n");
                return 0;
        }
        if (cmd == BQ27541_BQ27411_CMD_INVALID) {
                return 0;
        }
        mutex_lock(&bq27541_i2c_access);
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
        gauge_ic->client->timing = 300;
#endif
#endif
        i2c_smbus_write_byte_data(gauge_ic->client, cmd, writeData);
        mutex_unlock(&bq27541_i2c_access);
        return 0;
}


/* OPPO 2013-08-24 wangjc Add begin for add adc interface. */
static int bq27541_get_battery_cc(void)    /*  sjc20150105  */
{
	int ret = 0;
        int cc = 0;

        if (!gauge_ic) {
                return 0;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return gauge_ic->cc_pre;
        }
        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_cc, &cc);
                if (ret) {
                        dev_err(gauge_ic->dev, "error reading cc.\n");
                        return ret;
                }
        } else {
                if (gauge_ic->cc_pre) {
                        return gauge_ic->cc_pre;
                } else {
                        return 0;
                }
        }

        gauge_ic->cc_pre = cc;
        return cc;
}

static int bq27541_get_battery_fcc(void)        /*  sjc20150105  */
{
        int ret = 0;
        int fcc = 0;

        if (!gauge_ic) {
                return 0;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return gauge_ic->fcc_pre;
        }
        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_fcc, &fcc);
                if (ret) {
                        dev_err(gauge_ic->dev, "error reading fcc.\n");
                        return ret;
                }
        } else {
                if (gauge_ic->fcc_pre) {
                        return gauge_ic->fcc_pre;
                } else {
                        return 0;
                }
        }

        gauge_ic->fcc_pre = fcc;
        return fcc;
}

static int bq27541_get_battery_soh(void)        /*  sjc20150105  */
{
        int ret = 0;
        int soh = 0;

        if (!gauge_ic) {
                return 0;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return gauge_ic->soh_pre;
        }
        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_soh, &soh);
                if (ret) {
                        dev_err(gauge_ic->dev, "error reading fcc.\n");
                        return ret;
                }
        } else {
                if (gauge_ic->soh_pre) {
                        return gauge_ic->soh_pre;
                } else {
                        return 0;
                }
        }

        gauge_ic->soh_pre = soh;
        return soh;
}

static int bq27541_soc_calibrate(int soc)
{
        unsigned int soc_calib;
        /*int counter_temp = 0; */
/*
        if (!gauge_ic->batt_psy){
                gauge_ic->batt_psy = power_supply_get_by_name("battery");
                gauge_ic->soc_pre = soc;
        }
*/
        if (!gauge_ic) {
                return 0;
        }
        soc_calib = soc;

        if (soc >= 100) {
                soc_calib = 100;
        } else if (soc < 0) {
                soc_calib = 0;
        }
        gauge_ic->soc_pre = soc_calib;
        /*pr_info("soc:%d, soc_calib:%d\n", soc, soc_calib); */
        return soc_calib;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */

static void bq27541_cntl_cmd(int subcmd)
{
        bq27541_i2c_txsubcmd(BQ27541_BQ27411_REG_CNTL, subcmd);
}

static int bq27541_get_battery_mvolts(void)
{
        int ret = 0;
        int volt = 0;

        if (!gauge_ic) {
                return 0;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return gauge_ic->batt_vol_pre;
        }

        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_volt, &volt);
                if (ret) {
                        dev_err(gauge_ic->dev, "error reading voltage, ret:%d\n", ret);
						gauge_ic->batt_cell_max_vol = gauge_ic->max_vol_pre;
        				gauge_ic->batt_cell_min_vol = gauge_ic->min_vol_pre;
                        return gauge_ic->batt_vol_pre;
                }
			  	gauge_ic->batt_cell_max_vol = volt;
    			gauge_ic->batt_cell_min_vol = volt;
				gauge_ic->batt_vol_pre = volt;
				gauge_ic->max_vol_pre = gauge_ic->batt_cell_max_vol;
    			gauge_ic->min_vol_pre = gauge_ic->batt_cell_min_vol;
    			return volt;
        } else {
                return gauge_ic->batt_vol_pre;
        }

}

static int bq27541_get_battery_mvolts_2cell_max(void)
{
	if(!gauge_ic) {
		return 0;
	}
	
	return 3800;
}

static int bq27541_get_battery_mvolts_2cell_min(void)
{
	if(!gauge_ic) {
		return 0;
	}
	
	return 3800;
}


static int bq27541_get_battery_temperature(void)
{
        int ret = 0;
        int temp = 0;
        static int count = 0;

        if (!gauge_ic) {
                return 0;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
        }

        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_temp, &temp);
                if (ret) {
                        count++;
                        dev_err(gauge_ic->dev, "error reading temperature\n");
                        if (count > 1) {
                                count = 0;

                                gauge_ic->temp_pre = -400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
                                return -400;
                        } else {
                                return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
                        }
                }
                count = 0;
        } else {
                return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
        }
        gauge_ic->temp_pre = temp;
        return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27541_get_batt_remaining_capacity(void)
{
        int ret;
        int cap = 0;

        if (!gauge_ic) {
                return 0;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return gauge_ic->rm_pre;
        }
        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_rm, &cap);
                if (ret) {
                        dev_err(gauge_ic->dev, "error reading capacity.\n");
                        return ret;
                }
                gauge_ic->rm_pre = cap;
                return gauge_ic->rm_pre;
        } else {
                return gauge_ic->rm_pre;
        }
}

static int bq27541_get_battery_soc(void)
{
        int ret;
        int soc = 0;

        if (!gauge_ic) {
                return 50;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return gauge_ic->soc_pre;
        }

        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_soc, &soc);
                if (ret) {
                        dev_err(gauge_ic->dev, "error reading soc.ret:%d\n", ret);
                        goto read_soc_err;
                }
        } else {
                if (gauge_ic->soc_pre) {
                        return gauge_ic->soc_pre;
                } else {
                        return 0;
                }
        }

        soc = bq27541_soc_calibrate(soc);
        return soc;

read_soc_err:
        if (gauge_ic->soc_pre) {
                return gauge_ic->soc_pre;
        } else {
                return 0;
        }
}


static int bq27541_get_average_current(void)
{
        int ret;
        int curr = 0;

        if (!gauge_ic) {
                return 0;
        }
        if (atomic_read(&gauge_ic->suspended) == 1) {
                return -gauge_ic->current_pre;
        }

        if (oppo_vooc_get_allow_reading() == true) {
                ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_ai, &curr);
                if (ret) {
                        dev_err(gauge_ic->dev, "error reading current.\n");
                        return gauge_ic->current_pre;
                }
        } else {
                return -gauge_ic->current_pre;
        }
        /* negative current */
        if (curr&0x8000) {
                curr = -((~(curr-1))&0xFFFF);
        }
        gauge_ic->current_pre = curr;
        return -curr;
}

static bool bq27541_get_battery_authenticate(void)
{
	return true;
}

static int bq27541_get_prev_battery_mvolts(void)
{
	if (!gauge_ic) {
		return 0;
	}

	return gauge_ic->batt_vol_pre;
}

static int bq27541_get_prev_battery_mvolts_2cell_max(void)
{
	if (!gauge_ic) {
		return 3800;
	}

	return 3800;
}

static int bq27541_get_prev_battery_mvolts_2cell_min(void)
{
	if (!gauge_ic) {
		return 3800;
	}

	return 3800;
}

static int bq27541_get_prev_battery_temperature(void)
{
	if (!gauge_ic) {
		return 0;
	}

	return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27541_get_prev_battery_soc(void)
{
	if (!gauge_ic) {
		return 50;
	}

	return gauge_ic->soc_pre;
}

static int bq27541_get_prev_average_current(void)
{
	if (!gauge_ic) {
		return 0;
	}

	return -gauge_ic->current_pre;
}
static void bq27541_set_battery_full(bool full)
{
        /* Do nothing */
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip);

static int bq28z610_modify_dod0(void)
{
	if (!gauge_ic) {
			return 0;
	}
	
	if(gauge_ic->batt_bq28z610){
		bq28z610_modify_dod0_parameter(gauge_ic);
	}
	return 0;
}

static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip);

static int  bq28z610_update_soc_smooth_parameter(void)
{

	if (!gauge_ic) {
		return -1;
	}

	if(gauge_ic->batt_bq28z610){
		bq28z610_modify_soc_smooth_parameter(gauge_ic);

	}
	return 0; 
}

static struct oppo_gauge_operations bq27541_gauge_ops = {
        .get_battery_mvolts                	= bq27541_get_battery_mvolts,
        .get_battery_temperature        	= bq27541_get_battery_temperature,
        .get_batt_remaining_capacity 		= bq27541_get_batt_remaining_capacity,
        .get_battery_soc                    = bq27541_get_battery_soc,
        .get_average_current                = bq27541_get_average_current,
        .get_battery_fcc                    = bq27541_get_battery_fcc,
        .get_battery_cc                     = bq27541_get_battery_cc,
        .get_battery_soh                    = bq27541_get_battery_soh,
        .get_battery_authenticate        	= bq27541_get_battery_authenticate,
        .set_battery_full                   = bq27541_set_battery_full,
	    .get_prev_battery_mvolts    		= bq27541_get_prev_battery_mvolts,
	    .get_prev_battery_temperature 		= bq27541_get_prev_battery_temperature,
        .get_prev_battery_soc				= bq27541_get_prev_battery_soc,
	    .get_prev_average_current			= bq27541_get_prev_average_current,
	    .get_battery_mvolts_2cell_max		= bq27541_get_battery_mvolts_2cell_max,
		.get_battery_mvolts_2cell_min		= bq27541_get_battery_mvolts_2cell_min,
		.get_prev_battery_mvolts_2cell_max		= bq27541_get_prev_battery_mvolts_2cell_max,
		.get_prev_battery_mvolts_2cell_min		= bq27541_get_prev_battery_mvolts_2cell_min,
		.update_battery_dod0				= bq28z610_modify_dod0,
		.update_soc_smooth_parameter		= bq28z610_update_soc_smooth_parameter,
};

static void gauge_set_cmd_addr(struct chip_bq27541 *chip, int device_type)
{
        if (device_type == DEVICE_BQ27541) {
                chip->cmd_addr.reg_cntl = BQ27541_BQ27411_REG_CNTL;
                chip->cmd_addr.reg_temp = BQ27541_REG_TEMP;
                chip->cmd_addr.reg_volt = BQ27541_REG_VOLT;
                chip->cmd_addr.reg_flags = BQ27541_REG_FLAGS;
                chip->cmd_addr.reg_nac = BQ27541_REG_NAC;
                chip->cmd_addr.reg_fac = BQ27541_REG_FAC;
                chip->cmd_addr.reg_rm = BQ27541_REG_RM;
                chip->cmd_addr.reg_fcc = BQ27541_REG_FCC;
                chip->cmd_addr.reg_ai = BQ27541_REG_AI;
                chip->cmd_addr.reg_si = BQ27541_REG_SI;
                chip->cmd_addr.reg_mli = BQ27541_REG_MLI;
                chip->cmd_addr.reg_ap = BQ27541_REG_AP;
                chip->cmd_addr.reg_soc = BQ27541_REG_SOC;
                chip->cmd_addr.reg_inttemp = BQ27541_REG_INTTEMP;
                chip->cmd_addr.reg_soh = BQ27541_REG_SOH;
                chip->cmd_addr.flag_dsc = BQ27541_FLAG_DSC;
                chip->cmd_addr.flag_fc = BQ27541_FLAG_FC;
                chip->cmd_addr.cs_dlogen = BQ27541_CS_DLOGEN;
                chip->cmd_addr.cs_ss = BQ27541_CS_SS;

                chip->cmd_addr.reg_ar = BQ27541_REG_AR;
                chip->cmd_addr.reg_artte = BQ27541_REG_ARTTE;
                chip->cmd_addr.reg_tte = BQ27541_REG_TTE;
                chip->cmd_addr.reg_ttf = BQ27541_REG_TTF;
                chip->cmd_addr.reg_stte = BQ27541_REG_STTE;
                chip->cmd_addr.reg_mltte = BQ27541_REG_MLTTE;
                chip->cmd_addr.reg_ae = BQ27541_REG_AE;
                chip->cmd_addr.reg_ttecp = BQ27541_REG_TTECP;
                chip->cmd_addr.reg_cc = BQ27541_REG_CC;
                chip->cmd_addr.reg_nic = BQ27541_REG_NIC;
                chip->cmd_addr.reg_icr = BQ27541_REG_ICR;
                chip->cmd_addr.reg_logidx = BQ27541_REG_LOGIDX;
                chip->cmd_addr.reg_logbuf = BQ27541_REG_LOGBUF;
                chip->cmd_addr.reg_dod0 = BQ27541_REG_DOD0;

                chip->cmd_addr.subcmd_cntl_status = BQ27541_SUBCMD_CTNL_STATUS;
                chip->cmd_addr.subcmd_device_type = BQ27541_SUBCMD_DEVCIE_TYPE;
                chip->cmd_addr.subcmd_fw_ver = BQ27541_SUBCMD_FW_VER;
                chip->cmd_addr.subcmd_dm_code = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_prev_macw = BQ27541_SUBCMD_PREV_MACW;
                chip->cmd_addr.subcmd_chem_id = BQ27541_SUBCMD_CHEM_ID;
                chip->cmd_addr.subcmd_set_hib = BQ27541_SUBCMD_SET_HIB;
                chip->cmd_addr.subcmd_clr_hib = BQ27541_SUBCMD_CLR_HIB;
                chip->cmd_addr.subcmd_set_cfg = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_sealed = BQ27541_SUBCMD_SEALED;
                chip->cmd_addr.subcmd_reset = BQ27541_SUBCMD_RESET;
                chip->cmd_addr.subcmd_softreset = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_exit_cfg = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_enable_dlog = BQ27541_SUBCMD_ENABLE_DLOG;
                chip->cmd_addr.subcmd_disable_dlog = BQ27541_SUBCMD_DISABLE_DLOG;
                chip->cmd_addr.subcmd_enable_it = BQ27541_SUBCMD_ENABLE_IT;
                chip->cmd_addr.subcmd_disable_it = BQ27541_SUBCMD_DISABLE_IT;

                chip->cmd_addr.subcmd_hw_ver = BQ27541_SUBCMD_HW_VER;
                chip->cmd_addr.subcmd_df_csum = BQ27541_SUBCMD_DF_CSUM;
                chip->cmd_addr.subcmd_bd_offset = BQ27541_SUBCMD_BD_OFFSET;
                chip->cmd_addr.subcmd_int_offset = BQ27541_SUBCMD_INT_OFFSET;
                chip->cmd_addr.subcmd_cc_ver = BQ27541_SUBCMD_CC_VER;
                chip->cmd_addr.subcmd_ocv = BQ27541_SUBCMD_OCV;
                chip->cmd_addr.subcmd_bat_ins = BQ27541_SUBCMD_BAT_INS;
                chip->cmd_addr.subcmd_bat_rem = BQ27541_SUBCMD_BAT_REM;
                chip->cmd_addr.subcmd_set_slp = BQ27541_SUBCMD_SET_SLP;
                chip->cmd_addr.subcmd_clr_slp = BQ27541_SUBCMD_CLR_SLP;
                chip->cmd_addr.subcmd_fct_res = BQ27541_SUBCMD_FCT_RES;
                chip->cmd_addr.subcmd_cal_mode = BQ27541_SUBCMD_CAL_MODE;
        } else {                /*device_bq27411*/
                chip->cmd_addr.reg_cntl = BQ27411_REG_CNTL;
                chip->cmd_addr.reg_temp = BQ27411_REG_TEMP;
                chip->cmd_addr.reg_volt = BQ27411_REG_VOLT;
                chip->cmd_addr.reg_flags = BQ27411_REG_FLAGS;
                chip->cmd_addr.reg_nac = BQ27411_REG_NAC;
                chip->cmd_addr.reg_fac = BQ27411_REG_FAC;
                chip->cmd_addr.reg_rm = BQ27411_REG_RM;
                chip->cmd_addr.reg_fcc = BQ27411_REG_FCC;
                chip->cmd_addr.reg_ai = BQ27411_REG_AI;
                chip->cmd_addr.reg_si = BQ27411_REG_SI;
                chip->cmd_addr.reg_mli = BQ27411_REG_MLI;
                chip->cmd_addr.reg_ap = BQ27411_REG_AP;
                chip->cmd_addr.reg_soc = BQ27411_REG_SOC;
                chip->cmd_addr.reg_inttemp = BQ27411_REG_INTTEMP;
                chip->cmd_addr.reg_soh = BQ27411_REG_SOH;
                chip->cmd_addr.flag_dsc = BQ27411_FLAG_DSC;
                chip->cmd_addr.flag_fc = BQ27411_FLAG_FC;
                chip->cmd_addr.cs_dlogen = BQ27411_CS_DLOGEN;
                chip->cmd_addr.cs_ss = BQ27411_CS_SS;
                /*bq27541 external standard cmds*/
                chip->cmd_addr.reg_ar = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_artte = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_tte = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_ttf = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_stte = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_mltte = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_ae = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_ttecp = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_cc = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_nic = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_icr = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_logidx = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_logbuf = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.reg_dod0 = BQ27541_BQ27411_CMD_INVALID;

                chip->cmd_addr.subcmd_cntl_status = BQ27411_SUBCMD_CNTL_STATUS;
                chip->cmd_addr.subcmd_device_type = BQ27411_SUBCMD_DEVICE_TYPE;
                chip->cmd_addr.subcmd_fw_ver = BQ27411_SUBCMD_FW_VER;
                chip->cmd_addr.subcmd_dm_code = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_prev_macw = BQ27411_SUBCMD_PREV_MACW;
                chip->cmd_addr.subcmd_chem_id = BQ27411_SUBCMD_CHEM_ID;
                chip->cmd_addr.subcmd_set_hib = BQ27411_SUBCMD_SET_HIB;
                chip->cmd_addr.subcmd_clr_hib = BQ27411_SUBCMD_CLR_HIB;
                chip->cmd_addr.subcmd_set_cfg = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_sealed = BQ27411_SUBCMD_SEALED;
                chip->cmd_addr.subcmd_reset = BQ27411_SUBCMD_RESET;
                chip->cmd_addr.subcmd_softreset = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_exit_cfg = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_enable_dlog = BQ27411_SUBCMD_ENABLE_DLOG;
                chip->cmd_addr.subcmd_disable_dlog = BQ27411_SUBCMD_DISABLE_DLOG;
                chip->cmd_addr.subcmd_enable_it = BQ27411_SUBCMD_ENABLE_IT;
                chip->cmd_addr.subcmd_disable_it = BQ27411_SUBCMD_DISABLE_IT;
                /*bq27541 external sub cmds*/
                chip->cmd_addr.subcmd_hw_ver = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_df_csum = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_bd_offset = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_int_offset = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_cc_ver = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_ocv = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_bat_ins = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_bat_rem = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_set_slp = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_clr_slp = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_fct_res = BQ27541_BQ27411_CMD_INVALID;
                chip->cmd_addr.subcmd_cal_mode = BQ27541_BQ27411_CMD_INVALID;
        }
}

static void bq27541_hw_config(struct chip_bq27541 *chip)
{
        int ret = 0, flags = 0, device_type = 0, fw_ver = 0;

        bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
        udelay(66);
        bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &flags);
        udelay(66);
        ret = bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &flags);
        if (ret < 0) {
                chip->device_type = DEVICE_BQ27541;
                pr_err(" error reading register %02x ret = %d\n",
                          BQ27541_BQ27411_REG_CNTL, ret);
                return;
        }
        udelay(66);
        bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
        udelay(66);
        bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_DEVICE_TYPE);
        udelay(66);
        bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &device_type);

        udelay(66);
        bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
        udelay(66);
        bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_FW_VER);
        udelay(66);
        bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &fw_ver);

        if (device_type == DEVICE_TYPE_BQ27411) {
                chip->device_type = DEVICE_BQ27411;
        } else {
                chip->device_type = DEVICE_BQ27541;
                bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
                udelay(66);
                bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_ENABLE_IT);
        }
		
        gauge_set_cmd_addr(chip, chip->device_type);
		
		if (device_type == DEVICE_TYPE_BQ28Z610) {
			chip->cmd_addr.reg_ai = Bq28Z610_REG_TI;
		}
        dev_err(chip->dev, "DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X\n",
                        device_type, fw_ver);
}

static void bq27541_parse_dt(struct chip_bq27541 *chip)
{
        struct device_node *node = chip->dev->of_node;

        chip->modify_soc_smooth = of_property_read_bool(node, "qcom,modify-soc-smooth");
		chip->batt_bq28z610 = of_property_read_bool(node, "qcom,batt_bq28z610");
}

static int sealed(void)
{
        /*    return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
        int value = 0;

        bq27541_cntl_cmd(CONTROL_STATUS);
        /*    bq27541_cntl_cmd(di, CONTROL_STATUS);*/
        usleep_range(10000, 10000);
        bq27541_read_i2c(CONTROL_STATUS, &value);
        /*    chg_debug(" REG_CNTL: 0x%x\n", value); */

        if (gauge_ic->device_type == DEVICE_BQ27541) {
                return value & BIT(14);
        } else if (gauge_ic->device_type == DEVICE_BQ27411) {
                return value & BIT(13);
        } else {
                return 1;
        }
}

static int seal(void)
{
        int i = 0;

        if (sealed()) {
                pr_err("bq27541/27411 sealed, return\n");
                return 1;
        }
        bq27541_cntl_cmd(SEAL_SUBCMD);
        usleep_range(10000, 10000);
        for (i = 0;i < SEAL_POLLING_RETRY_LIMIT;i++) {
                if (sealed()) {
                        return 1;
                }
                usleep_range(10000, 10000);
        }
        return 0;
}


static int unseal(u32 key)
{
        int i = 0;

        if (!sealed()) {
                goto out;
        }
        if (gauge_ic->device_type == DEVICE_BQ27541) {
                /*    bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);*/
                bq27541_cntl_cmd(0x1115);
                usleep_range(10000, 10000);
                /*    bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
                bq27541_cntl_cmd(0x1986);
                usleep_range(10000, 10000);
        }
        else if (gauge_ic->device_type == DEVICE_BQ27411) {
                /*    bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);*/
                bq27541_cntl_cmd(0x8000);
                usleep_range(10000, 10000);
                /*    bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
                bq27541_cntl_cmd(0x8000);
                usleep_range(10000, 10000);
        }
        bq27541_cntl_cmd(0xffff);
        usleep_range(10000, 10000);
        bq27541_cntl_cmd(0xffff);
        usleep_range(10000, 10000);

        while (i < SEAL_POLLING_RETRY_LIMIT) {
                i++;
                if (!sealed()) {
                        break;
                }
                usleep_range(10000, 10000);
        }

out:
        chg_debug("bq27541 : i=%d\n", i);

        if (i == SEAL_POLLING_RETRY_LIMIT) {
                pr_err("bq27541 failed\n");
                return 0;
        } else {
                return 1;
        }
}

static int bq27411_write_block_data_cmd(struct chip_bq27541 *chip,
                                int block_id, u8 reg_addr, u8 new_value)
{
        int rc = 0;
        u8 old_value = 0, old_csum = 0, new_csum = 0;
        /*u8 new_csum_test = 0, csum_temp = 0;*/

        usleep_range(1000, 1000);
        bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, block_id);
        usleep_range(10000, 10000);
        rc = bq27541_read_i2c_onebyte(reg_addr, &old_value);
        if (rc) {
                pr_err("%s read reg_addr = 0x%x fail\n", __func__, reg_addr);
                return 1;
        }
        if (old_value == new_value) {
                return 0;
        }
        usleep_range(1000, 1000);
        rc = bq27541_read_i2c_onebyte(BQ27411_CHECKSUM_ADDR, &old_csum);
        if (rc) {
                pr_err("%s read checksum fail\n", __func__);
                return 1;
        }
        usleep_range(1000, 1000);
        bq27541_i2c_txsubcmd_onebyte(reg_addr, new_value);
        usleep_range(1000, 1000);
        new_csum = (old_value + old_csum - new_value) & 0xff;
/*
        csum_temp = (255 - old_csum - old_value) % 256;
        new_csum_test = 255 - ((csum_temp + new_value) % 256);
*/
        usleep_range(1000, 1000);
        bq27541_i2c_txsubcmd_onebyte(BQ27411_CHECKSUM_ADDR, new_csum);
        pr_err("bq27411 write blk_id = 0x%x, addr = 0x%x, old_val = 0x%x, new_val = 0x%x, old_csum = 0x%x, new_csum = 0x%x\n",
                block_id, reg_addr, old_value, new_value, old_csum, new_csum);
        return 0;
}

static int bq27411_read_block_data_cmd(struct chip_bq27541 *chip,
                                int block_id, u8 reg_addr)
{
        u8 value = 0;

        usleep_range(1000, 1000);
        bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, block_id);
        usleep_range(10000, 10000);
        bq27541_read_i2c_onebyte(reg_addr, &value);
        return value;
}

static int bq27411_enable_config_mode(struct chip_bq27541 *chip, bool enable)
{
        int config_mode = 0, i = 0, rc = 0;

        if (enable) {                /*enter config mode*/
                usleep_range(1000, 1000);
                bq27541_cntl_cmd(BQ27411_SUBCMD_SET_CFG);
                usleep_range(1000, 1000);
                for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
                        i++;
                        rc = bq27541_read_i2c(BQ27411_SUBCMD_CONFIG_MODE, &config_mode);
                        if (rc < 0) {
                                pr_err("%s i2c read error\n", __func__);
                                return 1;
                        }
                        if (config_mode & BIT(4)) {
                                break;
                        }
                        msleep(50);
                }
        } else {                /* exit config mode */
                usleep_range(1000, 1000);
                bq27541_cntl_cmd(BQ27411_SUBCMD_EXIT_CFG);
                usleep_range(1000, 1000);
                for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
                        i++;
                        rc = bq27541_read_i2c(BQ27411_SUBCMD_CONFIG_MODE, &config_mode);
                        if (rc < 0) {
                                pr_err("%s i2c read error\n", __func__);
                                return 1;
                        }
                        if ((config_mode & BIT(4)) == 0) {
                                break;
                        }
                        msleep(50);
                }
        }
        if (i == BQ27411_CONFIG_MODE_POLLING_LIMIT) {
                pr_err("%s fail config_mode = 0x%x, enable = %d\n", __func__, config_mode, enable);
                return 1;
        } else {
                pr_err("%s success i = %d, config_mode = 0x%x, enable = %d\n",
                        __func__, i, config_mode, enable);
                return 0;
        }
}

static bool bq27411_check_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
        int value_read = 0;
        u8 dead_band_val = 0, op_cfgb_val = 0, dodat_val = 0, rc = 0;

        return true;        /*not check because it costs 5.5 seconds */

        msleep(4000);
        if (sealed()) {
                if (!unseal(BQ27411_UNSEAL_KEY)) {
                        return false;
                } else {
                        msleep(50);
                }
        }

        if (is_powerup) {
                dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
                op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
                dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
        } else {        /*shutdown*/
                dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
                op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
                dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
        }
        rc = bq27411_enable_config_mode(chip, true);
        if (rc) {
                pr_err("%s enable config mode fail\n", __func__);
                return false;
        }
        /*enable block data control */
        rc = bq27541_i2c_txsubcmd_onebyte(BQ27411_BLOCK_DATA_CONTROL, 0x00);
        if (rc) {
                pr_err("%s enable block data control fail\n", __func__);
                goto check_error;
        }
        usleep_range(5000, 5000);

        /*check cc-dead-band*/
        value_read = bq27411_read_block_data_cmd(chip,
                                                BQ27411_CC_DEAD_BAND_ID, BQ27411_CC_DEAD_BAND_ADDR);
        if (value_read != dead_band_val) {
                pr_err("%s cc_dead_band error, value_read = 0x%x\n", __func__, value_read);
                goto check_error;
        }

        /*check opconfigB*/
        value_read = bq27411_read_block_data_cmd(chip,
                                                BQ27411_OPCONFIGB_ID, BQ27411_OPCONFIGB_ADDR);
        if (value_read != op_cfgb_val) {
                pr_err("%s opconfigb error, value_read = 0x%x\n", __func__, value_read);
                goto check_error;
        }

        /*check dodateoc*/
        value_read = bq27411_read_block_data_cmd(chip,
                                                BQ27411_DODATEOC_ID, BQ27411_DODATEOC_ADDR);
        if (value_read != dodat_val) {
                pr_err("%s dodateoc error, value_read = 0x%x\n", __func__, value_read);
                goto check_error;
        }
        bq27411_enable_config_mode(chip, false);
        return true;

check_error:
        bq27411_enable_config_mode(chip, false);
        return false;
}

static int bq27411_write_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
        int rc = 0;
        u8 dead_band_val = 0, op_cfgb_val = 0, dodat_val = 0;

        if (is_powerup) {
                dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
                op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
                dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
        } else {        /*shutdown */
                dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
                op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
                dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
        }

        /*enter config mode */
        rc = bq27411_enable_config_mode(chip, true);
        if (rc) {
                pr_err("%s enable config mode fail\n", __func__);
                return 1;
        }
        /*enable block data control */
        bq27541_i2c_txsubcmd_onebyte(BQ27411_BLOCK_DATA_CONTROL, 0x00);

        usleep_range(5000, 5000);
        /* step1: update cc-dead-band */
        rc = bq27411_write_block_data_cmd(chip, BQ27411_CC_DEAD_BAND_ID,
                        BQ27411_CC_DEAD_BAND_ADDR, dead_band_val);
        if (rc) {
                pr_err("%s cc_dead_band fail\n", __func__);
                goto exit_config_mode;
        }
        /* step2: update opconfigB */
        rc = bq27411_write_block_data_cmd(chip, BQ27411_OPCONFIGB_ID,
                        BQ27411_OPCONFIGB_ADDR, op_cfgb_val);
        if (rc) {
                pr_err("%s opconfigB fail\n", __func__);
                goto exit_config_mode;
        }
        /* step3: update dodateoc */
        rc = bq27411_write_block_data_cmd(chip, BQ27411_DODATEOC_ID,
                        BQ27411_DODATEOC_ADDR, dodat_val);
        if (rc) {
                pr_err("%s dodateoc fail\n", __func__);
                goto exit_config_mode;
        }
        bq27411_enable_config_mode(chip, false);
        return 0;

exit_config_mode:
        bq27411_enable_config_mode(chip, false);
        return 1;
}

static void bq27411_modify_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
        int rc = 0;
        bool check_result = false, tried_again = false;

        if (chip->modify_soc_smooth == false || chip->device_type == DEVICE_BQ27541) {
                return;
        }

        pr_err("%s begin\n", __func__);
        if (sealed()) {
                if (!unseal(BQ27411_UNSEAL_KEY)) {
                        return;
                } else {
                        msleep(50);
                }
        }
write_parameter:
        rc = bq27411_write_soc_smooth_parameter(chip, is_powerup);
        if (rc && tried_again == false) {
                tried_again = true;
                goto write_parameter;
        } else {
                check_result = bq27411_check_soc_smooth_parameter(chip, is_powerup);
                if (check_result == false && tried_again == false) {
                        tried_again = true;
                        goto write_parameter;
                }
        }

        usleep_range(1000, 1000);
        if (sealed() == 0) {
                usleep_range(1000, 1000);
                seal();
        }
        pr_err("%s end\n", __func__);
}

static int bq8z610_sealed(void)
{
        int value = 0;
		u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
		
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, BQ28Z610_SEAL_STATUS);

        usleep_range(10000, 10000);

		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
		pr_err("%s bq8z610_sealed CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n", __func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);

		value = (CNTL1_VAL[3] & BQ28Z610_SEAL_BIT);
		if(value == BQ28Z610_SEAL_VALUE)
		{
			pr_err("bq8z610 sealed, value = %x return 1\n",value);
			return 1;
		}
		else	
		{
			pr_err("bq8z610 sealed, value = %x return 0\n",value);
			return 0;
		}
}

static int bq8z610_seal(void)
{
        int i = 0;

        if (bq8z610_sealed()) {
                pr_err("bq8z610 sealed, return\n");
                return 1;
        }
		bq27541_i2c_txsubcmd(0, BQ28Z610_SEAL_SUBCMD);
        //usleep_range(10000, 10000);
        msleep(1000);
        for (i = 0;i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT;i++) {
                if (bq8z610_sealed()) {
                    return 1;
                }
				//bq27541_i2c_txsubcmd(0, BQ28Z610_SEAL_SUBCMD);
                usleep_range(10000, 10000);
        }
        return 0;
}


static int bq8z610_unseal(void)
{
        int i = 0;

        if (!bq8z610_sealed()) {
                goto out;
        }
		bq27541_i2c_txsubcmd(0, BQ28Z610_UNSEAL_SUBCMD1);
        usleep_range(10000, 10000);
		//msleep(100);
		bq27541_i2c_txsubcmd(0, BQ28Z610_UNSEAL_SUBCMD2);
        //usleep_range(10000, 10000);
		msleep(1000);


        while (i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
            i++;
            if (!bq8z610_sealed()) {
                    break;
            }
			usleep_range(10000, 10000);
        }

out:
        chg_debug("bq8z610 : i=%d\n", i);

        if (i == SEAL_POLLING_RETRY_LIMIT) {
                pr_err("bq8z610 unseal failed\n");
                return 0;
        } else {
                return 1;
        }
}



static int bq28z610_write_soc_smooth_parameter(struct chip_bq27541 *chip)
{
        //int rc = 0;
		u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};

		u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = {0xF4,0x46,0xdC,0x00};
		//u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = {0x08,0x47,0x78,0x00};//120ma
		u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = {0x08,0x47,0x96,0x00};//150ma
		u8 CNTL1_write3[BQ28Z610_REG_CNTL1_SIZE] = {0x0C,0x47,0x28,0x00};
		
		bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write1[0]);
		msleep(100);

		//bq8z610_cntl2_cmd(0x06E9);
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x06E9);
        //usleep_range(10000, 5000);
		msleep(100);

		//bq8z610_cntl1_cmd(0x46F4);
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x46F4);
        //usleep_range(5000, 5000);
		msleep(100);

		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
		pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n", __func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x00DC))
		{
			pr_err("%s Charge Term Taper Current 150mA (=0x0096) -> 220mA (=0x00DC). ERR.\n", __func__);
			return -1;
		}
		else
		{
			pr_err("%s Charge Term Taper Current  (=0x0096) -> 220mA (=0x00DC). OK\n", __func__);
		}

		bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write2[0]);
		msleep(100);

		//bq8z610_cntl2_cmd(0x06E9);
		//bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0638);//120ma
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x061A);//150ma
        //usleep_range(5000, 5000);
        msleep(100);

		//bq8z610_cntl1_cmd(0x46F4);
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x4708);
        //usleep_range(5000, 5000);
		msleep(100);

		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
		pr_err("%s Dsg Current Threshold CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n", __func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		//if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0078))//120ma
		if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096))
		{
			pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) ERR.\n", __func__);
			return -1;
		}
		else
		{
			pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) OK\n", __func__);
		}
				
		bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write3[0]);
		msleep(100);

		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0684);
		//usleep_range(5000, 5000);
		msleep(100);

		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x470C);
		//usleep_range(5000, 5000);
		msleep(100);

		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
		pr_err("%s Quit Current CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n", __func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x470C) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0028))
		{
			pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). ERR.\n", __func__);
			return -1;
		}
		else
		{
			pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). OK\n", __func__);
		}
		return 0;
		
}


static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip)
{
        int rc = 0;
        bool tried_again = false;


        pr_err("%s begin\n", __func__);
        if (bq8z610_sealed()) {
                if (!bq8z610_unseal()) {
                        return;
                } else {
                        msleep(50);
                }
        }
write_parameter:
        rc = bq28z610_write_soc_smooth_parameter(chip);
        if (rc && tried_again == false) {
                tried_again = true;
                goto write_parameter;
        }

        usleep_range(1000, 1000);
        if (bq8z610_sealed() == 0) {
                usleep_range(1000, 1000);
                bq8z610_seal();
        }
        pr_err("%s end\n", __func__);
}


static int bq8z610_check_gauge_enable(void)
{
        /*    return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
        int value = 0;
		u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
		
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, BQ28Z610_REG_GAUGE_EN);

        //usleep_range(10000, 10000);
		msleep(1000);

		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
		pr_err("%s  CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n", __func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);

		value = (CNTL1_VAL[2] & BQ28Z610_GAUGE_EN_BIT);
		if(value == BQ28Z610_GAUGE_EN_BIT)
		{
			pr_err("bq8z610 gauge_enable, value = %x return 1\n",value);
			return 1;
		}
		else	
		{
			pr_err("bq8z610 gauge_enable, value = %x return 0\n",value);
			return 0;
		}
}

static int bq28z610_write_dod0_parameter(struct chip_bq27541 *chip)
{

		//bq8z610_cntl1_cmd(0x46F4);
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021);
        //usleep_range(5000, 5000);
		msleep(1000);

		//bq8z610_cntl1_cmd(0x00DC);
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021);
        //usleep_range(5000, 5000);
		msleep(2000);

		if(bq8z610_check_gauge_enable() == false)
		{
			//bq8z610_cntl1_cmd(0x00DC);
			bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021);
	        //usleep_range(5000, 5000);
			msleep(300);
		}
		return 0;		
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip)
{
        int rc = 0;

        pr_err("%s begin\n", __func__);
        if (bq8z610_sealed()) {
                if (!bq8z610_unseal()) {
                        return;
                } else {
                        msleep(50);
                }
        }
        rc = bq28z610_write_dod0_parameter(chip);
		
        usleep_range(1000, 1000);
        if (bq8z610_sealed() == 0) {
                usleep_range(1000, 1000);
                bq8z610_seal();
        }
        pr_err("%s end\n", __func__);
}

static void register_gauge_devinfo(struct chip_bq27541 *chip)
{
        int ret = 0;
        char *version;
        char *manufacture;

        switch (chip->device_type) {
        case DEVICE_BQ27541:
                version = "bq27541";
                manufacture = "TI";
                break;
        case DEVICE_BQ27411:
                version = "bq27411";
                manufacture = "TI";
                break;
        default:
                version = "unknown";
                manufacture = "UNKNOWN";
                break;
        }
        ret = register_device_proc("gauge", version, manufacture);
        if (ret) {
                pr_err("register_gauge_devinfo fail\n");
        }
}

static void bq27541_reset(struct i2c_client *client)
{
        int ui_soc = oppo_chg_get_ui_soc();

        if (bq27541_gauge_ops.get_battery_mvolts() <= 3300
                        && bq27541_gauge_ops.get_battery_mvolts() > 2500
                        && ui_soc == 0
                        && bq27541_gauge_ops.get_battery_temperature() > 150) {
                if (!unseal(BQ27541_UNSEAL_KEY)) {
                        pr_err("bq27541 unseal fail !\n");
                        return;
                }
                chg_debug("bq27541 unseal OK vol = %d, ui_soc = %d, temp = %d!\n", bq27541_gauge_ops.get_battery_mvolts(),
                    ui_soc, bq27541_gauge_ops.get_battery_temperature());

                if (gauge_ic->device_type == DEVICE_BQ27541) {
                        bq27541_cntl_cmd(BQ27541_RESET_SUBCMD);
                } else if (gauge_ic->device_type == DEVICE_BQ27411) {
                        bq27541_cntl_cmd(BQ27411_RESET_SUBCMD);  /*  27411  */
                }
                msleep(50);

                if (gauge_ic->device_type == DEVICE_BQ27411) {
                        if (!seal()) {
                                pr_err("bq27411 seal fail\n");
                        }
                }
                msleep(150);
                chg_debug("bq27541_reset, point = %d\r\n", bq27541_gauge_ops.get_battery_soc());
        } else if (gauge_ic) {
                bq27411_modify_soc_smooth_parameter(gauge_ic, false);
        }
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int bq27541_pm_resume(struct device *dev)
{
        if (!gauge_ic) {
                return 0;
        }
        atomic_set(&gauge_ic->suspended, 0);
        bq27541_get_battery_soc();

        return 0;
}

static int bq27541_pm_suspend(struct device *dev)
{
        if (!gauge_ic) {
                return 0;
        }
        atomic_set(&gauge_ic->suspended, 1);

        return 0;
}

static const struct dev_pm_ops bq27541_pm_ops = {
        .resume                = bq27541_pm_resume,
        .suspend                = bq27541_pm_suspend,
};
#else /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/
static int bq27541_resume(struct i2c_client *client)
{
        if (!gauge_ic) {
                return 0;
        }
        atomic_set(&gauge_ic->suspended, 0);
        bq27541_get_battery_soc();
        return 0;
}

static int bq27541_suspend(struct i2c_client *client, pm_message_t mesg)
{
        if (!gauge_ic) {
                return 0;
        }
        atomic_set(&gauge_ic->suspended, 1);
        return 0;
}
#endif /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/

bool oppo_gauge_ic_chip_is_null(void)
{
        if (!gauge_ic) {
                return true;
        } else {
                return false;
        }
}

static int bq27541_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
        struct chip_bq27541 *fg_ic;
        struct oppo_gauge_chip        *chip;

        fg_ic = kzalloc(sizeof(*fg_ic), GFP_KERNEL);
        if (!fg_ic) {
                dev_err(&client->dev, "failed to allocate device info data\n");
                return -ENOMEM;
        }

        i2c_set_clientdata(client, fg_ic);
        fg_ic->dev = &client->dev;
        fg_ic->client = client;
        atomic_set(&fg_ic->suspended, 0);
		gauge_ic = fg_ic;
        bq27541_parse_dt(fg_ic);
        bq27541_hw_config(fg_ic);
/*
        INIT_DELAYED_WORK(&fg_ic->hw_config, bq27541_hw_config);
        schedule_delayed_work(&fg_ic->hw_config, 0);
*/
        fg_ic->soc_pre = 50;
		if(fg_ic->batt_bq28z610){
				fg_ic->batt_vol_pre = 3800;
		}
		else{
				fg_ic->batt_vol_pre = 3800;
		}

		fg_ic->max_vol_pre = 3800;
		fg_ic->min_vol_pre = 3800;
			      
        fg_ic->current_pre = 999;

        bq27411_modify_soc_smooth_parameter(fg_ic, true);

        chip = devm_kzalloc(&client->dev,
                        sizeof(struct oppo_gauge_chip), GFP_KERNEL);
        if (!chip) {
                pr_err("kzalloc() failed.\n");
                gauge_ic = NULL;
                return -ENOMEM;
        }

        chip->client = client;
        chip->dev = &client->dev;
        chip->gauge_ops = &bq27541_gauge_ops;
        chip->device_type = gauge_ic->device_type;
        oppo_gauge_init(chip);
        register_gauge_devinfo(fg_ic);

        chg_debug(" success\n");
        return 0;
}
/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/


static const struct of_device_id bq27541_match[] = {
        { .compatible = "oppo,bq27541-battery"},
        { },
};

static const struct i2c_device_id bq27541_id[] = {
        { "bq27541-battery", 0},
        {},
};
MODULE_DEVICE_TABLE(i2c, bq27541_id);


static struct i2c_driver bq27541_i2c_driver = {
        .driver                = {
                .name = "bq27541-battery",
                .owner        = THIS_MODULE,
                .of_match_table = bq27541_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
                .pm                = &bq27541_pm_ops,
#endif
        },
        .probe                = bq27541_driver_probe,
        .shutdown        = bq27541_reset,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
        .resume         = bq27541_resume,
        .suspend        = bq27541_suspend,
#endif
        .id_table        = bq27541_id,
};
/*----------------------------------------------------------------------------*/
/*static void  bq27541_exit(void)
{
        i2c_del_driver(&bq27541_i2c_driver);
}*/
/*----------------------------------------------------------------------------*/

module_i2c_driver(bq27541_i2c_driver);
MODULE_DESCRIPTION("Driver for bq27541 charger chip");
MODULE_LICENSE("GPL v2");
