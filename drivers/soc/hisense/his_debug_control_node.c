#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>

static struct kobject *his_debug_flag_kobj;

int his_register_debug_control_node(const struct attribute *attr)
{
	int ret;

	if (!attr) {
		pr_err("%s NULL pointer\n", __FUNCTION__);
		return -1;
	}

	if (!his_debug_flag_kobj) {
		his_debug_flag_kobj = kobject_create_and_add("debug_control2", NULL);
		if (!his_debug_flag_kobj) {
			pr_err("%s kobject create failed\n", __FUNCTION__);
			return -1;
		}
	}

	ret = sysfs_create_file(his_debug_flag_kobj, attr);
	if (ret < 0) {
		pr_err("%s Error creating sysfs group\n", __FUNCTION__);
		return -1;
	}

	pr_info("%s register %s ok\n", __FUNCTION__, attr->name);

	return 0;
}
EXPORT_SYMBOL(his_register_debug_control_node);

