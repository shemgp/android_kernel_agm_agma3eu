#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/his_emmc_ops.h>
#include <linux/his_debug_control_node.h>

#define FLAG_UPDATE_GPT_GROW				(0x55475054)
#define FLAG_FORMAT_GROW					(0x464D4184)
#define FLAG_NO_SD_RAMDUMP_ENABLE			(0x44554D50)
#define FLAG_SHRINK_USERDATA				(0x5348524B)

static void hs_enable_no_sd_ramdump(int value)
{
	int flag;
	int old_value;

	his_emmc_read(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_NO_SD_RAMDUMP_ENABLE_OFFSET, 
			&flag, sizeof(flag));
	old_value = (flag == FLAG_NO_SD_RAMDUMP_ENABLE);
	if (old_value == !!value) {
		pr_info("%s : have already set to %d\n", __FUNCTION__, value);
		return;
	}

	if (value == 1)	{
		flag = FLAG_SHRINK_USERDATA;
		his_emmc_write(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_SHRINK_USERDATA_OFFSET, 
			sizeof(flag), &flag);

		flag = FLAG_NO_SD_RAMDUMP_ENABLE;
		his_emmc_write(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_NO_SD_RAMDUMP_ENABLE_OFFSET, 
			sizeof(flag), &flag);
	} else {
		flag = 0;
		his_emmc_write(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_SHRINK_USERDATA_OFFSET, 
			sizeof(flag), &flag);

		flag = 0;
		his_emmc_write(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_NO_SD_RAMDUMP_ENABLE_OFFSET, 
			sizeof(flag), &flag);
	}
}

static ssize_t no_sd_ramdump_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int value;

	if (sscanf(buf, "%d", &value) != 1) {
		pr_err("debug_store: sscanf is wrong! \n");
		return -EINVAL;
	}

	pr_info("%s : enable %d\n", __FUNCTION__, value);
	hs_enable_no_sd_ramdump(value);

	return len;
}

static ssize_t no_sd_ramdump_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int flag = 0;

	his_emmc_read(NO_SD_RAMDUMP_FLAG_PARTITION, FLAG_NO_SD_RAMDUMP_ENABLE_OFFSET, 
			&flag, sizeof(flag));

	return sprintf(buf, "%d\n", flag == FLAG_NO_SD_RAMDUMP_ENABLE);
}

static DEVICE_ATTR(no_sd_ramdump, S_IWUSR | S_IWGRP | S_IRUGO,
		no_sd_ramdump_show, no_sd_ramdump_store);

static int __init his_no_sd_ramdump_init(void)
{
	int ret;

	ret = his_register_debug_control_node(&dev_attr_no_sd_ramdump.attr);
	if (ret < 0) {
		pr_err("Error creating poweroff sysfs group\n");
		return -1;
	}

	pr_info("%s ok\n", __FUNCTION__);

	return 0;
}
module_init(his_no_sd_ramdump_init);

