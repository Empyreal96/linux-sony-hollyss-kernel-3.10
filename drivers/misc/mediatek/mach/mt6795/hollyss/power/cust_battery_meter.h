/*
 * Copyright(C) 2016 Foxconn International Holdings, Ltd. All rights reserved.
 */
#ifndef _CUST_BATTERY_METER_H
#define _CUST_BATTERY_METER_H

#include <mach/mt_typedefs.h>

/* ============================================================ */
/* define */
/* ============================================================ */
/* #define SOC_BY_AUXADC */
#define SOC_BY_HW_FG
#define HW_FG_FORCE_USE_SW_OCV
/* #define SOC_BY_SW_FG */

/* #define CONFIG_DIS_CHECK_BATTERY */
/* #define FIXED_TBAT_25 */

/* ADC resistor  */
#define R_BAT_SENSE 4
#define R_I_SENSE 4
#define R_CHARGER_1 330
#define R_CHARGER_2 39

#define TEMPERATURE_T0             110
#define TEMPERATURE_T1             0
#define TEMPERATURE_T2             25
#define TEMPERATURE_T3             50
#define TEMPERATURE_T              255	/* This should be fixed, never change the value */

#define FG_METER_RESISTANCE	0

/* Qmax for battery  */
/* CORE-EL-DMS06429107-00*[ */
/* MTK ALPS02145872 suggest to decrease the QMax value to solve this issue.
    The new value is equal to around (93.8% * old value). */
#if 0 //(93.8%)
#define Q_MAX_POS_50	2538
#define Q_MAX_POS_25	2519
#define Q_MAX_POS_0		2311
#define Q_MAX_NEG_10	1809
	
#define Q_MAX_POS_50_H_CURRENT	2509
#define Q_MAX_POS_25_H_CURRENT	2464
#define Q_MAX_POS_0_H_CURRENT	1662
#define Q_MAX_NEG_10_H_CURRENT	825
#else /* The below are old values.*/
#define Q_MAX_POS_50	2706
#define Q_MAX_POS_25	2686
#define Q_MAX_POS_0		2464
#define Q_MAX_NEG_10	1929

#define Q_MAX_POS_50_H_CURRENT	2675
#define Q_MAX_POS_25_H_CURRENT	2627
#define Q_MAX_POS_0_H_CURRENT	1772
#define Q_MAX_NEG_10_H_CURRENT	880
#endif
/* CORE-EL-DMS06429107-00*] */

/* Discharge Percentage */
#define OAM_D5		 1	/* 1 : D5,   0: D2 */


/* battery meter parameter */
#define CHANGE_TRACKING_POINT
#define CUST_TRACKING_POINT  1
#define CUST_R_SENSE         68
#define CUST_HW_CC		    0
#define AGING_TUNING_VALUE   103
#define CUST_R_FG_OFFSET    0

#define OCV_BOARD_COMPESATE	0	/* mV */
#define R_FG_BOARD_BASE		1000
#define R_FG_BOARD_SLOPE	1000	/* slope */
/* CORE-EL-dynamic_car_tune_value-01*[ */
#define CAR_TUNE_VALUE_OLD	103 //1.027... set to 103(%), get the value from DP device
#define CAR_TUNE_VALUE_OLD_DOD0	103
#define CAR_TUNE_VALUE_NEW		101  //SP and After AP2, hardware circuit changed, the value changed
#define CAR_TUNE_VALUE_NEW_DOD0	82
/* CORE-EL-dynamic_car_tune_value-01*] */

/* HW Fuel gague  */
#define CURRENT_DETECT_R_FG	10	/* 1mA */
#define MinErrorOffset       1000
#define FG_VBAT_AVERAGE_SIZE 18
#define R_FG_VALUE			10	/* mOhm, base is 20 */

/* fg 2.0 */
#define DIFFERENCE_HWOCV_RTC		30
#define DIFFERENCE_HWOCV_SWOCV		10
#define DIFFERENCE_SWOCV_RTC		10
#define MAX_SWOCV			3

#define DIFFERENCE_VOLTAGE_UPDATE	20
#define AGING1_LOAD_SOC			70
#define AGING1_UPDATE_SOC		30
#define BATTERYPSEUDO100		95
#define BATTERYPSEUDO1			4

#define Q_MAX_BY_SYS		/* 8. Qmax varient by system drop voltage. */
#define Q_MAX_SYS_VOLTAGE		3400
#define SHUTDOWN_GAUGE0
#define SHUTDOWN_GAUGE1_XMINS
#define SHUTDOWN_GAUGE1_MINS		60

#define SHUTDOWN_SYSTEM_VOLTAGE		3400
#define CHARGE_TRACKING_TIME		60
#define DISCHARGE_TRACKING_TIME		10

#define RECHARGE_TOLERANCE		10
/* SW Fuel Gauge */
#define MAX_HWOCV			5
#define MAX_VBAT			90
#define DIFFERENCE_HWOCV_VBAT		30

/* fg 1.0 */
#define CUST_POWERON_DELTA_CAPACITY_TOLRANCE	40
#define CUST_POWERON_LOW_CAPACITY_TOLRANCE		5
#define CUST_POWERON_MAX_VBAT_TOLRANCE			90
#define CUST_POWERON_DELTA_VBAT_TOLRANCE		30

/* Disable Battery check for HQA */
#ifdef CONFIG_MTK_DISABLE_POWER_ON_OFF_VOLTAGE_LIMITATION
#define FIXED_TBAT_25
#endif

/* Dynamic change wake up period of battery thread when suspend*/
#define VBAT_NORMAL_WAKEUP		3600	/* 3.6V */
#define VBAT_LOW_POWER_WAKEUP		3500	/* 3.5v */
#define NORMAL_WAKEUP_PERIOD		5400	/* 90 * 60 = 90 min */
#define LOW_POWER_WAKEUP_PERIOD		300	/* 5 * 60 = 5 min */
#define CLOSE_POWEROFF_WAKEUP_PERIOD	30	/* 30 s */

#define INIT_SOC_BY_SW_SOC
/* #define SYNC_UI_SOC_IMM                       //3. UI SOC sync to FG SOC immediately */
#define MTK_ENABLE_AGING_ALGORITHM	/* 6. Q_MAX aging algorithm */
#define MD_SLEEP_CURRENT_CHECK	/* 5. Gauge Adjust by OCV 9. MD sleep current check */
					/*#define Q_MAX_BY_CURRENT*//* 7. Qmax varient by current loading. */

#endif				/* #ifndef _CUST_BATTERY_METER_H */
