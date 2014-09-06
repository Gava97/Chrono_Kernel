/*
 * Analog Devices ADAU1373 Audio Codec drive
 *
 * Copyright 2011 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __SOUND_ADAU1373_H__
#define __SOUND_ADAU1373_H__

struct adau1373_platform_data {
	bool input_differential[4];
	bool lineout_differential;
	bool lineout_ground_sense;

	unsigned int num_drc;
	uint8_t drc_setting[3][13];
};

#endif
