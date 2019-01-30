/*
 * iacore.h  --  Audience ia6xx Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IACORE_H
#define _IACORE_H

#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#if (defined(CONFIG_ARCH_MSM) ||  defined(CONFIG_ARCH_QCOM))
#include <linux/regulator/consumer.h>
#endif
#include <linux/slab.h>
#include <sound/soc.h>
#include <linux/kthread.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <wakelock.h>
#include "iaxxx.h"
#include "iacore-uart.h"

#if (defined(CONFIG_SND_SOC_IA_SOUNDWIRE) && defined(CONFIG_SND_SOC_IA_SDW_X86))
#include "iacore-sdw-x86.h"
#endif

#include <linux/version.h>

struct iacore_priv;

#define FW_SLEEP_TIMER_TEST 1

#define IA_CMD_ACCESS_WR_MAX 9
#define IA_CMD_ACCESS_RD_MAX 9

/* TODO: condition of kernel version or commit code to specific kernels */
#define FORCED_FW_RECOVERY_OFF 0
#define FORCED_FW_RECOVERY_ON  1

#ifdef CONFIG_SND_SOC_IA_FW_RECOVERY
/*
 * Increment/decrement disable firmware recovery use count
 * Firmware recovery will be performed when use count is ZERO or
 * forced recovery requested in following function:
 * int iacore_fw_recovery_unlocked(struct iacore_priv *iacore, int is_forced)
 */
#define INC_DISABLE_FW_RECOVERY_USE_CNT(iacore) \
	do { \
		iacore->disable_fw_recovery_cnt++; \
		pr_debug("fw_cnt++ = %d\n", \
			iacore->disable_fw_recovery_cnt); \
	} while (0)

#define DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore) \
	do { \
		if (iacore->disable_fw_recovery_cnt) \
			iacore->disable_fw_recovery_cnt--; \
		pr_debug("fw_cnt-- = %d\n", \
			iacore->disable_fw_recovery_cnt); \
	} while (0)

#define IACORE_FW_RECOVERY_FORCED_OFF(iacore) \
	do { \
		int ret = iacore_fw_recovery_unlocked(iacore, FORCED_FW_RECOVERY_OFF); \
		if (ret < 0) \
			pr_err("Firmware recovery failed %d\n", ret); \
	} while (0)
#else
#define INC_DISABLE_FW_RECOVERY_USE_CNT(iacore)
#define DEC_DISABLE_FW_RECOVERY_USE_CNT(iacore)
#define IACORE_FW_RECOVERY_FORCED_OFF(iacore)
#endif

/* Standard commands used by all chips */

#define IACORE_SBL_I2S_SYNC_CMD		0x03
#define IA_SYNC_CMD			0x8000
#define IA_WAKEUP_SYNC_CMD		0x9000
#define IA_SYNC_POLLING			0x0000
#define IA_SYNC_ACK			0x80000000
#define IA_SBL_SYNC_ACK			0x8000FFFF

#define IA_SET_ALGO_PARAM_ID		0x8017
#define IA_SET_ALGO_PARAM		0x8018

#define IA_SET_POWER_STATE		0x9010
#define SLEEP_STATE_MASK		0x03

enum {
	IA_DEEP_SLEEP = 0,
	IA_CHIP_SLEEP,
	IA_LOW_POWER,
	IA_FW_RETENTION,
	IA_SLEEP_MIN = IA_DEEP_SLEEP,
	IA_SLEEP_MAX = IA_FW_RETENTION,
};

#define IA_MAX_RETRY_3			3
#define IA_MAX_RETRY_5			5
#define IA_SYNC_MAX_RETRY		IA_MAX_RETRY_3 /*IA_MAX_RETRY_5*/

#define IA_NOT_READY		0x00000000
#define IA_ILLEGAL_CMD		0xFFFF0000
#define IA_ILLEGAL_RESP		0xFFFF0000

#define IA_EVENT_RESPONSE_CMD		0x801a
#define IA_LEVEL_TRIGGER_LOW		0x0001
#define IA_LEVEL_TRIGGER_HIGH		0x0002
#define IA_EDGE_FALLING			0x0003
#define IA_EDGE_RISING			0x0004

#define IA_PDM_AUD_DATA_PORT_CMD	0x90040000
#define IA_I2S_AUD_DATA_PORT_CMD	0x90040001
#define IA_SDW_AUD_DATA_PORT_CMD	0x90040002

#define IA_CLK_STP_MODE_CMD		0x802B
#define CLK_STP_MODE_0			0x0000
#define CLK_STP_MODE_1			0x0001

#define IA6XX_DAI_I2S	0
#define IA6XX_DAI_PDM	1

/*
 * Codec Interrupt event type
 */
#define IA_NO_EVENT			0x0000
#define IA_VS_INTR_EVENT		0x0001
#define IA_IGNORE_EVENT         0x00FE
#define IA_VS_FW_RECOVERY		0x00FF
#define IA_FW_CRASH			0xFFFF

#define IA_MASK_INTR_EVENT		0x0000FFFF

#define IA_SYNC_POLLING			0x0000

#define IA_SET_DEVICE_PARAMID		0x800C
#define IA_SET_DEVICE_PARAM		0x800D
#define IA_CONFIG_DATA_PORT		0x802C
#define IA_CONFIG_ENABLE		0x01
#define IA_CONFIG_1CHAN			0x01
#define IA_CONFIG_2CHAN			0x02
#define IA_CONFIG_16k			0x01
#define IA_CONFIG_48k			0x04
#define IA_CONFIG_96k			0x05
#define IA_CONFIG_192k			0x06

#define IA_ALGO_RESET			0x806C
#define IA_GET_EVENT			0x806D
#define IA_SET_SAMPLE_RATE		0x8030
#define IA_SET_PRESET			0x8031
#define IA_SET_PRESET_NOREP		0x9031
#define IA_SET_ROUTE			0x8032
#define IA_SET_LL_ROUTE			0x9032
#define IA_SET_STOP_ROUTE		0x8033
#define IA_SET_BUFFERED_DATA_FORMAT	0x8034
#define IA_SET_FRAME_SIZE		0x8035
#define IA_SET_AUD_PORT_CLK_FREQ	0x8042
#define IA_SET_CVS_PRESET		0x906F

#define IA_VS_PROCESSING_MODE		0x5003
#define IA_VS_DETECT_KEYWORD		0x0000
#define IA_VS_ALGO_MEM_RESET		0x500E
#define IA_VS_MEM_PERFORM_RESET		0x0001

#define IA_VS_OEM_DETECT_SENSITIVITY		0x5008
#define IA_VS_USR_DETECT_SENSITIVITY		0x5009
#define IA_VS_VOICEID_DETECT_SENSITIVITY	0x500D

#define IA_BARGEIN_VOICEQ		0x5019
#define IA_BARGEIN_VOICEQ_ENABLE	0x0001
#define IA_BARGEIN_VOICEQ_DISABLE	0x0000

#define IA_STOP_ROUTE_VALUE		0x0000
#define IA_FRAME_SIZE_2MS		0x0002
#define IA_FRAME_SIZE_10MS		0x000A
#define IA_FRAME_SIZE_16MS		0x0010
#define IA_SAMPLE_RATE_16KHZ		0x0001
#define IA_SAMPLE_RATE_48KHZ		0x0004
#define IA_BUFFER_DATA_FORMAT_Q15	0x0002

#define IA_SET_PRESET_VALUE		0x0480
#define IA_SET_CVS_PRESET_VALUE		0x0480

#define IA_SET_BYPASS_MODE		0x8040
#define IA_BYPASS_MODE_ON		0x0001
#define IA_BYPASS_MODE_OFF		0x0000

#define IA_READ_DATA_BLOCK		0x802E
#define IA_WRITE_DATA_BLOCK		0x802F

#define IA_KW_PRESERVATION		0x802A
#define IA_DISABLE_BUFFERING		0
#define IA_DISABLE_PRESERVATION		1
#define IA_ENABLE_PRESERVATION		2

#define IA_CVQ_BUFF_OVERFLOW		0x806E
#define CVQ_BUFF_OVERFLOW_OCCURRED	0x0001

#define IA_SET_PRESET_VAD_ON_CMD	0x806F
#define IA_SET_PRESET_VAD_OFF_CMD	0x8031


#define DBG_ID       13
#define DBG_BLK_SIZE (2 * 1024)
#if (DBG_BLK_SIZE > PAGE_SIZE)
#error "Making DBG_BLK_SIZE greater than PAGE_SIZE breaks sysfs read support"
#endif

#define IA_DELAY_1MS				1000	/* 1 ms*/
#define IA_DELAY_2MS				2000	/* 2 ms*/
#define IA_DELAY_5MS				5000	/* 5 ms*/
#define IA_DELAY_10MS				10000	/* 10 ms*/
#define IA_DELAY_12MS				12000	/* 15 ms*/
#define IA_DELAY_15MS				15000	/* 15 ms*/
#define IA_DELAY_20MS				20000	/* 20 ms*/
#define IA_DELAY_25MS				25000	/* 25 ms*/
#define IA_DELAY_30MS				30000	/* 30 ms*/
#define IA_DELAY_35MS				35000	/* 35 ms*/
#define IA_DELAY_40MS				40000	/* 40 ms*/
#define IA_DELAY_50MS				50000	/* 50 ms*/
#define IA_DELAY_60MS				60000	/* 60 ms*/
#define IA_DELAY_70MS				70000	/* 70 ms*/

#define IA_RESP_TOUT				IA_DELAY_12MS /* 20ms */
#define IA_RESP_POLL_TOUT			4000  /*  4ms */
#define IA_MAX_RETRIES		\
			(IA_RESP_TOUT / IA_RESP_POLL_TOUT)


#define IA_MAX_WDB_RETRIES			20
#define IA6XX_WDB_BLOCK_SIZE		512
#define IA_SDW_RESP_POLL_TOUT			7000  /*  7ms */

#define IA_SPI_RETRY_DELAY	IA_DELAY_5MS	/*  5ms */
#define IA_SPI_MAX_RETRIES	20		/* Number of retries */
#define IA_SPI_CONT_RETRY	10	/* Retry for read without delay */
#define IA_SPI_1MS_DELAY	IA_DELAY_1MS	/* 1 ms*/
#define IA_STRT_STRM_CMD_TOUT	IA_DELAY_5MS	/* 5000ms */

#define IA_STOP_STRM_CMD_RETRY	48	/* 48 words or 192 bytes */

#if (defined(CONFIG_ARCH_MSM) ||  defined(CONFIG_ARCH_QCOM))
#define IACORE_PWR_VTG_MIN_UV  1800000
#define IACORE_PWR_VTG_MAX_UV  1800000
#endif /*CONFIG_SND_SOC_IA_USE_VOLTAGE_REGULATOR*/

/* ia firmware states
 *
 * POWER_OFF	- Chip in power off state.
 * SBL		- Chip in boot loader.
 * FW_LOADED	- Chip in FW loaded state.
 * HW_BYPASS	- Chip in boot loader PDM Bypass mode.
 * FW_HW_BYPASS - Chip in hw bypass mode with FW loaded and firmware in sleep.
 * SPL_PDM_RECORD - Chip in Sepcial PDM record mode with FW loaded.
 * SW_BYPASS	- Chip in bypass mode with FW loaded and firmware in active
 *		  state performing conversion from PDM to I2S.
 * IO_STREAMING - Chip in streaming mode with some route active with the
 *		  firmware loaded.
 * IO_BARGEIN_STREAMING - Chip in streaming mode with barge-in route active
 *		   with the bargein firmware loaded.
 * VS_MODE	- Chip in voicesense mode with the voicesense route active
 *		  before putting it to sleep.
 *		  Also on keyword detection chip will go to this state.
 * VS_SLEEP	- Chip in voicesense sleep looking for KW detection.
 * BARGEIN_SLEEP - Chip in bargein sleep looking for KW detection.
 * VS_BURSTING	- Chip in VS_MODE with CVQ bursting ON.
 * BARGEIN_BURSTING - Chip in Bargein streaming mode with some route active
 *		   with firmware loaded.
 * FW_SLEEP	- Chip in sleep with fw loaded and voicesense disabled.
 * DEEP_SLEEP	- Chip in Deep sleep with fw loaded and voicesense disabled.
 */
enum iacore_fw_state {
	POWER_OFF,
	SBL,
	FW_LOADED,
	HW_BYPASS,
	FW_HW_BYPASS,
	SPL_PDM_RECORD,
	SW_BYPASS,
	IO_STREAMING,
	IO_BARGEIN_STREAMING,
	VS_MODE,
	VS_SLEEP,
	BARGEIN_SLEEP,
	VS_BURSTING,
	BARGEIN_BURSTING,
	FW_SLEEP,
	DEEP_SLEEP,
	PROXY_MODE, /* moving to this state is only possible through ioctl */
	FW_MIN = POWER_OFF,
	FW_MAX = DEEP_SLEEP,
};

struct iacore_api_access {
	u32 read_msg[IA_CMD_ACCESS_RD_MAX];
	unsigned int read_msg_len;
	u32 write_msg[IA_CMD_ACCESS_WR_MAX];
	unsigned int write_msg_len;
	unsigned int val_shift;
	unsigned int val_max;
};

enum {
	IA_BOOT_SETUP_NREQ = 0,
	IA_BOOT_SETUP_REQ = 1,
};

enum {
	IA_MSG_READ,
	IA_MSG_WRITE,
};

/* Bypass Mode ON OFF */
enum iacore_bypass_mode {
	IA_BYPASS_OFF,
	IA_BYPASS_ON,
	IA_BYPASS_SOUNDWIRE_DHWPT = 4,
};

/* IA VS ALGO MEMORY SET/RESET */
enum {
	IA_ALGO_MEM_RESET = 0,
	IA_ALGO_MEM_SET,
};

/* chip Power On Off */
enum {
	IA_POWER_OFF,
	IA_POWER_ON,
};

/*Set Audio port clock Frequency values*/
enum {
	IA_16KHZ_16BIT_2CH = 0x0000, /*512 kHz*/
	IA_24KHZ_16BIT_2CH = 0x0001, /*768 kHz*/
	IA_32KHZ_16BIT_2CH = 0x0002, /*1024 kHz*/
	IA_48KHZ_16BIT_2CH = 0x0003, /*1536 kHz*/
	IA_64KHZ_16BIT_2CH = 0x0004, /*2048 kHz*/
	IA_96KHZ_16BIT_2CH = 0x0005, /*3072 kHz*/
	IA_192KHZ_16BIT_2CH = 0x0006, /*6144 kHz*/
	IA_384KHZ_16BIT_2CH = 0x0007, /*12288 kHz*/
};

/* Routes */
enum iacore_audio_routes {
	IA_1CH_PCM_IN0_PCM_OUT0			= 0x0000,
	IA_1CH_PDM_IN0_PDM_OUT0			= 0x0001,
	IA_1CH_PDM_IN0_PDM_OUT1			= 0x0002,
	IA_1CH_SDW_PCM_IN_SDW_PCM_OUT		= 0x0003,
	IA_1CH_PDM_IN0_PCM_OUT0_LL		= 0x0004,
	IA_1CH_PDM_IN0_SDW_PCM_OUT_LL		= 0x0005,
	IA_1CH_VQ_PDM_IN0			= 0x0006,
	IA_1CH_VQ_PDM_IN2			= 0x0007,
	IA_2CH_VQ_BUFFERED_PDM_IN		= 0x0008,
	IA_1CH_PDM_IN0_PCM_OUT0			= 0x0009,
	IA_1CH_PDM_IN0_SDW_PCM_OUT		= 0x000A,
	IA_1CH_SDW_PDM_IN_SDW_PCM_OUT		= 0x000B,
	IA_1CH_PDM_IN0_SDW_PDM_OUT		= 0x000C,
	IA_1CH_SDW_PDM_IN_SDW_PDM_OUT		= 0x000D,
	IA_1CH_TX_AUDIO_BURST_PCM_OUT0		= 0x000E,
	IA_1CH_TX_AUDIO_BURST_SDW_PCM_OUT	= 0x000F,
	IA_2CH_IN_48K_PCM_PDM_IN_VQ		= 0x001B,
	IA_2CH_IN_48K_PCM_PDM_IN_VQ_1CH_PCM_OUT	= 0x001C,
	IA_1CH_IN_48K_PCM_PDM_IN_VQ		= 0x001D,
	IA_2CH_IN_1CH_IOSTREAM_BARGEIN		= 0x001F,
	IA_2CH_48K_PCM_PDM_IN_NO_OUT		= 0x0020,
	IA_ROUTE_SELECTED_NONE = 0xFFFF
};

/* Maximum size of keyword parameter block in bytes. */
#define IACORE_VS_KEYWORD_PARAM_MAX 512

/* Base name used by character devices. */
#define IACORE_CDEV_NAME "adnc"
#define IACORE_CVQ_DEV_NAME "adnc-cvq"
#define IACORE_RAW_DEV_NAME "iaraw"

/* device ops table for streaming operations */
struct ia_stream_device {
	int (*open)(struct iacore_priv *iacore);
	int (*read)(struct iacore_priv *iacore, void *buf, int len);
	int (*close)(struct iacore_priv *iacore);
	int (*wait)(struct iacore_priv *iacore);
	int (*config)(struct iacore_priv *config);
};

/*Generic boot ops*/
struct iacore_boot_ops {
	int (*interface_detect)(struct iacore_priv *iacore);
	int (*setup)(struct iacore_priv *iacore, bool bootsetup);
	int (*sbl_bypass_setup)(struct iacore_priv *iacore);
	int (*finish)(struct iacore_priv *iacore);
};

/*Generic Bus ops*/
struct iacore_bus_ops {
	int (*open)(struct iacore_priv *iacore);
	int (*read)(struct iacore_priv *iacore, void *buf, int len);
	int (*write)(struct iacore_priv *iacore, const void *buf, int len);
	int (*block_write)(struct iacore_priv *iacore, const void *buf,
			   int len);
	int (*close)(struct iacore_priv *iacore);
	int (*wait)(struct iacore_priv *iacore);
	int (*wakeup)(struct iacore_priv *iacore);

	int (*cmd)(struct iacore_priv *iacore, u32 cmd, u32 *resp);
	u32 (*cpu_to_bus)(struct iacore_priv *iacore, u32 resp);
	u32 (*bus_to_cpu)(struct iacore_priv *iacore, u32 resp);
	int (*rdb)(struct iacore_priv *iacore, void *buf, size_t len);
};

/*iacore reset chip ops */
struct iacore_reset_bus_ops {
	int (*read)(struct iacore_priv *iacore, u8 reset_reg, u8 *value);
	int (*write)(struct iacore_priv *iacore, u8 reset_reg, u8 value);
};

/*Generic bus function*/
struct iacore_bus {
	struct iacore_reset_bus_ops reset_ops;
	struct iacore_bus_ops ops;
	int (*setup_bus_intf)(struct iacore_priv *iacore);
	u32 last_response;
};

struct iacore_macro {
	u32 cmd;
	u32 resp;
	struct timespec timestamp;
};

#if (defined(CONFIG_ARCH_MSM) || defined(CONFIG_ARCH_MT6797) ||  defined(CONFIG_ARCH_QCOM))
struct pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *wake_active;
	struct pinctrl_state *irq_active;

	u32 has_pinctrl;
};
#endif

#define	IA_MAX_ROUTE_MACRO_CMD		300
/* Max size of cmd_history line */
#define IA_MAX_CMD_HISTORY_LINE_SIZE	100
#define	IA_MAX_CMD_HISTORY_COUNT	300

extern struct iacore_macro cmd_hist[IA_MAX_CMD_HISTORY_COUNT];
extern int cmd_hist_index;

struct iacore_priv {
	struct device *dev;
	struct snd_soc_codec *codec;
	const char *vs_filename;

	const char *bargein_filename;

	u8 fw_state;
	u32 fw_type;		/* VoiceQ or Barge-in */
	bool skip_boot_setup;

	void *voice_sense;
	u8 bypass_mode;
	int  ia6xx_i2s_config;

	struct iaxxx_platform_data *pdata;
	struct ia_stream_device streamdev;

	struct regulator *pwr_vdd;

	struct iacore_boot_ops boot_ops;
	struct iacore_bus bus;
	int (*probe)(struct device *dev);
	int (*remove)(struct device *dev);

	int ia_streaming_mode;
	int cmd_history_size;

	void (*device_recovery)(struct iacore_priv *iacore);

	struct timespec last_resp_time;

	struct mutex streaming_mutex;
	struct mutex access_lock;
	struct mutex uart_lock;
	struct mutex irq_lock;
	struct wake_lock wakelock;

	/* used to lock race b/w CVQ & Bypass mode threads */
	//struct mutex fw_state_mutex;

#if (defined(CONFIG_ARCH_MSM) ||  defined(CONFIG_ARCH_QCOM))
	struct regulator *vcc_i2c;
	bool i2c_pull_up;
#endif

#if (defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_ARCH_MT6797)|| defined(CONFIG_SND_SOC_SDM845))
	struct pinctrl_info pnctrl_info;
#endif

	struct snd_soc_codec_driver *codec_drv;
	struct snd_soc_dai_driver *dai;
	u32 dai_nr;
	u32 api_addr_max;

	struct iacore_api_access *api_access;
	void *priv;

	u8 uart_ready;
	atomic_t uart_users;

	int codec_ismaster;

	u32 iacore_event_type;
	//struct mutex iacore_event_type_mutex;

	u32 disable_fw_recovery_cnt;
	bool in_recovery;
	unsigned char *debug_buff;
	u32 rdb_buf_len;
	/* sysfs related entries for iacore */
	struct kobject kobj;
	struct kset *kset;
	void *dev_data;
	enum iacore_audio_routes selected_route;
	bool active_route_set;
	bool low_latency_route_set;
	bool skip_stop_route_cmd;

	bool irq_enabled;
	bool irq_event_detected;
	void *cdev_priv;
	void *i2s_perf;

	wait_queue_head_t cvq_wait_queue;
	u32 iacore_cv_kw_detected;

	bool need_16bit_spi_swap;

	int raw_buffered_read_sts;

	wait_queue_head_t irq_waitq;

	u32 spl_pdm_mode;
#ifdef FW_SLEEP_TIMER_TEST
	struct timer_list fs_timer;
	spinlock_t fs_tmr_lock;
	wait_queue_head_t fs_thread_wait;
	u32 fs_thread_wait_flg;
	struct task_struct *fs_thread;
#endif

	bool dbg_fw;

	struct task_struct *uart_thread;
	wait_queue_head_t uart_wait_q;
	wait_queue_head_t uart_compl_q;
	struct mutex uart_thread_mutex;
	atomic_t uart_thread_cond;
};

struct iacore_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct iacore_priv *iacore,
			struct iacore_sysfs_attr *attr, char *buf);
	ssize_t (*store)(struct iacore_priv *iacore,
			struct iacore_sysfs_attr *attr, const char *buf,
			size_t count);
};
#define to_iacore_priv(x) container_of(x, struct iacore_priv, kobj)
#define IACORE_ATTR(_name, _mode, _show, _store) \
	struct iacore_sysfs_attr attr_##_name = __ATTR(_name, _mode, _show, \
						       _store)
#define to_iacore_priv_attr(x) container_of(x, struct iacore_sysfs_attr, attr)

#define iacore_resp(obj) ((obj)->bus.last_response)

int ia6xx_dai_nr(void);
extern int iacore_power_init(struct iacore_priv *iacore);
extern void iacore_power_ctrl(struct iacore_priv *iacore, bool value);
extern int iacore_cmd(struct iacore_priv *iacore, u32 cmd, u32 *resp);
int iacore_cmd_nopm(struct iacore_priv *iacore, u32 cmd, u32 *resp);
int iacore_cmd_locked(struct iacore_priv *iacore, u32 cmd, u32 *resp);
extern int iacore_write_block(struct iacore_priv *iacore,
		const u32 *cmd_block);
extern int iacore_read(struct iacore_priv *iacore,
			       unsigned int reg);
extern int iacore_write(struct iacore_priv *iacore, unsigned int reg,
		       unsigned int value);
int iacore_put_streaming_mode(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol);
int iacore_get_streaming_mode(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol);
int iacore_get_rdb_size(struct iacore_priv *iacore, int id);
extern int iacore_datablock_read(struct iacore_priv *iacore, void *buf,
					size_t len);
extern int iacore_datablock_write(struct iacore_priv *iacore, const void *buf,
				  size_t wdb_len, size_t actual_len);
int iacore_datablock_open(struct iacore_priv *iacore);
int iacore_datablock_close(struct iacore_priv *iacore);
extern int iacore_datablock_wait(struct iacore_priv *iacore);
int iacore_register_snd_codec(struct iacore_priv *iacore);

int iacore_wakeup_unlocked(struct iacore_priv *iacore);
void iacore_request_fw_cb(const struct firmware *fw, void *context);

extern irqreturn_t iacore_irq_work(int irq, void *data);

//#ifdef CONFIG_PM_RUNTIME
#ifdef CONFIG_PM
void iacore_pm_enable(struct iacore_priv *iacore);
void iacore_pm_vs_enable(struct iacore_priv *iacore, bool value);
void iacore_pm_put_autosuspend(struct iacore_priv *iacore);
int iacore_pm_put_sync_suspend(struct iacore_priv *iacore);
int iacore_pm_get_sync(struct iacore_priv *iacore);

int iacore_pm_runtime_suspend(struct device *dev);
int iacore_pm_runtime_resume(struct device *dev);
int iacore_pm_suspend(struct device *dev);
int iacore_pm_resume(struct device *dev);
void iacore_pm_complete(struct device *dev);

#else

static inline void iacore_pm_enable(struct iacore_priv *iacore) {}
static inline void iacore_pm_vs_enable(struct iacore_priv *iacore,
								bool value) {}
static inline void iacore_pm_put_autosuspend(struct iacore_priv *iacore) {}


static inline int iacore_pm_put_sync_suspend(struct iacore_priv *iacore)
	{ return 0; }

static inline int iacore_pm_get_sync(struct iacore_priv *iacore)
	{ return 0; }

static inline void iacore_pm_complete(struct device *dev)
	{ return; }

#endif

int iacore_platform_init(void);
int iacore_fw_recovery_unlocked(struct iacore_priv *iacore, int is_forced);
int iacore_collect_rdb(struct iacore_priv *iacore);
int read_debug_data(struct iacore_priv *iacore, void *buf);

#define IA_SELECT_STREAMING		0x9028
#define IA_RX_ENDPOINT			0x000C
#define IA_STRM_MGR_TX0_ENDPOINT	0x0008
#define IA_BAF_MGR_TX0_ENDPOINT		0x001C

#define IA_SET_DIGI_GAIN               0x8015
#define IA_10DB_DIGI_GAIN              0x0C0A
#define IA_DIGI_GAIN              0x0C00

#define IA_SET_IOSTREAMING		0x9025
#define IACORE_IOSTREAM_DISABLE		0
#define IACORE_IOSTREAM_ENABLE		1

#ifdef CONFIG_SND_SOC_IA_SPI
#define IA_STOP_IOSTREAM_BURST_CMD	0x8025
#else
#define IA_STOP_IOSTREAM_BURST_CMD	0x9025
#endif

#ifdef CONFIG_SND_SOC_IA_I2S_PERF
#define IACORE_STREAM_DISABLE	0x10
#define IACORE_STREAM_ENABLE	0x11
#else
#define IACORE_STREAM_DISABLE	0
#define IACORE_STREAM_ENABLE	1
#endif
#define IACORE_CVQ_STREAM_BURST_I2S_CMD		0x8026
#define IACORE_CVQ_STREAM_BURST_I2S_START	0x0011
#define IACORE_CVQ_STREAM_BURST_I2S_STOP	0x0010
#define IACORE_SET_START_STREAM_BURST_CMD	0x90260000
#ifdef CONFIG_SND_SOC_IA_SPI
#define IACORE_SET_STOP_STREAM_BURST_CMD	0x80260000
#else
#define IACORE_SET_STOP_STREAM_BURST_CMD	0x90260000
#endif

#define IACORE_CHIP_SOFT_RESET_CMD	0x90030001

/* Take api_mutex before calling this function */
static inline void update_cmd_history(u32 cmd, u32 resp)
{
	cmd_hist[cmd_hist_index].cmd = cmd;
	cmd_hist[cmd_hist_index].resp = resp;
	get_monotonic_boottime(&cmd_hist[cmd_hist_index].timestamp);
	cmd_hist_index = (cmd_hist_index + 1) % IA_MAX_ROUTE_MACRO_CMD;
}

extern int ia6xx_core_probe(struct device *dev);
int iacore_stop_bypass_unlocked(struct iacore_priv *iacore);
int iacore_change_state_unlocked(struct iacore_priv *iacore, u8 new_state);
int iacore_change_state_lock_safe(struct iacore_priv *iacore, u8 new_state);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 10))
#define iacore_kcontrol_codec snd_soc_kcontrol_codec
#else
#define iacore_kcontrol_codec snd_kcontrol_chip
#endif

u8 iacore_get_power_state(struct iacore_priv *iacore);
#ifdef FW_SLEEP_TIMER_TEST
void iacore_fw_sleep_timer_fn(unsigned long data);
int iacore_fw_sleep_thread(void *ptr);
int setup_fw_sleep_timer(struct iacore_priv *iacore, u32 force);
int setup_fw_sleep_timer_unlocked(struct iacore_priv *iacore, u32 timeout);
#endif
int iacore_chip_softreset(struct iacore_priv *iacore, bool put_to_sleep);
const char *iacore_fw_state_str(u8 state);
bool iacore_fw_loaded_unlocked(struct iacore_priv *iacore);
bool iacore_fw_loaded_lock_safe(struct iacore_priv *iacore);
int iacore_check_and_reload_fw_unlocked(struct iacore_priv *iacore);
void iacore_poweroff_chip(struct iacore_priv *iacore);
int iacore_recover_chip_to_fw_sleep_unlocked(struct iacore_priv *iacore);
int iacore_set_chip_sleep(struct iacore_priv *iacore, u32 sleep_state);
long ia_cvq_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long ia_cvq_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void iacore_enable_irq(struct iacore_priv *iacore);
void iacore_disable_irq(struct iacore_priv *iacore);
void iacore_disable_irq_nosync(struct iacore_priv *iacore);
#ifdef CONFIG_SND_SOC_IA_I2S_PERF
bool iacore_i2sperf_active(struct iacore_priv *iacore);
int iacore_i2sperf_wdb_prepare(struct iacore_priv *iacore);
int iacore_i2sperf_bursting_prepare(struct iacore_priv *iacore);
void iacore_i2sperf_bursting_done(struct iacore_priv *iacore);
#endif

int iacore_uart_thread_enable(struct iacore_priv *iacore, int enable);
int ia_uart_kthread_init(struct iacore_priv *iacore);
void ia_uart_kthread_exit(struct iacore_priv *iacore);

#define IACORE_LOCK_DEBUG
#ifdef IACORE_LOCK_DEBUG
#define IACORE_MUTEX_LOCK(lock)			\
{							\
	pr_debug("Acquiring Mutex\n");	\
	mutex_lock(lock);		\
	pr_debug("Acquiring Mutex done\n");	\
}

#define IACORE_MUTEX_UNLOCK(lock)			\
{							\
	pr_debug("Release Mutex\n");	\
	mutex_unlock(lock);	\
	pr_debug("Release Mutex done\n");	\
}
#else

#define IACORE_MUTEX_LOCK(lock)     mutex_lock(lock);
#define IACORE_MUTEX_UNLOCK(lock)   mutex_unlock(lock);

#endif

#endif /* _IACORE_H */
