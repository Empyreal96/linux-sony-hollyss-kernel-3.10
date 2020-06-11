/*
 * Copyright(C) 2015 Foxconn International Holdings, Ltd. All rights reserved.
 */
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt



#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include "mach/mtk_thermal_monitor.h"

//CORE-KC-ThermalMitigation-00+[
#include <linux/pid.h>
#include <linux/signal.h>
#include <linux/sched.h>

extern struct proc_dir_entry *mtk_thermal_get_proc_drv_therm_dir_entry(void);

/* The following two ID should sync with the define in thermald part, otherwise
 * this mehcnaism cannot work well.
 * And should not use the same code ID as MTK default, so we start from 20.
 */
#define CAM_HEATED_OVER_CRITICAL	20
#define CAM_HEATED_IS_SAFETY		21
//CORE-KC-ThermalMitigation-00+]

#define MAX_NUM_INSTANCE_MTK_COOLER_CAM  1

#if 1
#define mtk_cooler_cam_dprintk(fmt, args...) \
  do { pr_debug("thermal/cooler/cam " fmt, ##args); } while (0)
#else
#define mtk_cooler_cam_dprintk(fmt, args...)
#endif

static struct thermal_cooling_device *cl_cam_dev[MAX_NUM_INSTANCE_MTK_COOLER_CAM] = { 0 };
static unsigned long cl_cam_state[MAX_NUM_INSTANCE_MTK_COOLER_CAM] = { 0 };

static unsigned int _cl_cam;

#define MAX_LEN (256)

//CORE-KC-ThermalMitigation-00+[
static unsigned int tm_pid;
static unsigned int tm_input_pid;
static struct task_struct g_task;
static struct task_struct *pg_task = &g_task;
static int ov_cri_happened = 0;
//CORE-KC-ThermalMitigation-00+]

static ssize_t _cl_cam_write(struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	int ret = 0;
	char tmp[MAX_LEN] = { 0 };

	len = (len < (MAX_LEN-1)) ? len : (MAX_LEN-1);
	/* write data to the buffer */
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
	}

	ret = kstrtouint(tmp, 10, &_cl_cam);
	if (ret)
		WARN_ON(1);

	mtk_cooler_cam_dprintk("%s %s = %d\n", __func__, tmp, _cl_cam);

	return len;
}

static int _cl_cam_read(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", _cl_cam);
	mtk_cooler_cam_dprintk("%s %d\n", __func__, _cl_cam);

	return 0;
}

static int _cl_cam_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
	return single_open(file, _cl_cam_read, PDE_DATA(inode));
#else
	return single_open(file, _cl_cam_read, PDE(inode)->data);
#endif
}

static const struct file_operations _cl_cam_fops = {
	.owner = THIS_MODULE,
	.open = _cl_cam_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = _cl_cam_write,
	.release = single_release,
};

//CORE-KC-ThermalMitigation-00+[
static int cam_send_signal(int signal_code)
{
	int ret = 0;

	if (tm_input_pid == 0) {
		mtk_cooler_cam_dprintk("[%s] pid is empty\n", __func__);
		ret = -1;
	}

	mtk_cooler_cam_dprintk("[%s]pid is %d, %d\n", __func__, tm_pid, tm_input_pid);

	if (ret == 0 && tm_input_pid != tm_pid) {
		tm_pid = tm_input_pid;
		pg_task = get_pid_task(find_vpid(tm_pid), PIDTYPE_PID);
	}

	if (ret == 0 && pg_task) {
		siginfo_t info;
		info.si_signo = SIGIO;
		info.si_errno = 0;
		info.si_code = signal_code;
		info.si_addr = NULL;
		ret = send_sig_info(SIGIO, &info, pg_task);
	}

	if (ret != 0)
		mtk_cooler_cam_dprintk("[%s] ret=%d\n", __func__, ret);

	return ret;
}
//CORE-KC-ThermalMitigation-00+]

static int mtk_cl_cam_get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = 1;
	/* mtk_cooler_cam_dprintk("mtk_cl_cam_get_max_state() %s %d\n", cdev->type, *state); */
	return 0;
}

static int mtk_cl_cam_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = *((unsigned long *)cdev->devdata);
	/* mtk_cooler_cam_dprintk("mtk_cl_cam_get_cur_state() %s %d\n", cdev->type, *state); */
	return 0;
}

static int mtk_cl_cam_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	/* mtk_cooler_cam_dprintk("mtk_cl_cam_set_cur_state() %s %d\n", cdev->type, state); */

	*((unsigned long *)cdev->devdata) = state;

	if (1 == state) {
		_cl_cam = 1;
//CORE-KC-ThermalMitigation-00+[
		if (0 == ov_cri_happened) {
			cam_send_signal(CAM_HEATED_OVER_CRITICAL);
			ov_cri_happened = 1;
		}
//CORE-KC-ThermalMitigation-00+]
	} else {
		_cl_cam = 0;
//CORE-KC-ThermalMitigation-00+[
		if (1 == ov_cri_happened) {
			cam_send_signal(CAM_HEATED_IS_SAFETY);
			ov_cri_happened = 0;
		}
//CORE-KC-ThermalMitigation-00+]
	}

	return 0;
}

/* bind fan callbacks to fan device */
static struct thermal_cooling_device_ops mtk_cl_cam_ops = {
	.get_max_state = mtk_cl_cam_get_max_state,
	.get_cur_state = mtk_cl_cam_get_cur_state,
	.set_cur_state = mtk_cl_cam_set_cur_state,
};

//CORE-KC-ThermalMitigation-00+[
static ssize_t _mtk_cl_cam_pid_write(struct file *filp, const char __user *buf, size_t len,
                                    loff_t *data)
{
	int ret = 0;
	char tmp[MAX_LEN] = { 0 };
	// write data to the buffer
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
	}

	ret = kstrtouint(tmp, 10, &tm_input_pid);
	if (ret)
		WARN_ON(1);

	mtk_cooler_cam_dprintk("%s %s = %d\n", __func__, tmp, tm_input_pid);

	return len;
}

static int _mtk_cl_cam_pid_read(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", tm_input_pid);
	mtk_cooler_cam_dprintk("%s %d\n", __func__, tm_input_pid);

	return 0;
}


static int _mtk_cl_cam_pid_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
	return single_open(file, _mtk_cl_cam_pid_read, PDE_DATA(inode));
#else
	return single_open(file, _mtk_cl_cam_pid_read, PDE(inode)->data);
#endif
}

static const struct file_operations _cl_cam_pid_fops = {
	.owner = THIS_MODULE,
	.open = _mtk_cl_cam_pid_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = _mtk_cl_cam_pid_write,
	.release = single_release,
};
//CORE-KC-ThermalMitigation-00+]

static int mtk_cooler_cam_register_ltf(void)
{
	int i;
	mtk_cooler_cam_dprintk("register ltf\n");

	for (i = MAX_NUM_INSTANCE_MTK_COOLER_CAM; i-- > 0;) {
		char temp[20] = { 0 };
		sprintf(temp, "mtk-cl-cam%02d", i);
		cl_cam_dev[i] = mtk_thermal_cooling_device_register(temp,
								    (void *)&cl_cam_state[i],
								    &mtk_cl_cam_ops);
	}

	return 0;
}

static void mtk_cooler_cam_unregister_ltf(void)
{
	int i;
	mtk_cooler_cam_dprintk("unregister ltf\n");

	for (i = MAX_NUM_INSTANCE_MTK_COOLER_CAM; i-- > 0;) {
		if (cl_cam_dev[i]) {
			mtk_thermal_cooling_device_unregister(cl_cam_dev[i]);
			cl_cam_dev[i] = NULL;
			cl_cam_state[i] = 0;
		}
	}
}


static int __init mtk_cooler_cam_init(void)
{
	int err = 0;
	int i;

	for (i = MAX_NUM_INSTANCE_MTK_COOLER_CAM; i-- > 0;) {
		cl_cam_dev[i] = NULL;
		cl_cam_state[i] = 0;
	}

	mtk_cooler_cam_dprintk("init\n");

	{
		struct proc_dir_entry *entry;
//CORE-KC-ThermalMitigation-00+[
		struct proc_dir_entry *dir_entry = NULL;

		dir_entry = mtk_thermal_get_proc_drv_therm_dir_entry();
//CORE-KC-ThermalMitigation-00+]

#if 0
		entry = create_proc_entry("driver/cl_cam", S_IRUGO | S_IWUSR, NULL);
		if (NULL != entry) {
			entry->read_proc = _cl_cam_read;
			entry->write_proc = _cl_cam_write;
		}
#endif
//CORE-KC-ThermalMitigation-00+[
		entry = proc_create("cl_cam_pid", S_IRUGO | S_IWUSR | S_IWGRP, dir_entry, &_cl_cam_pid_fops);
		if (!entry) {
			mtk_cooler_cam_dprintk("%s cl_cam_pid creation failed\n", __func__);
		} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
			proc_set_user(entry, 0, 1000);
#else
			entry->gid = 1000;
#endif
                }
//CORE-KC-ThermalMitigation-00+]

		entry = proc_create("driver/cl_cam", S_IRUGO | S_IWUSR, NULL, &_cl_cam_fops);
		if (!entry) {
			mtk_cooler_cam_dprintk("%s driver/cl_cam creation failed\n", __func__);
		}
	}

	err = mtk_cooler_cam_register_ltf();
	if (err)
		goto err_unreg;

	return 0;

 err_unreg:
	mtk_cooler_cam_unregister_ltf();
	return err;
}

static void __exit mtk_cooler_cam_exit(void)
{
	mtk_cooler_cam_dprintk("exit\n");

	mtk_cooler_cam_unregister_ltf();
}
module_init(mtk_cooler_cam_init);
module_exit(mtk_cooler_cam_exit);
