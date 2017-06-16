/*
  * Author:  zhanglei <zhanglei38@hisense.com>
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

enum{
	BOOT_LOG_LEVEL_SBL1,
	BOOT_LOG_LEVEL_LK,
	BOOT_LOG_LEVEL_MAX,
} boot_log_level;

#define  BOOT_LOG_MEM_ADDR		0xB0000000
#define  BOOT_LOG_MEM_SIZE		0x80000
#define  BOOT_LOG_LK_START		0x40000
#define  SHOW_LOG_BUF_SIZE		4096
#define  SHOW_LOG_MAX_TIME		64

static struct dentry *show_boot_log_dbgfs_dent;
static unsigned char boot_log_show_level;
static char sbl1_log_show_time;
static char lk_log_show_time;
static char *boot_log_ptr;
static char *boot_lk_log_ptr;
static char *boot_sbl1_log_ptr;

/************************************************************************************************
*  function name         : his_show_boot_log
*  function description : Print boot log from DDR.
					When cat this node, the function will be called repeatedly until return zero or negative.
*  parameter      :
*  author  :  zhanglei
*  date     :  2016-04-29
************************************************************************************************/
static ssize_t his_show_boot_log(struct file *file, char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int buf_size = 0;
	unsigned int bytes_written = 0;
	char *buf = NULL;

	buf = kzalloc(sizeof(char) * SHOW_LOG_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("boot LOG: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	if (!boot_log_ptr) {
		boot_log_ptr = ioremap_wc(BOOT_LOG_MEM_ADDR, BOOT_LOG_MEM_SIZE);
		if (!boot_log_ptr) {
			pr_err("boot LOG: %s, Failure to map base.\n", __func__);
			kfree(buf);
			return -EPERM;
		}
		boot_sbl1_log_ptr = boot_log_ptr;
		boot_lk_log_ptr = boot_log_ptr + BOOT_LOG_LK_START;
	}

	if (boot_log_show_level == BOOT_LOG_LEVEL_SBL1) {
		if ((*boot_sbl1_log_ptr == '\0') || (sbl1_log_show_time >= SHOW_LOG_MAX_TIME)) {
			pr_info("boot LOG: SBL1 log print complete, print lk log\n");
			boot_log_show_level = BOOT_LOG_LEVEL_LK;
		} else
			boot_sbl1_log_ptr += (SHOW_LOG_BUF_SIZE * sbl1_log_show_time);
	} else if (boot_log_show_level == BOOT_LOG_LEVEL_LK) {
		if ((*boot_lk_log_ptr == '\0') || (lk_log_show_time >= SHOW_LOG_MAX_TIME)) {
			pr_info("boot LOG: ALL log print complete\n");
			boot_log_show_level = BOOT_LOG_LEVEL_MAX;
		} else
			boot_lk_log_ptr += (SHOW_LOG_BUF_SIZE * lk_log_show_time);
	}

	if (boot_log_show_level == BOOT_LOG_LEVEL_SBL1) {
		pr_info("boot LOG: sbl1_boot_log  content: physical %x, virtual %p\n",
			BOOT_LOG_MEM_ADDR, boot_sbl1_log_ptr);
		buf_size = ksize(buf);
		bytes_written = scnprintf(buf, buf_size, "%s\n", boot_sbl1_log_ptr);
		sbl1_log_show_time += 1;
	} else if (boot_log_show_level == BOOT_LOG_LEVEL_LK) {
		pr_info("boot LOG: lk_boot_log  content: physical %x, virtual %p\n",
			BOOT_LOG_MEM_ADDR, boot_lk_log_ptr);
		buf_size = ksize(buf);
		bytes_written = scnprintf(buf, buf_size, "%s\n", boot_lk_log_ptr);
		lk_log_show_time += 1;
	} else if (boot_log_show_level >= BOOT_LOG_LEVEL_MAX) {
		boot_log_show_level = BOOT_LOG_LEVEL_SBL1;
		sbl1_log_show_time = 0;
		lk_log_show_time = 0;
		iounmap(boot_log_ptr);
		boot_log_ptr = NULL;
		ret = 0;
		goto print_end;
	}
	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_written);
print_end:
	kfree(buf);
	return ret;
}

const struct file_operations show_boot_log_dbgfs_ops = {
	.read = his_show_boot_log,
};

int show_boot_log_debugfs_init(void)
{
	struct dentry *entry = NULL;

	show_boot_log_dbgfs_dent = debugfs_create_dir("boot_log", 0);
	if (IS_ERR(show_boot_log_dbgfs_dent))
		return -ENOMEM;

	entry = debugfs_create_file("boot_log", 0400, show_boot_log_dbgfs_dent, 0,
				    &show_boot_log_dbgfs_ops);
	boot_log_show_level = BOOT_LOG_LEVEL_SBL1;
	lk_log_show_time = 0;
	sbl1_log_show_time = 0;
	boot_log_ptr = NULL;
	if (!entry)
		goto err;

	return 0;
err:
	debugfs_remove_recursive(show_boot_log_dbgfs_dent);
	return -ENOMEM;
}

void show_boot_log_debugfs_cleanup(void)
{
	if (show_boot_log_dbgfs_dent) {
		debugfs_remove_recursive(show_boot_log_dbgfs_dent);
		show_boot_log_dbgfs_dent = NULL;
	}
}

static int __init show_boot_log_status_init(void)
{
	int ret = 0;
	ret = show_boot_log_debugfs_init();
	if (ret)
		return -EPERM;
	return 0;
}

static void show_boot_log_status_exit(void)
{
	pr_info("boot LOG: show_boot_log_status_exit exiting ..\n");
	show_boot_log_debugfs_cleanup();
	pr_info("boot LOG: done show_boot_log_status_exit exit\n");
}

module_init(show_boot_log_status_init);
module_exit(show_boot_log_status_exit);
