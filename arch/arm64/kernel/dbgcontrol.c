#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kallsyms.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <asm/uaccess.h>
#include <asm/cputype.h>
#include <linux/seq_file.h>
#include <linux/rtpm_prio.h>

#if defined(CONFIG_ARM_PSCI) || (CONFIG_ARM64)
//#include "../../../drivers/misc/mediatek/mach/mt6752/include/mach/mt_secure_api.h"
#endif
//#include <mach/smp.h>


#define TA_RCA_CONTROL_BITS_NO            64000

#define LOCAL_TAG "[dbgcontrol]"
#define BUFSIZE 128



//bit0 =1 : add /proc/dbgcontrol/xxx
//bit1 =1 : force output uart log
//bit2 =1 : disable ramdump feature

#define DBG_CONTROL_DEBUG_FEATURE_ENABLE    1
#define DBG_CONTROL_UART_ENABLE       (1 << 1)
#define DBG_CONTROL_DISABLE_RAMDUMP       (1 << 2)

static int debug_feature_enable = 0;
int debugcontrol_uart_disable = 1;

#if defined(CONFIG_MTK_AEE_MRDUMP)
extern bool mrdump_enable;
#endif

static int local_option = 0;

static int __init debugcontrol_cmdline_parameter_parser(char *str)
{

    if ((str != NULL) && (*str != '\0'))
    {
        local_option = simple_strtoul(str, NULL, 16);
        printk("[debugcontrol] Parsed option = 0x%x \n", local_option);

        if (local_option & DBG_CONTROL_DEBUG_FEATURE_ENABLE)
            debug_feature_enable = 1;

        if (local_option & DBG_CONTROL_UART_ENABLE)
            debugcontrol_uart_disable = 0;

#if defined(CONFIG_MTK_AEE_MRDUMP)
        if (local_option & DBG_CONTROL_DISABLE_RAMDUMP)
            mrdump_enable = 0;
#endif

    }
    return 0;
}

//early_param("oemandroidboot.babe09a9", debugcontrol_cmdline_parameter_parser);
early_param("oemandroidboot.babefa00", debugcontrol_cmdline_parameter_parser);


static struct proc_dir_entry *dbgcontrol_proc_dir;

#define  ADD_PROC_ENTRY(name, entry, mode)\
  if (!proc_create(#name, S_IFREG | S_IROTH | S_IWOTH | S_IRGRP | S_IWGRP |mode, dbgcontrol_proc_dir, &proc_##entry##_fops)) \
    printk("proc_create %s failed\n", #name)

#define ADD_FILE_OPS(entry) \
  static const struct file_operations proc_##entry##_fops = { \
    .open = proc_##entry##_open, \
    .read = seq_read, \
    .llseek = seq_lseek, \
    .release= single_release, \
  }



#ifdef __aarch64__
#undef BUG
#define BUG() *((unsigned *)0xaed) = 0xDEAD
#endif


//generate-modem
extern void dbgcontrol_assert_modem(void);
__weak void dbgcontrol_assert_modem(void){};

static int generate_modem_proc_show(struct seq_file *m, void *v)
{
   seq_printf(m,"%s \n","modem crash Generated!\n");
   printk("%s:This modem crash is trigged by dbgcontrol.Please ignore it.\n",LOCAL_TAG);
   dbgcontrol_assert_modem();
   return 0;
}

static int proc_generate_modem_open(struct inode *inode, struct file *file)
{
    return single_open(file, generate_modem_proc_show, NULL);
}


//generate-KE
static int generate_KE_proc_show(struct seq_file *m, void *v)
{
   seq_printf(m,"%s \n","KE Generated!\n");
   printk("%s:This KE is trigged by dbgcontrol.Please ignore it.\n",LOCAL_TAG);
   BUG();
   return 0;
}

static int proc_generate_KE_open(struct inode *inode, struct file *file)
{
   return single_open(file, generate_KE_proc_show, NULL);
}


//generate-WDT
static struct task_struct *WDT_wk_tsk[NR_CPUS];
static int kwdt_thread_handle(void *arg)
{
   int cpu;
   u64 mpidr;
   static int preempt_already = 0;
   struct sched_param param = {.sched_priority = RTPM_PRIO_WDT };
   mpidr = read_cpuid_mpidr();
   cpu = (mpidr & 0xff) + ((mpidr & 0xff00)>>6);
   sched_setscheduler(current, SCHED_FIFO, &param);
   set_current_state(TASK_INTERRUPTIBLE);
   printk("%s:kwdt_thread_handle:cpu %d.\n",LOCAL_TAG,cpu);
   msleep(1000);

  //if(cpu == 0)
   if(!preempt_already) {
     printk("%s:kwdt_thread_handle:lock cpu %d.\n",LOCAL_TAG,cpu);
     preempt_already = 1;
     preempt_disable();
     //local_irq_disable();
     while (1);
   } else {
      printk("%s:kwdt_thread_handle: skip cpu %d.\n",LOCAL_TAG,cpu);
   }
   return 0;
}

static int trigger_WDT(void)
{
  int i;
  char name[BUFSIZE];
  printk("%s:This WDT is trigged by dbgcontrol.Please ignore it.\n",LOCAL_TAG);
  for (i = 0; i < nr_cpu_ids; i++) {
    sprintf(name, "dc-wdt-%d", i);
    printk("%s:[WDT]thread name: %s\n",LOCAL_TAG, name);
    WDT_wk_tsk[i] = kthread_create(kwdt_thread_handle, NULL, name);
    if (IS_ERR(WDT_wk_tsk[i])) {
        int ret = PTR_ERR(WDT_wk_tsk[i]);
        WDT_wk_tsk[i] = NULL;
        return ret;
       }
       kthread_bind(WDT_wk_tsk[i], i);
     }

   for (i = 0; i < nr_cpu_ids; i++) {
       printk("%s:wake_up_process(WDT_wk_tsk[%d])\n",LOCAL_TAG,i);
       wake_up_process(WDT_wk_tsk[i]);
     }
    return 0;
}


static int generate_WDT_proc_show(struct seq_file *m, void *v)
{
   seq_printf(m,"%s \n","WDT Generated!\n");
   trigger_WDT();

    return 0;
}

static int proc_generate_WDT_open(struct inode *inode, struct file *file)
{
   return single_open(file, generate_WDT_proc_show, NULL);
}


//generate-HWREBOOT
static struct task_struct *HWREBOOT_wk_tsk[NR_CPUS];
void notrace dbgcontrol_wdt_atf_hang(void)
{
    int cpu;
    u64 mpidr;
    mpidr = read_cpuid_mpidr();
    cpu = (mpidr & 0xff) + ((mpidr & 0xff00)>>6);
    printk("%s:CPU %d : dbgcontrol_wdt_atf_hang\n",LOCAL_TAG, cpu);
    local_fiq_disable();
    preempt_disable();
    local_irq_disable();
    while (1);
}

#define DEBUGCONTROL_MTK_SIP_KERNEL_WDT                  0x82000204
static noinline int dbgcontrol_secure_call(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
{
    register u64 reg0 __asm__("x0") = function_id;
    register u64 reg1 __asm__("x1") = arg0;
    register u64 reg2 __asm__("x2") = arg1;
    register u64 reg3 __asm__("x3") = arg2;
    int ret = 0;

    asm volatile(
        "smc    #0\n"
        : "+r" (reg0)
        : "r" (reg1), "r" (reg2), "r" (reg3));

    ret = (int)reg0;
    return ret;
}


static int khwreboot_thread_test(void *arg)
{
  int cpu;
  u64 mpidr;
  struct sched_param param = {.sched_priority = RTPM_PRIO_WDT };
  mpidr = read_cpuid_mpidr();
  cpu = (mpidr & 0xff) + ((mpidr & 0xff00)>>6);
  printk("%s:CPU %d : khwreboot_thread_test\n",LOCAL_TAG, cpu);
  sched_setscheduler(current, SCHED_FIFO, &param);
  set_current_state(TASK_INTERRUPTIBLE);
  msleep(1000);
  local_fiq_disable();
  preempt_disable();
  local_irq_disable();
  while (1);
  return 0;
}


static int trigger_HWREBOOT(void)
{
    int i;
    char name[BUFSIZE];
    printk("%s:This HWREBOOT is trigged by dbgcontrol.Please ignore it.\n",LOCAL_TAG);

#ifdef CONFIG_ARM64
        dbgcontrol_secure_call(DEBUGCONTROL_MTK_SIP_KERNEL_WDT, (u64)&dbgcontrol_wdt_atf_hang, 0, 0);
#endif
#ifdef CONFIG_ARM_PSCI
       dbgcontrol_secure_call(DEBUGCONTROL_MTK_SIP_KERNEL_WDT, (u32)&dbgcontrol_wdt_atf_hang, 0, 0);
#endif

  for (i = 0; i < nr_cpu_ids; i++) {
     sprintf(name, "dc-hwreboot-%d", i);
     printk("%s:[HWREBOOT]thread name: %s\n",LOCAL_TAG, name);
     HWREBOOT_wk_tsk[i] = kthread_create(khwreboot_thread_test, NULL, name);
     if (IS_ERR(HWREBOOT_wk_tsk[i])) {
         int ret = PTR_ERR(HWREBOOT_wk_tsk[i]);
         HWREBOOT_wk_tsk[i] = NULL;
         return ret;
        }
        kthread_bind(HWREBOOT_wk_tsk[i], i);
      }

    for (i = 0; i < nr_cpu_ids; i++) {
        printk("%s:wake_up_process(HWREBOOT_wk_tsk[%d])\n",LOCAL_TAG,i);
        wake_up_process(HWREBOOT_wk_tsk[i]);
      }
     return 0;
}


static int generate_HWREBOOT_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m,"%s \n","HWREBOOT Generated!\n");
    trigger_HWREBOOT();
     return 0;
}

static int proc_generate_HWREBOOT_open(struct inode *inode, struct file *file)
{
   return single_open(file, generate_HWREBOOT_proc_show, NULL);
}


//nested panic
static int dbgcontorl_nested_panic(struct notifier_block *this, unsigned long event, void *ptr)
{
     printk("%s => force nested panic:\n",LOCAL_TAG);
     BUG();
     return 0;
}

static struct notifier_block dbgcontorl_panic_blk = {
       .notifier_call = dbgcontorl_nested_panic,
       .priority = INT_MAX - 100,
};


static int generate_nested_KE_proc_show(struct seq_file *m, void *v)
{
    atomic_notifier_chain_register(&panic_notifier_list, &dbgcontorl_panic_blk);
    //register_die_notifier(&dbgcontorl_panic_blk);
    printk("%s => panic_notifier_list registered\n",LOCAL_TAG);
    BUG();
    return 0;
}

static int proc_generate_nested_KE_open(struct inode *inode, struct file *file)
{
   return single_open(file, generate_nested_KE_proc_show, NULL);
}



//hwreboot in wdt
static int dbgcontorl_HWREBOOT_panic(struct notifier_block *this, unsigned long event, void *ptr)
{
     printk("%s => force HWREBOOT panic:\n",LOCAL_TAG);
     trigger_HWREBOOT();
     return 0;
}

static struct notifier_block dbgcontorl_hwreboot_blk = {
     .notifier_call = dbgcontorl_HWREBOOT_panic,
     .priority = INT_MAX - 100,
};

static void trigger_HWREBOOT_in_WDT(void)
{
   register_die_notifier(&dbgcontorl_hwreboot_blk);
   trigger_WDT();
}


static int generate_HWREBOOT_in_WDT_show(struct seq_file *m, void *v)
{
    trigger_HWREBOOT_in_WDT();
    return 0;
}

static int proc_generate_HWREBOOT_in_WDT_open(struct inode *inode, struct file *file)
{
   return single_open(file, generate_HWREBOOT_in_WDT_show, NULL);
}



//get status
static int get_status_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m,"Mista value[%d] :[0x%x]\n",TA_RCA_CONTROL_BITS_NO,local_option);
    return 0;
}

static int proc_get_status_open(struct inode *inode, struct file *file)
{
   return single_open(file, get_status_proc_show, NULL);
}


#if 0

//dbgcontrol device
static int dbgcontrol_open(struct inode *ip, struct file *fp)
{
    return 0;
}

static int dbgcontrol_release(struct inode *ip, struct file *fp)
{
    return 0;
}

static long dbgcontrol_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
   return 0;
}

static ssize_t dbgcontrol_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
  return count;
}

static ssize_t dbgcontrol_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
  return 0;
}

static struct file_operations dbgcontrol_fops = {
    .owner = THIS_MODULE,
    .read = dbgcontrol_read,
    .write = dbgcontrol_write,
    .open = dbgcontrol_open,
    .release = dbgcontrol_release,
    .unlocked_ioctl = dbgcontrol_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = dbgcontrol_ioctl,
#endif
};

static struct miscdevice dbgcontrol_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "dbgcontrol",
    .fops = &dbgcontrol_fops,
};

#endif


ADD_FILE_OPS(generate_modem);
ADD_FILE_OPS(generate_KE);
ADD_FILE_OPS(generate_WDT);
ADD_FILE_OPS(generate_HWREBOOT);
ADD_FILE_OPS(generate_nested_KE);
ADD_FILE_OPS(generate_HWREBOOT_in_WDT);
ADD_FILE_OPS(get_status);


static int __init dbgcontrol_init(void)
{

   //misc_register(&dbgcontrol_dev);

   if(debug_feature_enable) {

    dbgcontrol_proc_dir = proc_mkdir("dbgcontrol", NULL);
    if (dbgcontrol_proc_dir == NULL) {
       printk("%s:dbgcontrol proc_mkdir failed\n",LOCAL_TAG);
       return -ENOMEM;
    }

    ADD_PROC_ENTRY(generate-modem, generate_modem, S_IRUSR | S_IWUSR);
    ADD_PROC_ENTRY(generate-KE, generate_KE, S_IRUSR | S_IWUSR);
    ADD_PROC_ENTRY(generate-WDT, generate_WDT, S_IRUSR | S_IWUSR);
    ADD_PROC_ENTRY(generate-HWREBOOT, generate_HWREBOOT, S_IRUSR | S_IWUSR);
    ADD_PROC_ENTRY(generate-nested-KE, generate_nested_KE, S_IRUSR | S_IWUSR);
    ADD_PROC_ENTRY(generate-HWREBOOT-in-WDT, generate_HWREBOOT_in_WDT, S_IRUSR | S_IWUSR);
    ADD_PROC_ENTRY(get-status, get_status, S_IRUSR | S_IWUSR);
  }

    return 0;
}

static void __exit dbgcontrol_deinit( void )
{

      //misc_deregister(&dbgcontrol_dev);

     if(debug_feature_enable) {
         remove_proc_entry("generate-modem", dbgcontrol_proc_dir);
         remove_proc_entry("generate-KE", dbgcontrol_proc_dir);
         remove_proc_entry("generate-WDT", dbgcontrol_proc_dir);
         remove_proc_entry("generate-HWREBOOT", dbgcontrol_proc_dir);
         remove_proc_entry("generate-nested-KE", dbgcontrol_proc_dir);
         remove_proc_entry("generate-HWREBOOT-in-WDT", dbgcontrol_proc_dir);
         remove_proc_entry("get-status", dbgcontrol_proc_dir);
         remove_proc_entry("dbgcontrol", NULL);
      }
}



module_init(dbgcontrol_init);
module_exit(dbgcontrol_deinit);

MODULE_DESCRIPTION("dbgcontrol");
MODULE_LICENSE("GPL v2");




