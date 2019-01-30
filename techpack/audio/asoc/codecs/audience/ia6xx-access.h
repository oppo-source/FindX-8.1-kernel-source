/*
 * ia6xx-access.h  --  IA6XX Soc Audio access values
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IA6XX_ACCESS_H
#define _IA6XX_ACCESS_H

#define IA6XX_API_WORD(upper, lower) ((upper << 16) | lower)

static struct iacore_api_access ia6xx_api_access[IA6XX_API_ADDR_MAX] = {
	[IA6XX_ALGO_PROCESSING] = {
		.read_msg = { IA6XX_API_WORD(0x8043, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x801c, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
	[IA6XX_CHANGE_STATUS] = {
		.read_msg = { IA6XX_API_WORD(0x804f, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x804f, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 4,
	},
	[IA6XX_FW_FIRST_CHAR] = {
		.read_msg = { IA6XX_API_WORD(0x8020, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8020, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	[IA6XX_FW_NEXT_CHAR] = {
		.read_msg = { IA6XX_API_WORD(0x8021, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8021, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	[IA6XX_EVENT_RESPONSE] = {
		.read_msg = { IA6XX_API_WORD(0x801a, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x901a, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 4,
	},
	[IA6XX_VOICE_SENSE_ENABLE] = {
		.read_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
	[IA6XX_VOICE_SENSE_SET_KEYWORD] = {
		.read_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 4,
	},
	[IA6XX_VOICE_SENSE_EVENT] = {
		.read_msg = { IA6XX_API_WORD(0x806d, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 2,
	},
	[IA6XX_VOICE_SENSE_TRAINING_MODE] = {
		.read_msg = { IA6XX_API_WORD(IA6XX_GET_ALGO_PARAM, 0x5003) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM_ID, 0x5003),
				IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM, 0x0000) },
		.write_msg_len = 8,
		.val_shift = 0,
		.val_max = 2,
	},
	[IA6XX_VOICE_SENSE_DETECTION_SENSITIVITY] = {
		.read_msg = { IA6XX_API_WORD(IA6XX_GET_ALGO_PARAM, 0x5004) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM_ID, 0x5004),
			       IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM, 0x0000) },
		.write_msg_len = 8,
		.val_shift = 0,
		.val_max = 10,
	},
	[IA6XX_VOICE_ACTIVITY_DETECTION_SENSITIVITY] = {
		.read_msg = { IA6XX_API_WORD(IA6XX_GET_ALGO_PARAM, 0x5005) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM_ID, 0x5005),
			       IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM, 0x0000) },
		.write_msg_len = 8,
		.val_shift = 0,
		.val_max = 10,
	},
	[IA6XX_VOICE_SENSE_TRAINING_RECORD] = {
		.read_msg = { IA6XX_API_WORD(IA6XX_GET_ALGO_PARAM, 0x5006) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM_ID, 0x5006),
			       IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM, 0x0000) },
		.write_msg_len = 8,
		.val_shift = 0,
		.val_max = 2,
	},
	[IA6XX_VOICE_SENSE_TRAINING_STATUS] = {
		.read_msg = { IA6XX_API_WORD(IA6XX_GET_ALGO_PARAM, 0x5007) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM_ID, 0x5007),
			       IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM, 0x0000) },
		.write_msg_len = 8,
		.val_shift = 0,
		.val_max = 7,
	},
	[IA6XX_VOICE_SENSE_TRAINING_MODEL_LENGTH] = {
		.read_msg = { IA6XX_API_WORD(IA6XX_GET_ALGO_PARAM, 0x500A) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM_ID, 0x500A),
			       IA6XX_API_WORD(IA6XX_SET_ALGO_PARAM, 0x0000) },
		.write_msg_len = 8,
		.val_shift = 0,
		.val_max = 75,
	},
	[IA6XX_VOICE_SENSE_DEMO_ENABLE] = {
		.read_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
	[IA6XX_VS_STORED_KEYWORD] = {
		.read_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8000, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
	[IA6XX_VS_INT_OSC_MEASURE_START] = {
		.read_msg = { IA6XX_API_WORD(0x8070, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x9070, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
	[IA6XX_VS_INT_OSC_MEASURE_STATUS] = {
		.read_msg = { IA6XX_API_WORD(0x8071, 0x0000) },
		.read_msg_len = 4,
		.write_msg = { IA6XX_API_WORD(0x8071, 0x0000) },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
};

#endif /* _IA6XX_ACCESS_H */
