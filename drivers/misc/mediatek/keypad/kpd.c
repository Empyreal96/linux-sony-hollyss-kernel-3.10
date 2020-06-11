/*
 * Copyright (C) 2010 MediaTek, Inc.
 *
 * Author: Terry Chang <terry.chang@mediatek.com>
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

/*kpd.h file path: ALPS/mediatek/kernel/include/linux */
#include <linux/kpd.h>
#include <linux/wakelock.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#endif

/* PERI-BJ-KEYPAD_BringUp-00+[ */
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/fih_hw_info.h>
/* PERI-BJ-KEYPAD_BringUp-00+] */


#include <linux/fih_sw_info.h>	//CORE-TH-manual_crash-00+

/* PERI-JC-KEYPAD_Wakelock-00+[ */
#include <linux/wakelock.h>
#include <linux/jiffies.h>
/* PERI-JC-KEYPAD_Wakelock-00+] */


#define KPD_NAME	"mtk-kpd"
#define MTK_KP_WAKESOURCE	/* this is for auto set wake up source */

#define MT_KP_IRQ_ID -1//KP_IRQ_BIT_ID //PERI-BJ-KEYPAD_BringUp-00+

#if defined(CONFIG_MTK_AEE_MRDUMP)
#define PWK_DUMP
#endif

#ifdef __aarch64__
#undef BUG
#define BUG() *((unsigned *)0xaed) = 0xDEAD
#endif

#ifdef CONFIG_OF
void __iomem *kp_base;
static unsigned int kp_irqnr;
#endif
struct input_dev *kpd_input_dev;
static bool kpd_suspend;
static int kpd_show_hw_keycode = 1;
static int kpd_show_register = 1;
static int call_status;
static volatile int service_menu_state = 0;     /* PERI-FG-HALL_SENSOR_PORTING-01+ */
struct wake_lock key_wake_lock;/* PERI-JC-KEYPAD_Wakelock-00+ */
struct wake_lock kpd_suspend_lock;	/* For suspend usage */

/*for kpd_memory_setting() function*/
static u16 kpd_keymap[KPD_NUM_KEYS];
static u16 kpd_keymap_state[KPD_NUM_MEMS];
/***********************************/

/* for slide QWERTY */
#if KPD_HAS_SLIDE_QWERTY
static void kpd_slide_handler(unsigned long data);
static DECLARE_TASKLET(kpd_slide_tasklet, kpd_slide_handler, 0);
static u8 kpd_slide_state = !KPD_SLIDE_POLARITY;
#endif
#if !defined(CONFIG_MTK_LEGACY)
struct keypad_dts_data kpd_dts_data;
#endif
/* for Power key using EINT */
#if KPD_PWRKEY_USE_EINT
static void kpd_pwrkey_handler(unsigned long data);
static DECLARE_TASKLET(kpd_pwrkey_tasklet, kpd_pwrkey_handler, 0);
#endif

/* for keymap handling */
static void kpd_keymap_handler(unsigned long data);
static DECLARE_TASKLET(kpd_keymap_tasklet, kpd_keymap_handler, 0);

/*********************************************************************/
static void kpd_memory_setting(void);

/*********************************************************************/
static int kpd_pdrv_probe(struct platform_device *pdev);
static int kpd_pdrv_remove(struct platform_device *pdev);
#ifndef USE_EARLY_SUSPEND
static int kpd_pdrv_suspend(struct platform_device *pdev, pm_message_t state);
static int kpd_pdrv_resume(struct platform_device *pdev);
#endif

#ifdef CONFIG_OF
static const struct of_device_id kpd_of_match[] = {
	{ .compatible = "mediatek,KP", },
	{},
};
#endif
/* PERI-BJ-KEYPAD_BringUp-00+[ */
struct gpio_button_data
{
        unsigned        gpio, trigger, code, debounce , activelow;
        void (*Handler)(void);
};
struct gpio_keys_data
{
        struct timer_list timer;
        struct work_struct work;
        struct gpio_button_data button ;
};
struct gpio_keys_data *gpio_keys;

static void kpd_keys_work_func(struct work_struct *work)
{
        struct gpio_keys_data *bdata =
                container_of(work, struct gpio_keys_data, work);

        int state = (mt_get_gpio_in(bdata->button.gpio|0x80000000)? 1 : 0)^bdata->button.activelow;
        pr_info( "kpd_keys %d %s\n", bdata->button.code, (state?"down":"up"));
        input_report_key(kpd_input_dev, bdata->button.code, !!state );
        input_sync(kpd_input_dev);

        if( state == 1)
        {/* PERI-JC-KEYPAD_Wakelock-00+[ */
                if(bdata->button.code == KEY_CAMERA)
                {
                        wake_lock_timeout( &key_wake_lock, 2*HZ );
                }

                mt_eint_set_polarity(bdata->button.gpio, MT_EINT_POL_POS);
        }/* PERI-JC-KEYPAD_Wakelock-00+] */
        else
                mt_eint_set_polarity(bdata->button.gpio, MT_EINT_POL_NEG);
        mt_eint_set_sens(bdata->button.gpio, MT_LEVEL_SENSITIVE);
        mt_eint_unmask(bdata->button.gpio);
}
static void Kpd_keys_timer_func(unsigned long _data)
{
        struct gpio_keys_data *bdata = (struct gpio_keys_data *)_data;

        int state = (mt_get_gpio_in(bdata->button.gpio|0x80000000)? 1 : 0)^bdata->button.activelow;
        pr_info( "kpd_keys %d %s\n", bdata->button.code, (state?"down":"up"));
        input_report_key(kpd_input_dev, bdata->button.code, !!state );
        input_sync(kpd_input_dev);
        if( state == 1)
        {// PERI-JC-KEYPAD_Wakelock-00+[
                if(bdata->button.code == KEY_CAMERA)
                {
                        wake_lock_timeout( &key_wake_lock, 2*HZ );
                }
        }// PERI-JC-KEYPAD_Wakelock-00+]
/*
        if( state == 1)
        {// PERI-JC-KEYPAD_Wakelock-00+[
                if(bdata->button.code == KEY_CAMERA)
                {
                        wake_lock_timeout( &key_wake_lock, 2*HZ );
                }

                mt_eint_set_polarity(bdata->button.gpio, MT_EINT_POL_POS);
        }// PERI-JC-KEYPAD_Wakelock-00+]
        else
                mt_eint_set_polarity(bdata->button.gpio, MT_EINT_POL_NEG);
        mt_eint_set_sens(bdata->button.gpio, MT_LEVEL_SENSITIVE);
        mt_eint_unmask(bdata->button.gpio);
 */
        //schedule_work(&bdata->work);
}
static void Kpd_focus_keys_Handler(void)
{
        struct gpio_keys_data *key = gpio_keys ;

        int state = (mt_get_gpio_in(key->button.gpio|0x80000000)? 1 : 0)^key->button.activelow;
        pr_info( "Kpd_focus_keys_Handler:kpd_keys %d %s\n", key->button.code, (state?"down":"up"));
        input_report_key(kpd_input_dev, key->button.code, !!state );
        input_sync(kpd_input_dev);
        if( state == 1)
                mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_POS);
        else
                mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_NEG);
        mt_eint_set_sens(key->button.gpio, MT_LEVEL_SENSITIVE);
        mt_eint_unmask(key->button.gpio);
        /*if (key->button.debounce)
                mod_timer(&key->timer,
                        jiffies + msecs_to_jiffies(key->button.debounce));
        else
                schedule_work(&key->work);*/
}
static void Kpd_camera_keys_Handler(void)
{
        struct gpio_keys_data *key = (gpio_keys+1) ;

        int state = (mt_get_gpio_in(key->button.gpio|0x80000000)? 1 : 0)^key->button.activelow;
        pr_info( "Kpd_camera_keys_Handler:kpd_keys %d %s\n", key->button.code, (state?"down":"up"));
        input_report_key(kpd_input_dev, key->button.code, !!state );
        input_sync(kpd_input_dev);
        if( state == 1)
        {// PERI-JC-KEYPAD_Wakelock-00+[
                if(key->button.code == KEY_CAMERA)
                {
                        wake_lock_timeout( &key_wake_lock, 2*HZ );
                }
        }// PERI-JC-KEYPAD_Wakelock-00+]
        if( state == 1)
                mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_POS);
        else
                mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_NEG);
        mt_eint_set_sens(key->button.gpio, MT_LEVEL_SENSITIVE);
        mt_eint_unmask(key->button.gpio);

        /*if (key->button.debounce)
                mod_timer(&key->timer,
                        jiffies + msecs_to_jiffies(key->button.debounce));
        else
                schedule_work(&key->work);*/
}
static void Kpd_voldown_keys_Handler(void)
{
        struct gpio_keys_data *key = (gpio_keys+2) ;

        int state = (mt_get_gpio_in(key->button.gpio|0x80000000)? 1 : 0)^key->button.activelow;
        pr_info( "Kpd_voldown_keys_Handler:kpd_keys %d %s\n", key->button.code, (state?"down":"up"));
        input_report_key(kpd_input_dev, key->button.code, !!state );
        input_sync(kpd_input_dev);
        if( state == 1)
                mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_POS);
        else
                mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_NEG);
        mt_eint_set_sens(key->button.gpio, MT_LEVEL_SENSITIVE);
        mt_eint_unmask(key->button.gpio);

        /*if (key->button.debounce)
                mod_timer(&key->timer,
                        jiffies + msecs_to_jiffies(key->button.debounce));
        else
                schedule_work(&key->work);*/
}
/* PERI-BJ-KEYPAD_BringUp-00+] */

/* PERI-FG-HALL_SENSOR_PORTING-00+[ */
static void kpd_hall_sensor_handler(void)
{
    struct gpio_keys_data *key = (gpio_keys+3);

    int state = (mt_get_gpio_in(key->button.gpio|0x80000000)? 1 : 0) ^ key->button.activelow;

    if (service_menu_state == 0)        /* PERI-FG-HALL_SENSOR_PORTING-01+ */
    {
        printk("%s: %s\n", __func__, (state ? "cover" : "uncover"));
        /* for SW_LID, 1: lid open => slid, 0: lid shut => closed */
        input_report_switch(kpd_input_dev, key->button.code, !!state);
        input_sync(kpd_input_dev);
    }

    if (state)
        mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_POS);
    else
        mt_eint_set_polarity(key->button.gpio, MT_EINT_POL_NEG);

    mt_eint_unmask(key->button.gpio);
}
/* PERI-FG-HALL_SENSOR_PORTING-00+] */

static struct platform_driver kpd_pdrv = {
	.probe = kpd_pdrv_probe,
	.remove = kpd_pdrv_remove,
#ifndef USE_EARLY_SUSPEND
	.suspend = kpd_pdrv_suspend,
	.resume = kpd_pdrv_resume,
#endif
	.driver = {
		.name = KPD_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = kpd_of_match,
#endif
	},
};

/********************************************************************/
static void kpd_memory_setting(void)
{
	kpd_init_keymap(kpd_keymap);
	kpd_init_keymap_state(kpd_keymap_state);
	return;
}

/*****************for kpd auto set wake up source*************************/

static ssize_t kpd_store_call_state(struct device_driver *ddri, const char *buf, size_t count)
{
	if (sscanf(buf, "%u", &call_status) != 1) {
		kpd_print("kpd call state: Invalid values\n");
		return -EINVAL;
	}

	switch (call_status) {
	case 1:
		kpd_print("kpd call state: Idle state!\n");
		break;
	case 2:
		kpd_print("kpd call state: ringing state!\n");
		break;
	case 3:
		kpd_print("kpd call state: active or hold state!\n");
		break;

	default:
		kpd_print("kpd call state: Invalid values\n");
		break;
	}
	return count;
}

static ssize_t kpd_show_call_state(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	res = snprintf(buf, PAGE_SIZE, "%d\n", call_status);
	return res;
}

static DRIVER_ATTR(kpd_call_state, S_IWUSR | S_IRUGO, kpd_show_call_state, kpd_store_call_state);

/* PERI-FG-HALL_SENSOR_PORTING-00+[ */
static ssize_t kpd_show_hall_sensor_state(struct device_driver *ddri, char *buf)
{
    struct gpio_keys_data *key = (gpio_keys+3);
    ssize_t res;
    res = snprintf(buf, PAGE_SIZE, "%d\n", mt_get_gpio_in(key->button.gpio|0x80000000));
    return res;
}

static DRIVER_ATTR(kpd_hall_sensor_state, S_IRUGO, kpd_show_hall_sensor_state, NULL);
/* PERI-FG-HALL_SENSOR_PORTING-00+] */
/* PERI-FG-HALL_SENSOR_PORTING-01+[ */
static ssize_t kpd_store_service_menu_state(struct device_driver *ddri, const char *buf, size_t count)
{
    if (sscanf(buf, "%u", &service_menu_state) != 1)
    {
        printk("%s: Invalid values!\n", __func__);
        return -EINVAL;
    }

    printk("%s: %d\n", __func__, service_menu_state);
    return count;
}

static ssize_t kpd_show_service_menu_state(struct device_driver *ddri, char *buf)
{
    ssize_t res;
    res = snprintf(buf, PAGE_SIZE, "%d\n", service_menu_state);
    return res;
}

static DRIVER_ATTR(kpd_service_menu_state, S_IRUGO | S_IWUSR | S_IWGRP, kpd_show_service_menu_state, kpd_store_service_menu_state);
/* PERI-FG-HALL_SENSOR_PORTING-01+] */


static struct driver_attribute *kpd_attr_list[] = {
	&driver_attr_kpd_call_state,
        &driver_attr_kpd_hall_sensor_state,     /* PERI-FG-HALL_SENSOR_PORTING-00+ */
        &driver_attr_kpd_service_menu_state,    /* PERI-FG-HALL_SENSOR_PORTING-01+ */
};

/*----------------------------------------------------------------------------*/
static int kpd_create_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(kpd_attr_list) / sizeof(kpd_attr_list[0]));
	if (driver == NULL)
		return -EINVAL;

	for (idx = 0; idx < num; idx++) {
		err = driver_create_file(driver, kpd_attr_list[idx]);
		if (err) {
			kpd_info("driver_create_file (%s) = %d\n", kpd_attr_list[idx]->attr.name, err);
			break;
		}
	}
	return err;
}

/*----------------------------------------------------------------------------*/
static int kpd_delete_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(kpd_attr_list) / sizeof(kpd_attr_list[0]));

	if (!driver)
		return -EINVAL;

	for (idx = 0; idx < num; idx++)
		driver_remove_file(driver, kpd_attr_list[idx]);

	return err;
}

/*----------------------------------------------------------------------------*/
/********************************************************************************************/
/************************************************************************************************************************************************/
/* for autotest */
#if KPD_AUTOTEST
static const u16 kpd_auto_keymap[] = {
	KEY_MENU,
	KEY_HOME, KEY_BACK,
	KEY_CALL, KEY_ENDCALL,
	KEY_VOLUMEUP, KEY_VOLUMEDOWN,
	KEY_FOCUS, KEY_CAMERA,
};
#endif
/* for AEE manual dump */
#define AEE_VOLUMEUP_BIT	0
#define AEE_VOLUMEDOWN_BIT	1
#define AEE_DELAY_TIME		15
/* enable volup + voldown was pressed 5~15 s Trigger aee manual dump */
#define AEE_ENABLE_5_15		1
static struct hrtimer aee_timer;
static unsigned long aee_pressed_keys;
static bool aee_timer_started;

#if AEE_ENABLE_5_15
#define AEE_DELAY_TIME_5S	5
static struct hrtimer aee_timer_5s;
static bool aee_timer_5s_started;
static bool flags_5s;
#endif


#ifdef PWK_DUMP
#define AEE_POWERKEY_BIT 2
static struct hrtimer aee_timer_powerkey_30s;
static bool aee_timer_powerkey_30s_started;
#define AEE_DELAY_TIME_30S 30
#endif
static inline void kpd_update_aee_state(void)
{
	if (aee_pressed_keys == ((1 << AEE_VOLUMEUP_BIT) | (1 << AEE_VOLUMEDOWN_BIT))) {
		/* if volumeup and volumedown was pressed the same time then start the time of ten seconds */
		aee_timer_started = true;

#if AEE_ENABLE_5_15
		aee_timer_5s_started = true;
		hrtimer_start(&aee_timer_5s, ktime_set(AEE_DELAY_TIME_5S, 0), HRTIMER_MODE_REL);
#endif
		hrtimer_start(&aee_timer, ktime_set(AEE_DELAY_TIME, 0), HRTIMER_MODE_REL);
		kpd_print("aee_timer started\n");
	} else {
		/*
		 * hrtimer_cancel - cancel a timer and wait for the handler to finish.
		 * Returns:
		 * 0 when the timer was not active.
		 * 1 when the timer was active.
		 */
		if (aee_timer_started) {
			if (hrtimer_cancel(&aee_timer)) {
				kpd_print("try to cancel hrtimer\n");
#if AEE_ENABLE_5_15
				if (flags_5s) {
					kpd_print("Pressed Volup + Voldown5s~15s then trigger aee manual dump.\n");
					aee_kernel_reminding("manual dump", "Trigger Vol Up +Vol Down 5s");
				}
#endif

			}
#if AEE_ENABLE_5_15
			flags_5s = false;
#endif
			aee_timer_started = false;
			kpd_print("aee_timer canceled\n");
		}
#if AEE_ENABLE_5_15
		/*
		 * hrtimer_cancel - cancel a timer and wait for the handler to finish.
		 * Returns:
		 * 0 when the timer was not active.
		 * 1 when the timer was active.
		 */
		if (aee_timer_5s_started) {
			if (hrtimer_cancel(&aee_timer_5s))
				kpd_print("try to cancel hrtimer (5s)\n");
			aee_timer_5s_started = false;
			kpd_print("aee_timer canceled (5s)\n");
		}
#endif
	}
#ifdef PWK_DUMP
                 if(aee_pressed_keys == 1<<AEE_POWERKEY_BIT) {
                   printk("aee_timer_powerkey_30s_started  true  \n");
                   aee_timer_powerkey_30s_started = true;
                   hrtimer_start(&aee_timer_powerkey_30s,ktime_set(AEE_DELAY_TIME_30S, 0),HRTIMER_MODE_REL);
                  } else {
                    if(aee_timer_powerkey_30s_started) {
                       if(hrtimer_cancel(&aee_timer_powerkey_30s)) {
                         kpd_print("try to cancel aee_timer_powerkey_30s  \n");
                        }
                        aee_timer_powerkey_30s_started = false;
                        printk("aee_timer_powerkey_30s_started  false \n");
                        kpd_print("aee_timer aee_timer_powerkey_30s stop \n");
                      }
                 }
#endif



}

static void kpd_aee_handler(u32 keycode, u16 pressed)
{
	if (pressed) {
		if (keycode == KEY_VOLUMEUP)
			__set_bit(AEE_VOLUMEUP_BIT, &aee_pressed_keys);
		else if (keycode == KEY_VOLUMEDOWN)
			__set_bit(AEE_VOLUMEDOWN_BIT, &aee_pressed_keys);
#ifdef PWK_DUMP
		 else if(keycode == KEY_POWER) {
		 	printk(KPD_SAY "kpd_aee_handler  KEY_POWER  __set_bit \n");
			__set_bit(AEE_POWERKEY_BIT, &aee_pressed_keys);
		}
#endif
		else
			return;
		kpd_update_aee_state();
	} else {
		if (keycode == KEY_VOLUMEUP)
			__clear_bit(AEE_VOLUMEUP_BIT, &aee_pressed_keys);
		else if (keycode == KEY_VOLUMEDOWN)
			__clear_bit(AEE_VOLUMEDOWN_BIT, &aee_pressed_keys);
#ifdef PWK_DUMP
		else if(keycode == KEY_POWER) {
			printk(KPD_SAY "kpd_aee_handler  KEY_POWER  __clear_bit \n");
			__clear_bit(AEE_POWERKEY_BIT, &aee_pressed_keys);
		}
#endif
		else
			return;
		kpd_update_aee_state();
	}
}

static enum hrtimer_restart aee_timer_func(struct hrtimer *timer)
{
	/* kpd_info("kpd: vol up+vol down AEE manual dump!\n"); */
	/* aee_kernel_reminding("manual dump ", "Triggered by press KEY_VOLUMEUP+KEY_VOLUMEDOWN"); */
	/*aee_trigger_kdb();*/
	return HRTIMER_NORESTART;
}

#if AEE_ENABLE_5_15
static enum hrtimer_restart aee_timer_5s_func(struct hrtimer *timer)
{

	/* kpd_info("kpd: vol up+vol down AEE manual dump timer 5s !\n"); */
	flags_5s = true;
	return HRTIMER_NORESTART;
}
#endif


#ifdef PWK_DUMP
extern unsigned int debug_force_trigger_panic_enable;
void * get_pwron_cause_virt_addr(void);/*CORE-TH-manual_crash-00+*/

static enum hrtimer_restart aee_timer_30s_func(struct hrtimer *timer) {

 unsigned int *pwron_cause_ptr; 	/*CORE-TH-manual_crash-00+*/
	pr_err("*************FORCE CRASH***************");
	printk("in aee_timer_30s_func debug_force_trigger_panic_enable %d\n",debug_force_trigger_panic_enable);

	/*CORE-TH-manual_crash-00+*/
	pwron_cause_ptr = (unsigned int*) get_pwron_cause_virt_addr();
	if (pwron_cause_ptr != NULL){
	*pwron_cause_ptr |= MTD_PWR_ON_EVENT_FORCE_TRIGGER_PANIC;
	}
	/*CORE-TH-manual_crash-00-*/

	//if(debug_force_trigger_panic_enable)
	  BUG();

	return HRTIMER_NORESTART;
}
#endif


/************************************************************************************************************************************************/

#if KPD_HAS_SLIDE_QWERTY
static void kpd_slide_handler(unsigned long data)
{
	bool slid;
	u8 old_state = kpd_slide_state;

	kpd_slide_state = !kpd_slide_state;
	slid = (kpd_slide_state == !!KPD_SLIDE_POLARITY);
	/* for SW_LID, 1: lid open => slid, 0: lid shut => closed */
	input_report_switch(kpd_input_dev, SW_LID, slid);
	input_sync(kpd_input_dev);
	kpd_print("report QWERTY = %s\n", slid ? "slid" : "closed");

	if (old_state)
		mt_set_gpio_pull_select(GPIO_QWERTYSLIDE_EINT_PIN, 0);
	else
		mt_set_gpio_pull_select(GPIO_QWERTYSLIDE_EINT_PIN, 1);
	/* for detecting the return to old_state */
	mt65xx_eint_set_polarity(KPD_SLIDE_EINT, old_state);
	mt65xx_eint_unmask(KPD_SLIDE_EINT);
}

static void kpd_slide_eint_handler(void)
{
	tasklet_schedule(&kpd_slide_tasklet);
}
#endif

#if KPD_PWRKEY_USE_EINT
static void kpd_pwrkey_handler(unsigned long data)
{
	kpd_pwrkey_handler_hal(data);
}

static void kpd_pwrkey_eint_handler(void)
{
	tasklet_schedule(&kpd_pwrkey_tasklet);
}
#endif
/*********************************************************************/

/*********************************************************************/
#if KPD_PWRKEY_USE_PMIC
void kpd_pwrkey_pmic_handler(unsigned long pressed)
{
	kpd_print("Power Key generate, pressed=%ld\n", pressed);
	if (!kpd_input_dev) {
		kpd_print("KPD input device not ready\n");
		return;
	}
	kpd_pmic_pwrkey_hal(pressed);
#ifdef PWK_DUMP
	printk(KPD_SAY "Power Key generate, pressed=%ld enter kpd_aee_handler \n", pressed);
	kpd_aee_handler(KEY_POWER, pressed);
#endif

}
#endif

void kpd_pmic_rstkey_handler(unsigned long pressed)
{
	kpd_print("PMIC reset Key generate, pressed=%ld\n", pressed);
	if (!kpd_input_dev) {
		kpd_print("KPD input device not ready\n");
		return;
	}
	kpd_pmic_rstkey_hal(pressed);
#ifdef KPD_PMIC_RSTKEY_MAP
	kpd_aee_handler(KPD_PMIC_RSTKEY_MAP, pressed);
#endif
}

/*********************************************************************/

/*********************************************************************/
static void kpd_keymap_handler(unsigned long data)
{
	int i, j;
	bool pressed;
	u16 new_state[KPD_NUM_MEMS], change, mask;
	u16 hw_keycode, linux_keycode;
	kpd_get_keymap_state(new_state);

	wake_lock_timeout(&kpd_suspend_lock, HZ / 2);

	for (i = 0; i < KPD_NUM_MEMS; i++) {
		change = new_state[i] ^ kpd_keymap_state[i];
		if (!change)
			continue;

		for (j = 0; j < 16; j++) {
			mask = 1U << j;
			if (!(change & mask))
				continue;

			hw_keycode = (i << 4) + j;
			/* bit is 1: not pressed, 0: pressed */
			pressed = !(new_state[i] & mask);
			if (kpd_show_hw_keycode)
				kpd_print("(%s) HW keycode = %u\n", pressed ? "pressed" : "released", hw_keycode);
			BUG_ON(hw_keycode >= KPD_NUM_KEYS);
			linux_keycode = kpd_keymap[hw_keycode];
			if (unlikely(linux_keycode == 0)) {
				kpd_print("Linux keycode = 0\n");
				continue;
			}
			kpd_aee_handler(linux_keycode, pressed);

			kpd_backlight_handler(pressed, linux_keycode);
			input_report_key(kpd_input_dev, linux_keycode, pressed);
			input_sync(kpd_input_dev);
			kpd_print("report Linux keycode = %u\n", linux_keycode);
		}
	}

	memcpy(kpd_keymap_state, new_state, sizeof(new_state));
	kpd_print("save new keymap state\n");
#ifdef CONFIG_OF
	enable_irq(kp_irqnr);
#else
	enable_irq(MT_KP_IRQ_ID);
#endif
}

static irqreturn_t kpd_irq_handler(int irq, void *dev_id)
{
	/* use _nosync to avoid deadlock */
#ifdef CONFIG_OF
	disable_irq_nosync(kp_irqnr);
#else
	disable_irq_nosync(MT_KP_IRQ_ID);
#endif
	tasklet_schedule(&kpd_keymap_tasklet);
	return IRQ_HANDLED;
}

/*********************************************************************/

/*****************************************************************************************/
long kpd_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* void __user *uarg = (void __user *)arg; */

	switch (cmd) {
#if KPD_AUTOTEST
	case PRESS_OK_KEY:	/* KPD_AUTOTEST disable auto test setting to resolve CR ALPS00464496 */
		if (test_bit(KEY_OK, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS OK KEY!!\n");
			input_report_key(kpd_input_dev, KEY_OK, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support OK KEY!!\n");
		}
		break;
	case RELEASE_OK_KEY:
		if (test_bit(KEY_OK, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE OK KEY!!\n");
			input_report_key(kpd_input_dev, KEY_OK, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support OK KEY!!\n");
		}
		break;
	case PRESS_MENU_KEY:
		if (test_bit(KEY_MENU, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS MENU KEY!!\n");
			input_report_key(kpd_input_dev, KEY_MENU, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support MENU KEY!!\n");
		}
		break;
	case RELEASE_MENU_KEY:
		if (test_bit(KEY_MENU, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE MENU KEY!!\n");
			input_report_key(kpd_input_dev, KEY_MENU, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support MENU KEY!!\n");
		}

		break;
	case PRESS_UP_KEY:
		if (test_bit(KEY_UP, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS UP KEY!!\n");
			input_report_key(kpd_input_dev, KEY_UP, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support UP KEY!!\n");
		}
		break;
	case RELEASE_UP_KEY:
		if (test_bit(KEY_UP, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE UP KEY!!\n");
			input_report_key(kpd_input_dev, KEY_UP, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support UP KEY!!\n");
		}
		break;
	case PRESS_DOWN_KEY:
		if (test_bit(KEY_DOWN, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS DOWN KEY!!\n");
			input_report_key(kpd_input_dev, KEY_DOWN, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support DOWN KEY!!\n");
		}
		break;
	case RELEASE_DOWN_KEY:
		if (test_bit(KEY_DOWN, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE DOWN KEY!!\n");
			input_report_key(kpd_input_dev, KEY_DOWN, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support DOWN KEY!!\n");
		}
		break;
	case PRESS_LEFT_KEY:
		if (test_bit(KEY_LEFT, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS LEFT KEY!!\n");
			input_report_key(kpd_input_dev, KEY_LEFT, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support LEFT KEY!!\n");
		}
		break;
	case RELEASE_LEFT_KEY:
		if (test_bit(KEY_LEFT, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE LEFT KEY!!\n");
			input_report_key(kpd_input_dev, KEY_LEFT, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support LEFT KEY!!\n");
		}
		break;

	case PRESS_RIGHT_KEY:
		if (test_bit(KEY_RIGHT, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS RIGHT KEY!!\n");
			input_report_key(kpd_input_dev, KEY_RIGHT, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support RIGHT KEY!!\n");
		}
		break;
	case RELEASE_RIGHT_KEY:
		if (test_bit(KEY_RIGHT, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE RIGHT KEY!!\n");
			input_report_key(kpd_input_dev, KEY_RIGHT, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support RIGHT KEY!!\n");
		}
		break;
	case PRESS_HOME_KEY:
		if (test_bit(KEY_HOME, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS HOME KEY!!\n");
			input_report_key(kpd_input_dev, KEY_HOME, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support HOME KEY!!\n");
		}
		break;
	case RELEASE_HOME_KEY:
		if (test_bit(KEY_HOME, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE HOME KEY!!\n");
			input_report_key(kpd_input_dev, KEY_HOME, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support HOME KEY!!\n");
		}
		break;
	case PRESS_BACK_KEY:
		if (test_bit(KEY_BACK, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS BACK KEY!!\n");
			input_report_key(kpd_input_dev, KEY_BACK, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support BACK KEY!!\n");
		}
		break;
	case RELEASE_BACK_KEY:
		if (test_bit(KEY_BACK, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE BACK KEY!!\n");
			input_report_key(kpd_input_dev, KEY_BACK, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support BACK KEY!!\n");
		}
		break;
	case PRESS_CALL_KEY:
		if (test_bit(KEY_CALL, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS CALL KEY!!\n");
			input_report_key(kpd_input_dev, KEY_CALL, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support CALL KEY!!\n");
		}
		break;
	case RELEASE_CALL_KEY:
		if (test_bit(KEY_CALL, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE CALL KEY!!\n");
			input_report_key(kpd_input_dev, KEY_CALL, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support CALL KEY!!\n");
		}
		break;

	case PRESS_ENDCALL_KEY:
		if (test_bit(KEY_ENDCALL, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS ENDCALL KEY!!\n");
			input_report_key(kpd_input_dev, KEY_ENDCALL, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support ENDCALL KEY!!\n");
		}
		break;
	case RELEASE_ENDCALL_KEY:
		if (test_bit(KEY_ENDCALL, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE ENDCALL KEY!!\n");
			input_report_key(kpd_input_dev, KEY_ENDCALL, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support ENDCALL KEY!!\n");
		}
		break;
	case PRESS_VLUP_KEY:
		if (test_bit(KEY_VOLUMEUP, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS VOLUMEUP KEY!!\n");
			input_report_key(kpd_input_dev, KEY_VOLUMEUP, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support VOLUMEUP KEY!!\n");
		}
		break;
	case RELEASE_VLUP_KEY:
		if (test_bit(KEY_VOLUMEUP, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE VOLUMEUP KEY!!\n");
			input_report_key(kpd_input_dev, KEY_VOLUMEUP, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support VOLUMEUP KEY!!\n");
		}
		break;
	case PRESS_VLDOWN_KEY:
		if (test_bit(KEY_VOLUMEDOWN, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS VOLUMEDOWN KEY!!\n");
			input_report_key(kpd_input_dev, KEY_VOLUMEDOWN, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support VOLUMEDOWN KEY!!\n");
		}
		break;
	case RELEASE_VLDOWN_KEY:
		if (test_bit(KEY_VOLUMEDOWN, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE VOLUMEDOWN KEY!!\n");
			input_report_key(kpd_input_dev, KEY_VOLUMEDOWN, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support VOLUMEDOWN KEY!!\n");
		}
		break;
	case PRESS_FOCUS_KEY:
		if (test_bit(KEY_FOCUS, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS FOCUS KEY!!\n");
			input_report_key(kpd_input_dev, KEY_FOCUS, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support FOCUS KEY!!\n");
		}
		break;
	case RELEASE_FOCUS_KEY:
		if (test_bit(KEY_FOCUS, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE FOCUS KEY!!\n");
			input_report_key(kpd_input_dev, KEY_FOCUS, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support RELEASE KEY!!\n");
		}
		break;
	case PRESS_CAMERA_KEY:
		if (test_bit(KEY_CAMERA, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS CAMERA KEY!!\n");
			input_report_key(kpd_input_dev, KEY_CAMERA, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support CAMERA KEY!!\n");
		}
		break;
	case RELEASE_CAMERA_KEY:
		if (test_bit(KEY_CAMERA, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE CAMERA KEY!!\n");
			input_report_key(kpd_input_dev, KEY_CAMERA, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support CAMERA KEY!!\n");
		}
		break;
	case PRESS_POWER_KEY:
		if (test_bit(KEY_POWER, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] PRESS POWER KEY!!\n");
			input_report_key(kpd_input_dev, KEY_POWER, 1);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support POWER KEY!!\n");
		}
		break;
	case RELEASE_POWER_KEY:
		if (test_bit(KEY_POWER, kpd_input_dev->keybit)) {
			kpd_print("[AUTOTEST] RELEASE POWER KEY!!\n");
			input_report_key(kpd_input_dev, KEY_POWER, 0);
			input_sync(kpd_input_dev);
		} else {
			kpd_print("[AUTOTEST] Not Support POWER KEY!!\n");
		}
		break;
#endif

	case SET_KPD_KCOL:
		kpd_auto_test_for_factorymode();	/* API 3 for kpd factory mode auto-test */
		kpd_print("[kpd_auto_test_for_factorymode] test performed!!\n");
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int kpd_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations kpd_dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = kpd_dev_ioctl,
	.open = kpd_dev_open,
};

/*********************************************************************/
static struct miscdevice kpd_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = KPD_NAME,
	.fops = &kpd_dev_fops,
};

static int kpd_open(struct input_dev *dev)
{
	kpd_slide_qwerty_init();	/* API 1 for kpd slide qwerty init settings */
	return 0;
}

#if !defined(CONFIG_MTK_LEGACY)
void kpd_get_dts_info(void)
{
	struct device_node *node;
	node = of_find_compatible_node(NULL, NULL, "mediatek, kpd");
	if (node) {
		of_property_read_u32(node, "kpd-key-debounce", &kpd_dts_data.kpd_key_debounce);
		of_property_read_u32(node, "kpd-sw-pwrkey", &kpd_dts_data.kpd_sw_pwrkey);
		of_property_read_u32(node, "kpd-hw-pwrkey", &kpd_dts_data.kpd_hw_pwrkey);
		of_property_read_u32(node, "kpd-sw-rstkey", &kpd_dts_data.kpd_sw_rstkey);
		of_property_read_u32(node, "kpd-hw-rstkey", &kpd_dts_data.kpd_hw_rstkey);
		of_property_read_u32(node, "kpd-use-extend-type", &kpd_dts_data.kpd_use_extend_type);
		of_property_read_u32(node, "kpd-pwrkey-eint-gpio", &kpd_dts_data.kpd_pwrkey_eint_gpio);
		of_property_read_u32(node, "kpd-pwrkey-gpio-din", &kpd_dts_data.kpd_pwrkey_gpio_din);
		of_property_read_u32(node, "kpd-hw-dl-key1", &kpd_dts_data.kpd_hw_dl_key1);
		of_property_read_u32(node, "kpd-hw-dl-key2", &kpd_dts_data.kpd_hw_dl_key2);
		of_property_read_u32(node, "kpd-hw-dl-key3", &kpd_dts_data.kpd_hw_dl_key3);
		of_property_read_u32(node, "kpd-hw-recovery-key", &kpd_dts_data.kpd_hw_recovery_key);
		of_property_read_u32(node, "kpd-hw-factory-key", &kpd_dts_data.kpd_hw_factory_key);
		of_property_read_u32_array(node, "kpd-hw-init-map", kpd_dts_data.kpd_hw_init_map,
					   ARRAY_SIZE(kpd_dts_data.kpd_hw_init_map));

		kpd_info("key-debounce = %d, sw-pwrkey = %d, hw-pwrkey = %d, hw-rstkey = %d, sw-rstkey = %d\n",
			 kpd_dts_data.kpd_key_debounce, kpd_dts_data.kpd_sw_pwrkey, kpd_dts_data.kpd_hw_pwrkey,
			 kpd_dts_data.kpd_hw_rstkey, kpd_dts_data.kpd_sw_rstkey);
	} else {
		kpd_info("[kpd]%s can't find compatible custom node\n", __func__);
	}
}
#endif
static int kpd_pdrv_probe(struct platform_device *pdev)
{

	int i, r;
	int err = 0;

#ifdef CONFIG_OF
	kp_base = of_iomap(pdev->dev.of_node, 0);
	if (!kp_base) {
		kpd_info("KP iomap failed\n");
		return -ENODEV;
	};

	kp_irqnr = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!kp_irqnr) {
		kpd_info("KP get irqnr failed\n");
		return -ENODEV;
	}
	kpd_info("kp base: 0x%p, addr:0x%p,  kp irq: %d\n", kp_base, &kp_base, kp_irqnr);
#endif
        /* PERI-BJ-KEYPAD_BringUp-00+[ */
        struct gpio_keys_data *key ;

  //Default for DP phase and following phase
        struct gpio_button_data button_data[] =
        {/*
           {  GPIO2  , CUST_EINTF_TRIGGER_FALLING|CUST_EINTF_TRIGGER_RISING , KEY_FOCUS      ,100   , 1 , Kpd_focus_keys_Handler },
           {  GPIO11  , CUST_EINTF_TRIGGER_FALLING|CUST_EINTF_TRIGGER_RISING , KEY_CAMERA    ,100   , 1 , Kpd_camera_keys_Handler },
           {  GPIO124, CUST_EINTF_TRIGGER_FALLING|CUST_EINTF_TRIGGER_RISING , KEY_VOLUMEDOWN ,100   , 1 , Kpd_voldown_keys_Handler },
           */
           {  GPIO2  , CUST_EINTF_TRIGGER_LOW|CUST_EINTF_TRIGGER_HIGH , KEY_FOCUS      ,100   , 1 , Kpd_focus_keys_Handler },
           {  GPIO11  , CUST_EINTF_TRIGGER_LOW|CUST_EINTF_TRIGGER_HIGH , KEY_CAMERA    ,100   , 1 , Kpd_camera_keys_Handler },
           {  GPIO124, CUST_EINTF_TRIGGER_LOW|CUST_EINTF_TRIGGER_HIGH , KEY_VOLUMEDOWN ,100   , 1 , Kpd_voldown_keys_Handler },
           {  GPIO9  , CUST_EINTF_TRIGGER_FALLING|CUST_EINTF_TRIGGER_RISING , SW_LID         ,100     , 1 , kpd_hall_sensor_handler },  /* PERI-FG-HALL_SENSOR_PORTING-00+ */
        };

        if(fih_get_product_phase()<PHASE_PD){
                //EVM phase
                button_data[0].gpio = GPIO0;
                button_data[2].gpio = GPIO122;
        }else if(fih_get_product_phase()<PHASE_DP){
                //PDP phase
                button_data[2].gpio = GPIO122;
        }
        int nbutton=(sizeof(button_data)/sizeof(struct gpio_button_data));
        /* PERI-BJ-KEYPAD_BringUp-00+] */

#if defined(CONFIG_MTK_LEGACY)	/* This not need now */
#ifdef CONFIG_MTK_LDVT
	kpd_ldvt_test_init();	/* API 2 for kpd LFVT test enviroment settings */
#endif
#endif
	/* initialize and register input device (/dev/input/eventX) */
	kpd_input_dev = input_allocate_device();
	if (!kpd_input_dev)
		return -ENOMEM;

	kpd_input_dev->name = KPD_NAME;
	kpd_input_dev->id.bustype = BUS_HOST;
	kpd_input_dev->id.vendor = 0x2454;
	kpd_input_dev->id.product = 0x6500;
	kpd_input_dev->id.version = 0x0010;
	kpd_input_dev->open = kpd_open;
        /* PERI-BJ-KEYPAD_BringUp-00+[ */
        gpio_keys = kzalloc(sizeof(struct gpio_keys_data) * nbutton , GFP_KERNEL);
        for( i = 0 ; i < nbutton ; ++i )
        {
                key = ( gpio_keys + i ) ;
                key->button.gpio = button_data[i].gpio;
                key->button.trigger = button_data[i].trigger;
                key->button.code = button_data[i].code;
                key->button.debounce = button_data[i].debounce;
                key->button.activelow = button_data[i].activelow;
                key->button.Handler = button_data[i].Handler;

                INIT_WORK(&key->work, kpd_keys_work_func);
                setup_timer(&key->timer, Kpd_keys_timer_func, (unsigned long)key);
                __set_bit(key->button.code, kpd_input_dev->keybit);

                mt_set_gpio_mode((key->button.gpio|0x80000000), GPIO_MODE_00);
                mt_set_gpio_dir((key->button.gpio|0x80000000), GPIO_DIR_IN);
                mt_set_gpio_pull_enable((key->button.gpio|0x80000000), GPIO_PULL_ENABLE);
                mt_set_gpio_pull_select((key->button.gpio|0x80000000), GPIO_PULL_UP);
                if(key->button.debounce)
                        mt_eint_set_hw_debounce(key->button.gpio, key->button.debounce );
                mt_eint_registration(key->button.gpio, key->button.trigger, key->button.Handler, 0);
                mt_eint_unmask(key->button.gpio);
        }
        /* PERI-BJ-KEYPAD_BringUp-00+] */


#if !defined(CONFIG_MTK_LEGACY)
	kpd_get_dts_info();
#endif
	/* fulfill custom settings */
	kpd_memory_setting();

	__set_bit(EV_KEY, kpd_input_dev->evbit);

#if (KPD_PWRKEY_USE_EINT || KPD_PWRKEY_USE_PMIC)
#if !defined(CONFIG_MTK_LEGACY)
	__set_bit(kpd_dts_data.kpd_sw_pwrkey, kpd_input_dev->keybit);
#else
	__set_bit(KPD_PWRKEY_MAP, kpd_input_dev->keybit);
#endif
	kpd_keymap[8] = 0;
#endif
#if !defined(CONFIG_MTK_LEGACY)
	if (!kpd_dts_data.kpd_use_extend_type) {
		for (i = 17; i < KPD_NUM_KEYS; i += 9)	/* only [8] works for Power key */
			kpd_keymap[i] = 0;
	}
#else
#if !KPD_USE_EXTEND_TYPE
	for (i = 17; i < KPD_NUM_KEYS; i += 9)	/* only [8] works for Power key */
		kpd_keymap[i] = 0;
#endif
#endif
	for (i = 0; i < KPD_NUM_KEYS; i++) {
		if (kpd_keymap[i] != 0)
			__set_bit(kpd_keymap[i], kpd_input_dev->keybit);
	}

#if KPD_AUTOTEST
	for (i = 0; i < ARRAY_SIZE(kpd_auto_keymap); i++)
		__set_bit(kpd_auto_keymap[i], kpd_input_dev->keybit);
#endif

/* PERI-FG-HALL_SENSOR_PORTING-00*[ */
//#if KPD_HAS_SLIDE_QWERTY
        __set_bit(EV_SW, kpd_input_dev->evbit);
        __set_bit(SW_LID, kpd_input_dev->swbit);
//#endif
/* PERI-FG-HALL_SENSOR_PORTING-00*] */


#if KPD_HAS_SLIDE_QWERTY
	__set_bit(EV_SW, kpd_input_dev->evbit);
	__set_bit(SW_LID, kpd_input_dev->swbit);
#endif
#if !defined(CONFIG_MTK_LEGACY)
	if (kpd_dts_data.kpd_sw_rstkey)
		__set_bit(kpd_dts_data.kpd_sw_rstkey, kpd_input_dev->keybit);
#else
#ifdef KPD_PMIC_RSTKEY_MAP
	__set_bit(KPD_PMIC_RSTKEY_MAP, kpd_input_dev->keybit);
#endif
#endif
#ifdef KPD_KEY_MAP
	__set_bit(KPD_KEY_MAP, kpd_input_dev->keybit);
#endif
	kpd_input_dev->dev.parent = &pdev->dev;
	r = input_register_device(kpd_input_dev);
	if (r) {
		kpd_info("register input device failed (%d)\n", r);
		input_free_device(kpd_input_dev);
		return r;
	}

	/* register device (/dev/mt6575-kpd) */
	kpd_dev.parent = &pdev->dev;
	r = misc_register(&kpd_dev);
	if (r) {
		kpd_info("register device failed (%d)\n", r);
		input_unregister_device(kpd_input_dev);
		return r;
	}

	wake_lock_init(&kpd_suspend_lock, WAKE_LOCK_SUSPEND, "kpd wakelock");

	/* register IRQ and EINT */
#if !defined(CONFIG_MTK_LEGACY)
	kpd_set_debounce(kpd_dts_data.kpd_key_debounce);
#else
	kpd_set_debounce(KPD_KEY_DEBOUNCE);
#endif
#ifdef CONFIG_OF
	r = request_irq(kp_irqnr, kpd_irq_handler, IRQF_TRIGGER_NONE, KPD_NAME, NULL);
#else
	r = request_irq(MT_KP_IRQ_ID, kpd_irq_handler, IRQF_TRIGGER_FALLING, KPD_NAME, NULL);
#endif
	if (r) {
		kpd_info("register IRQ failed (%d)\n", r);
		misc_deregister(&kpd_dev);
		input_unregister_device(kpd_input_dev);
		return r;
	}
	mt_eint_register();

#ifndef KPD_EARLY_PORTING	/*add for avoid early porting build err the macro is defined in custom file */
	long_press_reboot_function_setting();	/* /API 4 for kpd long press reboot function setting */
#endif
	hrtimer_init(&aee_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aee_timer.function = aee_timer_func;

#if AEE_ENABLE_5_15
	hrtimer_init(&aee_timer_5s, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aee_timer_5s.function = aee_timer_5s_func;
#endif
	wake_lock_init(&key_wake_lock, WAKE_LOCK_SUSPEND, "gpiokey");/* PERI-JC-KEYPAD_Wakelock-00+ */
#ifdef PWK_DUMP
       hrtimer_init(&aee_timer_powerkey_30s, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
       aee_timer_powerkey_30s.function = aee_timer_30s_func;
#endif

	err = kpd_create_attr(&kpd_pdrv.driver);
	if (err) {
		kpd_info("create attr file fail\n");
		wake_lock_destroy(&key_wake_lock);/* PERI-JC-KEYPAD_Wakelock-00+] */
		kpd_delete_attr(&kpd_pdrv.driver);
		return err;
	}
	kpd_info("%s Done\n", __func__);
	return 0;
}

/* should never be called */
static int kpd_pdrv_remove(struct platform_device *pdev)
{
	wake_lock_destroy(&key_wake_lock);/* PERI-JC-KEYPAD_Wakelock-00+] */
	return 0;
}

#ifndef USE_EARLY_SUSPEND
static int kpd_pdrv_suspend(struct platform_device *pdev, pm_message_t state)
{
	kpd_suspend = true;
#ifdef MTK_KP_WAKESOURCE
	if (call_status == 2) {
		kpd_print("kpd_pdrv_suspend wake up source enable!! (%d)\n", kpd_suspend);
	} else {
		kpd_wakeup_src_setting(0);
		kpd_print("kpd_pdrv_suspend wake up source disable!! (%d)\n", kpd_suspend);
	}
#endif
	kpd_disable_backlight();
	kpd_print("suspend!! (%d)\n", kpd_suspend);
	return 0;
}

static int kpd_pdrv_resume(struct platform_device *pdev)
{
	kpd_suspend = false;
#ifdef MTK_KP_WAKESOURCE
	if (call_status == 2) {
		kpd_print("kpd_pdrv_suspend wake up source enable!! (%d)\n", kpd_suspend);
	} else {
		kpd_print("kpd_pdrv_suspend wake up source resume!! (%d)\n", kpd_suspend);
		kpd_wakeup_src_setting(1);
	}
#endif
	kpd_print("resume!! (%d)\n", kpd_suspend);
	return 0;
}
#else
#define kpd_pdrv_suspend	NULL
#define kpd_pdrv_resume		NULL
#endif

#ifdef USE_EARLY_SUSPEND
static void kpd_early_suspend(struct early_suspend *h)
{
	kpd_suspend = true;
#ifdef MTK_KP_WAKESOURCE
	if (call_status == 2) {
		kpd_print("kpd_early_suspend wake up source enable!! (%d)\n", kpd_suspend);
	} else {
		/* kpd_wakeup_src_setting(0); */
		kpd_print("kpd_early_suspend wake up source disable!! (%d)\n", kpd_suspend);
	}
#endif
	kpd_disable_backlight();
	kpd_print("early suspend!! (%d)\n", kpd_suspend);
}

static void kpd_early_resume(struct early_suspend *h)
{
	kpd_suspend = false;
#ifdef MTK_KP_WAKESOURCE
	if (call_status == 2) {
		kpd_print("kpd_early_resume wake up source resume!! (%d)\n", kpd_suspend);
	} else {
		kpd_print("kpd_early_resume wake up source enable!! (%d)\n", kpd_suspend);
		/* kpd_wakeup_src_setting(1); */
	}
#endif
	kpd_print("early resume!! (%d)\n", kpd_suspend);
}

static struct early_suspend kpd_early_suspend_desc = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = kpd_early_suspend,
	.resume = kpd_early_resume,
};
#endif

#ifdef CONFIG_MTK_SMARTBOOK_SUPPORT
#ifdef CONFIG_HAS_SBSUSPEND
static struct sb_handler kpd_sb_handler_desc = {
	.level = SB_LEVEL_DISABLE_KEYPAD,
	.plug_in = sb_kpd_enable,
	.plug_out = sb_kpd_disable,
};
#endif
#endif

static int __init kpd_mod_init(void)
{
	int r;

	r = platform_driver_register(&kpd_pdrv);
	if (r) {
		kpd_info("register driver failed (%d)\n", r);
		return r;
	}
#ifdef USE_EARLY_SUSPEND
	register_early_suspend(&kpd_early_suspend_desc);
#endif

#ifdef CONFIG_MTK_SMARTBOOK_SUPPORT
#ifdef CONFIG_HAS_SBSUSPEND
	register_sb_handler(&kpd_sb_handler_desc);
#endif
#endif

	return 0;
}

/* should never be called */
static void __exit kpd_mod_exit(void)
{
}

module_init(kpd_mod_init);
module_exit(kpd_mod_exit);

module_param(kpd_show_hw_keycode, int, 0644);
module_param(kpd_show_register, int, 0644);

MODULE_AUTHOR("yucong.xiong <yucong.xiong@mediatek.com>");
MODULE_DESCRIPTION("MTK Keypad (KPD) Driver v0.4");
MODULE_LICENSE("GPL");
