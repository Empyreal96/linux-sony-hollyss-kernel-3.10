/*****************************************************************************
	Copyright(c) 2013 FCI Inc. All Rights Reserved

	File name : fc8300.c

	Description : Driver source file

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

	History :
	----------------------------------------------------------------------
*******************************************************************************/
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>


#include <mach/eint.h>
#include <mach/mt_pm_ldo.h>
#include <cust_gpio_usage.h>
#include <cust_eint.h>
#include <cust_eint_md1.h>
#ifdef BUILD_LK
	#include <platform/mt_gpio.h>
	#include <string.h>
#elif defined(BUILD_UBOOT)
	#include <asm/arch/mt_gpio.h>
#else
	#include <mach/mt_gpio.h>
#endif

#include "fc8300.h"
#include "bbm.h"
#include "fci_oal.h"
#include "fci_tun.h"
#include "fc8300_regs.h"
#include "fc8300_isr.h"
#include "fci_hal.h"
#include <linux/fih_hw_info.h> 


struct ISDBT_INIT_INFO_T *hInit;

#define RING_BUFFER_SIZE	(188 * 320 * 50)

/* GPIO(RESET & INTRRUPT) Setting */
#define FC8300_NAME		"isdbt"

//PROD-DH-DtvPorting-00+[

#define GPIO_ISDBT_IRQ (GPIO13 | 0x80000000)
#define GPIO_ISDBT_PWR_EN (GPIO15 | 0x80000000)
#define GPIO_EN_DTV_V1P8 (GPIO140 | 0x80000000)
#define GPIO_ISDBT_RST (GPIO152 | 0x80000000)

#define DTV_SPI_SCK (GPIO166 | 0x80000000)
#define DTV_SPI_MISO (GPIO167 | 0x80000000)
#define DTV_SPI_MOSI (GPIO168 | 0x80000000)
#define DTV_SPI_CS (GPIO169 | 0x80000000)
//PROD-DH-DtvPorting-00+]
#define GPIO_FM_DTV_SWITCH (GPIO96 | 0x80000000)


//u8 static_ringbuffer[RING_BUFFER_SIZE];

enum ISDBT_MODE driver_mode = ISDBT_POWEROFF;
static DEFINE_MUTEX(ringbuffer_lock);

static DECLARE_WAIT_QUEUE_HEAD(isdbt_isr_wait);

#ifndef BBM_I2C_TSIF
static u8 isdbt_isr_sig;
static struct task_struct *isdbt_kthread;

void isdbt_irq(void)
{
	isdbt_isr_sig = 1;
	wake_up_interruptible(&isdbt_isr_wait);
    /*mt_eint_unmask(CUST_EINT_ALS_NUM);*/
	return;
	//return IRQ_HANDLED;
}
#endif

int isdbt_hw_setting(void)
{
	print_log(0, "[FC8300] isdbt_hw_setting \n");

	mt_set_gpio_mode(GPIO_ISDBT_RST,  GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_ISDBT_RST, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_ISDBT_RST, GPIO_OUT_ZERO);

	//PROD-DH-DtvPorting-00+[ might be moved to isdbt_hw_init
	mt_set_gpio_mode(GPIO_EDP_EINT_PIN,  GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_EDP_EINT_PIN, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(GPIO_EDP_EINT_PIN, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(GPIO_EDP_EINT_PIN, GPIO_PULL_UP);
	//PROD-DH-DtvPorting-00+]

	mt_eint_registration(CUST_EINT_EDP_INTN_NUM
		, CUST_EINT_EDP_INTN_TYPE, isdbt_irq, 1);
	mt_eint_mask(CUST_EINT_EDP_INTN_NUM);

	return 0;
}

/*POWER_ON & HW_RESET & INTERRUPT_CLEAR */
void isdbt_hw_init(void)
{
	int i = 0;
	print_log(0, "[FC8300] BAND_ID = %d \n", fih_get_band_id());
	while (driver_mode == ISDBT_DATAREAD) {
		msWait(100);
		if (i++ > 5)
			break;
	}

	print_log(0, "[FC8300] isdbt_hw_init \n");

	/*hwPowerOn(MT6323_POWER_LDO_VIO28, VOL_2800, "2V8_DTV");*/
	/*hwPowerOn(MT6323_POWER_LDO_VIO18, VOL_1800, "1V8_DTV");*/
	mt_set_gpio_mode(GPIO_ISDBT_RST,  GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_ISDBT_RST, GPIO_DIR_OUT);
    msWait(2);
	

	//PROD-DH-DtvPorting-00+[
	//SPI
	mt_set_gpio_mode(DTV_SPI_SCK, GPIO_MODE_01);
	mt_set_gpio_mode(DTV_SPI_MISO, GPIO_MODE_01);
	mt_set_gpio_mode(DTV_SPI_MOSI, GPIO_MODE_01);
	mt_set_gpio_mode(DTV_SPI_CS, GPIO_MODE_01);
	
	mt_set_gpio_mode(GPIO_EN_DTV_V1P8, GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_EN_DTV_V1P8, GPIO_DIR_OUT);

	mt_set_gpio_mode(GPIO_ISDBT_PWR_EN, GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_ISDBT_PWR_EN, GPIO_DIR_OUT);

	mt_set_gpio_out(GPIO_EN_DTV_V1P8, GPIO_OUT_ONE);
	mt_set_gpio_out(GPIO_ISDBT_PWR_EN, GPIO_OUT_ONE);
	msWait(3);	
	//PROD-DH-DtvPorting-00+]

	
    mt_set_gpio_out(GPIO_ISDBT_RST, GPIO_OUT_ONE);
    /*mt_set_gpio_out(GPIO_ISDBT_PWR_EN, GPIO_OUT_ONE);*/
    msWait(10);
    mt_set_gpio_out(GPIO_ISDBT_RST, GPIO_OUT_ZERO);
    msWait(15);
    mt_set_gpio_out(GPIO_ISDBT_RST, GPIO_OUT_ONE);
    msWait(15);

	//only control VTCXO2 before SAMBA AP3 (including AP3)
	print_log(0, "fih_get_band_id() = 0x%x,  fih_get_product_phase() = 0x%x\n", fih_get_band_id(), fih_get_product_phase());
	if(fih_get_band_id() == BAND_SAMBA && fih_get_product_phase() <= PHASE_TP1_TP2)
	{
		print_log(0, "[FC8300] Power On FM LDO EM \n");
		//Turn on FM_DTV_SWITCH power
		hwPowerOn(MT6331_POWER_LDO_VTCXO2, VOL_2800, "FM LDO EM");
		msleep(10);
	}
	//Config GPIO_FM_DTV_SWITCH to 0(DTV)
	mt_set_gpio_mode(GPIO_FM_DTV_SWITCH,  GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_FM_DTV_SWITCH, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_FM_DTV_SWITCH, GPIO_OUT_ZERO);


	driver_mode = ISDBT_POWERON;

}

/*POWER_OFF */
void isdbt_hw_deinit(void)
{
	print_log(0, "[FC8300] isdbt_hw_deinit \n");
	driver_mode = ISDBT_POWEROFF;

    /*mt_set_gpio_out(GPIO_ISDBT_PWR_EN, GPIO_OUT_ZERO);*/
    mt_set_gpio_out(GPIO_ISDBT_RST, GPIO_OUT_ZERO);
	mt_eint_mask(CUST_EINT_EDP_INTN_NUM);
	/*hwPowerDown(MT6323_POWER_LDO_VIO18, "1V8_DTV");*/
	/*hwPowerDown(MT6323_POWER_LDO_VIO28, "2V8_DTV");*/

	//PROD-DH-DtvPorting-01+[
	//SPI
	mt_set_gpio_mode(DTV_SPI_SCK, GPIO_MODE_00);
	mt_set_gpio_mode(DTV_SPI_MISO, GPIO_MODE_00);
	mt_set_gpio_mode(DTV_SPI_MOSI, GPIO_MODE_00);
	mt_set_gpio_mode(DTV_SPI_CS, GPIO_MODE_00);

	mt_set_gpio_dir(DTV_SPI_SCK, GPIO_DIR_IN);
	mt_set_gpio_dir(DTV_SPI_MISO, GPIO_DIR_IN);
	mt_set_gpio_dir(DTV_SPI_MOSI, GPIO_DIR_IN);
	mt_set_gpio_dir(DTV_SPI_CS, GPIO_DIR_IN);
	
	mt_set_gpio_mode(GPIO_EN_DTV_V1P8, GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_EN_DTV_V1P8, GPIO_DIR_IN);

	mt_set_gpio_mode(GPIO_ISDBT_PWR_EN, GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_ISDBT_PWR_EN, GPIO_DIR_IN);

	mt_set_gpio_mode(GPIO_ISDBT_RST, GPIO_MODE_GPIO);
	mt_set_gpio_dir(GPIO_ISDBT_RST, GPIO_DIR_IN);

	
	//Config GPIO_FM_DTV_SWITCH to 1(FM)
	//Keep GPIO FM_DTV_SWITCH low to prevent cuurent leakage.
	//mt_set_gpio_mode(GPIO_FM_DTV_SWITCH,  GPIO_MODE_GPIO);
	//mt_set_gpio_dir(GPIO_FM_DTV_SWITCH, GPIO_DIR_OUT);
	//mt_set_gpio_out(GPIO_FM_DTV_SWITCH, GPIO_OUT_ONE);
	//Turn off FM_DTV_SWITCH power
	//only control VTCXO2 before SAMBA AP3 (including AP3)
	print_log(0, "fih_get_band_id() = 0x%x,  fih_get_product_phase() = 0x%x\n", fih_get_band_id(), fih_get_product_phase());
	if(fih_get_band_id() == BAND_SAMBA && fih_get_product_phase() <= PHASE_TP1_TP2)
	{
		print_log(0, "[FC8300] Power Down FM LDO EM \n");
		hwPowerDown(MT6331_POWER_LDO_VTCXO2, "FM LDO EM");
	}
	//PROD-DH-DtvPorting-01+]
}

//PT-DH-FTM_DTV_Command-00*
//int data_callback(ulong hDevice, u8 bufid, u8 *data, int len)
ulong data_callback(ulong hDevice, u8 bufid, u8 *data, int len)
{
	struct ISDBT_INIT_INFO_T *hInit;
	struct list_head *temp;
	hInit = (struct ISDBT_INIT_INFO_T *)hDevice;	

	list_for_each(temp, &(hInit->hHead))
	{
		struct ISDBT_OPEN_INFO_T *hOpen;

		hOpen = list_entry(temp, struct ISDBT_OPEN_INFO_T, hList);

		if (hOpen->isdbttype == TS_TYPE) {
			mutex_lock(&ringbuffer_lock);
			if (fci_ringbuffer_free(&hOpen->RingBuffer) < len) {
				/*print_log(hDevice, "f"); */
				/* return 0 */;
				FCI_RINGBUFFER_SKIP(&hOpen->RingBuffer, len);
			}

			fci_ringbuffer_write(&hOpen->RingBuffer, data, len);

			wake_up_interruptible(&(hOpen->RingBuffer.queue));

			mutex_unlock(&ringbuffer_lock);
		}
	}

	return 0;
}


#ifndef BBM_I2C_TSIF
static int isdbt_thread(void *hDevice)
{
	struct ISDBT_INIT_INFO_T *hInit = (struct ISDBT_INIT_INFO_T *)hDevice;

	set_user_nice(current, -20);

	print_log(hInit, "[FC8300] isdbt_kthread enter\n");

	bbm_com_ts_callback_register((ulong)hInit, data_callback);

	while (1) {
		wait_event_interruptible(isdbt_isr_wait,
			isdbt_isr_sig || kthread_should_stop());		

		if (driver_mode == ISDBT_POWERON) {
			driver_mode = ISDBT_DATAREAD;
			bbm_com_isr(hInit);
			if (driver_mode == ISDBT_DATAREAD)
				driver_mode = ISDBT_POWERON;
		}
		

		isdbt_isr_sig = 0;

		if (kthread_should_stop())
			break;
	}

	bbm_com_ts_callback_deregister();

	print_log(hInit, "[FC8300] isdbt_kthread exit\n");

	return 0;
}
#endif

const struct file_operations isdbt_fops = {
	/*.owner		= THIS_MODULE,*/
	.unlocked_ioctl		= isdbt_ioctl,
        .compat_ioctl		= isdbt_ioctl,
	.open		= isdbt_open,
	.read		= isdbt_read,
	.release	= isdbt_release,
};

static struct miscdevice fc8300_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = FC8300_NAME,
    .fops = &isdbt_fops,
};

int isdbt_open(struct inode *inode, struct file *filp)
{
	struct ISDBT_OPEN_INFO_T *hOpen;

	print_log(hInit, "[FC8300] isdbt open\n");

	hOpen = kmalloc(sizeof(struct ISDBT_OPEN_INFO_T), GFP_KERNEL);

	//hOpen->buf = &static_ringbuffer[0]; //Static
	print_log(hInit, "[FC8300] isdbt open, vmalloc\n");
	hOpen->buf = vmalloc(RING_BUFFER_SIZE);//SW-PRODUCTION-DH-VMALLOC-00+
	/*kmalloc(RING_BUFFER_SIZE, GFP_KERNEL);*/
	hOpen->isdbttype = 0;

	list_add(&(hOpen->hList), &(hInit->hHead));

	hOpen->hInit = (HANDLE *)hInit;

	if (hOpen->buf == NULL) {
		print_log(hInit, "[FC8300] ring buffer malloc error\n");
		return -ENOMEM;
	}

	fci_ringbuffer_init(&hOpen->RingBuffer, hOpen->buf, RING_BUFFER_SIZE);

	filp->private_data = hOpen;

	return 0;
}

ssize_t isdbt_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	s32 avail;
	s32 non_blocking = filp->f_flags & O_NONBLOCK;
	struct ISDBT_OPEN_INFO_T *hOpen
		= (struct ISDBT_OPEN_INFO_T *)filp->private_data;
	struct fci_ringbuffer *cibuf = &hOpen->RingBuffer;
	ssize_t len, read_len = 0;

	if (!cibuf->data || !count)	{
		/*print_log(hInit, " return 0\n"); */
		return 0;
	}

	if (non_blocking && (fci_ringbuffer_empty(cibuf)))	{
		/*print_log(hInit, "return EWOULDBLOCK\n"); */
		return -EWOULDBLOCK;
	}

	if (wait_event_interruptible(cibuf->queue,
		!fci_ringbuffer_empty(cibuf))) {
		print_log(hInit, "return ERESTARTSYS\n");
		return -ERESTARTSYS;
	}

	mutex_lock(&ringbuffer_lock);

	avail = fci_ringbuffer_avail(cibuf);

	if (count >= avail)
		len = avail;
	else
		len = count - (count % 188);

	read_len = fci_ringbuffer_read_user(cibuf, buf, len);

	mutex_unlock(&ringbuffer_lock);

	return read_len;
}

int isdbt_release(struct inode *inode, struct file *filp)
{
	struct ISDBT_OPEN_INFO_T *hOpen;

	print_log(hInit, "[FC8300] isdbt_release\n");

	hOpen = filp->private_data;

	hOpen->isdbttype = 0;

	list_del(&(hOpen->hList));
	/*kfree(hOpen->buf);*/
	vfree(hOpen->buf);//SW-PRODUCTION-DH-VMALLOC-00+

	if (hOpen != NULL)
		kfree(hOpen);

	return 0;
}


#ifndef BBM_I2C_TSIF
void isdbt_isr_check(HANDLE hDevice)
{
	u8 isr_time = 0;

	bbm_com_write(hDevice, DIV_BROADCAST, BBM_BUF_ENABLE, 0x00);

	while (isr_time < 10) {
		if (!isdbt_isr_sig)
			break;

		msWait(10);
		isr_time++;
	}

}
#endif

long isdbt_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	s32 res = BBM_NOK;
	s32 err = 0;
	s32 size = 0;
	struct ISDBT_OPEN_INFO_T *hOpen;

	struct ioctl_info info;

	if (_IOC_TYPE(cmd) != IOCTL_MAGIC)
		return -EINVAL;
	if (_IOC_NR(cmd) >= IOCTL_MAXNR)
		return -EINVAL;

	hOpen = filp->private_data;

	size = _IOC_SIZE(cmd);

	switch (cmd) {
	case IOCTL_ISDBT_RESET:
		res = bbm_com_reset(hInit, DIV_BROADCAST);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_RESET \n");
		break;
	case IOCTL_ISDBT_INIT:
		res = bbm_com_i2c_init(hInit, FCI_HPI_TYPE);
		res |= bbm_com_probe(hInit, DIV_BROADCAST);
		if (res) {
			print_log(hInit, "[FC8300] FC8300 Initialize Fail \n");
			break;
		}
		res |= bbm_com_init(hInit, DIV_BROADCAST);
		break;
	case IOCTL_ISDBT_BYTE_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_byte_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8 *)(&info.buff[1]));
		err |= copy_to_user((void *)arg, (void *)&info, size);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_BYTE_READ\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_WORD_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_word_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u16 *)(&info.buff[1]));
		err |= copy_to_user((void *)arg, (void *)&info, size);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_WORD_READ\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_LONG_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_long_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u32 *)(&info.buff[1]));
		err |= copy_to_user((void *)arg, (void *)&info, size);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_LONG_READ\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_BULK_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_bulk_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8 *)(&info.buff[2]), info.buff[1]);
		err |= copy_to_user((void *)arg, (void *)&info, size);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_BULK_READ\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_BYTE_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_byte_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8)info.buff[1]);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_BYTE_WRITE\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_WORD_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_word_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u16)info.buff[1]);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_WORD_WRITE\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_LONG_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_long_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u32)info.buff[1]);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_LONG_WRITE\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_BULK_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_bulk_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8 *)(&info.buff[2]), info.buff[1]);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_BULK_WRITE\
			[0x%x][0x%x] \n", info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_TUNER_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_read(hInit, DIV_BROADCAST, (u8)info.buff[0]
			, (u8)info.buff[1],  (u8 *)(&info.buff[3])
			, (u8)info.buff[2]);
		err |= copy_to_user((void *)arg, (void *)&info, size);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_TUNER_READ\
			[0x%x][0x%x] \n", info.buff[0], info.buff[3]);
		break;
	case IOCTL_ISDBT_TUNER_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_write(hInit, DIV_BROADCAST, (u8)info.buff[0]
			, (u8)info.buff[1], (u8 *)(&info.buff[3])
			, (u8)info.buff[2]);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_TUNER_WRITE\
			[0x%x][0x%x] \n", info.buff[0], info.buff[3]);
		break;
	case IOCTL_ISDBT_TUNER_SET_FREQ:
		{
			u32 f_rf;
			u8 subch;
			err = copy_from_user((void *)&info, (void *)arg, size);

			f_rf = (u32)info.buff[0];
			subch = (u8)info.buff[1];
#ifndef BBM_I2C_TSIF
			mt_eint_mask(CUST_EINT_EDP_INTN_NUM);
			isdbt_isr_check(hInit);
#endif
			res = bbm_com_tuner_set_freq(hInit
				, DIV_BROADCAST, f_rf, subch);
#ifndef BBM_I2C_TSIF
			mutex_lock(&ringbuffer_lock);
			fci_ringbuffer_flush(&hOpen->RingBuffer);
			mutex_unlock(&ringbuffer_lock);
			bbm_com_write(hInit
				, DIV_BROADCAST, BBM_BUF_ENABLE, 0x01);
			mt_eint_unmask(CUST_EINT_EDP_INTN_NUM);
#endif
			print_log(hInit
			, "[FC8300] IOCTL_ISDBT_TUNER_SET_FREQ [%d][%d] \n"
			, f_rf, subch);
		}
		break;
	case IOCTL_ISDBT_TUNER_SELECT:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_select(hInit
			, DIV_BROADCAST, (u32)info.buff[0], (u32)info.buff[1]);

		print_log(hInit, "[FC8300] IOCTL_ISDBT_TUNER_SELECT [%d][%d] \n"
			, info.buff[0], info.buff[1]);
		break;
	case IOCTL_ISDBT_TS_START:
		hOpen->isdbttype = TS_TYPE;
		print_log(hInit, "[FC8300] IOCTL_ISDBT_TS_START \n");
		break;
	case IOCTL_ISDBT_TS_STOP:
		hOpen->isdbttype = 0;
		print_log(hInit, "[FC8300] IOCTL_ISDBT_TS_STOP \n");
		break;
	case IOCTL_ISDBT_POWER_ON:
		isdbt_hw_init();
		print_log(hInit, "[FC8300] IOCTL_ISDBT_POWER_ON \n");
		break;
	case IOCTL_ISDBT_POWER_OFF:
		isdbt_hw_deinit();
		print_log(hInit, "[FC8300] IOCTL_ISDBT_POWER_OFF \n");
		break;
	case IOCTL_ISDBT_SCAN_STATUS:
		res = bbm_com_scan_status(hInit, DIV_BROADCAST);
		print_log(hInit
			, "[FC8300] IOCTL_ISDBT_SCAN_STATUS : %d\n", res);
		break;
	case IOCTL_ISDBT_TUNER_GET_RSSI:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_get_rssi(hInit
			, DIV_BROADCAST, (s32 *)&info.buff[0]);
		err |= copy_to_user((void *)arg, (void *)&info, size);
		break;

	case IOCTL_ISDBT_DEINIT:
		mt_eint_mask(CUST_EINT_EDP_INTN_NUM);
		res = bbm_com_deinit(hInit, DIV_BROADCAST);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_DEINIT \n");
		break;

	default:
		print_log(hInit, "isdbt ioctl error!\n");
		res = BBM_NOK;
		break;
	}

	if (err < 0) {
		print_log(hInit, "copy to/from user fail : %d", err);
		res = BBM_NOK;
	}
	return res;
}

static int __init isdbt_init(void)
{
	s32 res;

	//SW-PRODUCTION-DH-Init_dtv_driver_for_samba_only-00+[
	if(BAND_SAMBA != fih_get_band_id())
	{
		print_log(hInit, "dtv driver is only enabled for SAMBA\n");
		return -1;
	}
	//SW-PRODUCTION-DH-Init_dtv_driver_for_samba_only-00+]
	print_log(hInit, "isdbt_init 20141212\n");

	res = misc_register(&fc8300_misc_device);

	if (res < 0) {
		print_log(hInit, "isdbt init fail : %d\n", res);
		return res;
	}

	isdbt_hw_setting();

	hInit = kmalloc(sizeof(struct ISDBT_INIT_INFO_T), GFP_KERNEL);


#if defined(BBM_I2C_TSIF) || defined(BBM_I2C_SPI)
	res = bbm_com_hostif_select(hInit, BBM_I2C);
#else
	res = bbm_com_hostif_select(hInit, BBM_SPI);
#endif

	if (res)
		print_log(hInit, "isdbt host interface select fail!\n");

#ifndef BBM_I2C_TSIF
	if (!isdbt_kthread)	{
		print_log(hInit, "kthread run\n");
		isdbt_kthread = kthread_run(isdbt_thread
			, (void *)hInit, "isdbt_thread");
	}
#endif

	INIT_LIST_HEAD(&(hInit->hHead));

	return 0;
}

static void __exit isdbt_exit(void)
{
	print_log(hInit, "isdbt isdbt_exit \n");

	isdbt_hw_deinit();
#ifndef BBM_I2C_TSIF
	if (isdbt_kthread)
		kthread_stop(isdbt_kthread);

	isdbt_kthread = NULL;
#endif

	bbm_com_hostif_deselect(hInit);

	if (hInit != NULL)
		kfree(hInit);

	misc_deregister(&fc8300_misc_device);

}

module_init(isdbt_init);
module_exit(isdbt_exit);

MODULE_AUTHOR("FCI");
MODULE_DESCRIPTION("FC8300 driver");
MODULE_LICENSE("GPL");

