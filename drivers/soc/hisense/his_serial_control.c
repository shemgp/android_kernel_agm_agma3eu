/*
	Hisense code:wangyongqing
*/
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/his_emmc_ops.h>
#include <linux/his_debug_control_node.h>

int msm_serial_hsl_enable;
EXPORT_SYMBOL_GPL(msm_serial_hsl_enable);

/* control the msm serial console register */
static int __init serial_console_enable_setup(char *p)
{
	if (!strcmp(p, "1"))
		msm_serial_hsl_enable = 1;
	else
		msm_serial_hsl_enable = 0;

	pr_err("%s: serial_console_enable = %d\n", __func__, msm_serial_hsl_enable);

	return 0;
}
early_param("hse", serial_console_enable_setup);

static void hs_control_serial_console(int value)
{
	int flag;
	int old_value;

	his_emmc_read(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_ENABLE_SERIAL_CONSOLE_OFFSET, 
			&flag, sizeof(flag));
	old_value = flag;
	if (old_value == !!value) {
		pr_info("%s : have already set to %d\n", __FUNCTION__, value);
		return;
	}

	if (value == 1)	{
		flag = 1;
		his_emmc_write(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_ENABLE_SERIAL_CONSOLE_OFFSET, 
			sizeof(flag), &flag);
	} else {
		flag = 0;
		his_emmc_write(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_ENABLE_SERIAL_CONSOLE_OFFSET, 
			sizeof(flag), &flag);
	}
}

static ssize_t serial_console_control_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int value;

	if (sscanf(buf, "%d", &value) != 1) {
		pr_err("debug_store: sscanf is wrong! \n");
		return -EINVAL;
	}

	pr_info("%s : enable %d\n", __FUNCTION__, value);
	hs_control_serial_console(value);

	return len;
}

static ssize_t serial_console_control_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int flag = 0;

	his_emmc_read(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_ENABLE_SERIAL_CONSOLE_OFFSET, 
			&flag, sizeof(flag));

	return sprintf(buf, "%d\n", flag);
}

static DEVICE_ATTR(serial_console_control, S_IWUSR | S_IWGRP | S_IRUGO,
		serial_console_control_show, serial_console_control_store);

static int __init serial_console_control_init(void)
{
	int ret;

	ret = his_register_debug_control_node(&dev_attr_serial_console_control.attr);
	if (ret < 0) {
		pr_err("Error creating poweroff sysfs group\n");
		return -1;
	}

	pr_info("%s ok\n", __FUNCTION__);

	return 0;
}
module_init(serial_console_control_init);