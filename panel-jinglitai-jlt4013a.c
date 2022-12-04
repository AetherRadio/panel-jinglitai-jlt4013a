// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Jinglitai JLT4013A LCD Panel.
 *
 * Copyright (C) Rui Oliveira 2022
 * Copyright (C) Oleg Belousov 2022
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>

#define ST7701S_SWRESET 0x01
#define ST7701S_SLPOUT 0x11
#define ST7701S_DISPOFF 0x28
#define ST7701S_DISPON 0x29
#define ST7701S_COLMOD 0x3A

#define ST7701S_CN2BKxSEL 0xFF

/* BK0 */

#define ST7701S_LNESET 0xC0
#define ST7701S_PORCTRL 0xC1
#define ST7701S_INVSET 0xC2
#define ST7701S_PVGAMCTRL 0xB0
#define ST7701S_NVGAMCTRL 0xB1

/* BK1 */

#define ST7701S_VRHS 0xB0
#define ST7701S_VCOM 0xB1
#define ST7701S_VGHSS 0xB2
#define ST7701S_TESTCMD 0xB3
#define ST7701S_VGLS 0xB5
#define ST7701S_PWCTRL1 0xB7
#define ST7701S_PWCTRL2 0xB8
#define ST7701S_PWCTRL3 0xB9
#define ST7701S_SPD1 0xC1
#define ST7701S_SPD2 0xC2
#define ST7701S_MIPISET1 0xD0

#define ST7701S_TRY(val, func)                                                          \
	do {                                                                            \
		if ((val = (func))) {                                                   \
			pr_warn("Jinglitai JLT4013A: SPI write failed with error %d\n", \
				val);                                                   \
			return val;                                                     \
		}                                                                       \
	} while (0)

static const struct of_device_id jlt4013a_of_match[] = {
	{ .compatible = "jinglitai,jlt4013a" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jlt4013a_of_match);

struct jlt4013a {
	struct drm_panel panel;
	struct spi_device *spi;
	struct gpio_desc *reset;
	struct regulator *supply;
};

enum st7701s_prefix {
	ST7701S_COMMAND = 0,
	ST7701S_DATA = 1,
};

static int st7701s_spi_write(struct jlt4013a *ctx, enum st7701s_prefix prefix,
			     u8 data)
{
	struct spi_transfer xfer = {};
	struct spi_message msg;
	u16 txbuf = ((prefix & 1) << 8) | data;

	spi_message_init(&msg);

	xfer.tx_buf = &txbuf;
	xfer.bits_per_word = 9;
	xfer.len = sizeof(txbuf);

	spi_message_add_tail(&xfer, &msg);
	return spi_sync(ctx->spi, &msg);
}

static int st7701s_write_command(struct jlt4013a *ctx, u8 cmd)
{
	return st7701s_spi_write(ctx, ST7701S_COMMAND, cmd);
}

static int st7701s_write_data(struct jlt4013a *ctx, u8 cmd)
{
	return st7701s_spi_write(ctx, ST7701S_DATA, cmd);
}

static inline struct jlt4013a *panel_to_jlt4013a(struct drm_panel *panel)
{
	return container_of(panel, struct jlt4013a, panel);
}

static int jlt4013a_prepare(struct drm_panel *panel)
{
	struct jlt4013a *ctx = panel_to_jlt4013a(panel);

	/* Enable power supply */

	int ret = regulator_enable(ctx->supply);
	if (ret) {
		pr_warn("Jinglitai JLT4013A: Failed to enable power supply\n");
		return ret;
	}
	msleep(120);

	/* Reset routine */

	gpiod_set_value(ctx->reset, 1);
	msleep(120);
	gpiod_set_value(ctx->reset, 0);
	msleep(120); // Sleep mandated by the datasheet

	/* Initialization routine */

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_SLPOUT));
	msleep(120);

	/* BK0 */

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_CN2BKxSEL));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x77));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x01));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x10));

	/* Line below: 854 as Table 12.3.2.7 */
	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_LNESET));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xE9));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x03));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_PORCTRL));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x11));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x02));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_INVSET));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x31));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x03));

	/* Something strange */

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xCC));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x10));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_PVGAMCTRL));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x40));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x01));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x46));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x13));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x05));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x1B));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x07));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x15));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x12));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x4C));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x10));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xC8));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_NVGAMCTRL));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x40));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x02));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x86));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x13));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x05));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x1F));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x07));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x15));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x12));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x15));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x19));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x08));

	/* BK1 */

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_CN2BKxSEL));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x77));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x01));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x11));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_VRHS));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x50));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_VCOM));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x68));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_VGHSS));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x07));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_TESTCMD));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x80));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_VGLS));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x47));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_PWCTRL1));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x85));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_PWCTRL2));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x21));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_PWCTRL3));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x10));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_SPD1));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x21));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x36));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_SPD2));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x78));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_MIPISET1));
	ST7701S_TRY(ret,
		    st7701s_write_data(ctx, 0b01001001)); /* CRC error only? */

	/* Something strange */

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00)); /* Not sure */
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x02));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE1));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x08));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0A));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x07));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x33));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x33));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE2));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE3));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x33));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x33));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE4));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x44));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x44));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE5));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0E)); /* Not sure */
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x10));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0A));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0C));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE6));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x33));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x33));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE7));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x44));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x44));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xE8));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0D)); /* Not sure */
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0F));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x09));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x0B));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x2D));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xA0));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xEB));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x02));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x01));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xE4));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xE4));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x44));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x40));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xEC));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x02));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x01));

	ST7701S_TRY(ret, st7701s_write_command(ctx, 0xED));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xAB)); /* Not sure */
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x89));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x76));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x54));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x01));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xFF));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xFF));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xFF));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xFF));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xFF));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xFF));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x10));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x45));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x67));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x98));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0xBA));

	/* BK disable */

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_CN2BKxSEL));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x77));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x01));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x00));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_COLMOD));
	ST7701S_TRY(ret, st7701s_write_data(ctx, 0x70));

	ST7701S_TRY(ret, st7701s_write_command(ctx, ST7701S_DISPON));

	msleep(120);

	return ret;
}

static int jlt4013a_unprepare(struct drm_panel *panel)
{
	struct jlt4013a *ctx = panel_to_jlt4013a(panel);

	int ret = regulator_disable(ctx->supply);
	return ret;
}

static const struct drm_display_mode jlt4013a_default_display_mode = {
	.clock = 14616,
	.hdisplay = 480,
	.hsync_start = 480 + 32, // 512
	.hsync_end = 480 + 32 + 11, // 523
	.htotal = 480 + 32 + 11 + 2, // 525
	.vdisplay = 800,
	.vsync_start = 800 + 54, // 854
	.vsync_end = 800 + 54 + 41, // 895
	.vtotal = 800 + 54 + 41 + 33, // 928
	.width_mm = 52,
	.height_mm = 86,
};

static int jlt4013a_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	struct drm_display_mode *mode = drm_mode_duplicate(
		connector->dev, &jlt4013a_default_display_mode);
	if (mode == NULL) {
		dev_err(panel->dev,
			"Jinglitai JLT4013A: Failed to add default mode\n");
		return -EAGAIN;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bpc = 8;
	connector->display_info.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;

	drm_mode_probed_add(connector, mode);

	drm_display_info_set_bus_formats(&connector->display_info, &bus_format,
					 1);

	return 1;
}

static int jlt4013a_enable(struct drm_panel *panel)
{
	return 0;
}

static int jlt4013a_disable(struct drm_panel *panel)
{
	return 0;
}

static const struct drm_panel_funcs jlt4013afuncs = {
	.prepare = jlt4013a_prepare,
	.unprepare = jlt4013a_unprepare,
	.get_modes = jlt4013a_get_modes,
	.enable = jlt4013a_enable,
	.disable = jlt4013a_disable,
};

static int jlt4013a_probe(struct spi_device *spi)
{
	int err;

	struct device *dev = &spi->dev;

	struct jlt4013a *ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL) {
		return -EAGAIN;
	}

	ctx->spi = spi;
	spi_set_drvdata(spi, ctx);

	ctx->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(ctx->supply)) {
		dev_err(dev,
			"Jinglitai JLT4013A: Failed to get power supply\n");
		return PTR_ERR(ctx->supply);
	}

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(dev, "Jinglitai JLT4013A: Failed to get reset GPIO\n");
		return PTR_ERR(ctx->reset);
	}

	drm_panel_init(&ctx->panel, dev, &jlt4013afuncs,
		       DRM_MODE_CONNECTOR_DPI);

	err = drm_panel_of_backlight(&ctx->panel);
	if (err)
		return err;

	drm_panel_add(&ctx->panel);

	return 0;
}

static int jlt4013a_remove(struct spi_device *spi)
{
	struct jlt4013a *ctx = spi_get_drvdata(spi);

	drm_panel_remove(&(ctx->panel));
	return 0;
}

static struct spi_driver jlt4013a_driver = {
	.probe		= jlt4013a_probe,
	.remove		= jlt4013a_remove,
	.driver		= {
		.name	= "jlt4013a",
		.of_match_table = jlt4013a_of_match,
	},
};
module_spi_driver(jlt4013a_driver);

MODULE_AUTHOR("Rui Oliveira <ruimail24@gmail.com>");
MODULE_DESCRIPTION("Driver for the Jinglitai JLT4013A LCD Panel");
MODULE_LICENSE("GPL v2");
