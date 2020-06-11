#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/xlog.h>

#include "kd_camera_hw.h"

#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_camera_feature.h"

#include <linux/fih_hw_info.h> /*MM-SL-ModifyForDP-00+ */

/******************************************************************************
 * Debug configuration
******************************************************************************/
#define PFX "[kd_camera_hw]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, arg...)    xlog_printk(ANDROID_LOG_INFO, PFX , fmt, ##arg)

#define DEBUG_CAMERA_HW_K
#ifdef DEBUG_CAMERA_HW_K
#define PK_DBG PK_DBG_FUNC
#define PK_ERR(fmt, arg...)         xlog_printk(ANDROID_LOG_ERR, PFX , fmt, ##arg)
#define PK_XLOG_INFO(fmt, args...) \
                do {    \
                   xlog_printk(ANDROID_LOG_INFO, PFX , fmt, ##arg); \
                } while(0)
#else
#define PK_DBG(a,...)
#define PK_ERR(a,...)
#define PK_XLOG_INFO(fmt, args...)
#endif

extern void ISP_MCLK1_EN(BOOL En);
extern void ISP_MCLK2_EN(BOOL En);
extern void ISP_MCLK3_EN(BOOL En);

/*MM-SL-ModifyForDP-00*{ */
/*MM-SL-BringUpIMX230-00*{ */
/*MM-SL-BringUpIMX214-00*{ */
int kdCISModulePowerOn(CAMERA_DUAL_CAMERA_SENSOR_ENUM SensorIdx, char *currSensorName, BOOL On, char* mode_name)
{

	u32 pinSetIdx = 0;//default main sensor
	unsigned int product_phase = fih_get_product_phase();


    if (DUAL_CAMERA_MAIN_SENSOR == SensorIdx){
        pinSetIdx = 0;
    }
    else if (DUAL_CAMERA_SUB_SENSOR == SensorIdx) {
        pinSetIdx = 1;
    }
    else if (DUAL_CAMERA_MAIN_2_SENSOR == SensorIdx) {
        pinSetIdx = 2;
    }

	if (On) { //power ON
		PK_DBG("kdCISModulePowerOn -on:currSensorName=%s\n",currSensorName);
		PK_DBG("kdCISModulePowerOn -on:pinSetIdx=%d, product_phase = %d\n",pinSetIdx, product_phase);

 		if (currSensorName && (0 == strcmp(SENSOR_DRVNAME_IMX230_MIPI_RAW,currSensorName))){
				
			//Configure XCLR
			if(product_phase == PHASE_EVM){
				//GPIO41
				PK_DBG("[CAMERA SENSOR - IMX230] init GPIO41 \n");
				if(mt_set_gpio_mode(GPIO_CAMERA_CMRST_PIN,GPIO_MODE_00)){PK_DBG("[CAMERA XCLR] set gpio mode failed!! \n");}
				if(mt_set_gpio_dir(GPIO_CAMERA_CMRST_PIN, GPIO_DIR_OUT)){PK_DBG("[CAMERA XCLR] set gpio dir failed!! \n");}
				if(mt_set_gpio_out(GPIO_CAMERA_CMRST_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR] set gpio out down failed!! \n");}
			}else{
				//GPIO10
				PK_DBG("[CAMERA SENSOR - IMX230] init GPIO10 \n");
				if(mt_set_gpio_mode(GPIO_CAMERA_CMPDN_PIN,GPIO_MODE_00)){PK_DBG("[CAMERA XCLR] set gpio mode failed!! \n");}
				if(mt_set_gpio_dir(GPIO_CAMERA_CMPDN_PIN, GPIO_DIR_OUT)){PK_DBG("[CAMERA XCLR] set gpio dir failed!! \n");}
				if(mt_set_gpio_out(GPIO_CAMERA_CMPDN_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR] set gpio out down failed!! \n");}
			}
			mdelay(1);

			//Configure CMMCLK, GPIO42
			if(mt_set_gpio_mode(GPIO_CAMERA_CMMCLK_EN_PIN,GPIO_MODE_01)){PK_DBG("[CAMERA CMMCLK] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(GPIO_CAMERA_CMMCLK_EN_PIN,GPIO_DIR_OUT)){PK_DBG("[CAMERA CMMCLK] set gpio dir failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_CMMCLK_EN_PIN,GPIO_OUT_ZERO)){PK_DBG("[CAMERA CMMCLK] set gpio failed!! \n");}
			mdelay(1);
   

			//VCAM_IO 1.8V
			PK_DBG("[CAMERA SENSOR - IMX230] enable VCAM_IO 1.8V\n");
			if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_D2, VOL_1800,mode_name)){
				PK_DBG("[CAMERA SENSOR - IMX230] Fail to enable digital VCAM_IO power\n");
            	goto _kdCISModulePowerOn_exit_;
        	}
			mdelay(2);			

			//VCAM_A 
			if(product_phase == PHASE_EVM){
				PK_DBG("[CAMERA SENSOR - IMX230] enable VCAM_A 2.8V\n");
				if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_A, VOL_2800,mode_name)){
					PK_DBG("[CAMERA SENSOR - IMX230] Fail to enable analog VCAM_A power\n");
	            	goto _kdCISModulePowerOn_exit_;
	        	}
			}else{				
				PK_DBG("[CAMERA SENSOR - IMX230] enable VCAM_A 2.5V\n");
				if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_A, VOL_2500,mode_name)){
					PK_DBG("[CAMERA SENSOR - IMX230] Fail to enable analog VCAM_A power\n");
	            	goto _kdCISModulePowerOn_exit_;
	        	}
			}
			mdelay(2);
 
			//VCAM_D 1.1V
			PK_DBG("[CAMERA SENSOR - IMX230] enable VCAM_D 1.1V\n");
			if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_D, VOL_1100,mode_name)){
				PK_DBG("[CAMERA SENSOR] Fail to enable digital VCAM_D power\n");
                 goto _kdCISModulePowerOn_exit_;
            }
			mdelay(2);

			/*MM-SL-EnableLC898212AF-00+{ */		
			//VCAM_A2  2.8V			
			PK_DBG("[CAMERA SENSOR - IMX230] enable AF power - VCAM_A2 2.8V\n");			
			if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_A2, VOL_2800,mode_name)){				
				PK_DBG("[CAMERA SENSOR - IMX230] Fail to enable analog VCAM_A2 power\n");            
				goto _kdCISModulePowerOn_exit_;		
			}
			mdelay(3);			
			/*MM-SL-EnableLC898212AF-00+} */

			//enable CMMCLK
			PK_DBG("[CAMERA SENSOR - IMX230] enable CMMCLK\n");
			ISP_MCLK1_EN(TRUE);
			mdelay(2);

   
			//XCLR
			if(product_phase == PHASE_EVM){
				//GPIO41
				PK_DBG("[CAMERA SENSOR - IMX230] enable XCLR-GPIO41\n");
				if(mt_set_gpio_out(GPIO_CAMERA_CMRST_PIN, GPIO_OUT_ONE)){PK_DBG("[CAMERA XCLR] set gpio out failed!! \n");}
			}else{
				//GPIO10
				PK_DBG("[CAMERA SENSOR - IMX230] enable XCLR-GPIO10\n");
				if(mt_set_gpio_out(GPIO_CAMERA_CMPDN_PIN, GPIO_OUT_ONE)){PK_DBG("[CAMERA XCLR] set gpio out failed!! \n");}
			}
			mdelay(3);		
				
        }
		else if (currSensorName && (0 == strcmp(SENSOR_DRVNAME_IMX214_MIPI_RAW,currSensorName))){			

			/*MM-SL-IMX230LoadIncorrect-00+{ */
			if (pinSetIdx == 0){
				PK_DBG("[CAMERA SENSOR - IMX214] pinSetIdx = 0, bypass extra power on\n");
				return 0;
			}
			/*MM-SL-IMX230LoadIncorrect-00+} */
			//Configure XCLR_F, GPIO14
			if(mt_set_gpio_mode(GPIO_CAMERA_CMRST1_PIN,GPIO_MODE_00)){PK_DBG("[CAMERA XCLR_F] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(GPIO_CAMERA_CMRST1_PIN, GPIO_DIR_OUT)){PK_DBG("[CAMERA XCLR_F] set gpio dir failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_CMRST1_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR_F] set gpio out down failed!! \n");}
			mdelay(1);

			//Configure CM2MCLK, GPIO39
			if(mt_set_gpio_mode(GPIO_CAMERA_CM2MCLK_EN_PIN,GPIO_MODE_01)){PK_DBG("[CAMERA CM2MCLK] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(GPIO_CAMERA_CM2MCLK_EN_PIN,GPIO_DIR_OUT)){PK_DBG("[CAMERA CM2MCLK] set gpio dir failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_CM2MCLK_EN_PIN,GPIO_OUT_ZERO)){PK_DBG("[CAMERA CM2MCLK] set gpio failed!! \n");}
			mdelay(1);

			//VCAM_IO 1.8V
			PK_DBG("[CAMERA SENSOR - IMX214] enable VCAM_IO 1.8V\n");
			if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_D2, VOL_1800,mode_name)){
				PK_DBG("[CAMERA SENSOR - IMX214] Fail to enable digital VCAM_IO power\n");
                 goto _kdCISModulePowerOn_exit_;
            }
			mdelay(2);			

			//VCAM_A 2.8V
			if(product_phase == PHASE_EVM){
				PK_DBG("[CAMERA SENSOR - IMX214] enable VCAM_A 2.8V\n");
				if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_A, VOL_2800,mode_name)){
					PK_DBG("[CAMERA SENSOR- IMX214] Fail to enable analog VCAM_A power\n");
	                goto _kdCISModulePowerOn_exit_;
	            }
			}else{
				PK_DBG("[CAMERA SENSOR - IMX214] enable VGP1 2.8V\n");
				if(TRUE != hwPowerOn(CAMERA_POWER_VGP1, VOL_2800,mode_name)){
					PK_DBG("[CAMERA SENSOR- IMX214] Fail to enable analog VGP1 power\n");
	                goto _kdCISModulePowerOn_exit_;
				}
	        }			
			mdelay(2);

			//VCAM_D 1.1V
			if(product_phase == PHASE_EVM){
				PK_DBG("[CAMERA SENSOR - IMX214] enable VCAM_D 1.1V\n");
				if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_D, VOL_1100,mode_name)){
					PK_DBG("[CAMERA SENSOR - IMX214] Fail to enable digital VCAM_D power\n");
	                 goto _kdCISModulePowerOn_exit_;
	            }
			}else{
				PK_DBG("[CAMERA SENSOR - IMX214] enable VSRAM_DVFS1 1.1V\n");
				if(TRUE != hwPowerOn(CAMERA_POWER_VSRAM, VOL_DEFAULT,mode_name)){
					PK_DBG("[CAMERA SENSOR - IMX214] Fail to enable digital VSRAM_DVFS1 power\n");
	                 goto _kdCISModulePowerOn_exit_;
	            }
			}
			mdelay(2);

			/*MM-SL-EnableDW9714AF-00+{ */		
			//VCAM_A2  2.8V			
			PK_DBG("[CAMERA SENSOR - IMX214] enable AF power - VCAM_A2 2.8V\n");			
			if(TRUE != hwPowerOn(CAMERA_POWER_VCAM_A2, VOL_2800,mode_name)){				
				PK_DBG("[CAMERA SENSOR - IMX214] Fail to enable analog VCAM_A2 power\n");            
				goto _kdCISModulePowerOn_exit_;		
			}
			mdelay(3);
			/*MM-SL-EnableDW9714AF-00+} */

			//enable CM2MCLK
			PK_DBG("[CAMERA SENSOR - IMX214] enable CM2MCLK\n");
			ISP_MCLK2_EN(TRUE);
			mdelay(2);

   
			//XCLR_F, GPIO14
			PK_DBG("[CAMERA SENSOR- IMX214] enable XCLR_F-GPIO14\n");
			if(mt_set_gpio_out(GPIO_CAMERA_CMRST1_PIN, GPIO_OUT_ONE)){PK_DBG("[CAMERA XCLR_F] set gpio out failed!! \n");}	
			mdelay(3);		
        
        }

    }
	else{ //power OFF
		PK_DBG("kdCISModulePowerOn -Off:currSensorName=%s\n",currSensorName);
		PK_DBG("kdCISModulePowerOn -Off:pinSetIdx=%d\n",pinSetIdx);

		if (currSensorName && (0 == strcmp(SENSOR_DRVNAME_IMX230_MIPI_RAW,currSensorName))){
			//Configure XCLR
			if(product_phase == PHASE_EVM){
				//GPIO41
				PK_DBG("[CAMERA SENSOR - IMX230] init GPIO41 \n");
				if(mt_set_gpio_mode(GPIO_CAMERA_CMRST_PIN,GPIO_MODE_00)){PK_DBG("[CAMERA XCLR] set gpio mode failed!! \n");}
				if(mt_set_gpio_dir(GPIO_CAMERA_CMRST_PIN, GPIO_DIR_OUT)){PK_DBG("[CAMERA XCLR] set gpio dir failed!! \n");}
				if(mt_set_gpio_out(GPIO_CAMERA_CMRST_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR] set gpio out down failed!! \n");}
			}else{
				//GPIO10
				PK_DBG("[CAMERA SENSOR - IMX230] init GPIO10 \n");
				if(mt_set_gpio_mode(GPIO_CAMERA_CMPDN_PIN,GPIO_MODE_00)){PK_DBG("[CAMERA XCLR] set gpio mode failed!! \n");}
				if(mt_set_gpio_dir(GPIO_CAMERA_CMPDN_PIN, GPIO_DIR_OUT)){PK_DBG("[CAMERA XCLR] set gpio dir failed!! \n");}
				if(mt_set_gpio_out(GPIO_CAMERA_CMPDN_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR] set gpio out down failed!! \n");}
			}
			mdelay(1);

			//Configure CMMCLK, GPIO42
			if(mt_set_gpio_mode(GPIO_CAMERA_CMMCLK_EN_PIN,GPIO_MODE_01)){PK_DBG("[CAMERA CMMCLK] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(GPIO_CAMERA_CMMCLK_EN_PIN,GPIO_DIR_OUT)){PK_DBG("[CAMERA CMMCLK] set gpio dir failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_CMMCLK_EN_PIN,GPIO_OUT_ZERO)){PK_DBG("[CAMERA CMMCLK] set gpio failed!! \n");}
            mdelay(1);

			//XCLR_F,
			if(product_phase == PHASE_EVM){
				//GPIO41
				if(mt_set_gpio_out(GPIO_CAMERA_CMRST_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR] set gpio out failed!! \n");}
			}else{
				//GPIO10
				if(mt_set_gpio_out(GPIO_CAMERA_CMPDN_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR] set gpio out failed!! \n");}
			}
			mdelay(3);

			//disable CMMCLK
			ISP_MCLK1_EN(FALSE);
			mdelay(2);

			/*MM-SL-EnableLC898212AF-00+{ */		
			//VCAM_A2 2.8V			
			if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_A2, mode_name)){				
				PK_DBG("[CAMERA SENSOR - IMX230] Fail to disable AF power - VCAM_A2 power\n");				
				goto _kdCISModulePowerOn_exit_;            
			}			
			mdelay(2);			
			/*MM-SL-EnableLC898212AF-00+} */

			//VCAM_D 1.1V
			if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_D, mode_name)){
				PK_DBG("[CAMERA SENSOR - IMX230] Fail to disable digital VCAM_D power\n");
				goto _kdCISModulePowerOn_exit_;
            }
			mdelay(2);

			//VCAM_A 2.8V
			if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_A, mode_name)){
				PK_DBG("[CAMERA SENSOR - IMX230] Fail to disable analog VCAM_A power\n");
				goto _kdCISModulePowerOn_exit_;
            }
			mdelay(2);

			//VCAM_IO 1.8V
			if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_D2,mode_name)){
				PK_DBG("[CAMERA SENSOR - IMX230] Fail to disable digital VCAM_IO power\n");
				goto _kdCISModulePowerOn_exit_;
        }
			mdelay(2);
			
    }
		else if (currSensorName && (0 == strcmp(SENSOR_DRVNAME_IMX214_MIPI_RAW,currSensorName))){
			/*MM-SL-IMX230LoadIncorrect-00+{ */
			if (pinSetIdx == 0){
				PK_DBG("[CAMERA SENSOR - IMX214] bypass extra power off for pinSetIdx = 0\n");
				return 0;
			}
			/*MM-SL-IMX230LoadIncorrect-00+} */
			//Configure XCLR_F, GPIO14
			if(mt_set_gpio_mode(GPIO_CAMERA_CMRST1_PIN,GPIO_MODE_00)){PK_DBG("[CAMERA XCLR_F] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(GPIO_CAMERA_CMRST1_PIN, GPIO_DIR_OUT)){PK_DBG("[CAMERA XCLR_F] set gpio dir failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_CMRST1_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR_F] set gpio out down failed!! \n");}
			mdelay(1);

			//Configure CM2MCLK, GPIO39
			if(mt_set_gpio_mode(GPIO_CAMERA_CM2MCLK_EN_PIN,GPIO_MODE_01)){PK_DBG("[CAMERA CM2MCLK] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(GPIO_CAMERA_CM2MCLK_EN_PIN,GPIO_DIR_OUT)){PK_DBG("[CAMERA CM2MCLK] set gpio dir failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_CM2MCLK_EN_PIN,GPIO_OUT_ZERO)){PK_DBG("[CAMERA CM2MCLK] set gpio failed!! \n");}
			mdelay(1);

			//XCLR_F, GPIO14
			if(mt_set_gpio_out(GPIO_CAMERA_CMRST1_PIN, GPIO_OUT_ZERO)){PK_DBG("[CAMERA XCLR_F] set gpio out failed!! \n");}	
			mdelay(3);

			//disable CM2MCLK
			ISP_MCLK2_EN(FALSE);
			mdelay(2);

			/*MM-SL-EnableDW9714AF-00+{ */		
			//VCAM_A2 2.8V			
			if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_A2, mode_name)){				
				PK_DBG("[CAMERA SENSOR - IMX214] Fail to disable AF power - VCAM_A2 power\n");				
				goto _kdCISModulePowerOn_exit_;            
			}			
			mdelay(2);			
			/*MM-SL-EnableDW9714AF-00+} */

			//VCAM_D 1.1V
			if(product_phase == PHASE_EVM){
				if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_D, mode_name)){
					PK_DBG("[CAMERA SENSOR - IMX214] Fail to disable digital VCAM_D power\n");
					goto _kdCISModulePowerOn_exit_;
	        	}
			}else{			
				if(TRUE != hwPowerDown(CAMERA_POWER_VSRAM, mode_name)){
					PK_DBG("[CAMERA SENSOR - IMX214] Fail to disable digital DVFS1 power\n");
					goto _kdCISModulePowerOn_exit_;
	        	}
			}
			mdelay(2);

			//VCAM_A 2.8V
			if(product_phase == PHASE_EVM){
	        	if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_A,mode_name)) {
					PK_DBG("[CAMERA SENSOR - IMX214] Fail to disable analog VCAM_A power\n");
	            	goto _kdCISModulePowerOn_exit_;
	        	}
			}else{
				if(TRUE != hwPowerDown(CAMERA_POWER_VGP1,mode_name)) {
					PK_DBG("[CAMERA SENSOR - IMX214] Fail to disable analog VGP1 power\n");
	            	goto _kdCISModulePowerOn_exit_;
	        	}
			}
			mdelay(2);

			//VCAM_IO 1.8V
        	if(TRUE != hwPowerDown(CAMERA_POWER_VCAM_D2, mode_name)) {
				PK_DBG("[CAMERA SENSOR - IMX214] Fail to disable digital VCAM_IO power\n");
            	goto _kdCISModulePowerOn_exit_;
        	}
			mdelay(2);
			
        }
        
	
    }

    return 0;

_kdCISModulePowerOn_exit_:
    return -EIO;
    
}
/*MM-SL-BringUpIMX214-00*} */
/*MM-SL-BringUpIMX230-00*} */
/*MM-SL-ModifyForDP-00*} */

EXPORT_SYMBOL(kdCISModulePowerOn);

//!--
//


