/*
 * iacore-vs.h  --  voice sense interface for Audience ia6xx chips
 *
 * Copyright 2011-2013 Audience, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IACORE_VS_H
#define _IACORE_VS_H

/* KW model files info */
#define IA_INIT_PARAMS		_IO('T', 0x011)
#define IA_CVQ_START		_IO('T', 0x012)
#define IA_LOAD_KEYWORDS	_IO('T', 0x013)
#define IA_UNLOAD_KEYWORDS	_IO('T', 0x014)
#define IA_CVQ_STOP		_IO('T', 0x015)
#define IA_GET_KW_ID		_IO('T', 0x016)
#define IA_IS_PERFMODE		_IO('T', 0x017)
#define IA_LOAD_FIRMWARE	_IO('T', 0x018)
#define IA_INIT_IOSTREAM_PARAMS	_IO('T', 0x019)
#define IA_SETUP_BARGEIN	_IO('T', 0x01A)

#define MAX_KW_MODELS 5
#define IA_SYSCONFIG_MAJOR_OFFSET	0x20
#define IA_BOSKO_MAJOR_OFFSET		0x20

struct ia_kw_priv {
	u64	kw_buff_addr;

	/* Actual length after adding wdb headers and padding bytes */
	u32	kw_size;
	/* Length without the padding bytes and the wdb headers */
	u32	wdb_size;
};

struct ia_kw_info {
	struct	ia_kw_priv kw[MAX_KW_MODELS];
	u32	kw_count;
} __packed;

enum ia_cvq_rate {
	IA_8KHZ = 0,
	IA_16KHZ = 1,
	IA_24KHZ = 2,
	IA_48KHZ = 4,
};

enum ia_vq_mode {
	IA_VQ_MODE = 0,
	IA_CVQ_MODE,
	IA_BARGEIN_MODE,
};

enum ia_vad_mode {
	IA_NO_VAD = 0,
	IA_MIC_VAD,
};

enum ia_kw_preserve {
	IA_IGNORE_KW = 0,
	IA_PRESERVE_KW,
};

enum ia_format {
	IA_FORMAT_Q11 = 1,
	IA_FORMAT_Q15 = 2,
};

enum ia_frame_size {
	IA_1MS_FRAME = 1,
	IA_2MS_FRAME = 2,
	IA_8MS_FRAME = 8,
	IA_10MS_FRAME = 10,
	IA_15MS_FRAME = 15,
	IA_16MS_FRAME = 16,
};

enum ia_perf_mode {
	IA_NON_PERF_MODE = 0,
	IA_I2S_PERF_MODE,
};

#define PRESET_NONE			0
#define PRESET_VAD_ON_CVQ_KW_PRSRV	1
#define PRESET_VAD_ON_CVQ_NO_KW_PRSRV	2
#define PRESET_VAD_ON_VQ_NO_BUFFERING	3
#define PRESET_VAD_OFF_CVQ_KW_PRSRV	4
#define PRESET_VAD_OFF_CVQ_NO_KW_PRSRV	5
#define PRESET_VAD_OFF_VQ_NO_BUFFERING	6

#define PRESET_SPECIAL_PDM_48K		7
#define PRESET_I2S_RECORDING_48K	8
#define PRESET_SPECIAL_PDM_96K		9

#define PRESET_MASK			0xF

struct ia_cvq_params {
	u8 rate;
	u8 mode;
	u8 format;
	u8 frame_size;
	u8 kw_preserve;
	u8 vad;

	u8 vq_preset;
	//u8 bargein_preset;

	u8 oem_sense;
	u8 user_sense;
	u8 vid_sense;

	u8 endpoint1;
	u8 endpoint2;
};

#define DEF_OEM_KW_SENSITIVITY_THRESHOLD	0
#define DEF_USER_KW_SENSITIVITY_THRESHOLD	0
#define DEF_VOICEID_KW_SENSITIVITY_THRESHOLD	5

/* voice sense private data structure */
struct iacore_voice_sense {
	const struct firmware *vs;
	const struct firmware *hw;

	const struct firmware *bargein_sysconfig;
	const struct firmware *bargein_hw;

	struct ia_kw_info *kw_info;
	struct ia_cvq_params params;
	struct ia_cvq_params iostream_params;

	u32 oem_kw_sensitivity;
	u32 usr_kw_sensitivity;
	u32 voiceid_kw_sensitivity;

	bool cvq_sleep;
	bool kw_model_loaded;

	bool bargein_sts;
	bool rec_to_bargein;
	bool bargein_vq_enabled;
	bool cvq_stream_on_i2s_req;
};

#define GET_VS_PRIV(iacore) \
	((!iacore || !iacore->voice_sense) ? NULL : iacore->voice_sense)

extern int iacore_vs_init(struct iacore_priv *iacore);
extern void iacore_vs_exit(struct iacore_priv *iacore);

int iacore_vs_configure_unlocked(struct iacore_priv *iacore);
void iacore_release_models(struct iacore_priv *iacore);
int iacore_set_iostreaming_unlocked(struct iacore_priv *iacore, int value);
int iacore_set_vs_sleep(struct iacore_priv *iacore);
int iacore_stop_active_route_unlocked(struct iacore_priv *iacore);
int iacore_set_active_route(struct iacore_priv *iacore,
			bool ll_route, u32 route);
int iacore_load_firmware_unlocked(struct iacore_priv *iacore);
bool iacore_check_fw_reload(struct iacore_priv *iacore);
int iacore_reload_fw_unlocked(struct iacore_priv *iacore, u32 fw_state);
//int ia6xx_fw_download(void);
int iacore_set_bargein_vq_mode(struct iacore_priv *iacore, u32 enable);
int iacore_setup_bargein_mode_unlocked(struct iacore_priv *iacore, bool value);

static inline u8 is_vs_enabled(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);

	if (!voice_sense) {
		pr_err("invalid private pointer\n");
		return -EINVAL;
	}

	return voice_sense->cvq_sleep;
}

static inline void vs_reset_model_file(struct iacore_priv *iacore)
{
	struct iacore_voice_sense *voice_sense = GET_VS_PRIV(iacore);
	if (voice_sense)
		voice_sense->kw_model_loaded = false;
	else
		pr_err("Invalid voicesense private pointer\n");
}

#endif /* _IACORE_VS_H */
