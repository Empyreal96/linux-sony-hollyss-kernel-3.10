#include <linux/mrdump.h>
#include <mach/wd_api.h>
#include <linux/memblock.h>

#define LK_LOAD_ADDR (PHYS_OFFSET + 0x1E00000)
#define LK_LOAD_SIZE 0x300000
#define PRELOADER_ADDR (PHYS_OFFSET + 0x2100000)
#define PRELOADER_SIZE 0x200000

static void mrdump_hw_enable(bool enabled)
{
	struct wd_api *wd_api = NULL;
	get_wd_api(&wd_api);
	if (wd_api)
		wd_api->wd_dram_reserved_mode(enabled);
}

static void mrdump_reboot(void)
{
	int res;
	struct wd_api *wd_api = NULL;

	res = get_wd_api(&wd_api);
	if (res) {
		pr_alert("arch_reset, get wd api error %d\n", res);
		while (1)
			cpu_relax();
	} else {
		wd_api->wd_sw_reset(0);
	}
}

const struct mrdump_platform mrdump_v1_platform = {
	.hw_enable = mrdump_hw_enable,
	.reboot = mrdump_reboot
};

void mrdump_reserve_memory(void)
{
	memblock_reserve(PRELOADER_ADDR, PRELOADER_SIZE);
	memblock_reserve(LK_LOAD_ADDR, LK_LOAD_SIZE);
	mrdump_platform_init(NULL, &mrdump_v1_platform);
}
