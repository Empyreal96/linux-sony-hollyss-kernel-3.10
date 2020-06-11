/*
* Copyright (C) 2015 Sony Mobile Communications Inc.
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

#include <mach/mtk_rtc_hal_common.h>
#include <mach/mtk_rtc_tickstamp.h>

#define INFINITY ((unsigned int)-1)

#define VOID_EXIT_IF(x) do { if (x) goto _exit; } while(0);
#define VOID_RETURN_IF(x) do { if (x) return; } while(0);

#define TS_DIR "/cache/sc"
#define TS_PATH TS_DIR"/tickstamp"

static DEFINE_MUTEX(ts_stamp_rw_mutex);
static DEFINE_SPINLOCK(stamp_lock);
static struct tick_stamp g_stamp = { .epoch = MAX_STAMP };
static struct ts_work_struct ts_write_work;
static struct ts_work_struct ts_read_work;
static ticker_func g_ticker_fn;

static bool count_down(unsigned int *value, unsigned int delta) {
	if (!value || *value == 0 || *value < delta)
		return false;

	if (*value == INFINITY)
		return true;

	*value -= delta;
	return (*value != 0);
}

static bool wait_vfsmount(const char *dir_name, unsigned int trigger_interval,
	unsigned int retrigger_count) {
	int retval;
	struct path p;

	if (!dir_name)
		return false;

	while (count_down(&retrigger_count, 1)) {
		retval = kern_path(dir_name, LOOKUP_FOLLOW, &p);
		if (!retval)
			return true;
		msleep(trigger_interval);
	}

	return false;
}

static void fs_set(mm_segment_t *oldfs) {
	VOID_RETURN_IF(!oldfs);
	*oldfs = get_fs();
	set_fs(get_ds());
}

static void fs_restore(const mm_segment_t *oldfs) {
	VOID_RETURN_IF(!oldfs);
	set_fs(*oldfs);
}

static struct file* ts_open(const char *path, int flags, umode_t mode) {
	struct file *fp = NULL;
	mm_segment_t oldfs;

	fs_set(&oldfs);
	fp = filp_open(path, flags, mode);
	fs_restore(&oldfs);

	if (IS_ERR(fp)) {
		printk(KERN_ERR "Failed to open tickstamp file %s, (%ld)\n",
			path, PTR_ERR(fp));
		return NULL;
	}

	return fp;
}

static void ts_close(struct file *fp) {
	int retval = 0;

	VOID_EXIT_IF(!fp);
	retval = filp_close(fp, NULL);
	VOID_EXIT_IF(retval);
	fp = NULL;

_exit:
	if (retval)
		printk(KERN_ERR "Failed to close tickstamp file (%d)\n",
			retval);
}

static ssize_t ts_read(struct file *fp, char *data, size_t count) {
	int retval;
	mm_segment_t oldfs;

	fs_set(&oldfs);
	retval = vfs_read(fp, (char __user *)data, count, &fp->f_pos);
	fs_restore(&oldfs);

	return retval;
}

static ssize_t ts_write(struct file *fp, char *data, size_t count) {
	int retval;
	mm_segment_t oldfs;

	fs_set(&oldfs);
	retval = vfs_write(fp, (char __user *)data, count, &fp->f_pos);
	fs_restore(&oldfs);

	return retval;
}

static inline void get_stamp(struct tick_stamp *stamp) {
	unsigned long flags;

	VOID_RETURN_IF(!stamp);
	spin_lock_irqsave(&stamp_lock, flags);
	*stamp = g_stamp;
	spin_unlock_irqrestore(&stamp_lock, flags);
}

static void ts_read_stamp(struct work_struct *work) {
	int size = -EIO;
	struct file *tsfp = NULL;
	struct ts_work_struct *ts_read_work;
	unsigned long tick = 0;
	unsigned long flags;
	struct tick_stamp stamp;

	ts_read_work = container_of(work, struct ts_work_struct, work);
	VOID_EXIT_IF(!wait_vfsmount(TS_DIR, ts_read_work->trigger_interval,
		ts_read_work->retrigger_count));

	mutex_lock(&ts_stamp_rw_mutex);
	tsfp = ts_open(TS_PATH, O_RDONLY, 0);
	VOID_EXIT_IF(!tsfp);

	size = ts_read(tsfp, (char *)&stamp, sizeof(g_stamp));
	VOID_EXIT_IF(size < 0);

	spin_lock_irqsave(&stamp_lock, flags);
	g_stamp = stamp;
	spin_unlock_irqrestore(&stamp_lock, flags);

	if (likely(NULL != g_ticker_fn)) {
		tick = g_ticker_fn();
		/*
		 * Restamp the ticker if its epoch value is for any reason
		 * (eg. power loss) after the current RTC value.
		 * This will trigger increase of secure clock ticker version
		 */
		spin_lock_irqsave(&stamp_lock, flags);
		if (g_stamp.epoch > tick) {
			g_stamp.epoch = 0;
			size = -EINVAL;
		}
		spin_unlock_irqrestore(&stamp_lock, flags);
	}

_exit:
	ts_close(tsfp);
	mutex_unlock(&ts_stamp_rw_mutex);

	if (size < 0) {
		printk(KERN_ERR "Invalid or no tickstamp file found (%d)\n", size);
		ts_stamp(tick);
		if (!ts_set())
			printk(KERN_ERR "Failed to run tickstamp write work\n");
	}
}

static void ts_write_stamp(struct work_struct *work) {
	ssize_t size = -EIO;
	struct tick_stamp stamp;
	struct file *tsfp = NULL;
	struct ts_work_struct *ts_write_work;

	ts_write_work = container_of(work, struct ts_work_struct, work);
	VOID_EXIT_IF(!wait_vfsmount(TS_DIR, ts_write_work->trigger_interval,
		ts_write_work->retrigger_count));

	mutex_lock(&ts_stamp_rw_mutex);
	tsfp = ts_open(TS_PATH, O_CREAT | O_TRUNC | O_WRONLY, 0444);
	VOID_EXIT_IF(!tsfp);

	get_stamp(&stamp);

	size = ts_write(tsfp, (char *)&stamp, sizeof(stamp));
	VOID_EXIT_IF(size < 0);

_exit:
	ts_close(tsfp);
	mutex_unlock(&ts_stamp_rw_mutex);

	if (size < 0)
		printk(KERN_ERR "Failed to write tickstamp file (%zu)\n", size);
}

static void ts_work_init(struct ts_work_struct *ts_work, ts_worker_func work_fn,
	unsigned int retrigger_count, unsigned int trigger_interval) {
	VOID_RETURN_IF(!ts_work);
	INIT_WORK(&ts_work->work, work_fn);
	ts_work->retrigger_count = retrigger_count;
	ts_work->trigger_interval = trigger_interval;
}

static bool ts_work_run(struct ts_work_struct *ts_work) {
	if (!ts_work)
		return false;

	return schedule_work(&ts_work->work);
}

bool ts_init(ticker_func ticker_fn) {
	static bool init_done;
	bool retval = true;

	if (!init_done) {
		g_ticker_fn = ticker_fn;
		ts_work_init(&ts_write_work, ts_write_stamp, INFINITY, 1000);
		ts_work_init(&ts_read_work, ts_read_stamp, INFINITY, 1000);
		retval = ts_work_run(&ts_read_work);
		init_done = true;
	}

	return retval;
}

void ts_stamp(const unsigned long tick) {
	long delta;
	unsigned long flags;

	VOID_RETURN_IF(unlikely(NULL == g_ticker_fn));
	delta = (long)(g_ticker_fn() - tick);

	spin_lock_irqsave(&stamp_lock, flags);
	if (MAX_STAMP == g_stamp.epoch) {
		g_stamp.epoch = 0;
	}
	g_stamp.epoch += delta;
	spin_unlock_irqrestore(&stamp_lock, flags);
}


bool ts_set(void) {
	return ts_work_run(&ts_write_work) == 0;
}

void ts_get(unsigned long *epoch) {
	struct tick_stamp stamp;

	VOID_RETURN_IF(unlikely(NULL == epoch));
	get_stamp(&stamp);
	*epoch = stamp.epoch;
}
