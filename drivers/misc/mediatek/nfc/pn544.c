/*
 * Copyright (C) 2010 Trusted Logic S.A.
 * Copyright(C) 2013 Foxconn International Holdings, Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

//ALERT:relocate pn544.c under .\kernel\drivers\misc

/*
* Makefile//TODO:Here is makefile reference
* obj-$(CONFIG_PN544)+= pn544.o
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/pn544.h>
#include <linux/of_gpio.h>

//PERI-OH-NFC_HW_ID_Probe-00+{
#undef FIH_HWID_FUNC
#ifdef FIH_HWID_FUNC
#include <linux/fih_hw_info.h> 
#endif
//PERI-OH-NFC_HW_ID_Probe-00+}


#define MTK_GPIO_ENABLE
#ifdef MTK_GPIO_ENABLE
#include <linux/dma-mapping.h>

#include <cust_gpio_usage.h>
#include <cust_eint.h>

#include <mach/mt_gpio.h>
#include <mach/eint.h>

#include<mach/mt_pm_ldo.h>

/*function to init GPIO function*/
int pn547_request_gpio_resource(void *pdata)
{
    struct pn544_i2c_platform_data *pn547_pdata = (struct pn544_i2c_platform_data *) pdata;
    
    printk(KERN_INFO "[PN547] %s \n", __func__);

    if (pn547_pdata == NULL)
    {
        printk(KERN_ERR "[PN547] pdata is null");
        return -EINVAL;
    }

    //configure ven
    mt_set_gpio_mode(pn547_pdata->ven_gpio, GPIO_PN547_VEN_PIN_M_GPIO);
	mt_set_gpio_dir(pn547_pdata->ven_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(pn547_pdata->ven_gpio, GPIO_OUT_ZERO);
    
    //configure firm
    mt_set_gpio_mode(pn547_pdata->firm_gpio, GPIO_PN547_FIRM_PIN_M_GPIO);
	mt_set_gpio_dir(pn547_pdata->firm_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(pn547_pdata->firm_gpio, GPIO_OUT_ZERO);
    
    //configure irq
    mt_set_gpio_mode(pn547_pdata->irq_gpio, GPIO_PN547_IRQ_PIN_M_EINT);
	mt_set_gpio_dir(pn547_pdata->irq_gpio, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(pn547_pdata->irq_gpio, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(pn547_pdata->irq_gpio, GPIO_PULL_DOWN);
    
    return 0;
}

/*function to init VDD function*/
int pn547_request_vdd_resource(void *pdata)
{
    printk(KERN_INFO "[PN547] %s \n", __func__);

    //VREG_L5_VMC
    //printk(KERN_INFO "%s, enable VREG_L5", __func__);
    //if (!hwPowerOn(MT65XX_POWER_LDO_VMC1, VOL_1800, "pn544_pvdd"))
    //{
        //printk(KERN_ERR "[PN547] Enable MT65XX_POWER_LDO_VMC1 failed!");
        //return -EIO;
    //}

    //VREG_L14_VSIM1
    //printk(KERN_INFO "%s, enable VREG_L14", __func__);
    //if (!hwPowerOn(MT65XX_POWER_LDO_VSIM1, VOL_1800, "pn544_vdd_uim"))
    //{
        //printk(KERN_ERR "[PN547] Enable MT65XX_POWER_LDO_VSIM1 failed!");
        //return -EIO;
    //}
    
    return 0;
}

struct pn544_i2c_platform_data pn544_platform_data = {
    .irq_gpio = GPIO_PN547_IRQ_PIN,
    .ven_gpio = GPIO_PN547_VEN_PIN,
    .firm_gpio = GPIO_PN547_FIRM_PIN,
    .request_vdd_resource = pn547_request_vdd_resource,
    .request_gpio_resource = pn547_request_gpio_resource,
};

static struct i2c_board_info __initdata nfc_board_info = {I2C_BOARD_INFO("pn544", 0x28)};

//For DMA
static char *I2CDMAWriteBuf = NULL;
static unsigned int I2CDMAWriteBuf_pa = 0;
static char *I2CDMAReadBuf = NULL;
static unsigned int I2CDMAReadBuf_pa = 0;
#endif

//#define pr_err printk
//#define pr_debug printk
//#define pr_warning printk

#define MAX_BUFFER_SIZE	255 //MTK DMA max payload is 255 bytes
#undef DEBUG_I2C_ERROR

struct pn544_dev	
{
	wait_queue_head_t	read_wq;
	struct mutex		read_mutex;
	struct i2c_client	*client;
	struct miscdevice	pn544_device;
	int 				ven_gpio;
	int 				firm_gpio;
	int					irq_gpio;
	bool				irq_enabled;
	bool                IsPrbsTestMode;//SW2-Peripheral-NFC-CH-Debug_Menu-00++
	spinlock_t			irq_enabled_lock;
	unsigned int		pn544_sys_info; //MTD-SW3-PERIPHERAL-OwenHuang-NFC_DBG_MSG-00+
	atomic_t            balance_wake_irq; //PERI-OH-NFC_Unbalance_IRQ_Wake-00+
};

//MTD-SW3-PERIPHERAL-OwenHuang-NFC_DBG_MSG-00+{
#define DEBUG_BIT 			BIT(0)
#define IRQ_GPIO_BIT 		BIT(1)
#define DEFAULT_INFO_VALUE 	(0x00)
#define I2C_RETRY_TIME      0

/* Flag for determining if print function title */
enum {
PRINT_FUNC_NONE = 0,
PRINT_FUNC_ENTER,
PRINT_FUNC_LEAVE,
};

/*For dynamic debug message*/
#define PN544_DBG(dev, func_enter, fmt, args...)	\
	do { \
			if (!dev) \
				break; \
			else \
			{ \
				if (dev->pn544_sys_info & DEBUG_BIT) \
				{ \
					if (func_enter == PRINT_FUNC_LEAVE) \
						printk(KERN_INFO "[%s][DBG] OUT\n", __func__); \
					else if (func_enter == PRINT_FUNC_ENTER) \
						printk(KERN_INFO "[%s][DBG] IN\n", __func__); \
					\
					printk(fmt, ##args); \
				} \
			} \
	} while(0)

static struct pn544_dev *g_pn544_dev = NULL;

static ssize_t pn544_info_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	struct pn544_dev *pn544_dev = dev_get_drvdata(dev);

	if (!pn544_dev)
	{
		printk(KERN_ERR "invalid device data!\n");
		return -EIO;
	}

	/*Dump interrupt PIN GPIO State*/
	if (pn544_dev->pn544_sys_info & IRQ_GPIO_BIT)
	{
		printk(KERN_INFO "IRQ GPIO State: %d \n", mt_get_gpio_in(pn544_dev->irq_gpio));
	}

	return snprintf(buf, PAGE_SIZE, "INFO_Setting: 0x%02X , IRQ_GPIO: %s\n", pn544_dev->pn544_sys_info, 
					mt_get_gpio_in(pn544_dev->irq_gpio)? "High" : "Low");//MTD-Peripheral-OwenHuang-Fix_Code_Coverity-00*
	
}

static ssize_t pn544_info_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf, size_t count)
{
	struct pn544_dev *pn544_dev = dev_get_drvdata(dev);
	int ret = 0;
	unsigned long val = 0;

	if (!pn544_dev)
	{
		printk(KERN_ERR "invalid device data!\n");
		return -EIO;
	}

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
	{
		printk(KERN_ERR "Get buffer data error! (%d) \n", ret);
		return ret;
	}

	/*Get new settings value*/
	pn544_dev->pn544_sys_info = val; 
	
	return count;
}

DEVICE_ATTR(info, 0660, pn544_info_show, pn544_info_store);

static struct attribute *pn544_attributes[] = {
        &dev_attr_info.attr,
        NULL
};

static const struct attribute_group pn544_attr_group = {
        .attrs = pn544_attributes,
};
//MTD-SW3-PERIPHERAL-OwenHuang-NFC_DBG_MSG-00+}

static void pn544_disable_irq(struct pn544_dev *pn544_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pn544_dev->irq_enabled_lock, flags);
	if (pn544_dev->irq_enabled) 
	{
        PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "Disable IRQ");
		mt_eint_mask(pn544_dev->irq_gpio);
		pn544_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&pn544_dev->irq_enabled_lock, flags);
}

#ifdef MTK_GPIO_ENABLE
void pn544_dev_irq_handler(void)
{
   struct pn544_dev *pn544_dev = g_pn544_dev;
   
   if (NULL == pn544_dev)
   {
      printk(KERN_DEBUG "pn544_dev NULL\n");
      return;
   }
   PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "&pn544_dev=%p", pn544_dev);

   if (!mt_get_gpio_in(pn544_dev->irq_gpio)) 
   {
      printk(KERN_DEBUG "%s, IRQ PIN is not high\n", __func__);
      return;
   }
   
   pn544_disable_irq(pn544_dev); 
   wake_up(&pn544_dev->read_wq);
   //wake_up_interruptible(&mt6605_dev->read_wq);
   
   PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "wake_up &read_wq=%p", &pn544_dev->read_wq);
   PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "pn544_dev_irq_handler");
}
#endif

static ssize_t pn544_dev_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
	struct pn544_dev *pn544_dev = filp->private_data;
	//char tmp[MAX_BUFFER_SIZE];
	int ret = 0x00, i = 0x00;
	int retry = 0;

	if (count > MAX_BUFFER_SIZE)
		count = MAX_BUFFER_SIZE;

	PN544_DBG(pn544_dev, PRINT_FUNC_ENTER, "reading %zu bytes. \n", count);
	
	//SW2-Peripheral-NFC-CH-Debug_Menu-00++{
	if (pn544_dev != NULL && pn544_dev->IsPrbsTestMode == true)
    {
        printk(KERN_INFO "%s: In Test Mode, ignore...\n", __func__);
        return 0;
    }
	//SW2-Peripheral-NFC-CH-Debug_Menu-00++}
	
	mutex_lock(&pn544_dev->read_mutex);
    /*Check GPIO Value, if low we start to wait, else trigger read via i2c bus*/
	if (!mt_get_gpio_in(pn544_dev->irq_gpio)) 
	{
		if (filp->f_flags & O_NONBLOCK) 
		{
		    printk(KERN_INFO "[PN544] Return because of filp->f_flags & O_NONBLOCK... \n");
			ret = -EAGAIN;
			goto fail;
		}

        while (1)
        {
    		pn544_dev->irq_enabled = true;
            mt_eint_unmask(pn544_dev->irq_gpio); //enable irq
            PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "[PN544] wait_event_interruptible - IN\n");
    		ret = wait_event_interruptible(pn544_dev->read_wq,
    				mt_get_gpio_in(pn544_dev->irq_gpio));		
            PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "[PN544] wait_event_interruptible - OUT, ret = 0x%02X\n", ret);
    		pn544_disable_irq(pn544_dev);

    		if (ret)
            {   printk(KERN_ERR "%s, wait_event_interruptible->ret = 0x%X\n", __func__, ret);
    			goto fail;
            }
            
            if (mt_get_gpio_in(pn544_dev->irq_gpio) || ret == 0x00)
                break;

            PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "[PN544]continous wait for reading\n");
        }
	}
    
	/* Read data */
    pn544_dev->client->addr = (pn544_dev->client->addr & I2C_MASK_FLAG);
    pn544_dev->client->ext_flag |= (I2C_DMA_FLAG | I2C_A_FILTER_MSG | I2C_ENEXT_FLAG);
    pn544_dev->client->timing = 400;
	ret = i2c_master_recv(pn544_dev->client, (unsigned char *)(uintptr_t)I2CDMAReadBuf_pa, count);
	while (ret < 0 && retry < I2C_RETRY_TIME)
	{
		printk(KERN_ERR "[PN544] read data bus err! retry: %d \n", retry);
		retry++;
        pn544_dev->client->addr = (pn544_dev->client->addr & I2C_MASK_FLAG);
        pn544_dev->client->ext_flag |= (I2C_DMA_FLAG | I2C_A_FILTER_MSG | I2C_ENEXT_FLAG);
        pn544_dev->client->timing = 400;
		ret = i2c_master_recv(pn544_dev->client, (unsigned char *)(uintptr_t)I2CDMAReadBuf_pa, count);
	}
	mutex_unlock(&pn544_dev->read_mutex);

	if (ret < 0) 
	{
		pr_err("%s: i2c_master_recv returned %d\n", __func__, ret);

		/*Show data payload when i2c error*/
	#ifdef DEBUG_I2C_ERROR
		printk(">>PN547_READ-[");
		for(i = 0; i < count; i++)
		{
			printk("%02X,", tmp[i]);
			if (i == (count - 1))
				printk("]\n");
		}
	#endif
		return ret;
	}
	if (ret > count) 
	{
		pr_err("%s: received too many bytes from i2c (%d)\n", __func__, ret);
		return -EIO;
	}
	if (copy_to_user(buf, I2CDMAReadBuf, ret)) 
	{
		pr_warning("%s : failed to copy to user space\n", __func__);
		return -EFAULT;
	}

	PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "IFD->PC:");
	if (pn544_dev->pn544_sys_info & DEBUG_BIT)
	{
		for(i = 0; i < ret; i++)
		{
			PN544_DBG(pn544_dev, PRINT_FUNC_NONE, " %02X", I2CDMAReadBuf[i]);
		}
	}
	PN544_DBG(pn544_dev, PRINT_FUNC_LEAVE, "\n");
	
	return ret;

fail:
	mutex_unlock(&pn544_dev->read_mutex);
	return ret;
}

static ssize_t pn544_dev_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
	struct pn544_dev  *pn544_dev;
	//char tmp[MAX_BUFFER_SIZE];
	int ret,i;
	int retry = 0;

	pn544_dev = filp->private_data;

	if (count > MAX_BUFFER_SIZE)
	{
		count = MAX_BUFFER_SIZE;
	}
	if (copy_from_user(I2CDMAWriteBuf, buf, count)) 
	{
		pr_err("%s : failed to copy from user space\n", __func__);
		return -EFAULT;
	}

	PN544_DBG(pn544_dev, PRINT_FUNC_ENTER, "riting %zu bytes.\n", count);
	
	/* Write data */
    pn544_dev->client->addr = (pn544_dev->client->addr & I2C_MASK_FLAG);
    pn544_dev->client->ext_flag |= (I2C_DMA_FLAG | I2C_A_FILTER_MSG | I2C_ENEXT_FLAG);
    pn544_dev->client->timing = 400;
	ret = i2c_master_send(pn544_dev->client, (unsigned char *)(uintptr_t)I2CDMAWriteBuf_pa, count);
	while (ret < 0 && retry < I2C_RETRY_TIME)
	{
		printk(KERN_ERR "[PN544] write data bus error!, retry: %d \n", retry);
        pn544_dev->client->addr = (pn544_dev->client->addr & I2C_MASK_FLAG);
        pn544_dev->client->ext_flag |= (I2C_DMA_FLAG | I2C_A_FILTER_MSG | I2C_ENEXT_FLAG);
        pn544_dev->client->timing = 400;
		ret = i2c_master_send(pn544_dev->client, (unsigned char *)(uintptr_t)I2CDMAWriteBuf_pa, count);
		retry ++;
	}
	if (ret != count) 
	{
		pr_err("%s : i2c_master_send returned %d\n", __func__, ret);
		ret = -EIO;
	}

	PN544_DBG(pn544_dev, PRINT_FUNC_NONE, "PC->IFD");
	if (pn544_dev->pn544_sys_info & DEBUG_BIT)
	{
		for(i = 0; i < count; i++)
		{
			PN544_DBG(pn544_dev, PRINT_FUNC_NONE, " %02X", I2CDMAWriteBuf[i]);
		}
	}
	PN544_DBG(pn544_dev, PRINT_FUNC_LEAVE, "\n");

	/*Workaround for PN547C2 standby mode, when detect i2c error*/
	if (ret < 0)
	{
		/*Show data payload when i2c error*/
	#ifdef DEBUG_I2C_ERROR
		printk(">>PN547_WRITE-[");
		for (i = 0; i < count; i ++)
		{
			printk("%02X,", I2CDMAWriteBuf[i]);
			if (i == (count - 1))
				printk("]\n");
		}
	#endif
		printk(KERN_INFO "Waiting 1ms to wakup NXP PN547C2 from standby...\n");
		msleep(1); //wait for standby to wakeup
	}
	
	return ret;
}

static int pn544_dev_open(struct inode *inode, struct file *filp)
{

	struct pn544_dev *pn544_dev = container_of(filp->private_data, struct pn544_dev, pn544_device);
	
	filp->private_data = pn544_dev;

	pr_debug("%s : %d,%d\n", __func__, imajor(inode), iminor(inode));

	return 0;
}

//FIH-SW3-PERIPHERAL-BJ-NFC_Porting-00*
static long pn544_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct pn544_dev *pn544_dev = filp->private_data;

	switch (cmd) 
	{
	case PN544_SET_PWR:
		if (arg == 2) 
		{
			/* power on with firmware download (requires hw reset)
			 */
			printk("%s power on with firmware\n", __func__);
			mt_set_gpio_out(pn544_dev->ven_gpio, GPIO_OUT_ONE);
			mt_set_gpio_out(pn544_dev->firm_gpio, GPIO_OUT_ONE);
			msleep(10);//msleep(10);bennet
			mt_set_gpio_out(pn544_dev->ven_gpio, GPIO_OUT_ZERO);
			msleep(50);
			mt_set_gpio_out(pn544_dev->ven_gpio, GPIO_OUT_ONE);
			msleep(10);//msleep(10);bennet

            if (atomic_read(&pn544_dev->balance_wake_irq) == 0)
            {
                atomic_set(&pn544_dev->balance_wake_irq, 1);
                //irq_set_irq_wake(pn544_dev->client->irq, 1);
                printk("%s set irq wake enable\n", __func__);
            }
		} 
		else if (arg == 1) 
		{
			/* power on */
			printk("%s power on\n", __func__);
			mt_set_gpio_out(pn544_dev->firm_gpio, GPIO_OUT_ZERO);
			mt_set_gpio_out(pn544_dev->ven_gpio, GPIO_OUT_ONE);
			msleep(10);

			//PERI-OH-NFC_Unbalance_IRQ_Wake-00+{
			if (atomic_read(&pn544_dev->balance_wake_irq) == 0)
			{
				atomic_set(&pn544_dev->balance_wake_irq, 1);
				//irq_set_irq_wake(pn544_dev->client->irq, 1); //PERI-OH-NFC_Set_IRQ_Wake-00+
				printk("%s set irq wake enable\n", __func__);
			}
			//PERI-OH-NFC_Unbalance_IRQ_Wake-00+}
		} 
		else  if (arg == 0) 
		{
			/* power off */
			printk("%s power off\n", __func__);
			mt_set_gpio_out(pn544_dev->firm_gpio, GPIO_OUT_ZERO);
			mt_set_gpio_out(pn544_dev->ven_gpio, GPIO_OUT_ZERO);
			msleep(50);

			//PERI-OH-NFC_Unbalance_IRQ_Wake-00+{
			if (atomic_read(&pn544_dev->balance_wake_irq) == 1)
			{
				atomic_set(&pn544_dev->balance_wake_irq , 0);
				//irq_set_irq_wake(pn544_dev->client->irq, 0); //PERI-OH-NFC_Set_IRQ_Wake-00+
				printk("%s set irq wake disable\n", __func__);
			}
			//PERI-OH-NFC_Unbalance_IRQ_Wake-00+}
		//SW2-Peripheral-NFC-CH-Debug_Menu-00++{
			pn544_dev->IsPrbsTestMode = false;
		} 
        else if (arg == 3)
        {
            pn544_dev->IsPrbsTestMode = true;
            printk("%s: enable PRBS test mode!\n", __func__);
        }
        else 
        {
		//SW2-Peripheral-NFC-CH-Debug_Menu-00++}
			pr_err("%s bad arg %lx\n", __func__, arg);
			return -EINVAL;
		}
		break;
	default:
		pr_err("%s bad ioctl %u\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations pn544_dev_fops = 
{
	.owner	= THIS_MODULE,
	.llseek	= no_llseek,
	.read	= pn544_dev_read,
	.write	= pn544_dev_write,
	.open	= pn544_dev_open,
	.unlocked_ioctl = pn544_dev_ioctl,
	.compat_ioctl = pn544_dev_ioctl,
};

static int pn544_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct pn544_i2c_platform_data *platform_data = NULL;
	struct pn544_dev *pn544_dev = NULL;
	//struct device_node *np = client->dev.of_node;

	pr_info("%s : pn544 probe\n", __func__);

//PERI-OH-NFC_HW_ID_Probe-00+{
#ifdef FIH_HWID_FUNC
	if (fih_get_product_phase() < PHASE_PD)
	{
		printk(KERN_ERR "Detect hardware phase as Big Board, componet is not PN547, aborting! \n");
		return -EINVAL;
	}
	else
	{
		printk(KERN_INFO "Componets is PN547, continune probe...! \n");
	}
#endif
//PERI-OH-NFC_HW_ID_Probe-00+}
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		pr_err("%s : need I2C_FUNC_I2C\n", __func__);
		ret =  -ENODEV;
		goto err_exit;
	}
	
	pn544_dev = kzalloc(sizeof(*pn544_dev), GFP_KERNEL);
	if (pn544_dev == NULL) 
	{
		dev_err(&client->dev,
				"failed to allocate memory for module data\n");
		ret = -ENOMEM;
		goto err_exit;
	}

    platform_data = &pn544_platform_data;
	//gpio config
	if (platform_data != NULL && platform_data->request_gpio_resource != NULL)
    {   
	    ret = platform_data->request_gpio_resource((void *)platform_data);
	    if(ret)
	    {
		    pr_err("%s : gpio config error\n",__func__);
		    ret = -ENODEV;
		    goto err_exit;
	    }
    }
    //vdd config
    if (platform_data != NULL && platform_data->request_vdd_resource != NULL)
    {   
	    ret = platform_data->request_vdd_resource((void *)platform_data);
	    if(ret)
	    {
		    pr_err("%s : vdd config error\n",__func__);
		    ret = -ENODEV;
		    goto err_exit;
	    }
    }

	//SW2-Peripheral-NFC-CH-Debug_Menu-00++{
	//test mode flag
    pn544_dev->IsPrbsTestMode = false;
	//SW2-Peripheral-NFC-CH-Debug_Menu-00++}

	pn544_dev->irq_gpio = platform_data->irq_gpio;
	pn544_dev->ven_gpio  = platform_data->ven_gpio;
	pn544_dev->firm_gpio  = platform_data->firm_gpio;
	pn544_dev->client   = client;

#ifdef MTK_GPIO_ENABLE
    //assign to address 
    g_pn544_dev = pn544_dev;
    //allocate DMA buffer for MTK
#ifdef CONFIG_64BIT    
	I2CDMAWriteBuf = (char *)dma_alloc_coherent(&client->dev, MAX_BUFFER_SIZE, (dma_addr_t *)&I2CDMAWriteBuf_pa, GFP_KERNEL);
#else
    I2CDMAWriteBuf = (char *)dma_alloc_coherent(NULL, MAX_BUFFER_SIZE, (dma_addr_t *)&I2CDMAWriteBuf_pa, GFP_KERNEL);
#endif
    //I2CDMAWriteBuf = (char *)dma_alloc_coherent(NULL, MAX_BUFFER_SIZE, &I2CDMAWriteBuf_pa, GFP_KERNEL);
	if (I2CDMAWriteBuf == NULL) 
	{
		pr_err("%s : failed to allocate dma buffer\n", __func__);
		goto err_alloc_dma;
	}
#ifdef CONFIG_64BIT 	
	I2CDMAReadBuf = (char *)dma_alloc_coherent(&client->dev, MAX_BUFFER_SIZE, (dma_addr_t *)&I2CDMAReadBuf_pa, GFP_KERNEL);
#else
    I2CDMAReadBuf = (char *)dma_alloc_coherent(NULL, MAX_BUFFER_SIZE, (dma_addr_t *)&I2CDMAReadBuf_pa, GFP_KERNEL);
#endif	
	//I2CDMAReadBuf = (char *)dma_alloc_coherent(NULL, MAX_BUFFER_SIZE, &I2CDMAReadBuf_pa, GFP_KERNEL);
	if (I2CDMAReadBuf == NULL) 
	{
		pr_err("%s : failed to allocate dma buffer\n", __func__);
		goto err_alloc_dma;
	}
#endif

	/*Initialize counter of wake_irq*/
	atomic_set(&pn544_dev->balance_wake_irq, 0); //PERI-OH-NFC_Unbalance_IRQ_Wake-00+

	/* init mutex and queues */
	init_waitqueue_head(&pn544_dev->read_wq);
	mutex_init(&pn544_dev->read_mutex);
	spin_lock_init(&pn544_dev->irq_enabled_lock);

	pn544_dev->pn544_device.minor = MISC_DYNAMIC_MINOR;
	pn544_dev->pn544_device.name = "pn544";
	pn544_dev->pn544_device.fops = &pn544_dev_fops;

	ret = misc_register(&pn544_dev->pn544_device);
	if (ret) 
	{
		pr_err("%s : misc_register failed\n", __FILE__);
		goto err_misc_register;
	}

	/* request irq.  the irq is set whenever the chip has data available
	 * for reading.  it is cleared when all data has been read.
	 */
	pr_info("%s : requesting IRQ %d\n", __func__, client->irq);
	pn544_dev->irq_enabled = true;

#ifdef MTK_GPIO_ENABLE
    //request IRQ routine
    mt_eint_set_hw_debounce(CUST_EINT_NFC_NUM, CUST_EINT_NFC_DEBOUNCE_CN);
    mt_eint_registration(CUST_EINT_NFC_NUM, EINTF_TRIGGER_RISING, pn544_dev_irq_handler, 1); /*using edge trigger, not level trigger*/
#endif
	/*ret = request_irq(client->irq, pn544_dev_irq_handler, IRQF_TRIGGER_HIGH, client->name, pn544_dev);
	if (ret) 
	{
		dev_err(&client->dev, "request_irq failed\n");
		goto err_request_irq_failed;
	}*/
	
	pr_info("%s : disable IRQ\n", __func__);
    client->irq = CUST_EINT_NFC_NUM;
	pn544_disable_irq(pn544_dev);
	i2c_set_clientdata(client, pn544_dev);

	//MTD-SW3-PERIPHERAL-OwenHuang-NFC_DBG_MSG-00+{
	pn544_dev->pn544_sys_info = DEFAULT_INFO_VALUE;
	ret = sysfs_create_group(&client->dev.kobj, &pn544_attr_group);
	if (ret)
	{
		printk(KERN_ERR "Register device attr error! (%d) \n", ret);
		goto err_request_irq_failed;
	}
	//MTD-SW3-PERIPHERAL-OwenHuang-NFC_DBG_MSG-00+}

	return 0;

err_request_irq_failed:
	misc_deregister(&pn544_dev->pn544_device);
err_misc_register:
	mutex_destroy(&pn544_dev->read_mutex);
err_alloc_dma:
err_exit:
	if (pn544_dev != NULL)
	{
		gpio_free(pn544_dev->irq_gpio);
		gpio_free(pn544_dev->ven_gpio);
		gpio_free(pn544_dev->firm_gpio);
		kfree(pn544_dev);
	}
	return ret;
}

static int pn544_remove(struct i2c_client *client)
{
	struct pn544_dev *pn544_dev;

    //De-alloc DMA buffer
    if (I2CDMAWriteBuf)
	{
#ifdef CONFIG_64BIT 	
		dma_free_coherent(&client->dev, MAX_BUFFER_SIZE, I2CDMAWriteBuf, I2CDMAWriteBuf_pa);
#else
		dma_free_coherent(NULL, MAX_BUFFER_SIZE, I2CDMAWriteBuf, I2CDMAWriteBuf_pa);
#endif
		//dma_free_coherent(NULL, MAX_BUFFER_SIZE, I2CDMAWriteBuf, I2CDMAWriteBuf_pa);
		I2CDMAWriteBuf = NULL;
		I2CDMAWriteBuf_pa = 0;
	}
	if (I2CDMAReadBuf)
	{
#ifdef CONFIG_64BIT
		dma_free_coherent(&client->dev, MAX_BUFFER_SIZE, I2CDMAReadBuf, I2CDMAReadBuf_pa);
#else
		dma_free_coherent(NULL, MAX_BUFFER_SIZE, I2CDMAReadBuf, I2CDMAReadBuf_pa);
#endif
		//dma_free_coherent(NULL, MAX_BUFFER_SIZE, I2CDMAReadBuf, I2CDMAReadBuf_pa);
		I2CDMAReadBuf = NULL;
		I2CDMAReadBuf_pa = 0;
	}
    
	pn544_dev = i2c_get_clientdata(client);
	free_irq(client->irq, pn544_dev);
	misc_deregister(&pn544_dev->pn544_device);
	mutex_destroy(&pn544_dev->read_mutex);
	gpio_free(pn544_dev->irq_gpio);
	gpio_free(pn544_dev->ven_gpio);
	gpio_free(pn544_dev->firm_gpio);
	kfree(pn544_dev);

	return 0;
}

static const struct i2c_device_id pn544_id[] = {
	{ "pn544", 0 },
	{ }
};

#ifdef CONFIG_OF
static struct of_device_id pn544_match_table[] = {
	{ .compatible = "nxp,pn544",},
	{ },
};
#else
#define pn544_match_table NULL
#endif

static struct i2c_driver pn544_driver = {
	.id_table	= pn544_id,
	.probe		= pn544_probe,
	.remove		= pn544_remove,
	.driver		= 
	{
		.owner	= THIS_MODULE,
		.of_match_table = pn544_match_table,
		.name	= "pn544",
	},
};

/*
 * module load/unload record keeping
 */

static int __init pn544_dev_init(void)
{
	pr_info("Loading pn544 driver\n");
#ifdef MTK_GPIO_ENABLE
    i2c_register_board_info(3, &nfc_board_info, 1);
#endif
	return i2c_add_driver(&pn544_driver);
}
module_init(pn544_dev_init);

static void __exit pn544_dev_exit(void)
{
	pr_info("Unloading pn544 driver\n");
	i2c_del_driver(&pn544_driver);
}
module_exit(pn544_dev_exit);

MODULE_AUTHOR("Sylvain Fonteneau");
MODULE_DESCRIPTION("NFC PN544 driver");
MODULE_LICENSE("GPL");
