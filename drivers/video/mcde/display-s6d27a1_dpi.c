/*
 * S6D27A1 LCD DPI panel driver.
 *
 * Author: Gareth Phillips  <gareth.phillips@samsung.com>
 *
 * Derived from drivers/video/mcde/display-ws2401_dpi.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/wait.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/mfd/dbx500-prcmu.h>

#include <video/mcde_display.h>
#include <video/mcde_display-dpi.h>
#include <video/mcde_display_ssg_dpi.h>


#define ESD_PORT_NUM 			93
#define SPI_COMMAND			0
#define SPI_DATA			1

#define LCD_POWER_UP			1
#define LCD_POWER_DOWN			0
#define LDI_STATE_ON			1
#define LDI_STATE_OFF			0
/* Taken from the programmed value of the LCD clock in PRCMU */
#define PIX_CLK_FREQ			25000000
#define VMODE_XRES			480
#define VMODE_YRES			800
#define POWER_IS_ON(pwr)		((pwr) <= FB_BLANK_NORMAL)

#define MIN_BRIGHTNESS			0
#define MAX_BRIGHTNESS			255
#define DEFAULT_BRIGHTNESS		120

#define DCS_CMD_S6D27A1_RESCTL		0xB3	/* Resolution Select Control */
#define DCS_CMD_S6D27A1_PANELCTL2	0xB4	/* ASG Signal Control */
#define DCS_CMD_S6D27A1_BCMODE		0xC1	/* BC Mode */
#define DCS_CMD_S6D27A1_WRDISBV		0x51	/* Write Manual Brightness */
#define DCS_CMD_S6D27A1_WRCTRLD		0x53	/* Write BL Control */
#define DCS_CMD_S6D27A1_READID1		0xDA	/* Read panel ID 1 */
#define DCS_CMD_S6D27A1_READID2		0xDB	/* Read panel ID 2 */
#define DCS_CMD_S6D27A1_READID3		0xDC	/* Read panel ID 3 */
#define DCS_CMD_S6D27A1_PASSWD_L2	0xF0	/* Password1 Command for Level2 */
#define DCS_CMD_S6D27A1_DISPCTL		0xF2	/* Display Control */
#define DCS_CMD_S6D27A1_MANPWR		0xF3	/* Manual Control */
#define DCS_CMD_S6D27A1_PWRCTL1		0xF4	/* Power Control */
#define DCS_CMD_S6D27A1_SRCCTL		0xF6	/* Source Control */
#define DCS_CMD_S6D27A1_PANELCTL 	0xF7	/* Panel Control*/
#define DCS_CMD_S6D27A1_PGAMMACTL	0xFA	/* Positive Gamma Control */
#define DCS_CMD_S6D27A1_NGAMMACTL	0xFB	/* Negative Gamma Control */



#define DCS_CMD_SEQ_DELAY_MS	0xFE
#define DCS_CMD_SEQ_END		0xFF

#define DPI_DISP_TRACE	dev_dbg(&ddev->dev, "%s\n", __func__)

/* S6D27A1 PRCMU LCDCLK */
/* 60+++	79872000 unsafe
 * 60++ 	62400000 unsafe
 * 60+  	57051428 unsafe
 * 60   	49920000
 * 50   	39936000
 * 45   	36305454
 * 40   	33280000
 */
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/mfd/db8500-prcmu.h>

#define LCDCLK_SET(clk) prcmu_set_clock_rate(PRCMU_LCDCLK, (unsigned long) clk);

struct lcdclk_prop
{
	char *name;
	unsigned int clk;
};

static struct lcdclk_prop lcdclk_prop[] = {
  	[0] = {
		.name = "60++ Hz",
		.clk = 62400000,
	},
  	[1] = {
		.name = "60+ Hz",
		.clk = 57051428,
	},
	[2] = {
		.name = "60 Hz",
		.clk = 49920000,
	},
	[3] = {
		.name = "50 Hz",
		.clk = 39936000,
	},
	[4] = {
		.name = "45 Hz",
		.clk = 36305454,
	},
	[5] = {
		.name = "40 Hz",
		.clk = 33280000,
	},
};

static unsigned int lcdclk_usr = 2; /* 60 fps */

static void s6d27a1_lcdclk_thread(struct work_struct *ws2401_lcdclk_work)
{
	msleep(200);

	pr_err("[s6d27a1] LCDCLK %dHz\n", lcdclk_prop[lcdclk_usr].clk);

	LCDCLK_SET(lcdclk_prop[lcdclk_usr].clk);
}
static DECLARE_WORK(s6d27a1_lcdclk_work, s6d27a1_lcdclk_thread);

/* to be removed when display works */
//#define dev_dbg	dev_info
#define ESD_OPERATION
/*
#define ESD_TEST
*/

struct s6d27a1_dpi {
	struct device				*dev;
	struct spi_device			*spi;
	struct mutex				lock;
	unsigned int				beforepower;
	unsigned int				power;
	unsigned int				current_brightness;
	unsigned int				bl;
	bool					turn_on_backlight;
	unsigned int				ldi_state;
	unsigned char				panel_id;
	bool 					opp_is_requested;
	enum mcde_display_rotation		rotation;
	struct mcde_display_device		*mdd;
	struct lcd_device			*ld;
	struct backlight_device			*bd;
	struct ssg_dpi_display_platform_data	*pd;
	struct spi_driver			spi_drv;

#ifdef ESD_OPERATION
	unsigned int				lcd_connected;
	unsigned int				esd_enable;
	bool					esd_processing;
	unsigned int				esd_port;
	struct workqueue_struct			*esd_workqueue;
	struct work_struct			esd_work;
#ifdef ESD_TEST
	struct timer_list			esd_test_timer;
#endif
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend			earlysuspend;
#endif
};

#ifdef ESD_TEST
struct s6d27a1_dpi *pdpi;
#endif
static const u8 DCS_CMD_SEQ_S6D27A1_INIT[] = {
/*	Length	Command				Parameters */
	3,	DCS_CMD_S6D27A1_PASSWD_L2,	0x5A, 0x5A,			/* Unlock L2 Cmds */
	2,	DCS_CMD_S6D27A1_RESCTL,		0x22,				/* 480RGB x 800 */
	9,	DCS_CMD_S6D27A1_PANELCTL2,	0x00, 0x02, 0x03, 0x04, 0x05,
						0x08, 0x00, 0x0c,
	8,	DCS_CMD_S6D27A1_MANPWR,		0x01, 0x00, 0x00, 0x08, 0x08,
						0x02, 0x00,
	8,	DCS_CMD_S6D27A1_DISPCTL,	0x19, 0x00, 0x08, 0x0D, 0x03,
						0x41, 0x3F,
	11,	DCS_CMD_S6D27A1_PWRCTL1,	0x00, 0x00, 0x00, 0x00, 0x55,
						0x44, 0x05, 0x88, 0x4B, 0x50,
	7,	DCS_CMD_S6D27A1_SRCCTL,		0x03, 0x09, 0x8A, 0x00, 0x01,
						0x16,
	38,	DCS_CMD_S6D27A1_PANELCTL,	0x00, 0x05, 0x06, 0x07, 0x08,
						0x01, 0x09, 0x0D, 0x0A, 0x0E,
						0x0B, 0x0F, 0x0C, 0x10, 0x01,
						0x11, 0x12, 0x13, 0x14, 0x05,
						0x06, 0x07, 0x08, 0x01, 0x09,
						0x0D, 0x0A, 0x0E, 0x0B, 0x0F,
						0x0C, 0x10, 0x01, 0x11, 0x12,
						0x13, 0x14,
/*
	49,	DCS_CMD_S6D27A1_PGAMMACTL,	0x00, 0x02, 0x01, 0x08, 0x00,
						0x03, 0x00, 0x0A, 0x0B, 0x10,
						0x15, 0x15, 0x15, 0x1D, 0x11,
						0x06, 0x01, 0x02, 0x01, 0x08,
						0x00, 0x03, 0x00, 0x0B, 0x0C,
						0x11, 0x15, 0x15, 0x15, 0x1D,
						0x11, 0x06, 0x2D, 0x02, 0x01,
						0x08, 0x01, 0x05, 0x01, 0x0E,
						0x11, 0x19, 0x23, 0x28, 0x31,
						0x0C, 0x35, 0x2F,
	49,	DCS_CMD_S6D27A1_NGAMMACTL,	0x00, 0x02, 0x01, 0x08, 0x00,
						0x03, 0x00, 0x0A, 0x0B, 0x10,
						0x15, 0x15, 0x15, 0x1D, 0x11,
						0x06, 0x01, 0x02, 0x01, 0x08,
						0x00, 0x03, 0x00, 0x0B, 0x0C,
						0x11, 0x15, 0x15, 0x15, 0x1D,
						0x11, 0x06, 0x2D, 0x02, 0x01,
						0x08, 0x01, 0x05, 0x01, 0x0E,
						0x19, 0x11, 0x23, 0x28, 0x31,
						0x35, 0x3C, 0x2F,
*/						
	3,	DCS_CMD_S6D27A1_PASSWD_L2,	0xA5, 0xA5,

	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_ENABLE_BACKLIGHT_CONTROL[] = {
/*	Length	Command				Parameters */
	2,	DCS_CMD_S6D27A1_WRCTRLD,	0x2C,
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_DISABLE_BACKLIGHT_CONTROL[] = {
/*	Length	Command				Parameters */
	2,	DCS_CMD_S6D27A1_BCMODE,		0x00,
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_ENTER_SLEEP_MODE[] = {
/*	Length	Command				Parameters */
	1,	DCS_CMD_ENTER_SLEEP_MODE,
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_EXIT_SLEEP_MODE[] = {
/*	Length	Command				Parameters */
	1,	DCS_CMD_EXIT_SLEEP_MODE,
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_DISPLAY_ON[] = {
/*	Length	Command				Parameters */
	1,	DCS_CMD_SET_DISPLAY_ON,
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_DISPLAY_OFF[] = {
/*	Length	Command				Parameters */
	1,	DCS_CMD_SET_DISPLAY_OFF,
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_ORIENT_0[] = {
/*	Length	Command				Parameters */
	/* Flip V(d0), Flip H(d1), RGB/BGR(d3) */
	2,	DCS_CMD_SET_ADDRESS_MODE,	0x09,
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_S6D27A1_ORIENT_180[] = {
/*	Length	Command				Parameters */
	/* Flip V(d0), Flip H(d1), RGB/BGR(d3) */
	2,	DCS_CMD_SET_ADDRESS_MODE,	0x0A,
	DCS_CMD_SEQ_END
};

extern bool power_off_charging;
extern u32 sec_bootmode;

#if defined(CONFIG_MACH_CODINA_CHN) || defined(CONFIG_MACH_CODINA_EURO) || defined(CONFIG_MACH_CODINA)
extern u32 sec_lpm_bootmode;
#endif
static int s6d27a1_write_dcs_sequence(struct s6d27a1_dpi *lcd, const u8 *p_seq);

static void print_vmode(struct device *dev, struct mcde_video_mode *vmode)
{
/*
	dev_dbg(dev, "resolution: %dx%d\n",	vmode->xres, vmode->yres);
	dev_dbg(dev, "  pixclock: %d\n",	vmode->pixclock);
	dev_dbg(dev, "       hbp: %d\n",	vmode->hbp);
	dev_dbg(dev, "       hfp: %d\n",	vmode->hfp);
	dev_dbg(dev, "       hsw: %d\n",	vmode->hsw);
	dev_dbg(dev, "       vbp: %d\n",	vmode->vbp);
	dev_dbg(dev, "       vfp: %d\n",	vmode->vfp);
	dev_dbg(dev, "       vsw: %d\n",	vmode->vsw);
	dev_dbg(dev, "interlaced: %s\n", vmode->interlaced ? "true" : "false");
*/
}

static int try_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	struct ssg_dpi_display_platform_data *pdata = ddev->dev.platform_data;
	int res = -EINVAL;

	if (ddev == NULL || video_mode == NULL) {
		dev_warn(&ddev->dev, "%s:ddev = NULL or video_mode = NULL\n",
			__func__);
		return res;
	}

	if ((video_mode->xres == VMODE_XRES && video_mode->yres == VMODE_YRES) ||
	    (video_mode->xres == VMODE_YRES && video_mode->yres == VMODE_XRES)) {

		video_mode->hsw		= pdata->video_mode.hsw;
		video_mode->hbp		= pdata->video_mode.hbp;
		video_mode->hfp		= pdata->video_mode.hfp;
		video_mode->vsw		= pdata->video_mode.vsw;
		video_mode->vbp		= pdata->video_mode.vbp;
		video_mode->vfp		= pdata->video_mode.vfp;
		video_mode->interlaced	= pdata->video_mode.interlaced;
		video_mode->pixclock	= pdata->video_mode.pixclock;
		/* +445681 display padding */
		video_mode->xres_padding = ddev->x_res_padding;
		video_mode->yres_padding = ddev->y_res_padding;
		/* -445681 display padding */
		res = 0;
	}

	if (res == 0)
		print_vmode(&ddev->dev, video_mode);
	else
		dev_warn(&ddev->dev,
			"%s:Failed to find video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);

	return res;

}

static int set_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int res = -EINVAL;
	struct mcde_video_mode channel_video_mode;
	static int first_call = 1;

	struct s6d27a1_dpi *lcd = dev_get_drvdata(&ddev->dev);

	if (ddev == NULL || video_mode == NULL) {
		dev_warn(&ddev->dev, "%s:ddev = NULL or video_mode = NULL\n",
			__func__);
		goto out;
	}
	ddev->video_mode = *video_mode;
	print_vmode(&ddev->dev, video_mode);
	if ((video_mode->xres == VMODE_XRES && video_mode->yres == VMODE_YRES) ||
	    (video_mode->xres == VMODE_YRES && video_mode->yres == VMODE_XRES))
		res = 0;

	if (res < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);
		goto error;
	}

	channel_video_mode = ddev->video_mode;
	/* Dependant on if display should rotate or MCDE should rotate */
	if (ddev->rotation == MCDE_DISPLAY_ROT_90_CCW || ddev->rotation == MCDE_DISPLAY_ROT_90_CW) {
		channel_video_mode.xres = ddev->native_x_res;
		channel_video_mode.yres = ddev->native_y_res;
	}
	res = mcde_chnl_set_video_mode(ddev->chnl_state, &channel_video_mode);
	if (res < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode on channel\n",
			__func__);

		goto error;
	}
	/* notify mcde display driver about updated video mode, excepted for
	 * the first update to preserve the splash screen and avoid a
	 * stop_flow() */
	if (first_call && lcd->pd->platform_enabled)
		ddev->update_flags |= UPDATE_FLAG_PIXEL_FORMAT;
	else
		ddev->update_flags |= UPDATE_FLAG_VIDEO_MODE;

	first_call = 0;

	return res;
out:
error:
	return res;
}

static void s6d27a1_request_opp(struct s6d27a1_dpi *lcd)
{
	if ((!lcd->opp_is_requested) && (lcd->pd->min_ddr_opp > 0)) {
#if 0
	  if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
						LCD_DRIVER_NAME_S6D27A1,
						lcd->pd->min_ddr_opp)) {
			dev_err(lcd->dev, "add DDR OPP %d failed\n",
				lcd->pd->min_ddr_opp);
		}
#endif
		dev_dbg(lcd->dev, "DDR OPP requested at %d%%\n",lcd->pd->min_ddr_opp);
		lcd->opp_is_requested = true;
	}
}

static void s6d27a1_release_opp(struct s6d27a1_dpi *lcd)
{
	if (lcd->opp_is_requested) {
#if 0	  
		prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP, LCD_DRIVER_NAME_S6D27A1);
#endif
		lcd->opp_is_requested = false;
		dev_dbg(lcd->dev, "DDR OPP removed\n");
	}
}

/* Reverse order of power on and channel update as compared with MCDE default display update */
static int s6d27a1_display_update(struct mcde_display_device *ddev,
							bool tripple_buffer)
{
	int ret = 0;
	struct s6d27a1_dpi *lcd = dev_get_drvdata(&ddev->dev);

	/*Protection code for  power on /off test */
	if((lcd->dev <= 0) || (lcd->mdd <= 0) || (lcd->dev->platform_data <= 0))
		return ret;

	if (ddev->power_mode != MCDE_DISPLAY_PM_ON && ddev->set_power_mode) {
		ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_ON);
		if (ret < 0) {
			dev_warn(&ddev->dev,
				"%s:Failed to set power mode to on\n",
				__func__);
			return ret;
		}
	}

	ret = mcde_chnl_update(ddev->chnl_state, tripple_buffer);
	if (ret < 0) {
		dev_warn(&ddev->dev, "%s:Failed to update channel\n", __func__);
		return ret;
	}
	if (lcd->turn_on_backlight == false){

		lcd->turn_on_backlight = true;
		/* Allow time for one frame to be sent to the display before switching on the backlight */
		if (lcd->pd->bl_on_off) {
			lcd->pd->bl_on_off(false);			
			msleep(20);
			lcd->pd->bl_on_off(true);			
		}
	}
		
	return 0;
}

static int s6d27a1_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation)
{
	static int notFirstTime;
	int ret = 0;
	enum mcde_display_rotation final;
	struct s6d27a1_dpi *lcd = dev_get_drvdata(&ddev->dev);
	enum mcde_hw_rotation final_hw_rot;

	final = (360 + rotation - ddev->orientation) % 360;
	switch (final) {
	case MCDE_DISPLAY_ROT_180:	/* handled by LDI */
	case MCDE_DISPLAY_ROT_0:
		final_hw_rot = MCDE_HW_ROT_0;
		break;
	case MCDE_DISPLAY_ROT_90_CW:	/* handled by MCDE */
		final_hw_rot = MCDE_HW_ROT_90_CW;
		break;
	case MCDE_DISPLAY_ROT_90_CCW:	/* handled by MCDE */
		final_hw_rot = MCDE_HW_ROT_90_CCW;
		break;
	default:
		return -EINVAL;
	}

	if (rotation != ddev->rotation) {
		if (final == MCDE_DISPLAY_ROT_180) {
			if (final != lcd->rotation) {
				ret = s6d27a1_write_dcs_sequence(lcd,
						DCS_CMD_SEQ_S6D27A1_ORIENT_180);
				lcd->rotation = final;
			}
		} else if (final == MCDE_DISPLAY_ROT_0) {
			if (final != lcd->rotation) {
				ret = s6d27a1_write_dcs_sequence(lcd,
						DCS_CMD_SEQ_S6D27A1_ORIENT_0);
				lcd->rotation = final;
			}
			(void)mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		} else {
			ret = mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		}
		if (ret)
			return ret;
		dev_dbg(lcd->dev, "Display rotated %d\n", final);
	}
	ddev->rotation = rotation;
	/* avoid disrupting splash screen by changing update_flags */
	if (notFirstTime || (final != MCDE_DISPLAY_ROT_0)) {
		notFirstTime = 1;
		ddev->update_flags |= UPDATE_FLAG_ROTATION;
	}
	return 0;
}


static int s6d27a1_spi_write_byte(struct s6d27a1_dpi *lcd, int addr, int data)
{
	u16 buf;
	struct spi_message msg;

	struct spi_transfer xfer = {
		.len		= 2,
		.tx_buf		= &buf,
	};

	buf = (addr << 8) | data;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(lcd->spi, &msg);
}

static int s6d27a1_spi_read_byte(struct s6d27a1_dpi *lcd, int command, u8 *data)
{
	u16 buf[2];
	u16 rbuf[2];
	int ret;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len		= 4,
		.tx_buf		= buf,
		.rx_buf		= rbuf,
	};

	buf[0] = command;
	buf[1] = 0x100;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(lcd->spi, &msg);
	if (ret)
		return ret;

	*data = (rbuf[1] & 0x1FF) >> 1;

	return ret;
}


static int s6d27a1_write_dcs_sequence(struct s6d27a1_dpi *lcd, const u8 *p_seq)
{
	int ret = 0;
	int num_params;
	int param_count;

	mutex_lock(&lcd->lock);

	while ((p_seq[0] != DCS_CMD_SEQ_END) && !ret) {
		if (p_seq[0] == DCS_CMD_SEQ_DELAY_MS) {
			msleep(p_seq[1]);
			p_seq += 2;
		} else {
			ret = s6d27a1_spi_write_byte(lcd,
						SPI_COMMAND, p_seq[1]);

			num_params = p_seq[0] - 1;
			param_count = 0;

			while ((param_count < num_params) & !ret) {
				ret = s6d27a1_spi_write_byte(lcd,
					SPI_DATA, p_seq[param_count + 2]);
				param_count++;
			}

			p_seq += p_seq[0] + 1;
		}
	}

	mutex_unlock(&lcd->lock);

	if (ret != 0)
		dev_err(&lcd->mdd->dev, "failed to send DCS sequence.\n");

	return ret;
}


static int s6d27a1_dpi_read_panel_id(struct s6d27a1_dpi *lcd, u8 *idbuf)
{
	int ret;

	ret = s6d27a1_spi_read_byte(lcd, DCS_CMD_S6D27A1_READID1, &idbuf[0]);
	ret |= s6d27a1_spi_read_byte(lcd, DCS_CMD_S6D27A1_READID2, &idbuf[1]);
	ret |= s6d27a1_spi_read_byte(lcd, DCS_CMD_S6D27A1_READID3, &idbuf[2]);

	return ret;
}

static int s6d27a1_dpi_ldi_init(struct s6d27a1_dpi *lcd)
{
	int ret;

	ret = s6d27a1_write_dcs_sequence(lcd,
				DCS_CMD_SEQ_S6D27A1_EXIT_SLEEP_MODE);

	if (lcd->pd->sleep_out_delay)
		msleep(lcd->pd->sleep_out_delay);

	ret |= s6d27a1_write_dcs_sequence(lcd, DCS_CMD_SEQ_S6D27A1_INIT);


	if (lcd->pd->bl_ctrl)
		ret |= s6d27a1_write_dcs_sequence(lcd,
				DCS_CMD_SEQ_S6D27A1_ENABLE_BACKLIGHT_CONTROL);
	else
		ret |= s6d27a1_write_dcs_sequence(lcd,
				DCS_CMD_SEQ_S6D27A1_DISABLE_BACKLIGHT_CONTROL);

	return ret;
}

static int s6d27a1_dpi_ldi_enable(struct s6d27a1_dpi *lcd)
{
	int ret = 0;
	dev_dbg(lcd->dev, "s6d27a1_dpi_ldi_enable\n");

	ret |= s6d27a1_write_dcs_sequence(lcd, DCS_CMD_SEQ_S6D27A1_DISPLAY_ON);

	return ret;
}

static int s6d27a1_dpi_ldi_disable(struct s6d27a1_dpi *lcd)
{
	int ret;

	dev_dbg(lcd->dev, "s6d27a1_dpi_ldi_disable\n");
	ret = s6d27a1_write_dcs_sequence(lcd,
				DCS_CMD_SEQ_S6D27A1_ENTER_SLEEP_MODE);

	if (lcd->pd->sleep_in_delay)
		msleep(lcd->pd->sleep_in_delay);

	return ret;
}

static int s6d27a1_update_brightness(struct s6d27a1_dpi *lcd)
{
	int ret = 0;

	u8 DCS_CMD_SEQ_S6D27A1_UPDATE_BRIGHTNESS[] = {
	/*	Length	Command				Parameters */
		2,	DCS_CMD_S6D27A1_WRDISBV,		0x00,
		DCS_CMD_SEQ_END
	};

	if (lcd->pd->bl_ctrl) {
		DCS_CMD_SEQ_S6D27A1_UPDATE_BRIGHTNESS[2] = lcd->bl;
		ret = s6d27a1_write_dcs_sequence(lcd,
				DCS_CMD_SEQ_S6D27A1_UPDATE_BRIGHTNESS);
	}

	return ret;
}


static int s6d27a1_dpi_power_on(struct s6d27a1_dpi *lcd)
{
	int ret = 0;
	struct ssg_dpi_display_platform_data *dpd = NULL;

	s6d27a1_request_opp(lcd);

	dpd = lcd->pd;
	if (!dpd) {
		dev_err(lcd->dev, "s6d27a1_dpi platform data is NULL.\n");
		return -EFAULT;
	}

	dpd->power_on(dpd, LCD_POWER_UP);
	msleep(dpd->power_on_delay);

	if (!dpd->gpio_cfg_lateresume) {
		dev_err(lcd->dev, "gpio_cfg_lateresume is NULL.\n");
		return -EFAULT;
	} else
		dpd->gpio_cfg_lateresume();

	dpd->reset(dpd);
	msleep(dpd->reset_delay);

	ret = s6d27a1_dpi_ldi_init(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return ret;
	}
	dev_dbg(lcd->dev, "ldi init successful\n");

	ret = s6d27a1_dpi_ldi_enable(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to enable ldi.\n");
		return ret;
	}
	dev_dbg(lcd->dev, "ldi enable successful\n");

	s6d27a1_update_brightness(lcd);

#ifdef ESD_OPERATION
	if (lcd->lcd_connected)
		enable_irq(GPIO_TO_IRQ(lcd->esd_port));

	if (lcd->lcd_connected) {
		irq_set_irq_type(GPIO_TO_IRQ(lcd->esd_port), IRQF_TRIGGER_RISING);
		lcd->esd_enable = 1;
		pr_info("%s change lcd->esd_enable :%d \n", __func__,
				lcd->esd_enable);
	} else
		pr_info("%s lcd_connected : %d \n", __func__, lcd->lcd_connected);
#endif

	return 0;
}

static int s6d27a1_dpi_power_off(struct s6d27a1_dpi *lcd)
{
	int ret = 0;
	struct ssg_dpi_display_platform_data *dpd = NULL;

	dev_dbg(lcd->dev, "s6d27a1_dpi_power_off \n");

#ifdef ESD_OPERATION
	if (lcd->esd_enable) {

		lcd->esd_enable = 0;
		irq_set_irq_type(GPIO_TO_IRQ(lcd->esd_port), IRQF_TRIGGER_NONE);
		disable_irq_nosync(GPIO_TO_IRQ(lcd->esd_port));

		if (!list_empty(&(lcd->esd_work.entry))) {
			cancel_work_sync(&(lcd->esd_work));
			pr_info("%s cancel_work_sync", __func__);
		}

		pr_info("%s change lcd->esd_enable :%d\n", __func__,
				lcd->esd_enable);
	}
#endif

	dpd = lcd->pd;
	if (!dpd) {
		dev_err(lcd->dev, "platform data is NULL.\n");
		return -EFAULT;
	}

	ret = s6d27a1_dpi_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}

	if (!dpd->gpio_cfg_earlysuspend) {
		dev_err(lcd->dev, "gpio_cfg_earlysuspend is NULL.\n");
		return -EFAULT;
	} else
		dpd->gpio_cfg_earlysuspend();

	if (!dpd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else
		dpd->power_on(dpd, LCD_POWER_DOWN);

	s6d27a1_release_opp(lcd);

	return 0;
}

static int s6d27a1_dpi_power(struct s6d27a1_dpi *lcd, int power)
{
	int ret = 0;

	dev_dbg(lcd->dev, "%s(): old=%d (%s), new=%d (%s)\n", __func__,
		lcd->power, POWER_IS_ON(lcd->power) ? "on" : "off",
		power, POWER_IS_ON(power) ? "on" : "off"
		);

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = s6d27a1_dpi_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = s6d27a1_dpi_power_off(lcd);
	if (!ret)
		lcd->power = power;

	return ret;
}

#ifdef ESD_OPERATION
static void esd_work_func(struct work_struct *work)
{
	struct s6d27a1_dpi *lcd = container_of(work,
					struct s6d27a1_dpi, esd_work);
#if defined(CONFIG_MACH_CODINA_CHN) || defined(CONFIG_MACH_CODINA_EURO) || defined(CONFIG_MACH_CODINA)
	if (lcd->esd_enable && !lcd->esd_processing && !sec_lpm_bootmode
							&& (sec_bootmode != 2)) {
#else
	if (lcd->esd_enable && !lcd->esd_processing && !power_off_charging
						&& (sec_bootmode != 2)) {
#endif

		pr_info("%s lcd->esd_enable:%d start\n", __func__,
						lcd->esd_enable);
		lcd->esd_processing = true;
		s6d27a1_dpi_power_off(lcd);
		s6d27a1_dpi_power_on(lcd);
		lcd->esd_processing = false;

		/* low is normal. On PBA esd_port could be HIGH */
		if (gpio_get_value(lcd->esd_port)) {
			pr_info("%s esd_work_func re-armed\n", __func__);
			queue_work(lcd->esd_workqueue, &(lcd->esd_work));
		}
		pr_info("%s end\n", __func__);		
	}
}

static irqreturn_t esd_interrupt_handler(int irq, void *data)
{
	struct s6d27a1_dpi *lcd = data;
#if defined(CONFIG_MACH_CODINA_CHN) || defined(CONFIG_MACH_CODINA_EURO) || defined(CONFIG_MACH_CODINA)
	if (lcd->esd_enable && !lcd->esd_processing && !sec_lpm_bootmode
								&& (sec_bootmode != 2)) {
#else
	if (lcd->esd_enable && !lcd->esd_processing && !power_off_charging
						&& (sec_bootmode != 2)) {
#endif

		pr_info("%s lcd->esd_enable :%d\n", __func__, lcd->esd_enable);		
		if (list_empty(&(lcd->esd_work.entry)))
			queue_work(lcd->esd_workqueue, &(lcd->esd_work));
		else
			pr_info("%s esd_work_func is not empty\n", __func__);
	}

	return IRQ_HANDLED;
}
#endif
static int s6d27a1_dpi_set_power(struct lcd_device *ld, int power)
{
	struct s6d27a1_dpi *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return s6d27a1_dpi_power(lcd, power);
}

static int s6d27a1_dpi_get_power(struct lcd_device *ld)
{
	struct s6d27a1_dpi *lcd = lcd_get_data(ld);

	return lcd->power;
}

static struct lcd_ops s6d27a1_dpi_lcd_ops = {
	.set_power = s6d27a1_dpi_set_power,
	.get_power = s6d27a1_dpi_get_power,
};


/* This structure defines all the properties of a backlight */
struct backlight_properties s6d27a1_dpi_backlight_props = {
	.brightness	= DEFAULT_BRIGHTNESS,
	.max_brightness = MAX_BRIGHTNESS,
	.type = BACKLIGHT_RAW,
};

static int s6d27a1_dpi_get_brightness(struct backlight_device *bd)
{
	dev_dbg(&bd->dev, "lcd get brightness returns %d\n",
		bd->props.brightness);
	return bd->props.brightness;
}

static int s6d27a1_dpi_set_brightness(struct backlight_device *bd)
{
	int ret = 0, bl = bd->props.brightness;
	struct s6d27a1_dpi *lcd = bl_get_data(bd);

	if ((bl < MIN_BRIGHTNESS) || (bl > bd->props.max_brightness)) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d.\n",
			MIN_BRIGHTNESS, bd->props.max_brightness);
		return -EINVAL;
	}

	if ((lcd->ldi_state) && (lcd->current_brightness != lcd->bl)) {
		ret = s6d27a1_update_brightness(lcd);
		dev_info(lcd->dev, "brightness=%d, bl=%d\n",
			bd->props.brightness, lcd->bl);
		if (ret < 0)
			dev_err(&bd->dev, "update brightness failed.\n");
	}

	return ret;
}

static const struct backlight_ops s6d27a1_dpi_backlight_ops  = {
	.get_brightness = s6d27a1_dpi_get_brightness,
	.update_status = s6d27a1_dpi_set_brightness,
};

static signed char apeopp_requirement = 50, ddropp_requirement = 50;

static ssize_t s6d27a1_sysfs_show_opp(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "apeopp=%d\n"
			    "ddropp=%d\n",
			    apeopp_requirement,
			    ddropp_requirement);
}

static ssize_t s6d27a1_sysfs_store_opp(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	int val;
  
  	if (!strncmp(&buf[0], "apeopp=", 7))
	{
		sscanf(&buf[7], "%d", &val);
		
		if ((val != 25) && (val != 50) && (val != 100))
			goto out;
		
		apeopp_requirement = val;

		return len;
	}
	
	if (!strncmp(&buf[0], "ddropp=", 7))
	{
		sscanf(&buf[7], "%d", &val);
		
		if ((val != 25) && (val != 50) && (val != 100))
			goto out;
		
		apeopp_requirement = val;

		return len;
	}
	
out:
	return -EINVAL;
}
static DEVICE_ATTR(mcde_screenon_opp, 0644,
		 s6d27a1_sysfs_show_opp,  s6d27a1_sysfs_store_opp);

static ssize_t s6d27a1_sysfs_show_lcdclk(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;
	bool matched;

	sprintf(buf, "Current: %s\n\n", lcdclk_prop[lcdclk_usr].name);

	for (i = 0; i < ARRAY_SIZE(lcdclk_prop); i++) {
		if (i == lcdclk_usr)
			matched = true;
		else
			matched = false;

		sprintf(buf, "%s[%d][%s] %s\n", buf, i, matched ? "*" : " ", lcdclk_prop[i].name);
	}

	return strlen(buf);
}

static ssize_t s6d27a1_sysfs_store_lcdclk(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t len)
{
	int ret, tmp;

	ret = sscanf(buf, "%d", &tmp);
	if (!ret || (tmp < 0) || (tmp > 3)) {
		  pr_err("[s6d27a1] Bad cmd\n");
		  return -EINVAL;
	}

	lcdclk_usr = tmp;

	schedule_work(&s6d27a1_lcdclk_work);

	return len;
}

static DEVICE_ATTR(lcdclk, 0644, s6d27a1_sysfs_show_lcdclk, s6d27a1_sysfs_store_lcdclk);

static ssize_t s6d27a1_dpi_sysfs_store_lcd_power(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t len)
{
	int rc;
	int lcd_enable;
	struct s6d27a1_dpi *lcd = dev_get_drvdata(dev);

	dev_info(lcd->dev, "s6d27a1_dpi lcd_sysfs_store_lcd_power\n");

	rc = strict_strtoul(buf, 0, (unsigned long *)&lcd_enable);
	if (rc < 0)
		return rc;

	if (lcd_enable)
		s6d27a1_dpi_power(lcd, FB_BLANK_UNBLANK);
	else
		s6d27a1_dpi_power(lcd, FB_BLANK_POWERDOWN);

	return len;
}

static DEVICE_ATTR(lcd_power, 0444,
		NULL, s6d27a1_dpi_sysfs_store_lcd_power);

static ssize_t lcd_type_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
		return sprintf(buf, "SMD_S6D27A1\n");
}
static DEVICE_ATTR(lcd_type, 0664, lcd_type_show, NULL);

static ssize_t panel_id_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct s6d27a1_dpi *lcd = dev_get_drvdata(dev);
	u8 idbuf[3];

	if (s6d27a1_dpi_read_panel_id(lcd, idbuf)) {
		dev_err(lcd->dev, "Failed to read panel id\n");
		return sprintf(buf, "Failed to read panel id");
	} else {
		return sprintf(buf, "LCD Panel id = 0x%x, 0x%x, 0x%x\n",
				idbuf[0], idbuf[1], idbuf[2]);
	}
}
static DEVICE_ATTR(panel_id, 0664, panel_id_show, NULL);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void s6d27a1_dpi_mcde_early_suspend(
			struct early_suspend *earlysuspend);
static void s6d27a1_dpi_mcde_late_resume(
			struct early_suspend *earlysuspend);
#endif

#ifdef ESD_OPERATION
#ifdef ESD_TEST
static void est_test_timer_func(unsigned long data)
{
	pr_info("%s\n", __func__);

	if (list_empty(&(pdpi->esd_work.entry))) {
		disable_irq_nosync(GPIO_TO_IRQ(pdpi->esd_port));
		queue_work(pdpi->esd_workqueue, &(pdpi->esd_work));
		pr_info("%s invoked\n", __func__);
	}

	mod_timer(&pdpi->esd_test_timer,  jiffies + (3*HZ));
}
#endif
#endif
static int __devinit s6d27a1_dpi_spi_probe(struct spi_device *spi)
{
	int ret = 0;
	struct s6d27a1_dpi *lcd = container_of(spi->dev.driver,
					 struct s6d27a1_dpi, spi_drv.driver);

	dev_dbg(&spi->dev, "panel s6d27a1_dpi spi being probed\n");

	dev_set_drvdata(&spi->dev, lcd);

	/* s6d27a1_dpi lcd panel uses 3-wire 9bits SPI Mode. */
	spi->bits_per_word = 9;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi setup failed.\n");
		goto out;
	}

	lcd->spi = spi;

	/*
	 * if lcd panel was on from bootloader like u-boot then
	 * do not lcd on.
	 */
	if (!lcd->pd->platform_enabled) {
		/*
		 * if lcd panel was off from bootloader then
		 * current lcd status is powerdown and then
		 * it enables lcd panel.
		 */
		lcd->power = FB_BLANK_POWERDOWN;

		s6d27a1_dpi_power(lcd, FB_BLANK_UNBLANK);
	}

#ifdef ESD_OPERATION
	lcd->esd_workqueue = create_singlethread_workqueue("esd_workqueue");

	if (!lcd->esd_workqueue) {
		dev_info(lcd->dev, "esd_workqueue create fail\n");
		return -ENOMEM;
	}

	INIT_WORK(&(lcd->esd_work), esd_work_func);

	lcd->esd_port = ESD_PORT_NUM;

	if (request_threaded_irq(GPIO_TO_IRQ(lcd->esd_port), NULL,
	esd_interrupt_handler, IRQF_TRIGGER_RISING, "esd_interrupt", lcd)) {
			dev_info(lcd->dev, "esd irq request fail\n");
			free_irq(GPIO_TO_IRQ(lcd->esd_port), NULL);
			lcd->lcd_connected = 0;
	} else {
		/* low is normal. On PBA esd_port could be HIGH */
		if (!gpio_get_value(lcd->esd_port)) {
			dev_info(lcd->dev, "esd irq enabled on booting\n");
			lcd->esd_enable = 1;
			lcd->lcd_connected = 1;
		} else {
			dev_info(lcd->dev, "esd irq disabled on booting\n");
			disable_irq(GPIO_TO_IRQ(lcd->esd_port));
			lcd->esd_enable = 0;
			lcd->lcd_connected = 0;
		}
	}
	lcd->esd_processing = false;

	dev_info(lcd->dev, "esd work success\n");

	if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
			"codina_lcd_dpi", apeopp_requirement)) {
			pr_info("pcrm_qos_add APE failed\n");
	}
	
	if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
			"codina_lcd_dpi", ddropp_requirement)) {
			pr_info("pcrm_qos_add APE failed\n");
	}
	
#ifdef ESD_TEST
	pdpi = lcd;
	setup_timer(&lcd->esd_test_timer, est_test_timer_func, 0);
	mod_timer(&lcd->esd_test_timer,  jiffies + (3*HZ));
#endif
#endif

	dev_dbg(&spi->dev, "s6d27a1_dpi spi has been probed.\n");

out:
	return ret;
}

static int __devinit s6d27a1_dpi_mcde_probe(
				struct mcde_display_device *ddev)
{
	int ret = 0;
	struct s6d27a1_dpi *lcd = NULL;
	struct backlight_device *bd = NULL;
	struct ssg_dpi_display_platform_data *pdata = ddev->dev.platform_data;

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	if (pdata == NULL) {
		dev_err(&ddev->dev, "%s:Platform data missing\n", __func__);
		ret = -EINVAL;
		goto no_pdata;
	}

	if (ddev->port->type != MCDE_PORTTYPE_DPI) {
		dev_err(&ddev->dev,
			"%s:Invalid port type %d\n",
			__func__, ddev->port->type);
		ret = -EINVAL;
		goto invalid_port_type;
	}

	if (pdata->lcd_pwr_setup)
		pdata->lcd_pwr_setup(&ddev->dev);

	ddev->try_video_mode = try_video_mode;
	ddev->set_video_mode = set_video_mode;
	/* ddev->update = s6d27a1_display_update; */
	ddev->set_rotation = s6d27a1_set_rotation;

	lcd = kzalloc(sizeof(struct s6d27a1_dpi), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	mutex_init(&lcd->lock);

	dev_set_drvdata(&ddev->dev, lcd);
	lcd->mdd = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;
	lcd->turn_on_backlight = false;
	lcd->opp_is_requested = false;
	s6d27a1_request_opp(lcd);

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd->ld = lcd_device_register("panel", &ddev->dev,
					lcd, &s6d27a1_dpi_lcd_ops);
	if (IS_ERR(lcd->ld)) {
		ret = PTR_ERR(lcd->ld);
		goto out_free_lcd;
	} else {
		ret = device_create_file(&(lcd->ld->dev), &dev_attr_lcd_type);
		if (ret < 0)
			dev_err(&(lcd->ld->dev),
			"failed to add panel_type sysfs entries\n");
		ret = device_create_file(&(lcd->ld->dev), &dev_attr_panel_id);
		if (ret < 0)
			dev_err(&(lcd->ld->dev),
			"failed to add panel_id sysfs entries\n");
	}
#endif

	mutex_init(&lcd->lock);

	if (pdata->bl_ctrl) {
		bd = backlight_device_register("panel",
						&ddev->dev,
						lcd,
						&s6d27a1_dpi_backlight_ops,
						&s6d27a1_dpi_backlight_props);
		if (IS_ERR(bd)) {
			ret =  PTR_ERR(bd);
			goto out_backlight_unregister;
		}
	}

	lcd->bd = bd;

	ret = device_create_file(&(ddev->dev), &dev_attr_lcd_power);
	if (ret < 0)
		dev_err(&(ddev->dev),
			"failed to add lcd_power sysfs entries\n");
		
	ret = device_create_file(&(ddev->dev), &dev_attr_lcdclk);
	if (ret < 0)
		dev_err(&(ddev->dev), "failed to add sysfs entries\n");
	
	ret = device_create_file(&(ddev->dev), &dev_attr_mcde_screenon_opp);
	if (ret < 0)
		dev_err(&(ddev->dev), "failed to add sysfs entries\n");


	lcd->spi_drv.driver.name	= "pri_lcd_spi";
	lcd->spi_drv.driver.bus		= &spi_bus_type;
	lcd->spi_drv.driver.owner	= THIS_MODULE;
	lcd->spi_drv.probe		= s6d27a1_dpi_spi_probe;
	ret = spi_register_driver(&lcd->spi_drv);
	if (ret < 0) {
		dev_err(&(ddev->dev), "Failed to register SPI driver");
		goto out_backlight_unregister;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	lcd->earlysuspend.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	lcd->earlysuspend.suspend = s6d27a1_dpi_mcde_early_suspend;
	lcd->earlysuspend.resume  = s6d27a1_dpi_mcde_late_resume;
	register_early_suspend(&lcd->earlysuspend);
#endif
	//when screen is on, APE_OPP 25 sometimes messes it up
	if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
				"codina_lcd_dpi", 50)) {
			pr_info("pcrm_qos_add APE failed\n");
		}
	
	dev_dbg(&ddev->dev, "DPI display probed\n");

	goto out;

out_backlight_unregister:
	if (pdata->bl_ctrl)
		backlight_device_unregister(bd);
out_free_lcd:
	mutex_destroy(&lcd->lock);
	kfree(lcd);
invalid_port_type:
no_pdata:
out:
	return ret;
}

static int __devexit s6d27a1_dpi_mcde_remove(
				struct mcde_display_device *ddev)
{
	struct s6d27a1_dpi *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);
	s6d27a1_dpi_power(lcd, FB_BLANK_POWERDOWN);

	if (lcd->pd->bl_ctrl)
		backlight_device_unregister(lcd->bd);

	spi_unregister_driver(&lcd->spi_drv);
	kfree(lcd);

	return 0;
}

static void s6d27a1_dpi_mcde_shutdown(struct mcde_display_device *ddev)
{
	struct s6d27a1_dpi *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);
	mutex_lock(&ddev->display_lock);
	s6d27a1_dpi_power(lcd, FB_BLANK_POWERDOWN);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&lcd->earlysuspend);
#endif

	kfree(lcd);
	mutex_unlock(&ddev->display_lock);
	dev_dbg(&ddev->dev, "end %s\n", __func__);
}

static int s6d27a1_dpi_mcde_resume(struct mcde_display_device *ddev)
{
	int ret;
	struct s6d27a1_dpi *lcd = dev_get_drvdata(&ddev->dev);
	DPI_DISP_TRACE;

	/* set_power_mode will handle call platform_enable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);

	s6d27a1_dpi_power(lcd, FB_BLANK_UNBLANK);

	return ret;
}

static int s6d27a1_dpi_mcde_suspend(
		struct mcde_display_device *ddev, pm_message_t state)
{
	int ret = 0;
	struct s6d27a1_dpi *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	lcd->beforepower = lcd->power;
	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	ret = s6d27a1_dpi_power(lcd, FB_BLANK_POWERDOWN);

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);

	return ret;
}

static void requirements_add_thread(struct work_struct *requirements_add_work)
{
	if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
			"codina_lcd_dpi", 50)) {
		pr_info("pcrm_qos_add APE failed\n");
	}
}
static DECLARE_WORK(requirements_add_work, requirements_add_thread);

static void requirements_remove_thread(struct work_struct *requirements_remove_work)
{
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, "codina_lcd_dpi");
}
static DECLARE_WORK(requirements_remove_work, requirements_remove_thread);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void s6d27a1_dpi_mcde_early_suspend(
		struct early_suspend *earlysuspend)
{
	struct s6d27a1_dpi *lcd = container_of(earlysuspend,
						struct s6d27a1_dpi,
						earlysuspend);
	if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
			"codina_lcd_dpi", apeopp_requirement)) {
			pr_info("pcrm_qos_add APE failed\n");
	}
	
	if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
			"codina_lcd_dpi", ddropp_requirement)) {
			pr_info("pcrm_qos_add APE failed\n");
	}
	
	#ifdef CONFIG_DB8500_LIVEOPP
	schedule_work(&requirements_remove_work);
	#endif
	
	pm_message_t dummy;

	s6d27a1_dpi_mcde_suspend(lcd->mdd, dummy);

}

static void s6d27a1_dpi_mcde_late_resume(
		struct early_suspend *earlysuspend)
{
	struct s6d27a1_dpi *lcd = container_of(earlysuspend,
						struct s6d27a1_dpi,
						earlysuspend);
	
	#ifdef CONFIG_DB8500_LIVEOPP
	schedule_work(&requirements_add_work);
	#endif

	s6d27a1_dpi_mcde_resume(lcd->mdd);
	
	if (lcdclk_usr != 0) {
		pr_err("[s6d27a1] Rebasing LCDCLK...\n");
		schedule_work(&s6d27a1_lcdclk_work);
	}
	
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, "codina_lcd_dpi");
	prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP, "codina_lcd_dpi");

}
#endif

static struct mcde_display_driver s6d27a1_dpi_mcde __refdata = {
	.probe          = s6d27a1_dpi_mcde_probe,
	.remove         = s6d27a1_dpi_mcde_remove,
	.shutdown	= s6d27a1_dpi_mcde_shutdown,
#ifdef CONFIG_HAS_EARLYSUSPEND
	.suspend        = NULL,
	.resume         = NULL,
#else
	.suspend        = s6d27a1_dpi_mcde_suspend,
	.resume         = s6d27a1_dpi_mcde_resume,
#endif
	.driver		= {
		.name	= LCD_DRIVER_NAME_S6D27A1,
		.owner	= THIS_MODULE,
	},
};


static int __init s6d27a1_dpi_init(void)
{
	return mcde_display_driver_register(&s6d27a1_dpi_mcde);;
}

static void __exit s6d27a1_dpi_exit(void)
{
	mcde_display_driver_unregister(&s6d27a1_dpi_mcde);
}

module_init(s6d27a1_dpi_init);
module_exit(s6d27a1_dpi_exit);

MODULE_AUTHOR("Gareth Phillips <gareth.phillips@samsung.com>");
MODULE_DESCRIPTION("Sony S6D27A1 DPI Driver");
MODULE_LICENSE("GPL");

