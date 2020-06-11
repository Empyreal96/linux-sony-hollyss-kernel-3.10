/*+===========================================================================
  Copyright(c) 2012 FIH Corporation. All Rights Reserved.

  File:
        version_nonhlos.h

  Description:
        This header file contains version number definitions of 
        non-hlos side cpus image - for SCM and development purpose
        only

  Author:
        Bruce Chiu 2012-11-23
        Jimmy Sung 2012-07-05

============================================================================+*/

#ifndef VERSION_NONHLOS_H
#define VERSION_NONHLOS_H

// NON-HLOS version format: v<bsp version>.<platform number>.<branch name>.<build number>.31a06c90cb7eb9d26f7fefdbdd4d5906abe16000
#define VER_NONHLOS_BSP_VERSION      "6795"          //(5 digits, Dec.)
#define VER_NONHLOS_PLATFORM_NUMBER  "60"            //(2 digits, Dec.), i.e. 2.0/2.1(Eclair);2.2(Froyo), ...
#define VER_NONHLOS_BRANCH_NUMBER    "00"            //(2 digits, Dec.)
#define VER_NONHLOS_BUILD_NUMBER     "604181"    //(6 digits, Dec.)
#define VER_NONHLOS_GIT_COMMIT       "dc137c4403bfee99092d9b3fb93ab1b75a89c485"  //(40 digits, Hex.)

#endif // #ifndef VERSION_NONHLOS_H

