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

static inline struct jlt4013a *panel_to_jlt4013a(struct drm_panel *panel)
{
	return container_of(panel, struct jlt4013a, panel);
}

static int jlt4013a_prepare(struct drm_panel *panel)
{
	printk(KERN_DEBUG "Jinglitai JLT4013A: Preparing");

	struct jlt4013a *ctx = panel_to_jlt4013a(panel);
	printk(KERN_DEBUG "Jinglitai JLT4013A: LCD Panel found at %p", ctx);

	int ret = regulator_enable(ctx->supply);
	printk(KERN_DEBUG "Jinglitai JLT4013A: regulator returned %d", ret);
	if (ret == 0) {
		msleep(120);
	}

	return ret;
}

static int jlt4013a_unprepare(struct drm_panel *panel)
{
	printk(KERN_DEBUG "Jinglitai JLT4013A: Unpreparing");

	struct jlt4013a *ctx = panel_to_jlt4013a(panel);
	printk(KERN_DEBUG "Jinglitai JLT4013A: LCD Panel found at %p", ctx);

	int ret = regulator_disable(ctx->supply);
	printk(KERN_DEBUG "Jinglitai JLT4013A: regulator returned %d", ret);

	return ret;
}

// This is essentially the display mode of the Jinglitai JLT4013A.
static const struct drm_display_mode jlt4013a_default_display_mode = {
	.clock = 14616,
	.hdisplay = 480,
	.hsync_start = 480 + 32,
	.hsync_end = 480 + 32 + 11,
	.htotal = 480 + 32 + 11 + 2,
	.vdisplay = 800,
	.vsync_start = 800 + 54,
	.vsync_end = 800 + 54 + 41,
	.vtotal = 800 + 54 + 41 + 33,
	.width_mm = 52,
	.height_mm = 86,
};

static int jlt4013a_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	printk(KERN_DEBUG "Jinglitai JLT4013A: Getting modes");

	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	struct drm_display_mode *mode = drm_mode_duplicate(
		connector->dev, &jlt4013a_default_display_mode);
	if (mode == NULL) {
		dev_err(panel->dev,
			"Jinglitai JLT4013A: Failed to add default mode\n");
		return -EAGAIN;
	}

	printk(KERN_DEBUG "Jinglitai JLT4013A: Mode set at %p", mode);

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

static const struct drm_panel_funcs jlt4013afuncs = {
	.prepare = jlt4013a_prepare,
	.unprepare = jlt4013a_unprepare,
	.get_modes = jlt4013a_get_modes,
};

static int jlt4013a_probe(struct spi_device *spi)
{
	printk(KERN_DEBUG "Jinglitai JLT4013A: Probing");

	struct device *dev = &spi->dev;

	printk(KERN_DEBUG "Jinglitai JLT4013A: Device found at %p", dev);

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
	printk(KERN_DEBUG "Jinglitai JLT4013A: Backlight enabled", dev);

	drm_panel_add(&ctx->panel);

	return 0;
}

static int jlt4013a_remove(struct spi_device *spi)
{
	printk(KERN_DEBUG "Jinglitai JLT4013A: Removing");

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
