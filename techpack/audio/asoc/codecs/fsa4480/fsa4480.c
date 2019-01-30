/************************************************************************************
** File: - kernel/msm-4.9/techpack/audio/asoc/codecs/fsa4480/fsa4480.c
** VENDOR_EDIT
** Copyright (C), 2018-2020, OPPO Mobile Comm Corp., Ltd
**
** Description:
**     add driver for audio switch fsa4480
** Version: 1.0
** --------------------------- Revision History: --------------------------------
**      <author>                       <date>                  <desc>
** Le.Li@PSW.MM.AudioDriver    01/22/2018           creat this file
************************************************************************************/

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <sound/soc.h>

#include <soc/oppo/oppo_project.h>

#define NAME "fsa4480"
#define I2C_RETRY_DELAY		5 /* ms */
#define I2C_RETRIES		5
#define FSA4480_ERR_I2C	-1

#define 	FSA4480_DEVID		0x00
#define		FSA4480_OVPMSK		0x01
#define		FSA4480_OVPFLG		0x02
#define		FSA4480_OVPST		0x03
#define		FSA4480_SWEN		0x04
#define		FSA4480_SWSEL		0x05
#define		FSA4480_SWST0		0x06
#define		FSA4480_SWST1		0x07
#define		FSA4480_CNT_L		0x08
#define		FSA4480_CNT_R		0x09
#define		FSA4480_CNT_MIC		0x0A
#define		FSA4480_CNT_SEN		0x0B
#define		FSA4480_CNT_GND		0x0C
#define		FSA4480_TIM_R		0x0D
#define		FSA4480_TIM_MIC		0x0E
#define		FSA4480_TIM_SEN		0x0F
#define		FSA4480_TIM_GND		0x10
#define		FSA4480_ACC_DET		0x11
#define		FSA4480_FUN_EN		0x12
#define		FSA4480_RES_SET		0x13
#define		FSA4480_RES_VAL		0x14
#define		FSA4480_RES_THR		0x15
#define		FSA4480_RES_INV		0x16
#define		FSA4480_JACTYP		0x17
#define		FSA4480_DECINT		0x18
#define		FSA4480_DECMSK		0x19
#define		FSA4480_AUDREG1		0x1A
#define		FSA4480_AUDREG2		0x1B
#define		FSA4480_AUDDATA0	0x1C
#define		FSA4480_AUDDATA1	0x1D
#define		FSA4480_RESET		0x1E
#define		FSA4480_CURSET		0x1F

#define		FSA4480_ALLSW		0
#define		FSA4480_USB			1
#define		FSA4480_AUDIO		2
#define		FSA4480_MIC			3
#define		FSA4480_SBU			4

#define	FSA4480_DEV_VERSION		0x09
#define DEBUG_PRINT				1

static struct i2c_client *fsa4480_client;
struct fsa4480_dev *oppo_fsa4480;

#define PROC_DEBUG
#ifdef  PROC_DEBUG
#include <linux/proc_fs.h>
static struct proc_dir_entry* Debug_entry;
struct proc_file{
	struct file_operations opera;
	char name[20];
};
#endif


struct fsa4480_dev {
	struct	i2c_client	*client;
	unsigned int		irq_gpio;
	unsigned int irq;
	/* SW_IRQ state */
	bool			irq_enabled;
	spinlock_t		irq_enabled_lock;
	int fsa4480_i2c_state;
	struct mutex fsa4480_i2c_mutex;
	u8 hp_st;
};

/* I2C Read/Write Functions */
static int fsa4480_i2c_read(u8 reg, u8 *value)
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

	if (!fsa4480_client) {
		pr_err("%s: fsa4480 not work!!!!", __func__);
		return -EINVAL;
	}
	msgs[0].addr = fsa4480_client->addr;
	msgs[1].addr = fsa4480_client->addr;
	do {
		err = i2c_transfer(fsa4480_client->adapter, msgs,
					ARRAY_SIZE(msgs));
		if (err != ARRAY_SIZE(msgs)) {
			msleep_interruptible(I2C_RETRY_DELAY);
		}
	} while ((err != ARRAY_SIZE(msgs)) && (++tries < I2C_RETRIES));

	if (err != ARRAY_SIZE(msgs)) {
		dev_err(&fsa4480_client->dev, "read transfer error %d\n", err);
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int fsa4480_i2c_write(u8 reg, u8 value)
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


	if (!fsa4480_client) {
		pr_err("%s: fsa4480 not work!!!!", __func__);
		return -EINVAL;
	}
	msgs[0].addr = fsa4480_client->addr;

	buf[0] = reg;
	buf[1] = value;
	do {
		err = i2c_transfer(fsa4480_client->adapter, msgs,
				ARRAY_SIZE(msgs));
		if (err != ARRAY_SIZE(msgs)) {
			msleep_interruptible(I2C_RETRY_DELAY);
		}
	} while ((err != ARRAY_SIZE(msgs)) && (++tries < I2C_RETRIES));

	if (err != ARRAY_SIZE(msgs)) {
		dev_err(&fsa4480_client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}


int fsa4480_reg_read(u8 reg, u8 *value)
{
	int ret = -EINVAL;
	ret = fsa4480_i2c_read(reg, value);

	return ret;
}

int fsa4480_reg_write(u8 reg, u8 value)
{
	int retval;
	u8 old_value;

	retval = fsa4480_reg_read(reg, &old_value);
	pr_err("Old value = 0x%08X reg no. = 0x%02X\n", old_value, reg);

	if (retval != 0) {
		goto error;
	}

	retval = fsa4480_i2c_write(reg, value);
	if (retval != 0) {
		goto error;
	}

	retval = fsa4480_reg_read(reg, &old_value);
	pr_err("after write value = 0x%08X  reg no. = 0x%02X\n", old_value, reg);

error:
	return retval;
}

#ifdef PROC_DEBUG
static ssize_t fsa4480_proc_read_status(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	u8 addr = 0;
	u8 val = 0;
	int len = 0;
	char pri_buf[512];
	int err = 0;

	memset(pri_buf,0,sizeof(pri_buf));

	for(addr = FSA4480_DEVID; addr <= FSA4480_CURSET; addr++) {
		fsa4480_reg_read(addr, &val);
		len += sprintf(pri_buf+len, "R:%02x V:%02x\n", addr, val);
		val = 0;
	}
	err = copy_to_user(buf, pri_buf, len);

	if (len > *pos) {
		len -= *pos;
	}
	else
	{
		len = 0;
	}
	*pos =*pos + len;
	count -= len;

	return len;
}
static ssize_t fsa4480_proc_set_reg(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int reg = 0;
	int val = 0;
	int read = 0;
	char tmp[10] = {0};

	read = copy_from_user(tmp, buf, count);
	read = sscanf(tmp, "0x%x,0x%x", &reg, &val);
	if (read) {
		fsa4480_reg_write(reg, val);
		pr_info("%s reg = 0x%x, val = 0x%x, read = %d\n", __func__, reg, val, read);
	}
	else
	{
		pr_err("%s fail!!\n", __func__);
	}

	return count;
}

static struct proc_file proc_file_st[] = {
	{.name = "status",  .opera = {.read = fsa4480_proc_read_status}},
	{.name = "set_reg", .opera = {.write = fsa4480_proc_set_reg}},
};

static void fsa4480_create_proc_files(struct proc_dir_entry *entry)
{
	int count = sizeof(proc_file_st)/sizeof(proc_file_st[0]);
	int i = 0;
	for (i = 0; i < count; i++) {
		proc_create(proc_file_st[i].name, 0660, entry, &proc_file_st[i].opera);
	}
}

static void fsa4480_create_proc(void)
{
	Debug_entry = proc_mkdir_mode("fsa4480", 0660, NULL);
	fsa4480_create_proc_files(Debug_entry);
}
#endif

static int fsa4480_init_reg(void){
	int ret = 0;
	u8 regdata = 0;
	/*Here need to configure ENN pin = low, if it is connected GPIO, need to set GPIO low
	* If ENN pin is landing or set Low, the 04H bit 7 is 1, Device enable, else disable
	*/

	pr_err("Enter fsa4480_init_reg");
	fsa4480_reg_read(FSA4480_DEVID, &regdata);
	if (regdata != FSA4480_DEV_VERSION) {
		pr_err("FSA4480 regdata = 0x%x", regdata);
		pr_err("This device is not FSA4480 or ENN is High");
		return -1;
	}

	/* OVP Interrupt Mask: 00h= enable all OVP interrupt */
	ret = fsa4480_reg_write(FSA4480_OVPMSK, 0x00);

	/* set mic threshold data0 to 75mV*/
	ret |= fsa4480_reg_write(FSA4480_AUDDATA0, 0x08);
	/* set mic threshold data1 */
	ret |= fsa4480_reg_write(FSA4480_AUDDATA1, 0xff);

	/* Function Setting:  h4b = DEC open drain output */
	ret |= fsa4480_reg_write(FSA4480_FUN_EN, 0x44);
	/* Switch Enable: 80h= enable FSA4480 device */
	ret |= fsa4480_reg_write(FSA4480_SWEN, 0x80);
	/* 1fh default is 0x07, changed to 0x02 to decrease detect current to 200uA */
	ret |= fsa4480_reg_write(FSA4480_CURSET, 0x02);

	if (ret) {
		pr_err("[fsa4480] %s write ret failed \n", __func__);
		return ret;
	}

	if (DEBUG_PRINT) {
		pr_err("[fsa4480] %s Success \n", __func__);
	}

	return ret;
}

static int fsa4480_sw_enable(int which_sw, int is_enable)
{
	u8 regdata;

	fsa4480_reg_read(FSA4480_SWEN, &regdata);

	/* Is FSA4480 device enabled already? */
	if (((regdata&0x80) != 0x80) && (is_enable == 1)) {
		pr_err("please enable FSA4480 device");
		return -3;
	}

	/* Enable or disable USB or audio R/L switch */
	if (which_sw == FSA4480_USB || which_sw == FSA4480_AUDIO) {
		if (is_enable == 0) {
			regdata &= 0xe7;
			fsa4480_reg_write(FSA4480_SWEN, regdata);
			return 0;
		}
		else if (is_enable == 1) {
			regdata |= 0x18;
			fsa4480_reg_write(FSA4480_SWEN, regdata);
			return 0;
		}
		else {
			return -2;
		}
	}
	/* Enable or disable MIC switch */
	else if (which_sw == FSA4480_MIC) {
		if (is_enable == 0) {
			regdata &= 0xf8;
			fsa4480_reg_write(FSA4480_SWEN, regdata);
			return 0;
		}
		else if (is_enable == 1) {
			regdata |= 0x07;
			fsa4480_reg_write(FSA4480_SWEN, regdata);
			return 0;
		}
		else {
			return -2;
		}
	}
	/* Enable or disable SBU switch */
	else if (which_sw == FSA4480_SBU) {
		if (is_enable == 0) {
			regdata &= 0x9f;
			fsa4480_reg_write(FSA4480_SWEN, regdata);
			return 0;
		}
		else if (is_enable == 1) {
			regdata |= 0x60;
			fsa4480_reg_write(FSA4480_SWEN, regdata);
			return 0;
		}
		else {
			return -2;
		}

	}
	else {
		return -1;
	}
}


/* Switch USB/Audio R/L Path,  path=1 to USB, path=2 to Audio R/L */
static int fsa4480_sw_usb_select(int path)
{
	u8 reg_en, reg_sel;

	fsa4480_reg_read(FSA4480_SWEN, &reg_en);
	fsa4480_reg_read(FSA4480_SWSEL, &reg_sel);

	if ((reg_en&0x98) != 0x98) {
		pr_info("please enable USB channel first");
		return -2;
	}

	/* Turn to USB DP/DN */
	if (path == 1) {
		reg_sel|= 0x18;
		fsa4480_reg_write(FSA4480_SWSEL, reg_sel);
		return 0;

	}
	/* Turn to Audio R/L */
	else if (path == 2) {
		reg_sel&= 0xe7;
		fsa4480_reg_write(FSA4480_SWSEL, reg_sel);
		return 0;
	}
	else {
		return -1;
	}
}

/* Switch MIC Path, path=1 MIC to SBU1,path=2 MIC to SBU2 */
static int fsa4480_sw_mic_select(int path)
{
	u8 reg_en, reg_sel;

	fsa4480_reg_read(FSA4480_SWEN, &reg_en);
	fsa4480_reg_read(FSA4480_SWSEL, &reg_sel);

	if ((reg_en&0x87) != 0x87) {
		pr_info("please enable USB channel first");
		return -2;
	}

	/* Turn MIC to SBU1 */
	if (path == 1) {
		reg_sel|=0x07;
		//add for 17107 T13 DVT1 DVT2 and 17127 T0 pcb, the connection of sbux and Gsbux is in reverse
		if ((is_project(OPPO_17107) && (HW_VERSION__13 <= get_PCB_Version() &&
			HW_VERSION__15 >= get_PCB_Version())) || (is_project(OPPO_17127) &&
			(HW_VERSION__10 == get_PCB_Version()))){
			reg_sel&=~0x04;
		}
		fsa4480_reg_write(FSA4480_SWSEL, reg_sel);
		return 0;

	}
	/* Turn MIC to SBU2 */
	else if (path == 2) {
		reg_sel&= 0xf8;
		//add for 17107 T13 DVT1 DVT2 and 17127 T0 pcb, the connection of sbux and Gsbux is in reverse
		if ((is_project(OPPO_17107) && (HW_VERSION__13 <= get_PCB_Version() &&
			HW_VERSION__15 >= get_PCB_Version())) || (is_project(OPPO_17127) &&
			(HW_VERSION__10 == get_PCB_Version()))){
			reg_sel|= 0x04;
		}
		fsa4480_reg_write(FSA4480_SWSEL, reg_sel);
		return 0;
	}
	else {
		return -1;
	}
}

/* Switch SBUx path, path=1 SBU1_H to SBU1, Path=2 SBU1_H to SBU2. */
static int fsa4480_sw_subx_select(int path)
{
	u8 reg_en, reg_sel;
	fsa4480_reg_read(FSA4480_SWEN, &reg_en);
	fsa4480_reg_read(FSA4480_SWSEL, &reg_sel);

	if ((reg_en&0xe0) != 0xe0) {
		pr_info("please enable USB channel first");
		return -2;
	}

	/* Turn SBU1_H to SBU1 */
	if (path == 1) {
		reg_sel&=0x1f;
		fsa4480_reg_write(FSA4480_SWSEL, reg_sel);
		return 0;

	}
	/* Turn SBU1_H to SBU2 */
	else if (path == 2) {
		reg_sel |= 0x60;
		fsa4480_reg_write(FSA4480_SWSEL, reg_sel);
		return 0;
	}
	else {
		return -1;
	}
}

enum fsa4480_jack{
	aud_acc,
	no_aud_acc,
	hp_3pole,
	hs_sub1 = 4,
	hs_sub2 = 8
};

enum mbhc_jack{
	invalid = -1,
	none,
	headset,
	headphones,
	high_res,
	gnd_mic_swap,
	anc,
	selfie_stick,
	splitter_jack
};

u8 fsa4480_check_jack_status(void){
	u8 reg_sel;
	pr_err("Enter %s\n", __func__);
	fsa4480_reg_read(FSA4480_JACTYP, &reg_sel);
	pr_err("%s jack state = 0x%x\n", __func__, reg_sel);
	return (reg_sel&0x0F);
}

static bool fsa4480_enable_start_headset_det(void) {
	u8 dec_int = 0;
	pr_err("%s: Enter!\n", __func__);

	/* Switch Enable: 04h enable r l*/
	fsa4480_i2c_write(FSA4480_SWEN, 0x98);
	/* Switch Enable: 05h switch mic r l */
	fsa4480_i2c_write(FSA4480_SWSEL, 0x00);
	/* Switch Enable: 04h enable r l mic */
	mdelay(10);
	fsa4480_i2c_write(FSA4480_SWEN, 0x9b);
	mdelay(10);
	/* Function Setting:  12h, bit0 = 1, open auto mic DET */
	fsa4480_i2c_write(FSA4480_FUN_EN, 0x45);

	/* The delay time should more than 4ms */
	mdelay(8);

	fsa4480_i2c_read(FSA4480_DECINT, &dec_int);
	if (dec_int & 0x04) {
		return true;
	} else {
		return false;
	}
}

static void fsa4480_reset_switch_audio (void) {
	pr_err("%s: Enter!\n", __func__);

	fsa4480_i2c_write(FSA4480_SWEN, 0x00);
	mdelay(20);

	fsa4480_i2c_write(FSA4480_SWEN, 0x98);
	fsa4480_i2c_write(FSA4480_SWSEL, 0x00);
	mdelay(10);
	fsa4480_i2c_write(FSA4480_SWEN, 0x9F);
}

static void fsa4480_mic_gsbu_open(u8 hp_st) {
	switch (hp_st) {
	case hs_sub1:
		pr_err("%s: FSA4480 4 pole mic to sbu1!\n", __func__);
		fsa4480_sw_mic_select(1);
		break;
	case hs_sub2:
		pr_err("%s: FSA4480 4 pole mic to sbu2!\n", __func__);
		fsa4480_sw_mic_select(2);
		break;
	default:
		pr_err("%s: FSA4480 3 pole or other set mic to sbu2!\n", __func__);
		fsa4480_sw_mic_select(2);
	}
}

static u8 fsa4480_res_det(u8 hp_st) {
	int i = 0;
	u8 val[5];

	for ( ; i < 5; i++) {
		fsa4480_i2c_write(FSA4480_RES_SET, i);
		fsa4480_i2c_write(FSA4480_FUN_EN, 0x46);
		mdelay(10);
		fsa4480_i2c_read(FSA4480_RES_VAL, &val[i]);
		pr_err("FSA4480  PIN:%02x V:%02x\n", i, val[i]);
	}

	switch (hp_st) {
	case hs_sub1:
		if ((val[1] == 0xFF) && (val[2] == 0xFF)) {
			if (val[3] == 0xFF) {
				pr_err("%s: it's splitter_jack!\n", __func__);
				return high_res;
			} else {
				pr_err("%s: it's selfie_stick!\n", __func__);
				return selfie_stick;
			}
		}
		return headset;
	case hs_sub2:
		if ((val[1] == 0xFF) && (val[2] == 0xFF)) {
			if (val[4] == 0xFF) {
				pr_err("%s: it's splitter_jack!\n", __func__);
				return high_res;
			} else {
				pr_err("%s: it's selfie_stick!\n", __func__);
				return selfie_stick;
			}
		}
		return headset;
	case hp_3pole:
		if ((val[3] != 0xFF) && (val[4] != 0xFF)) {
			pr_err("%s: it's headphones!\n", __func__);
			return headphones;
		}
		return splitter_jack;
	default:
		return splitter_jack;
	}
}

bool fsa4480_switch_check_plug_in(void){
	int jack_type = headset;

	mutex_lock(&oppo_fsa4480->fsa4480_i2c_mutex);
	oppo_fsa4480->fsa4480_i2c_state = 1;

	jack_type = fsa4480_res_det(oppo_fsa4480->hp_st);

	oppo_fsa4480->fsa4480_i2c_state = 0;
	mutex_unlock(&oppo_fsa4480->fsa4480_i2c_mutex);

	if ((jack_type != splitter_jack) && (jack_type != high_res)) {
		return true;
	}
	return false;
}

int fsa4480_enable_audio_switch(void){
	u8 hp_st = 0;
	int i = 0;
	int jack_type = headset;
	u8 sbu1_sub2 = 0;
	u8 sbu2_sub1 = 0;
	u8 pinstate0 = 0;
	u8 pinstate1 = 0;

	mutex_lock(&oppo_fsa4480->fsa4480_i2c_mutex);

	oppo_fsa4480->fsa4480_i2c_state = 1;

	for (; i < 3; i++ ){
		if (fsa4480_enable_start_headset_det ()) {
			hp_st = fsa4480_check_jack_status();
			oppo_fsa4480->hp_st = hp_st;
			break;
		} else {
			pr_err("%s: Mic detecting have not been ready !\n", __func__);
		}
	}

	if (DEBUG_PRINT) {
		fsa4480_i2c_read(FSA4480_AUDREG1, &sbu1_sub2);
		fsa4480_i2c_read(FSA4480_AUDREG2, &sbu2_sub1);
		fsa4480_i2c_read(FSA4480_SWST0, &pinstate0);
		fsa4480_i2c_read(FSA4480_SWST1, &pinstate1);
		pr_err("%s: res sub1 to sub2 is 0x%02x, sub2 to sub1 is 0x%02x\n", __func__, sbu1_sub2, sbu2_sub1);
		pr_err("%s: pinstate0 is 0x%02x, pinstate1 is 0x%02x\n", __func__, pinstate0, pinstate1);
	}

	fsa4480_reset_switch_audio();

	fsa4480_mic_gsbu_open(hp_st);

	mdelay(10);

	jack_type = fsa4480_res_det(hp_st);

	fsa4480_reset_switch_audio();

	mdelay(10);

	fsa4480_mic_gsbu_open(hp_st);

	if (DEBUG_PRINT) {
		fsa4480_i2c_read(FSA4480_SWST0, &pinstate0);
		fsa4480_i2c_read(FSA4480_SWST1, &pinstate1);
		pr_err("%s: after check pinstate0 is 0x%02x, pinstate1 is 0x%02x\n", __func__, pinstate0, pinstate1);
	}

	oppo_fsa4480->fsa4480_i2c_state = 0;

	mutex_unlock(&oppo_fsa4480->fsa4480_i2c_mutex);
	return jack_type;
}

int fsa4480_check_pin_state(void){
	int ret = 0;
	u8 val[5];
	int i = 0;

	pr_err("%s: enter!\n", __func__);
	for ( ; i < 5; i++) {
		ret |= fsa4480_reg_write(FSA4480_RES_SET, i);
		ret |= fsa4480_reg_write(FSA4480_FUN_EN, 0x4f);
		udelay(1000);
		fsa4480_reg_read(FSA4480_RES_VAL, &val[i]);
		pr_err("FSA4480  PIN:%02x V:%02x\n", i, val[i]);
	}

	if ((val[1] != 0xFF) && (val[2] != 0xFF)) {
		return headset;
	}
	return splitter_jack;

}

int fsa4480_disable_audio_switch(void){
	int ret = 0;

	mutex_lock(&oppo_fsa4480->fsa4480_i2c_mutex);

	oppo_fsa4480->fsa4480_i2c_state = 1;
	ret |= fsa4480_sw_enable(FSA4480_USB, 1);
	ret |= fsa4480_sw_enable(FSA4480_MIC, 0);
	ret |= fsa4480_sw_usb_select(1);

	oppo_fsa4480->fsa4480_i2c_state = 0;
	oppo_fsa4480->hp_st = no_aud_acc;
	mutex_unlock(&oppo_fsa4480->fsa4480_i2c_mutex);

	return ret;
}

int fsa4480_audio_switch(int state){
	pr_info("Enter %s", __func__);

	switch (state){
		case no_aud_acc:
			pr_err("%s jack no_aud_acc\n", __func__);
			fsa4480_sw_enable(FSA4480_USB, 1);
			fsa4480_sw_enable(FSA4480_MIC, 0);
			fsa4480_sw_usb_select(1);
			break;
		case hp_3pole:
			pr_err("%s jack hp_3pole\n", __func__);
			fsa4480_sw_enable(FSA4480_AUDIO, 1);
			fsa4480_sw_usb_select(2);
			break;
		case hs_sub1:
			pr_err("%s jack hs_sub1\n", __func__);
			fsa4480_sw_enable(FSA4480_AUDIO, 1);
			fsa4480_sw_enable(FSA4480_MIC, 1);
			fsa4480_sw_usb_select(2);
			fsa4480_sw_mic_select(1);
			break;
		case hs_sub2:
			pr_err("%s jack hs_sub2\n", __func__);
			fsa4480_sw_enable(FSA4480_AUDIO, 1);
			fsa4480_sw_enable(FSA4480_MIC, 1);
			fsa4480_sw_usb_select(2);
			fsa4480_sw_mic_select(2);
			break;
		default:
			pr_err("%s jack state NG\n", __func__);
			fsa4480_sw_enable(FSA4480_AUDIO, 0);
			fsa4480_sw_enable(FSA4480_MIC, 0);
	}
	return state;
}

static int fsa4480_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int err = 0;
	struct device_node *np = NULL;

	if (DEBUG_PRINT) {
		pr_err("Enter %s", __func__);
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "check_functionality failed\n");
		return -EIO;
	}

	/* set global variables */
	fsa4480_client = client;
	err = fsa4480_init_reg();
	err |= fsa4480_sw_enable(FSA4480_USB, 1);
	err |= fsa4480_sw_usb_select(1);

	np = fsa4480_client->dev.of_node;
	oppo_fsa4480 = kzalloc(sizeof(*oppo_fsa4480), GFP_KERNEL);
	if (oppo_fsa4480 == NULL) {
		err = -ENOMEM;
		pr_err("fsa4480 kzalloc is invalid\n");
		return err;
	}
	dev_set_drvdata(&client->dev, oppo_fsa4480);
	mutex_init(&oppo_fsa4480->fsa4480_i2c_mutex);
	oppo_fsa4480->fsa4480_i2c_state = 0;
	oppo_fsa4480->irq_gpio = of_get_named_gpio(np, "oppo,fsa4480-irq", 0);
	oppo_fsa4480->client = client;
	oppo_fsa4480->irq_enabled = true;
	oppo_fsa4480->hp_st = no_aud_acc;
	spin_lock_init(&oppo_fsa4480->irq_enabled_lock);

	if (gpio_is_valid(oppo_fsa4480->irq_gpio)) {
		err = gpio_request(oppo_fsa4480->irq_gpio, "fsa4480-irq");
		if (err) {
			gpio_free(oppo_fsa4480->irq_gpio);
			pr_err("unable to request gpio [%d]\n", oppo_fsa4480->irq_gpio);
			/* return -EINVAL; */
		}
		err = gpio_direction_input(oppo_fsa4480->irq_gpio);
		msleep(50);
		oppo_fsa4480->irq = gpio_to_irq(oppo_fsa4480->irq_gpio);
		if (oppo_fsa4480->irq < 0) {
			pr_err("unable set GPIO [%d] to irq \n", oppo_fsa4480->irq_gpio);
			/* return -EINVAL; */
		}
	} else {
		pr_err("oppo_fsa4480->irq_gpio is invalid\n");
	}

	pr_err("client-> Name = %s\n", client->name);

	fsa4480_create_proc();		/* addf for debug */

	if (DEBUG_PRINT) {
		pr_err("exit %s err = %d", __func__, err);
	}
	return err;
}

EXPORT_SYMBOL_GPL(fsa4480_enable_audio_switch);
EXPORT_SYMBOL_GPL(fsa4480_disable_audio_switch);
EXPORT_SYMBOL_GPL(fsa4480_check_jack_status);
EXPORT_SYMBOL_GPL(fsa4480_audio_switch);
EXPORT_SYMBOL_GPL(fsa4480_sw_mic_select);
EXPORT_SYMBOL_GPL(fsa4480_sw_subx_select);
EXPORT_SYMBOL_GPL(fsa4480_check_pin_state);
EXPORT_SYMBOL_GPL(fsa4480_switch_check_plug_in);

static int __exit fsa4480_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id fsa4480_id[] = {
	{NAME, 0},
	{},
};

static const struct of_device_id fsa4480_match[] = {
	{ .compatible = "fsa,fsa4480" },
	{},
};

MODULE_DEVICE_TABLE(i2c, fsa4480_id);


static int fsa4480_suspend(struct device *dev)
{
	struct fsa4480_dev *oppo_fsa4480 = dev_get_drvdata(dev);

	if (oppo_fsa4480->fsa4480_i2c_state) {
		pr_err("%s: when i2c is reading and writing!\n", __func__);
		return FSA4480_ERR_I2C;
	}
	return 0;
}

static int fsa4480_resume(struct device *dev)
{
	struct fsa4480_dev *oppo_fsa4480 = dev_get_drvdata(dev);

	if (oppo_fsa4480->fsa4480_i2c_state) {
		pr_err("%s: when i2c is reading and writing!\n", __func__);
	}
	return 0;
}

const struct dev_pm_ops fsa4480_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fsa4480_suspend, fsa4480_resume)
};

static struct i2c_driver fsa4480_driver = {
	.driver = {
		.name = NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(fsa4480_match),
		.pm = &fsa4480_pm_ops,
	},
	.probe = fsa4480_probe,
	.remove = fsa4480_remove,
	.id_table = fsa4480_id,
};

static int __init fsa4480_init(void)
{
	pr_err("Enter %s", __func__);

	return i2c_add_driver(&fsa4480_driver);
}

static void __exit fsa4480_exit(void)
{
	pr_err("Enter %s", __func__);

	i2c_del_driver(&fsa4480_driver);

	return;
}


module_init(fsa4480_init);
module_exit(fsa4480_exit);

MODULE_DESCRIPTION("fsa4480 driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("OPPO");
