// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Jinglitai JLT4013A LCD Panel.
 *
 * Copyright (C) Rui Oliveira 2022
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

#define ST7701S_SWRESET     0x01
#define ST7701S_SLPOUT      0x11
#define ST7701S_DISPOFF     0x28
#define ST7701S_DISPON      0x29

#define ST7701S_TEST(val, func)			\
	do {					\
		if ((val = (func)))		\
			return val;		\
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

static int st7701s_spi_write(struct jlt4013a *ctx, enum st7701s_prefix prefix, u8 data)
{
	struct spi_transfer xfer = { };
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

	int ret = regulator_enable(ctx->supply);
	if (ret)
		return ret;
		
	msleep(120);

	gpiod_set_value(ctx->reset, 1);
	msleep(30);
	gpiod_set_value(ctx->reset, 0);
	msleep(120);

	ST7701S_TEST(ret, st7701s_write_command(ctx, ST7701S_SLPOUT));
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
	connector->display_info.bus_flags =
		DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE; // 4

	drm_mode_probed_add(connector, mode);

	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
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

	int err = drm_panel_of_backlight(&ctx->panel);
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
