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

#ifndef __MTK_RTC_TIMESTAMP_H__
#define __MTK_RTC_TIMESTAMP_H__

#include <linux/buffer_head.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/rtc.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/namei.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

#define MAX_STAMP ((unsigned long)-1)

typedef unsigned long (*ticker_func)(void);
typedef void (*ts_worker_func)(struct work_struct *);

struct ts_work_struct {
	struct work_struct work;
	unsigned int retrigger_count;
	unsigned int trigger_interval;
};

/* Keep the stamp in a struct just in case
 * we'd like to extend it in the future
 */
struct tick_stamp {
	unsigned long epoch;
};

/**
 * ts_init - init system tickstamp
 * @ticker_fn: ticker function that provides a monotonic tick
 *
 * Needs to be called before ts_get or ts_set.
 * Triggers delayed work to read tickstamp file from VFS, if any.
 * The scheduled work blocks until the mount point of the
 * stamp becomes available.
 */
bool ts_init(ticker_func ticker_fn);

/**
 * ts_get - get last system tickstamp
 * @epoch: epoch time format of last set tickstamp
 */
void ts_get(unsigned long *epoch);

/**
 * ts_set - writes tickstamp to VFS
 * Triggers work to write tickstamp to VFS.
 * The scheduled work blocks until the mount point of the
 * stamp becomes available.
 */
bool ts_set(void);

/**
 * ts_stamp - set/update system tickstamp
 * @tick: epoch time format of current system tick
 */
void ts_stamp(const unsigned long tick);

#endif
