/*
 * Copyright (C) 2013-2014 Hisense, Inc.
 *
 * Author:
 *   zhaoyufeng <zhaoyufeng@hisense.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/productinfo.h>


#define SECBOOT_CPUID_READ	0x0005E07C
#define SECBOOT_FUSE_READ	0x0005C098
#define CPUID_VALID_NUM		0x0FFFFFFF
#define FUSE_VALID_NUM		0x00303030
#define SECBOOT_BUF_SIZE	32

static struct dentry *cpuid_dbgfs_dent;
static struct dentry *fuse_dbgfs_dent;

static uint cpu_reg_read(u32 add)
{
	void __iomem *base;
	uint r_value;

	base = ioremap(add, 4);
	if (!base) {
		pr_err("cpu_reg_read: %s, Error read cpu register\n", __func__);
		return 0;
	}
	r_value = __raw_readl(base);
	iounmap(base);
	return r_value;
}


static ssize_t cpuid_dbgfs_read_cpuid(struct file *file, char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	char *buf = NULL;
	uint cpu_id = 0;
	int ret = 0;
	unsigned int buf_size = 0;
	unsigned int bytes_written = 0;

	buf = kzalloc(sizeof(char) * SECBOOT_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("cpu_id: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	cpu_id = CPUID_VALID_NUM & cpu_reg_read(SECBOOT_CPUID_READ);
	if (!cpu_id) {
		pr_err("cpu_id: %s, Error read cpu_id\n", __func__);
		kfree(buf);
		return -ENXIO;
	}
	buf_size = ksize(buf);
	bytes_written = scnprintf(buf, buf_size, "%08x", cpu_id);
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_written);
	kfree(buf);
	return ret;
}

static ssize_t fuse_dbgfs_read_fuse(struct file *file, char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	char *buf = NULL;
	uint fuse = 0;
	int ret = 0;
	bool fuse_is_exist = false;
	unsigned int buf_size = 0;
	unsigned int bytes_written = 0;

	buf = kzalloc(sizeof(char) * SECBOOT_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("cpu_id: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	fuse = cpu_reg_read(SECBOOT_FUSE_READ);
	fuse_is_exist = (FUSE_VALID_NUM == fuse) ? true : false;

	buf_size = ksize(buf);
	bytes_written = scnprintf(buf, buf_size, "%d\n", fuse_is_exist);
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_written);
	kfree(buf);
	return ret;
}

const struct file_operations cpuid_dbgfs_cpuid_ops = {
	.read = cpuid_dbgfs_read_cpuid,
};
const struct file_operations fuse_dbgfs_fuse_ops = {
	.read = fuse_dbgfs_read_fuse,
};

int cpuid_debugfs_init(void)
{
	struct dentry *entry = NULL;

	cpuid_dbgfs_dent = debugfs_create_dir("cpu_id", 0);
	if (IS_ERR(cpuid_dbgfs_dent))
		return -ENOMEM;

	entry = debugfs_create_file("cpu_id", 0444, cpuid_dbgfs_dent, 0,
				    &cpuid_dbgfs_cpuid_ops);
	if (!entry)
		goto err;

	return 0;
err:
	debugfs_remove_recursive(cpuid_dbgfs_dent);
	return -ENOMEM;
}

int fuse_debugfs_init(void)
{
	struct dentry *entry = NULL;

	fuse_dbgfs_dent = debugfs_create_dir("blow_fuse", 0);
	if (IS_ERR(fuse_dbgfs_dent))
		return -ENOMEM;

	entry = debugfs_create_file("blow_fuse_confirm", 0444, fuse_dbgfs_dent,
				0, &fuse_dbgfs_fuse_ops);
	if (!entry)
		goto err;

	return 0;
err:
	debugfs_remove_recursive(fuse_dbgfs_dent);
	return -ENOMEM;
}

void cpuid_debugfs_cleanup(void)
{
	if (cpuid_dbgfs_dent) {
		debugfs_remove_recursive(cpuid_dbgfs_dent);
		cpuid_dbgfs_dent = NULL;
	}
}

void fuse_debugfs_cleanup(void)
{
	if (fuse_dbgfs_dent) {
		debugfs_remove_recursive(fuse_dbgfs_dent);
		fuse_dbgfs_dent = NULL;
	}
}

static int __init fuse_status_init(void)
{
	int ret = 0;
	ret = cpuid_debugfs_init();
	if (ret)
		return -EPERM;
	ret = fuse_debugfs_init();
	if (ret) {
		cpuid_debugfs_cleanup();
		return -EPERM;
	}
	return 0;
}

static void fuse_status_exit(void)
{
	pr_info("fuse_status_exit exiting ..\n");
	cpuid_debugfs_cleanup();
	fuse_debugfs_cleanup();
	pr_info("done fuse_status_exit exit\n");
}


module_init(fuse_status_init);
module_exit(fuse_status_exit);
