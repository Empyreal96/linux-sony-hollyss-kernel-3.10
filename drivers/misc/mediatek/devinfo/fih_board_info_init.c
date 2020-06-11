/*
* Copyright(C) 2011-2015 Foxconn International Holdings, Ltd. All rights reserved
*/

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>

/*Header file which defined by FIH*/
#include <linux/fih_hw_info.h>
#include <linux/version_host.h>
#include <linux/version_nonhlos.h>


/*
   Get and print the MTK AP image's version information
*/
void fih_get_HLOS_info(void)
{
    char   fih_host_version[32];
    snprintf(fih_host_version, sizeof(fih_host_version), "%s.%s.%s.%s",
               VER_HOST_BSP_VERSION,
               VER_HOST_PLATFORM_NUMBER,
               VER_HOST_BRANCH_NUMBER,
               VER_HOST_BUILD_NUMBER);

    pr_info("=======================================================\r\n");
    pr_info("FIH HLOS version = %s \r\n",fih_host_version);
    pr_info("FIH HLOS git head = %s \r\n",VER_HOST_GIT_COMMIT);
	pr_info("=======================================================\r\n");
}

/*
   Get and print the MTK MD image's version information
*/
void fih_get_NONHLOS_info(void)
{
    char   fih_modem_version[32];
    snprintf(fih_modem_version, sizeof(fih_modem_version), "%s.%s.%s.%s",
               VER_NONHLOS_BSP_VERSION,
               VER_NONHLOS_PLATFORM_NUMBER,
               VER_NONHLOS_BRANCH_NUMBER,
               VER_NONHLOS_BUILD_NUMBER);

    pr_info("=======================================================\r\n");
    pr_info("FIH NON-HLOS version = %s \r\n",fih_modem_version);
    pr_info("FIH NON-HLOS git head = %s \r\n",VER_NONHLOS_GIT_COMMIT);
	pr_info("=======================================================\r\n");
}


/*
   1. Get and print the version of HLOS
   2. Create the FIH's PROC entry
*/

int __init fih_board_info_init(void)
{
	pr_info("fih_board_info_init() start!!!\r\n");
	fih_get_HLOS_info();
	fih_get_NONHLOS_info();
	fih_proc_init();
	pr_info("fih_board_info_init() finish!!!\r\n");
	return 0;
}
subsys_initcall(fih_board_info_init);
