/*
 * Copyright(C) 2015 Foxconn International Holdings, Ltd. All rights reserved.
 */ 

#include <linux/kernel.h>
#include <linux/string.h>

#include <mach/mtk_rtc.h>
#include <mach/mt_rtc_hw.h>
#include <mach/pmic_mt6329_sw.h>
#include <mach/wd_api.h>

/* CORE-TH-S1_FOTA_Patch-00+[ */
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
/* CORE-TH-S1_FOTA_Patch-00+] */

extern void wdt_arch_reset(char);

/* CORE-TH-S1_FOTA_Patch-00+[ */
#if 0
static void *remap_lowmem(phys_addr_t start, phys_addr_t size)
{
	struct page **pages;
	phys_addr_t page_start;
	unsigned int page_count;
	pgprot_t prot;
	unsigned int i;
	void *vaddr;

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);

	prot = pgprot_noncached(PAGE_KERNEL);

	pages = kmalloc(sizeof(struct page *) * page_count, GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n", __func__, page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = page_start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	vaddr = vmap(pages, page_count, VM_MAP, prot);
	kfree(pages);
	if (!vaddr) {
		pr_err("%s: Failed to map %u pages\n", __func__, page_count);
		return NULL;
	}

	return vaddr + offset_in_page(start);
}
#endif	
/* CORE-TH-S1_FOTA_Patch-00+] */

void * get_warmboot_addr(void); /* CORE-TH-S1_FOTA_Warmboot-00+ */

void arch_reset(char mode, const char *cmd)
{
	char reboot = 0;
	int res = 0;
	struct wd_api *wd_api = NULL;
	/* CORE-TH-S1_FOTA_Patch-00+[ */
	unsigned int *warmboot_addr_p;
	/* CORE-TH-S1_FOTA_Patch-00+] */

	res = get_wd_api(&wd_api);
	pr_notice("arch_reset: cmd = %s\n", cmd ? : "NULL");

	if (cmd && !strcmp(cmd, "charger")) {
		/* do nothing */
/* FXN-CORE-DY-No_Recovery_Mode-[ */
/*	} else if (cmd && !strcmp(cmd, "recovery")) { */
/*		rtc_mark_recovery(); */
/* FXN-CORE-DY-No_Recovery_Mode-] */
	} else if (cmd && !strcmp(cmd, "bootloader")) {
		rtc_mark_fast();
	}
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
	else if (cmd && !strcmp(cmd, "kpoc")) {
		rtc_mark_kpoc();
	}
#endif
/*
Note 2. Magic and reason should be store like that:
        Write: (reason[0:3] << 4 | magic[0:3]) in RTC_AL_HOU[8:15]
               (reason[4:7] << 4 | magic[4:7]) in RTC_AL_DOM[8:15]
               (reason[8:11] << 4 | magic[8:11]) in RTC_AL_DOW[8:15]
               (reason[12:15] << 4 | magic[12:15]) in RTC_SPAR0[8:15]

Include mach/mt_rtc_hw.h
#define RTC_BASE   0x4000
XX#define RTC_AL_HOU (RTC_BASE + 0x001c)
#define RTC_AL_MTH (RTC_BASE + 0x0022)
#define RTC_AL_DOM (RTC_BASE + 0x001e)
#define RTC_AL_DOW (RTC_BASE + 0x0020)
#define RTC_SPAR0  (RTC_BASE + 0x0030)

You should run busy wait first to make sure the register already been writen,
Read 0x4000 bit 6 til it is read as 0,

Reload process require you to 
1.      write register[0x4000] to 0x432d
2.      write register[0x403c] to 1
*/
	/* CORE-TH-S1_FOTA_Patch-00+[ */
	else if (cmd && !strcmp(cmd, "oemF")){
		/* CORE-TH-S1_FOTA_Warmboot-00+ */
		//warmboot_addr_p=remap_lowmem(0x1011A008, sizeof(*warmboot_addr_p));
		warmboot_addr_p = (unsigned int*)get_warmboot_addr();
		/* CORE-TH-S1_FOTA_Warmboot-00- */
		pr_notice("[BACAL] arch_reset: Warmboot reasen= %s\n", cmd);
                if(warmboot_addr_p){
                    unsigned int val = 0;

		    *warmboot_addr_p=0x6f46beef;
		    pmic_config_interface(0x4022, 0x6f, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }

		    pmic_config_interface(0x401e, 0x4e, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }

		    pmic_config_interface(0x4020, 0xfe, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }

		    pmic_config_interface(0x4030, 0x6b, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }


		    pmic_config_interface(0x4000, 0x432d, 0xffff, 0);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }

		    pmic_read_interface(0x4022, &val, 0xff, 8);
		    pr_err("[0x4022] = %x\n", val);
		    pmic_read_interface(0x401e, &val, 0xff, 8);
		    pr_err("[0x401e] = %x\n", val);
		    pmic_read_interface(0x4020, &val, 0xff, 8);
		    pr_err("[0x4020] = %x\n", val);
		    pmic_read_interface(0x4030, &val, 0xff, 8);
		    pr_err("[0x4030] = %x\n", val);
		}
	}
	else {
		/* CORE-TH-S1_FOTA_Warmboot-00+ */
		//warmboot_addr_p= remap_lowmem(0x1011A008, sizeof(*warmboot_addr_p));
		warmboot_addr_p = (unsigned int*)get_warmboot_addr();
		/* CORE-TH-S1_FOTA_Warmboot-00- */
		pr_notice("[BACAL] arch_reset: Warmboot reasen= %s\n", cmd);
		if(warmboot_addr_p){
                    unsigned int val = 0;

		    *warmboot_addr_p=0x7651beef;
		    pmic_config_interface(0x4022, 0x1f, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }


		    pmic_config_interface(0x401e, 0x5e, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }


		    pmic_config_interface(0x4020, 0x6e, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }


		    pmic_config_interface(0x4030, 0x7b, 0xff, 8);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }

		    pmic_config_interface(0x4000, 0x432d, 0xffff, 0);
		    pmic_config_interface(0x403c, 0x1, 0x1, 0);
		    while (1) {
		        pmic_read_interface(0x4000, &val, 0x1, 6);
                        if(!val) break;
		    }

		    pmic_read_interface(0x4022, &val, 0xff, 8);
		    pr_err("[0x4022] = %x\n", val);
		    pmic_read_interface(0x401e, &val, 0xff, 8);
		    pr_err("[0x401e] = %x\n", val);
		    pmic_read_interface(0x4020, &val, 0xff, 8);
		    pr_err("[0x4020] = %x\n", val);
		    pmic_read_interface(0x4030, &val, 0xff, 8);
		    pr_err("[0x4030] = %x\n", val);
		}
    	reboot = 1;
	}
	/* CORE-TH-S1_FOTA_Patch-00+] */


	if (res) {
		pr_notice("arch_reset, get wd api error %d\n", res);
	} else {
		wd_api->wd_sw_reset(reboot);
	}
}
