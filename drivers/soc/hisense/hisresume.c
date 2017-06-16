/*
 * hisresume.c
 *
 * Record resume info!
 *
 * Copyright (C) HMCT Corporation 2015
 *
 */

#include <linux/hisresume.h>
#include <linux/tick.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/rtc.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/workqueue.h>

const char *modulename[S_A_MAX_ID]={
"Start",
"PowerKey",
"DoubleClick",
"EMMC",
"CTP",
"LCD",
"TFCard",
"Main-camera",
"Front-camera",
"Sensor-hub",
"Accelerometer-sensor",
"Compass-sensor",
"Alps-sensor",
"BL"
};


typedef struct s_a_struct {//sleep_awake struct
	int id;
	ktime_t start_point;
	ktime_t end_point;
	unsigned long long module_duration;
}s_a_type;

static ktime_t s_a_start_point;//唤醒/休眠的开始时间
static ktime_t s_a_end_point;//唤醒/休眠的结束时间
static s_a_type resumeinfo_data[S_A_MAX_ID];
static s_a_type suspendinfo_data[S_A_MAX_ID];
static void* s_a_log_buf =  NULL;//保存log
static u16 s_a_log_size =0;//记录log_buf中的字符数
static loff_t s_a_file_size =0;//记录保存文件的大小
static struct delayed_work s_a_work;
static unsigned long long total_resume_duration;
static unsigned long long total_suspend_duration;
struct timespec resume_point_ts;
struct timespec suspend_point_ts;
	
static bool log_save = false;//默认不往data分区保存log
bool sys_enter_resume = false;//表示系统当前处于什么状态，便于电源键事件处理
int resume_from;//唤醒源
int suspend_from;//休眠原因

#define S_A_LOG_SIZE 2048//定义申请的邋log_buf的大小
#define FAIL    0
#define SUCCESS 1
#define S_A_LOG_PATH                "/data/s_a_log.txt"
#define DISK_PATH                "/data"




u8 s_a_fs_mounted(char *path_name)
{
    struct path root_path;
    struct path path;
    int err;
    err = kern_path("/", LOOKUP_FOLLOW, &root_path);

    if (err)
    {
        printk(KERN_ERR "%s :rootdir NOT Mounted: %d\n",__func__,err);
        return FAIL;
    }
    err = kern_path(path_name, LOOKUP_FOLLOW, &path);

    if (err)
    {
        printk(KERN_ERR "%s NOT Mounted: %d\n", path_name, err);
        return FAIL;
    }

    path_put(&path);
    return SUCCESS;
}
//log_src:记录log的指针，log_size:需要保存log的数量,
//file_ops:传入需要写入的文件位置
static void s_a_log_file_write(char* log_src,u16 log_size, loff_t file_ops)
{
	u8 ret = SUCCESS;
	mm_segment_t old_fs;
	static bool need_check_fs = true;
	struct file *log_filp = NULL;//160 byte

	if(need_check_fs == true)
	{
		ret = s_a_fs_mounted(DISK_PATH);
		if (FAIL == ret)
		{
			printk(KERN_ERR "%s :data not mounted.\n",__func__);
			return;
		}
		need_check_fs = false;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	log_filp = filp_open(S_A_LOG_PATH, O_RDWR | O_CREAT, 0666);
	if (IS_ERR(log_filp))
	{ 
		printk(KERN_ERR "Failed to open/create %s.\n", S_A_LOG_PATH);
		ret = FAIL;
		goto wirte_file_exit;
	}

	log_filp->f_op->llseek(log_filp, file_ops, SEEK_SET);//搜索到文件末尾
	log_filp->f_op->write(log_filp, (char*)log_src, log_size, &log_filp->f_pos);//写数据到文件

	wirte_file_exit:
	if (log_filp && !IS_ERR(log_filp))
	{
		filp_close(log_filp, NULL);
	}
	set_fs(old_fs);
	return;
}

static void s_a_log_save_work(struct work_struct *data)
{
	int i = 0;
	u16 one_log_size = 0;
	unsigned long long duration;
	int usecs;
	struct rtc_time tm;
	
	if(log_save != true){
		printk(KERN_ERR "%s :do not need to save log\n",__func__);
		return;
	}
	if(s_a_log_buf == NULL ){
		printk(KERN_ERR "%s :save log states error: log buf is NULL!!\n",__func__);
		return;
	}
//空行
	one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "\n");
	s_a_log_size += one_log_size;
//suspend log
	//打印utc
	rtc_time_to_tm(suspend_point_ts.tv_sec, &tm);
	one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size),"Suspend start: %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, suspend_point_ts.tv_nsec);
	s_a_log_size += one_log_size;
	//打印各设备时间
	for(i = 0; i < S_A_MAX_ID; i++)
	{
		if((suspendinfo_data[i].id != S_A_START) && 
			(suspendinfo_data[i].id != S_A_DB_CLICK_ID) && 
			(suspendinfo_data[i].id != S_A_POWERKEY_ID))
		{
			duration = (unsigned long long)suspendinfo_data[i].module_duration << 10;
			do_div(duration, NSEC_PER_USEC);
			usecs = duration;
			if (usecs == 0)
				usecs = 1;
			one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "%s-suspend: %ld.%03ld msecs\n", modulename[i], usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
			s_a_log_size += one_log_size;
		}
	}
	//打印总时间
	duration = (unsigned long long)total_suspend_duration << 10;
	do_div(duration, NSEC_PER_USEC);
	usecs = duration;
	if (usecs == 0)
		usecs = 1;
	if(suspend_from == S_A_POWERKEY_ID)
		one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "Suspend-from-Powerkey-Total-cost: %ld.%03ld msecs\n", usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
	else
		one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "Suspend-Tota-cost: %ld.%03ld msecs\n", usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
	s_a_log_size += one_log_size;

//空行
	one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "\n");
	s_a_log_size += one_log_size;
//resume log
	//打印utc
	rtc_time_to_tm(resume_point_ts.tv_sec, &tm);
	one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "Resume start: %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, resume_point_ts.tv_nsec);
	s_a_log_size += one_log_size;
	//打印各设备时间
	for(i = 0; i < S_A_MAX_ID; i++)
	{
		if((resumeinfo_data[i].id != S_A_START) && 
			(resumeinfo_data[i].id != S_A_DB_CLICK_ID) && 
			(resumeinfo_data[i].id != S_A_POWERKEY_ID))
		{
			duration = (unsigned long long)resumeinfo_data[i].module_duration << 10;
			do_div(duration, NSEC_PER_USEC);
			usecs = duration;
			if (usecs == 0)
				usecs = 1;
			one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "%s-resume: %ld.%03ld msecs\n", modulename[i], usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
			s_a_log_size += one_log_size;
		}
	}
	//打印总时间
	duration = (unsigned long long)total_resume_duration << 10;
	do_div(duration, NSEC_PER_USEC);
	usecs = duration;
	if (usecs == 0)
		usecs = 1;
	if(resume_from == S_A_POWERKEY_ID)
		one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "Resume-from-Powerkey-Total-cost: %ld.%03ld msecs\n", usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
	else if(resume_from == S_A_DB_CLICK_ID)
		one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "Resume-from-Double-Click-Total-cost: %ld.%03ld msecs\n", usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
	else
		one_log_size=sprintf((char *)(s_a_log_buf+s_a_log_size), "Resume-Total-cost: %ld.%03ld msecs\n", usecs / USEC_PER_MSEC, usecs % USEC_PER_MSEC);
	s_a_log_size += one_log_size;
		
	//写入log内容，并将buf和使用的buf数置位
	s_a_log_file_write(s_a_log_buf,s_a_log_size,s_a_file_size);
	s_a_file_size += s_a_log_size;//文件内容数量增加
	s_a_log_size = 0;//写完log后将log_size清0，以便下次计数
}

void resumeinfo_start(enum s_a_id id)
{
	ktime_t calltime;

	if(log_save != true){
		/*printk(KERN_ERR "%s :do not need to save log\n",__func__);*/
		return;
	}
	
	if(id >= S_A_MAX_ID)
	{
		printk(KERN_ERR "s_a_debug %s: bigger than S_A_MAX_ID!\n", __func__);
		return;
	}

	resumeinfo_data[id].id = id;
	
	calltime = ktime_get();

	if(sys_enter_resume == false)
	{
		sys_enter_resume = true;
		s_a_end_point = s_a_start_point = calltime;
		resume_from = S_A_START;
		getnstimeofday(&resume_point_ts);
	}
	
	if((id == S_A_POWERKEY_ID) ||(id == S_A_DB_CLICK_ID)){
		resume_from = id;
	}
	else
		resumeinfo_data[id].start_point = calltime;
}
EXPORT_SYMBOL(resumeinfo_start);

void resumeinfo_end(enum s_a_id id)
{
	ktime_t calltime;
	ktime_t delta;

	if(log_save != true){
		/*printk(KERN_ERR "%s :do not need to save log\n",__func__);*/
		return;
	}
	
	if(id >= S_A_MAX_ID)
	{
		printk(KERN_ERR "s_a_debug %s: bigger than S_A_MAX_ID!\n", __func__);
		return;
	}

	resumeinfo_data[id].id = id;
	
	calltime = ktime_get();

	if(id == S_A_BL_ID)
		s_a_end_point = calltime;
	resumeinfo_data[id].end_point = calltime;
		
	if((id != S_A_START) && (id != S_A_POWERKEY_ID) && (id != S_A_DB_CLICK_ID))
	{
		delta = ktime_sub(resumeinfo_data[id].end_point, resumeinfo_data[id].start_point);
		resumeinfo_data[id].module_duration= (unsigned long long) ktime_to_ns(delta) >> 10;
	}
	
	if(id == S_A_BL_ID)
	{
		if( (ktime_compare(s_a_end_point, s_a_start_point) >= 0) && (sys_enter_resume== true))
		{
			delta = ktime_sub(s_a_end_point, s_a_start_point);
			total_resume_duration =(unsigned long long) ktime_to_ns(delta)>>10;
		}
		queue_delayed_work(system_nrt_wq, &s_a_work,msecs_to_jiffies(500));		
	}
}
EXPORT_SYMBOL(resumeinfo_end);

static int resumeinfo_proc_show(struct seq_file *m, void *v)
{
	int i = 0;
	
	for(i = 0; i < S_A_MAX_ID; i++)
	{
		if((resumeinfo_data[i].id != S_A_START) && 
			(resumeinfo_data[i].id != S_A_DB_CLICK_ID) && 
			(resumeinfo_data[i].id != S_A_POWERKEY_ID))
		{
			seq_printf(m, "%s: cost: %lld usecs", modulename[i], resumeinfo_data[i].module_duration);
			seq_printf(m, "\n");
		}
	}

	seq_printf(m, "Total cost: %lld usecs", total_resume_duration);
	
	seq_printf(m, "\n");

	return 0;
}


static int resumeinfo_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, resumeinfo_proc_show, NULL);
}

static const struct file_operations resumeinfo_proc_fops = {
    .open       = resumeinfo_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
};

void suspendinfo_start(enum s_a_id id)
{
	ktime_t calltime;

	if(log_save != true){
		/*printk(KERN_ERR "%s :do not need to save log\n",__func__);*/
		return;
	}
	if(id >= S_A_MAX_ID)
	{
		printk(KERN_ERR "s_a_debug %s: bigger than S_A_MAX_ID!\n", __func__);
		return;
	}

	suspendinfo_data[id].id = id;
	
	calltime = ktime_get();
	
	if(sys_enter_resume == true)
	{
		sys_enter_resume = false;
		s_a_end_point = s_a_start_point = calltime;
		suspend_from = S_A_START;
		getnstimeofday(&suspend_point_ts);
	}

	if(id == S_A_POWERKEY_ID){
		suspend_from = id;
	}
	else
		suspendinfo_data[id].start_point = calltime;
}
EXPORT_SYMBOL(suspendinfo_start);

void suspendinfo_end(enum s_a_id id)
{
	ktime_t calltime;
	ktime_t delta;

	if(log_save != true){
		/*printk(KERN_ERR "%s :do not need to save log\n",__func__);*/
		return;
	}
	if(id >= S_A_MAX_ID)
	{
		printk(KERN_ERR "s_a_debug %s: bigger than S_A_MAX_ID!\n", __func__);
		return;
	}
	suspendinfo_data[id].id = id;
	
	calltime = ktime_get();

	if(id == S_A_BL_ID)
		s_a_end_point = calltime;
	
	suspendinfo_data[id].end_point = calltime;
		
	if((id != S_A_START) && (id != S_A_POWERKEY_ID) && (id != S_A_DB_CLICK_ID))
	{
		delta = ktime_sub(suspendinfo_data[id].end_point, suspendinfo_data[id].start_point);
		suspendinfo_data[id].module_duration= (unsigned long long) ktime_to_ns(delta) >> 10;
	}
	
	if(id == S_A_BL_ID)
	{
		if ( (ktime_compare(s_a_end_point, s_a_start_point) >= 0) && (sys_enter_resume== false))
		{
			delta = ktime_sub(s_a_end_point, s_a_start_point);
			total_suspend_duration =(unsigned long long) ktime_to_ns(delta)>>10;
		}		
	}
}
EXPORT_SYMBOL(suspendinfo_end);
static int suspendinfo_proc_show(struct seq_file *m, void *v)
{
	int i = 0;
	
	for(i = 0; i < S_A_MAX_ID; i++)
	{
		if((suspendinfo_data[i].id != S_A_START) && 
			(suspendinfo_data[i].id != S_A_DB_CLICK_ID) && 
			(suspendinfo_data[i].id != S_A_POWERKEY_ID))
		{
			seq_printf(m, "%s: cost: %lld usecs", modulename[i], suspendinfo_data[i].module_duration);
			seq_printf(m, "\n");
		}
	}

	seq_printf(m, "Total cost: %lld usecs", total_suspend_duration);
	
	seq_printf(m, "\n");

	return 0;
}


static int suspendinfo_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, suspendinfo_proc_show, NULL);
}

static const struct file_operations suspendinfo_proc_fops = {
    .open       = suspendinfo_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
};
static int log_ctrl_proc_show(struct seq_file *m, void *v)
{
	if(log_save == true)
	{
		seq_printf(m, "1");
		seq_printf(m, "\n");
	}
	else
	{
		seq_printf(m, "0");
		seq_printf(m, "\n");
	}

	return 0;
}

ssize_t log_ctrl_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	char value;
	int ret;

	ret = copy_from_user(&value, buff, sizeof(char));
	if(ret)
	{
		printk(KERN_ERR "%s: copy_from_user failed.\n",__func__);
		return -EPERM;
	}
		
	if(value == 49)//传入值为1时
		log_save = true;
	else
		log_save = false;
	
	   //设置打开保存log时，申请相关内存，如果内容申请失败则认为设置标准失败
	if(log_save == true)
   	{
		if (NULL == s_a_log_buf)
		{
			s_a_log_buf = kzalloc(S_A_LOG_SIZE, GFP_KERNEL);

			if (NULL == s_a_log_buf)
			{
				printk(KERN_ERR "s_a_debug  %s: get memory failed.\n",__func__);
				log_save = false;
				return -EFAULT;
			}
			s_a_log_size = 0;
		}
       }
	else
	{
		//设置关闭保存log时释放内存
		if (NULL != s_a_log_buf)
		{
			cancel_delayed_work_sync(&s_a_work);
			kfree(s_a_log_buf);
			s_a_log_size = 0;
		}
	}
	return len;
}

static int log_ctrl_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, log_ctrl_proc_show, NULL);
}

static const struct file_operations log_ctrl_proc_fops = {
    .open       = log_ctrl_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
    .write       = log_ctrl_proc_write,
};

static int __init proc_resumeinfo_init(void)
{
	struct proc_dir_entry *pe;
	int i = 0;
	
	s_a_end_point = s_a_start_point = ktime_set(0, 0);

	for(i=0; i<S_A_MAX_ID; i++)
	{
		resumeinfo_data[i].id = S_A_START;
		suspendinfo_data[i].id = S_A_START;
	}
	
	INIT_DELAYED_WORK(&s_a_work, s_a_log_save_work);
	
	pe = proc_mkdir("s_a_debug", NULL);
	if (!pe){
		printk(KERN_ERR "make proc/s_a_debug ERROR!!\n");
		return ENOMEM;
	}
	pe = proc_create("s_a_debug/resumeinfo", 0, NULL, &resumeinfo_proc_fops);//显示设备唤醒时间
	pe = proc_create("s_a_debug/suspendinfo", 0, NULL, &suspendinfo_proc_fops);//显示设备休眠时间
	pe = proc_create("s_a_debug/log_ctrl", 0, NULL, &log_ctrl_proc_fops);//是否打开data区保存时间信息
	if (!pe)
		return -ENOMEM;
	
	return 0;
}

module_init(proc_resumeinfo_init);
