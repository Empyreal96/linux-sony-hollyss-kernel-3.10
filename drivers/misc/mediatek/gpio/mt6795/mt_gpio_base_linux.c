#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#ifndef CONFIG_OF
#include <mach/mt_reg_base.h>
#endif
#include <mach/mt_gpio_base.h>
#include <mach/mt_gpio_core.h>
#include <mach/eint.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/of_gpio.h>
#include <linux/idr.h>

#include <cust_gpio_usage.h>
#include <mach/mt_gpio.h>
#include <linux/fih_hw_info.h>

//============================
typedef struct
{
  int HWID_LOGIC;
  fih_product_id_type PROJECT;
  fih_band_id_type BAND;
  fih_product_phase_type PHASE;
  fih_sim_type SIM;
}fih_hwid_map;

fih_hwid_map *HWID_MAP;

static fih_hwid_map HWID_MAP_MT6795[]=
{
  /* HOLLY */
  //===============BAND_GINA
  {0x10, PROJECT_HOLLY, BAND_GINA, PHASE_PD,     SIM_SINGLE},
  {0x11, PROJECT_HOLLY, BAND_GINA, PHASE_DP,     SIM_DUAL},
  {0x12, PROJECT_HOLLY, BAND_GINA, PHASE_SP_AP1,     SIM_DUAL},
  {0x13, PROJECT_HOLLY, BAND_GINA, PHASE_AP2,     SIM_DUAL},
  {0x14, PROJECT_HOLLY, BAND_GINA, PHASE_TP1_TP2,     SIM_DUAL},
  {0x15, PROJECT_HOLLY, BAND_GINA, PHASE_PQ,     SIM_DUAL},
  {0x16, PROJECT_HOLLY, BAND_GINA, PHASE_MP,     SIM_DUAL},
  //===============BAND_REX
  {0x20, PROJECT_HOLLY, BAND_REX, PHASE_PD,     SIM_SINGLE},
  {0x21, PROJECT_HOLLY, BAND_REX, PHASE_DP,     SIM_SINGLE},
  {0x22, PROJECT_HOLLY, BAND_REX, PHASE_SP_AP1,     SIM_SINGLE},
  {0x23, PROJECT_HOLLY, BAND_REX, PHASE_AP2,     SIM_SINGLE},
  {0x24, PROJECT_HOLLY, BAND_REX, PHASE_TP1_TP2,     SIM_SINGLE},
  {0x25, PROJECT_HOLLY, BAND_REX, PHASE_PQ,     SIM_SINGLE},
  {0x26, PROJECT_HOLLY, BAND_REX, PHASE_MP,     SIM_SINGLE},
  {0x2f, PROJECT_HOLLY, BAND_REX, PHASE_EVM,     SIM_DUAL},
  //===============BAND_APAC
  {0x30, PROJECT_HOLLY, BAND_APAC, PHASE_PD,     SIM_SINGLE},
  {0x31, PROJECT_HOLLY, BAND_APAC, PHASE_DP,     SIM_SINGLE},
  {0x32, PROJECT_HOLLY, BAND_APAC, PHASE_SP_AP1,     SIM_SINGLE},
  {0x33, PROJECT_HOLLY, BAND_APAC, PHASE_AP2,     SIM_SINGLE},
  {0x34, PROJECT_HOLLY, BAND_APAC, PHASE_TP1_TP2,     SIM_SINGLE},
  {0x35, PROJECT_HOLLY, BAND_APAC, PHASE_PQ,     SIM_SINGLE},
  {0x36, PROJECT_HOLLY, BAND_APAC, PHASE_MP,     SIM_SINGLE},
  //===============
  {0x41, PROJECT_HOLLY, BAND_APAC, PHASE_DP,     SIM_DUAL},
  {0x42, PROJECT_HOLLY, BAND_APAC, PHASE_SP_AP1,     SIM_DUAL},
  {0x43, PROJECT_HOLLY, BAND_APAC, PHASE_AP2,     SIM_DUAL},
  {0x44, PROJECT_HOLLY, BAND_APAC, PHASE_TP1_TP2,     SIM_DUAL},
  {0x45, PROJECT_HOLLY, BAND_APAC, PHASE_PQ,     SIM_DUAL},
  {0x46, PROJECT_HOLLY, BAND_APAC, PHASE_MP,     SIM_DUAL},
  {0x4f, PROJECT_HOLLY, BAND_APAC, PHASE_EVM2,     SIM_DUAL},
  //===============BAND_SAMBA
  {0x50, PROJECT_HOLLY, BAND_SAMBA, PHASE_PD,     SIM_SINGLE},
  {0x51, PROJECT_HOLLY, BAND_SAMBA, PHASE_DP,     SIM_DUAL},
  {0x52, PROJECT_HOLLY, BAND_SAMBA, PHASE_SP_AP1,     SIM_DUAL},
  {0x53, PROJECT_HOLLY, BAND_SAMBA, PHASE_AP2_AP3,     SIM_DUAL},
  {0x54, PROJECT_HOLLY, BAND_SAMBA, PHASE_TP1_TP2,     SIM_DUAL},
  {0x55, PROJECT_HOLLY, BAND_SAMBA, PHASE_PQ,     SIM_DUAL},
  {0x56, PROJECT_HOLLY, BAND_SAMBA, PHASE_MP,     SIM_DUAL},
  //===============BAND_GINA
  {0x61, PROJECT_HOLLY, BAND_GINA, PHASE_DP,     SIM_SINGLE},
  {0x62, PROJECT_HOLLY, BAND_GINA, PHASE_SP_AP1,     SIM_SINGLE},
  {0x63, PROJECT_HOLLY, BAND_GINA, PHASE_AP2,     SIM_SINGLE},
  {0x64, PROJECT_HOLLY, BAND_GINA, PHASE_TP1_TP2,     SIM_SINGLE},
  {0x65, PROJECT_HOLLY, BAND_GINA, PHASE_PQ,     SIM_SINGLE},
  {0x66, PROJECT_HOLLY, BAND_GINA, PHASE_MP,     SIM_SINGLE},
  //===============BAND_GINA2
  {0x92, PROJECT_HOLLY, BAND_GINA2, PHASE_SP5,     SIM_DUAL},
  //===============BAND_REX2
  {0xa2, PROJECT_HOLLY, BAND_REX2, PHASE_SP5,     SIM_SINGLE},
  //===============BAND_APAC2
  {0xb2, PROJECT_HOLLY, BAND_APAC2, PHASE_SP5,     SIM_SINGLE},
  //===============BAND_APAC2
  {0xc2, PROJECT_HOLLY, BAND_APAC2, PHASE_SP5,     SIM_DUAL},
  //===============BAND_SAMBA2
  {0xd2, PROJECT_HOLLY, BAND_SAMBA2, PHASE_SP5,     SIM_DUAL},
  //===============BAND_GINA2
  {0xe2, PROJECT_HOLLY, BAND_GINA2, PHASE_SP5,     SIM_SINGLE},
};

static unsigned int str_idx[4]={0};

fih_product_id_type FIH_PROJECT_ID = PROJECT_HOLLY;
fih_product_phase_type FIH_PHASE_ID = PHASE_EVM;
fih_band_id_type FIH_BAND_ID = BAND_GINA;
fih_sim_type FIH_SIM_ID = SIM_SINGLE;


unsigned int fih_get_product_id(void)
{
  return FIH_PROJECT_ID;
}
EXPORT_SYMBOL(fih_get_product_id);

unsigned int fih_get_product_phase(void)
{
  return FIH_PHASE_ID;
}
EXPORT_SYMBOL(fih_get_product_phase);

unsigned int fih_get_band_id(void)
{
  return FIH_BAND_ID;
}
EXPORT_SYMBOL(fih_get_band_id);

unsigned int fih_get_sim_id(void)
{
  return FIH_SIM_ID;
}
EXPORT_SYMBOL(fih_get_sim_id);



static const signed int pin_eint_map[MT_GPIO_BASE_MAX] = {

};
#ifndef CONFIG_MTK_FPGA 
static int mtk_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return mt_set_gpio_mode_base(offset, 0);
}

static int mtk_get_gpio_direction(struct gpio_chip *chip, unsigned offset)
{
	return 1 - mt_get_gpio_dir_base(offset);
}

static int mtk_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return mt_set_gpio_dir_base(offset, 0);
}

static int mtk_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	int status = mt_get_gpio_dir_base(offset);
	if (status == 0)
		return mt_get_gpio_in_base(offset);
	else if (status == 1)
		return mt_get_gpio_out_base(offset);
	return 1;
}

static int mtk_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	return mt_set_gpio_dir_base(offset, 1);
}

static void mtk_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	mt_set_gpio_out_base(offset, value);
}

static int mtk_gpio_to_irq(struct gpio_chip *chip, unsigned pin)
{
	return 0;
}

static int mtk_gpio_set_debounce(struct gpio_chip *chip, unsigned offset, unsigned debounce)
{
	mt_eint_set_hw_debounce(offset, debounce >> 10);
	return 0;
}
#else //CONFIG_MTK_FPGA
static int mtk_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int mtk_get_gpio_direction(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int mtk_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int mtk_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int mtk_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	return 0;
}

static void mtk_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	return;
}

static int mtk_gpio_to_irq(struct gpio_chip *chip, unsigned pin)
{
	return 0;
}

static int mtk_gpio_set_debounce(struct gpio_chip *chip, unsigned offset, unsigned debounce)
{
	return 0;
}
#endif//CONFIG_MTK_FPGA


void id2str_map(void)
{
  int i;

  for(i=0; project_id_map[i].ID != PROJECT_MAX; i++)
  {
    if(project_id_map[i].ID == FIH_PROJECT_ID)
    {
      str_idx[0]=i;
      break;
    }
  }

  for(i=0; band_id_map[i].ID != BAND_MAX; i++)
  {
    if(band_id_map[i].ID == FIH_BAND_ID)
    {
      str_idx[1]=i;
      break;
    }
  }

  for(i=0; phase_id_map[i].ID != PHASE_MAX; i++)
  {
    if(phase_id_map[i].ID == FIH_PHASE_ID)
    {
      str_idx[2]=i;
      break;
    }
  }

  for(i=0; sim_id_map[i].ID != SIM_MAX; i++)
  {
    if(sim_id_map[i].ID == FIH_SIM_ID)
    {
      str_idx[3]=i;
      break;
    }
  }
}


void hwid_info(void)
{
  id2str_map();

  pr_info("FIH_PROJECT: %s\r\n",project_id_map[str_idx[0]].STR);
  pr_info("FIH_BAND:    %s\r\n",band_id_map[str_idx[1]].STR);
  pr_info("FIH_PHASE:   %s\r\n",phase_id_map[str_idx[2]].STR);
  pr_info("FIH_SIM:     %s\r\n",sim_id_map[str_idx[3]].STR);
}

unsigned int hwid_map(void)
{
  int i,j=0, hwid_logic=0;
  int rc=-1;

  for(i=141; i<149; i++)
  {
    hwid_logic +=( ( mt_get_gpio_in(i) ) << j );
	j=j+1;
  }
  pr_info("HWID_LOGIC 0x%x\r\n",hwid_logic);

  HWID_MAP=HWID_MAP_MT6795;
  pr_info("CPU MT6795 HWID MAP\r\n");


  for(i=0; HWID_MAP[i].HWID_LOGIC!=0xff; i++)
  {
    if(hwid_logic==HWID_MAP[i].HWID_LOGIC)
    {
      FIH_PROJECT_ID = HWID_MAP[i].PROJECT;
      FIH_BAND_ID    = HWID_MAP[i].BAND;
      FIH_PHASE_ID   = HWID_MAP[i].PHASE;
      FIH_SIM_ID     = HWID_MAP[i].SIM;
      rc=0;
      break;
    }
  }

  return rc;
}

void fih_hwid_get(void)
{
  pr_info("======FXN HWID init start======\r\n");

  if(hwid_map())
  {
    pr_info("******map fail use default HWID******\r\n");
  }

  hwid_info();

  pr_info("================================\r\n");

}


static struct gpio_chip mtk_gpio_chip = {
	.label = "mtk-gpio",
	.request = mtk_gpio_request,
	.get_direction = mtk_get_gpio_direction,
	.direction_input = mtk_gpio_direction_input,
	.get = mtk_gpio_get,
	.direction_output = mtk_gpio_direction_output,
	.set = mtk_gpio_set,
	.base = MT_GPIO_BASE_START,
	.ngpio = MT_GPIO_BASE_MAX,
	.to_irq = mtk_gpio_to_irq,
	.set_debounce = mtk_gpio_set_debounce,
};

static int __init mtk_gpio_init(void)
{
	int i=9;
	int val=9;
	printk("XXXXXX mtk_gpio_init[\r\n");
    for(i=141; i<149; i++) {
		val = mt_get_gpio_in(i);
		printk("GPIO %d XXXXXX %d\n",i, val);
	}

	fih_hwid_get();
	
	printk("XXXXXX mtk_gpio_init]\r\n");
	
	
	return gpiochip_add(&mtk_gpio_chip);
}
postcore_initcall(mtk_gpio_init);
