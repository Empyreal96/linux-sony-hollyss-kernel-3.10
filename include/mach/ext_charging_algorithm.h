/*****************************************************************************
 *
 * Filename:
 * ---------
 *   ext_charging_algorithm.h
 *
 * Description:
 * ------------
 *   This file implements the charging algorithm
 *
 * Author:
 * -------
 *  Eric Liu (huaruiliu@fihspec.com)
 *
 * Copyright(C) 2015 Foxconn International Holdings, Ltd. All rights reserved.
 ****************************************************************************/
#ifndef _EXT_CHARGING_ALGORITHM_H_
#define _EXT_CHARGING_ALGORITHM_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/printk.h>


#define	MSG_NONE	0
#define	MSG_INFO	1
#define	MSG_DBG		2
#define	MSG_TRK		3
#define	MSG_WARN	4
#define	MSG_ERR		5
#define	MSG_MAX		6
//////////////////////////////////////////////////////////////////////////////
/////////////function switch/////////////////////////////////////////////////////
	 
#define ENABLE_EXT_CHARGING_ALG 1   /* you can turn off this charging algorithm here */
#define debug_level MSG_INFO 

/* turn on will feed fake battery information to algorithm */
#define DEBUG_EXT_CHG_ALG		0	

/* unlock safety timeout when battery voltage low */
#define ENABLE_UNLOCK_SAFETY_TIMEOUT_WHEN_BAT_LOW	1 

/* unlock safety timeout when battery voltage lower than 3.8V */
#define UNLOCK_SAFETY_TIMEOUT_WHEN_BAT_LOW_VOL		3800 

#define ENABLE_DETECT_VBUS_VOLT_ABNORMAL			0
//////////////////////////////////////////////////////////////////////////////

#define VBUS_MAX 6500				// 6.5 V
#define VBUS_MIN 4300				// 4.3 V

enum chg_sony_state {
	CSS_GENERAL = 0, 
	CSS_SAFETY_TIMEOUT,
	CSS_MAINTENANCE_60,
	CSS_MAINTENANCE_260,
	CSS_CHG_ERROR,
};
#define VDD_MAX_CALL_TIME	4350000
#define VDD_MAX_IN_60_HRS	4300000
#define VDD_MAX_IN_260_HRS	4250000

 /* 5*3600 s */
#define SAFTY_TIMER_AC					18000

 /* 100*3600 s */
#define SAFTY_TIMER_USB					360000

/* CORE-EL-fix_always_100_after_full_charged-00+[ */
#if 1  /* 1: normal mode, 0: test mode */ 
 /* 60*3600 s = 216000 */
#define MAINTENACE60_T					216000
 
 /* 200*3600 s = 720000+216000 */
#define MAINTENACE260_T					936000
#else 
#define MAINTENACE60_T					(60 *2) // 2 mins
 
#define MAINTENACE260_T					(60 *4) // 4 mins
#endif
/* CORE-EL-fix_always_100_after_full_charged-00+] */
 
#define VMAXSEL_NORMAL_DELTA			100
#define VMAXSEL_MAN_DELTA				200

/* CORE-EL-fix_main-00+[ */
#define RECHARGE_SOC_60					97 // in 25degC: 4300(97.65289)     4100(79.65866)
#define RECHARGE_SOC_260				94 // in 25degC: 4250(93.4404)       4050(74.34847)
/* CORE-EL-fix_main-00+] */

enum sony_battery_type {
	BATTERY_SONY_TYPEI = 0,
	BATTERY_SONY_TYPEII,
	BATTERY_SONY_INVALID,
};

enum battery_temp_status {
	BATTERY_TEMP_STATUS_FREEZE = 0,
	BATTERY_TEMP_STATUS_COLD,
	BATTERY_TEMP_STATUS_NORMAL,
	BATTERY_TEMP_STATUS_WARM,
	BATTERY_TEMP_STATUS_HOT,
	BATTERY_TEMP_STATUS_MAX,
};

enum charger_type {
	CHARGER_TYPE_USB = 0,	
	CHARGER_TYPE_AC,
	CHARGER_TYPE_MAX,
	CHARGER_TYPE_INVALID,
};

#define TEMP_HYSTERISIS_DEGC 2

struct charging_temp_range {
	int			high_thr_temp;
	int			low_thr_temp;
};

enum next_action {
	NEXT_ACTION_NONE = 0,
	NEXT_ACTION_MAN_60_to_260 = 1,
	NEXT_ACTION_START_NEW_CYCLE = 2,
	NEXT_ACTION_SAFETY_TIMEOUT = 4,
};

enum running_mode
{
    RUNNING_MODE_ON = 0,
    RUNNING_MODE_SUSPEND
};

#define DBG_MSG(level, msg, ...)\
do {\
	if ((level < MSG_MAX) && (level >= debug_level)) \
	{\
		char   buf[200];\
		char  *s = buf;\
		\
		s += snprintf(s, sizeof(buf) - (size_t)(s-buf), "[chg %s %d]:",  __func__, __LINE__);\
		\
		snprintf(s, sizeof(buf) - (size_t)(s-buf), msg, ##__VA_ARGS__);\
		printk(KERN_ERR "%s", buf);\
	}\
}while(0)

/////////////////////////////////////////////////////////////////////
// OEM should implement the below functions///////////////////////////////
int ext_oem_get_bat_real_soc(void); /* CORE-EL-fix_main-00+ */
int ext_oem_get_bat_temp (void);
enum charger_type ext_oem_get_charger_type(void);
bool ext_oem_is_in_call (void);
int ext_oem_get_vbus_mv(void);
int ext_oem_get_vbat_mv(void);
void ext_oem_enable_charing(bool enable);
/////////////////////////////////////////////////////////////////////
// The functions exported to OEM to use        ///////////////////////////////
bool ext_is_rechare_by_real_soc(void); /* CORE-EL-fix_main-00+ */
int ext_main_min_soc_level(void); /* CORE-EL-fix_always_100_after_full_charged-00+ */
extern void ext_usb_chg_boost(bool enable);
unsigned int ext_get_safety_timer(void);
unsigned int ext_get_main_timer(void);
extern bool ext_get_v_and_c(unsigned int *chg_current, unsigned int *chg_voltage);
extern void ext_plug_in_cable(void);
extern void ext_unplug_cable(void);
extern void ext_charging_done(void);
extern void ext_charging_brain(int passed_s);
extern void ext_charging_reset(void);
extern void ext_inject_charging_enable(char* func, int line, bool enable);
extern enum chg_sony_state ext_get_chg_stat(void);
enum battery_temp_status ext_get_bat_temp_stat(void);
extern void ext_inject_safety_timer(unsigned int safety_timer);
extern void ext_inject_man_timer(unsigned int maintenance_timer);
extern void ext_oem_actively_detect_charger_volt_invalid(bool chr_invalid);
extern unsigned int ext_get_resume_volt_delta(void);
/////////////////////////////////////////////////////////////////////

#if (ENABLE_EXT_CHARGING_ALG == 1)
#endif 
	 
#endif /* end of #ifndef _EXT_CHARGING_ALGORITHM_H_ */

