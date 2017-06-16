/*
 * Driver for unused GPIOs
 *
 * bravey qin <qinyong@hisense.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#include <linux/module.h>

static const struct of_device_id of_gpio_unused_match[] = {
	{ .compatible = "gpio-unused", },
	{},
};

static int gpio_unused_probe(struct platform_device *pdev)
{
	struct device_node *of_node = NULL;

	pr_info("gpio_unused_probe\n");

	of_node = pdev->dev.of_node;
	if (of_node == NULL)
		return -1;

	return 0;
}

static int gpio_unused_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver gpio_unused_driver = {
	.probe		= gpio_unused_probe,
	.remove		= gpio_unused_remove,
	.driver		= {
		.name	= "gpio-unused",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(of_gpio_unused_match),
	},
};

static int __init gpio_unused_driver_init(void)
{
	return platform_driver_register(&gpio_unused_driver);
}

late_initcall(gpio_unused_driver_init);

