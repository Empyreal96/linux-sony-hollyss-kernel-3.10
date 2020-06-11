/*****************************************************************************
 *
 * Filename:
 * ---------
 *   ext_charging_algorithm.c
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
#include <mach/ext_charging_algorithm.h>
//////////////////////////////////////////////////////////////////////////////
/////////////OEM functions/////////////////////////////////////////////////////
typedef struct {
	bool						chg_enabled;
	enum chg_sony_state			chg_state;
	enum battery_temp_status	bat_temp_status;
	unsigned int				safety_timer;
	unsigned int				maintenance_timer;
	unsigned int				resume_voltage_delta;
} __EXT_CHG_OEM_VARS__;

__EXT_CHG_OEM_VARS__ ext_oem_vars;
__EXT_CHG_OEM_VARS__ *the_chip = &ext_oem_vars;

/*
	SONY charging algorithm related 
*/
unsigned int charging_current[BATTERY_TEMP_STATUS_MAX][CHARGER_TYPE_MAX] = {
	{	/* FREEZE */
		0,		/* USB */
		0, 		/* AC */
	},
	{	/* COLD */
		50000,		/* USB */
		80000, 		/* AC */
	},
	{	/* NORMAL */
		50000,		/* USB */
		150000, 	/* AC */
	},
	{	/* WARM */
		50000,	/* USB */
		80000,	/* AC */
	},
	{	/* HOT */
		0,		/* USB */
		0,		/* AC */
	},
};

unsigned int charging_voltage[BATTERY_TEMP_STATUS_MAX]= {
	0,			/* FREEZE */
	4200000,	/* COLD */
	4350000,	/* NORMAL */
	4200000,	/* WARM */
	0,			/* HOT */
};

char *battery_temp_string[] = {
	"freeze",
	"cold",
	"normal",
	"warm",
	"hot",
};

/* used for alien battery (polling mode) */
struct charging_temp_range charging_temp_range_data_polling[BATTERY_TEMP_STATUS_MAX] ={
		{0 + TEMP_HYSTERISIS_DEGC, -255},							/* FREEZE */ 
		{10 + TEMP_HYSTERISIS_DEGC, 0},								/* COLD */ 
		{45, 10},													/* NORMAL */
		{60, 45 - TEMP_HYSTERISIS_DEGC},							/* WARM */
		{255, 60 - TEMP_HYSTERISIS_DEGC},							/* HOT */
};

//////////////////////////////////////////////////////////////////////////////
#define DEBUG_ME 1

#if (DEBUG_EXT_CHG_ALG == 1)

int ext_oem_get_bat_temp (void){
	return 25;
}

enum charger_type ext_oem_get_charger_type(void) {
	return CHARGER_TYPE_AC;
}

bool ext_oem_is_in_call (void){
	return true;
}

int ext_oem_get_vbus_mv(void){
	return 5500;
}

int ext_oem_get_vbat_mv(void) {
	return 4000;
}

void ext_oem_enable_charing(bool enable) {

	/* do nothing */
	return;
}
#endif
//////////////////////////////////////////////////////////////////////////////
/* CORE-EL-fix_main-00+[ */
bool ext_is_rechare_by_real_soc(void) {
	int real_soc = ext_oem_get_bat_real_soc();

	if (the_chip->chg_state == CSS_MAINTENANCE_60)
		if (real_soc < RECHARGE_SOC_60)
			return true;
	else if (the_chip->chg_state == CSS_MAINTENANCE_260)
		if (real_soc < RECHARGE_SOC_260)
			return true;

	return false;
}
/* CORE-EL-fix_main-00+] */

/* CORE-EL-fix_always_100_after_full_charged-00+[ */
int ext_main_min_soc_level(void) {
	if (the_chip->chg_state == CSS_MAINTENANCE_60)
		return RECHARGE_SOC_60;
	else if (the_chip->chg_state == CSS_MAINTENANCE_260)
		return RECHARGE_SOC_260;
	else /* not in mainteance state, return 0 */
		return 0;
}
/* CORE-EL-fix_always_100_after_full_charged-00+] */

void ext_usb_chg_boost(bool enable) {
	if (enable) {
		/* snoop usb charging current to 1200mA */
		/* %%TBTA: If it is proper to charge high current when in warm? */
		charging_current[BATTERY_TEMP_STATUS_COLD][CHARGER_TYPE_USB] = 120000;
		charging_current[BATTERY_TEMP_STATUS_NORMAL][CHARGER_TYPE_USB] = 120000;
		charging_current[BATTERY_TEMP_STATUS_WARM][CHARGER_TYPE_USB] = 100000;
		// CORE-EL-add_usb_chg_boost_log-00+
		DBG_MSG(MSG_ERR, "enable usb_chg_boost\n");
	}
	else {
		/* change usb charging current back to 500mA */
		charging_current[BATTERY_TEMP_STATUS_COLD][CHARGER_TYPE_USB] = 50000;
		charging_current[BATTERY_TEMP_STATUS_NORMAL][CHARGER_TYPE_USB] = 50000;
		charging_current[BATTERY_TEMP_STATUS_WARM][CHARGER_TYPE_USB] = 50000;
		// CORE-EL-add_usb_chg_boost_log-00+
		DBG_MSG(MSG_ERR, "disable usb_chg_boost\n");
	}
}

/* %%TODO: 
 1. implement resume voltage get function  (DONE)
 2. implement charging status get function  (DONE)
 3. ....
 */

unsigned int ext_get_safety_timer(void) {
	return the_chip->safety_timer;
}

unsigned int ext_get_main_timer(void) {
	return the_chip->maintenance_timer;
}

unsigned int ext_get_resume_volt_delta(void) {
	int local_call_state = ext_oem_is_in_call();

	/* If we are in call, we prefer to charge always. */
	if (local_call_state) {
		return 0;
	}
	
	return the_chip->resume_voltage_delta;
}

void ext_oem_actively_detect_charger_volt_invalid(bool chr_invalid) {
	if (chr_invalid) {
		int vbus_mv = ext_oem_get_vbus_mv();

//		the_chip->chg_state = CSS_CHG_ERROR;

		/* disable charging immediately */
		ext_oem_enable_charing(false);

		DBG_MSG(MSG_ERR, "invalid charger voltage %d\n", vbus_mv);
	}
}

/* turn on/off the code to detect vbus voltage abnormal */
#if (ENABLE_DETECT_VBUS_VOLT_ABNORMAL == 1)
#define CHARGER_VOLT_DETECT_MAX 3
static enum chg_sony_state ext_pool_detect_charger_volt_valid(void){
	static int detect_cnt = 0;
	int vbus_mv;

	/* if charger abnormal found, just return, to recover this error, must plug out the charger */
	if (the_chip->chg_state == CSS_CHG_ERROR)
		goto exit;

	vbus_mv = ext_oem_get_vbus_mv();
	
	if (vbus_mv > VBUS_MAX || vbus_mv < VBUS_MIN){
		detect_cnt ++;

		DBG_MSG(MSG_ERR, "invalid charger voltage %d(%d)\n", vbus_mv, detect_cnt);
		
		if (detect_cnt >= CHARGER_VOLT_DETECT_MAX) {
			the_chip->chg_state = CSS_CHG_ERROR;
			detect_cnt = 0;
			DBG_MSG(MSG_ERR, "invalid charger voltage %d(determined)\n", vbus_mv);
		}
	}
	else
		detect_cnt = 0;

exit:
	return the_chip->chg_state;
}
#endif

static enum battery_temp_status ext_pool_detect_battery_temp_status(){
	enum battery_temp_status 	bat_temp_status = the_chip->bat_temp_status;
	enum battery_temp_status 	new_bat_temp_status = bat_temp_status;
	int		avail_steps;
	int		steps;	
	bool	step_up 	= false;
	bool	step_down	= false;
	int		rbatt_temp;

	rbatt_temp = ext_oem_get_bat_temp();

	if (unlikely(bat_temp_status < BATTERY_TEMP_STATUS_FREEZE || bat_temp_status >  BATTERY_TEMP_STATUS_HOT)) {
		DBG_MSG(MSG_ERR, "invalid bat_temp_status %d\n", bat_temp_status);
		return the_chip->bat_temp_status;
	}

	if (rbatt_temp > charging_temp_range_data_polling[bat_temp_status].high_thr_temp) {
		step_up		= true;
	}
	else if (rbatt_temp < charging_temp_range_data_polling[bat_temp_status].low_thr_temp) {
		step_down	= true;
	}

	if (step_up) {
		avail_steps = BATTERY_TEMP_STATUS_HOT - bat_temp_status;

		for (steps = 0; steps < avail_steps; steps ++) {
			if (new_bat_temp_status < BATTERY_TEMP_STATUS_HOT) {
				new_bat_temp_status ++;

				if (charging_temp_range_data_polling[new_bat_temp_status].high_thr_temp > 
					rbatt_temp) {
					break;
				}
			}
		}
	}
	else if (step_down) {
		avail_steps = bat_temp_status - BATTERY_TEMP_STATUS_FREEZE;

		for (steps = 0; steps < avail_steps; steps ++) {

			if (new_bat_temp_status > BATTERY_TEMP_STATUS_FREEZE) {
				new_bat_temp_status --;
			
				if (charging_temp_range_data_polling[new_bat_temp_status].low_thr_temp < 
					rbatt_temp) {
					break;
				}
			}
		}
	}

	the_chip->bat_temp_status = new_bat_temp_status;
	return the_chip->bat_temp_status;
}

//////////////////////////////////////////////////////////////////////////////
void ext_inject_charging_enable(char* func, int line, bool enable){
	DBG_MSG(MSG_ERR, "[%s %d] inject charging enable %d\n", func, line, enable);
	the_chip->chg_enabled = enable;
}

void ext_inject_safety_timer(unsigned int safety_timer){
	DBG_MSG(MSG_ERR, "inject safety_timer %d\n", safety_timer);
	the_chip->safety_timer = safety_timer;
}

void ext_inject_man_timer(unsigned int maintenance_timer){
	DBG_MSG(MSG_ERR, "inject safety_timer %d\n", maintenance_timer);
	the_chip->maintenance_timer = maintenance_timer;
}

enum battery_temp_status ext_get_bat_temp_stat(void) {
	return the_chip->bat_temp_status;
}

enum chg_sony_state ext_get_chg_stat(void) {
	return the_chip->chg_state;
}

void ext_charging_reset(void) {
	the_chip->resume_voltage_delta = VMAXSEL_NORMAL_DELTA;
	the_chip->safety_timer = 0;
	the_chip->maintenance_timer = 0;
	the_chip->chg_state = CSS_GENERAL;

	the_chip->bat_temp_status = BATTERY_TEMP_STATUS_NORMAL;
	the_chip->chg_enabled = true;
}

void ext_plug_in_cable(void) {
}

void ext_unplug_cable(void) {
	DBG_MSG(MSG_ERR, "enter\n");
	ext_charging_reset();
}

void ext_charging_done(void) {
	/* When charge done, call this function to change charging state to maintenance */
	if(the_chip->chg_state == CSS_GENERAL) {
		the_chip->chg_state = CSS_MAINTENANCE_60;
		the_chip->safety_timer = 0;
		the_chip->maintenance_timer = 0;
		the_chip->resume_voltage_delta = VMAXSEL_MAN_DELTA;
	}
}

bool ext_get_v_and_c(unsigned int *chg_current, unsigned int *chg_voltage) {
	bool ret = false;
	enum battery_temp_status 	bat_temp_status = the_chip->bat_temp_status;
	enum charger_type			chg_type = ext_oem_get_charger_type();
	unsigned int temp_current; 
	unsigned int temp_voltage = 0;
	int local_call_state;

	if (the_chip->chg_state == CSS_SAFETY_TIMEOUT || the_chip->chg_state == CSS_CHG_ERROR) {
		temp_current = temp_voltage = 0;
		ret =true;
		goto exit;
	}
	
	if (unlikely(bat_temp_status < BATTERY_TEMP_STATUS_FREEZE || bat_temp_status >  BATTERY_TEMP_STATUS_HOT)) {
		DBG_MSG(MSG_ERR, "invalid bat_temp_status %d\n", bat_temp_status);
		goto exit;
	}

	if (unlikely(chg_type < CHARGER_TYPE_USB || chg_type >  CHARGER_TYPE_AC)) {
		DBG_MSG(MSG_ERR, "invalid chg_type %d\n", chg_type);
		goto exit;
	}

	local_call_state = ext_oem_is_in_call();

	if (local_call_state == 1) {

		/* ======> priority 2: call mode */
		temp_voltage = VDD_MAX_CALL_TIME;
	}
	else {
		/* only when not in call, we take maintenance voltage into consideration */ 

		/* ======> prioriy 3: maintenance */
		if (the_chip->chg_state == CSS_MAINTENANCE_60) {
			temp_voltage = VDD_MAX_IN_60_HRS;
		}
		else if (the_chip->chg_state == CSS_MAINTENANCE_260) {
			temp_voltage = VDD_MAX_IN_260_HRS;
		}
	}

	/** ======> priority 1: temperature */
	temp_current = charging_current[bat_temp_status][chg_type];

	if ((temp_voltage == 0) || // if not assign a valid value
		(temp_voltage > charging_voltage[bat_temp_status])) {
		temp_voltage = charging_voltage[bat_temp_status];
	}

	ret = true;
exit:

	if (likely(ret == true)) {
		DBG_MSG(MSG_INFO, "current %d, voltage %d\n", temp_current, temp_voltage);

		if (chg_current)
			*chg_current = temp_current;

		if (chg_voltage)
			*chg_voltage = temp_voltage;
	}
	
	return ret;
}

/* You must call me only when charger is exist */
/* this function do the following stuff
	1. calculate safety timer (and call state) to determine if charging timeout
	2. calculate maintenance timer to dtermine we are at which maintenance state 
*/
void ext_charging_brain(int passed_s) {
	int local_call_state;
	enum charger_type chg_type;
	unsigned int	safety_timer_max;

	/* turn on/off the code to detect vbus voltage abnormal */
	#if (ENABLE_DETECT_VBUS_VOLT_ABNORMAL == 1)
		ext_pool_detect_charger_volt_valid();
	#endif

	/* if charger invalid, only unplug cable can recover the error */
	if (the_chip->chg_state == CSS_CHG_ERROR) {
		goto exit;
	}

	/* if timeout, only unplug cable or battery low (if enabled) can recover the error */
	do {
		if (the_chip->chg_state == CSS_SAFETY_TIMEOUT) {

		/* escape safety timeout when battery low*/
		#if (ENABLE_UNLOCK_SAFETY_TIMEOUT_WHEN_BAT_LOW == 1)
			int bat_vol = ext_oem_get_vbat_mv();
			if (bat_vol <= UNLOCK_SAFETY_TIMEOUT_WHEN_BAT_LOW_VOL) {
				DBG_MSG(MSG_ERR, "escape safety timeout, bat vol = %d\n", bat_vol);
				the_chip->chg_state = CSS_GENERAL;
				the_chip->safety_timer = 0;
				break;
			}
		#endif

			goto exit;
		}
	} while (0);

	local_call_state = ext_oem_is_in_call();
	chg_type = ext_oem_get_charger_type();	
	
	ext_pool_detect_battery_temp_status();

	if (chg_type == CHARGER_TYPE_AC)
		safety_timer_max = SAFTY_TIMER_AC;
	else
		safety_timer_max = SAFTY_TIMER_USB;

	/* step 1: checking safety timer status */
	//if (charging_status == POWER_SUPPLY_STATUS_CHARGING && local_call_state == 0) {
	if (local_call_state == 0) {
		if (the_chip->chg_state == CSS_GENERAL) {
			the_chip->safety_timer += passed_s;
			if (the_chip->safety_timer >= safety_timer_max) {
				the_chip->chg_state = CSS_SAFETY_TIMEOUT;
			}
		}
	}
	
 	/* step 2: checking maintenance charging status */
	if ((the_chip->chg_state == CSS_MAINTENANCE_60) ||  (the_chip->chg_state == CSS_MAINTENANCE_260)) {

		the_chip->maintenance_timer += passed_s;
		
		if (the_chip->chg_state == CSS_MAINTENANCE_60) {
			if (the_chip->maintenance_timer >= MAINTENACE60_T) {
				the_chip->chg_state = CSS_MAINTENANCE_260;
				the_chip->resume_voltage_delta = VMAXSEL_MAN_DELTA;
			}
		}
		else{
			/* If maintenance time is more than 260 hours, back to previous mainteance state. */
			if (the_chip->maintenance_timer >= MAINTENACE260_T) {
				the_chip->maintenance_timer = 0;
				the_chip->chg_state = CSS_MAINTENANCE_60;
				the_chip->resume_voltage_delta = VMAXSEL_MAN_DELTA;
			}
		}
	}

exit:

	return;
//	DBG_MSG(MSG_ERR, "chg_state %d, temp_satus %d, safety %d, man %d, delta %d\n", 
//		the_chip->chg_state, the_chip->bat_temp_status, 
//		the_chip->safety_timer, the_chip->maintenance_timer, the_chip->resume_voltage_delta);
}

