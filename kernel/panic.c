/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (including mm and fs)
 * to indicate a major problem.
 */
#include <linux/debug_locks.h>
#include <linux/interrupt.h>
#include <linux/kmsg_dump.h>
#include <linux/kallsyms.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/kexec.h>
#include <linux/sched.h>
#include <linux/sysrq.h>
#include <linux/init.h>
#include <linux/nmi.h>
#include <linux/dmi.h>
#ifdef CONFIG_SAMSUNG_KERNEL_DEBUG
#include <asm/cacheflush.h>
#include <linux/smp.h>
#include <linux/percpu.h>

/* L1 & L2 cache management */
#include <asm/cacheflush.h>

void ux500_clean_l2_cache_all(void);

/* prcmu register dump */
void dbx500_dump_in_panic(void);
#endif /* CONFIG_SAMSUNG_KERNEL_DEBUG */

#define PANIC_TIMER_STEP 100
#define PANIC_BLINK_SPD 18

/* Machine specific panic information string */
char *mach_panic_string;

int panic_on_oops;
static unsigned long tainted_mask;
static int pause_on_oops;
static int pause_on_oops_flag;
static DEFINE_SPINLOCK(pause_on_oops_lock);

#ifndef CONFIG_PANIC_TIMEOUT
#define CONFIG_PANIC_TIMEOUT 0
#endif
int panic_timeout = CONFIG_PANIC_TIMEOUT;
EXPORT_SYMBOL_GPL(panic_timeout);

ATOMIC_NOTIFIER_HEAD(panic_notifier_list);

EXPORT_SYMBOL(panic_notifier_list);

static long no_blink(int state)
{
	return 0;
}

/* Returns how long it waited in ms */
long (*panic_blink)(int state);
EXPORT_SYMBOL(panic_blink);


#ifdef CONFIG_SAMSUNG_KERNEL_DEBUG
typedef struct tag_mmu_info {
	int SCTLR;
	int TTBR0;
	int TTBR1;
	int TTBCR;
	int DACR;
	int DFSR;
	int DFAR;
	int IFSR;
	int IFAR;
	int DAFSR;
	int IAFSR;
	int PMRRR;
	int NMRRR;
	int FCSEPID;
	int CONTEXT;
	int URWTPID;
	int UROTPID;
	int POTPIDR;
} t_kernel_sec_mmu_info;

/*ARM CORE regs mapping structure*/
typedef struct {
	/* COMMON */
	unsigned int r0;
	unsigned int r1;
	unsigned int r2;
	unsigned int r3;
	unsigned int r4;
	unsigned int r5;
	unsigned int r6;
	unsigned int r7;
	unsigned int r8;
	unsigned int r9;
	unsigned int r10;
	unsigned int r11;
	unsigned int r12;

	/* SVC */
	unsigned int r13_svc;
	unsigned int r14_svc;
	unsigned int spsr_svc;

	/* PC & CPSR */
	unsigned int pc;
	unsigned int cpsr;

	/* USR/SYS */
	unsigned int r13_usr;
	unsigned int r14_usr;

	/* FIQ */
	unsigned int r8_fiq;
	unsigned int r9_fiq;
	unsigned int r10_fiq;
	unsigned int r11_fiq;
	unsigned int r12_fiq;
	unsigned int r13_fiq;
	unsigned int r14_fiq;
	unsigned int spsr_fiq;

	/* IRQ */
	unsigned int r13_irq;
	unsigned int r14_irq;
	unsigned int spsr_irq;

	/* MON */
	unsigned int r13_mon;
	unsigned int r14_mon;
	unsigned int spsr_mon;

	/* ABT */
	unsigned int r13_abt;
	unsigned int r14_abt;
	unsigned int spsr_abt;

	/* UNDEF */
	unsigned int r13_und;
	unsigned int r14_und;
	unsigned int spsr_und;

} t_kernel_sec_arm_core_regsiters;

/*
__used t_kernel_sec_arm_core_regsiters kernel_sec_core_reg_dump;
__used t_kernel_sec_mmu_info kernel_sec_mmu_reg_dump;
*/
DEFINE_PER_CPU(t_kernel_sec_arm_core_regsiters, kernel_sec_core_reg_dump);
DEFINE_PER_CPU(t_kernel_sec_mmu_info, kernel_sec_mmu_reg_dump);

/* core reg dump function*/
void kernel_sec_get_core_reg_dump(t_kernel_sec_arm_core_regsiters * regs)
{
	asm(
		   // we will be in SVC mode when we enter this function. Collect SVC registers along with cmn registers.
		   "str r0, [%0,#0] \n\t"	// R0
		   "str r1, [%0,#4] \n\t"	// R1
		   "str r2, [%0,#8] \n\t"	// R2
		   "str r3, [%0,#12] \n\t"	// R3
		   "str r4, [%0,#16] \n\t"	// R4
		   "str r5, [%0,#20] \n\t"	// R5
		   "str r6, [%0,#24] \n\t"	// R6
		   "str r7, [%0,#28] \n\t"	// R7
		   "str r8, [%0,#32] \n\t"	// R8
		   "str r9, [%0,#36] \n\t"	// R9
		   "str r10, [%0,#40] \n\t"	// R10
		   "str r11, [%0,#44] \n\t"	// R11
		   "str r12, [%0,#48] \n\t"	// R12

		   /* SVC */
		   "str r13, [%0,#52] \n\t"	// R13_SVC
		   "str r14, [%0,#56] \n\t"	// R14_SVC
		   "mrs r1, spsr \n\t"		// SPSR_SVC
		   "str r1, [%0,#60] \n\t"

		   /* PC and CPSR */
		   "sub r1, r15, #0x4 \n\t"	// PC
		   "str r1, [%0,#64] \n\t"
		   "mrs r1, cpsr \n\t"		// CPSR
		   "str r1, [%0,#68] \n\t"

		   /* SYS/USR */
		   "mrs r1, cpsr \n\t"		// switch to SYS mode
		   "and r1, r1, #0xFFFFFFE0\n\t"
		   "orr r1, r1, #0x1f \n\t"
		   "msr cpsr,r1 \n\t"

		   "str r13, [%0,#72] \n\t"	// R13_USR
		   "str r14, [%0,#76] \n\t"	// R13_USR

		    /*FIQ*/
		    "mrs r1, cpsr \n\t"		// switch to FIQ mode
		   "and r1,r1,#0xFFFFFFE0\n\t"
		   "orr r1,r1,#0x11\n\t"
		   "msr cpsr,r1 \n\t"

		   "str r8, [%0,#80] \n\t"	// R8_FIQ
		   "str r9, [%0,#84] \n\t"	// R9_FIQ
		   "str r10, [%0,#88] \n\t"	// R10_FIQ
		   "str r11, [%0,#92] \n\t"	// R11_FIQ
		   "str r12, [%0,#96] \n\t"	// R12_FIQ
		   "str r13, [%0,#100] \n\t"	// R13_FIQ
		   "str r14, [%0,#104] \n\t"	// R14_FIQ
		   "mrs r1, spsr \n\t"		// SPSR_FIQ
		   "str r1, [%0,#108] \n\t"

		   /*IRQ*/
		   "mrs r1, cpsr \n\t"		// switch to IRQ mode
		   "and r1, r1, #0xFFFFFFE0\n\t"
		   "orr r1, r1, #0x12\n\t"
		   "msr cpsr,r1 \n\t"

		   "str r13, [%0,#112] \n\t"	// R13_IRQ
		   "str r14, [%0,#116] \n\t"	// R14_IRQ
		   "mrs r1, spsr \n\t"		// SPSR_IRQ
		   "str r1, [%0,#120] \n\t"

		    /*MON*/
		   "mrs r1, cpsr \n\t"		// switch to monitor mode
		   "and r1, r1, #0xFFFFFFE0\n\t"
		   "orr r1, r1, #0x16\n\t"
		   "msr cpsr,r1 \n\t"

		   "str r13, [%0,#124] \n\t"	// R13_MON
		   "str r14, [%0,#128] \n\t"	// R14_MON
		   "mrs r1, spsr \n\t"		// SPSR_MON
		   "str r1, [%0,#132] \n\t"

		   /*ABT*/
		   "mrs r1, cpsr \n\t"		// switch to Abort mode
		   "and r1, r1, #0xFFFFFFE0\n\t"
		   "orr r1, r1, #0x17\n\t"
		   "msr cpsr,r1 \n\t"

		   "str r13, [%0,#136] \n\t"	// R13_ABT
		   "str r14, [%0,#140] \n\t"	// R14_ABT
		   "mrs r1, spsr \n\t"		// SPSR_ABT
		   "str r1, [%0,#144] \n\t"
		   
		   /*UND*/
		   "mrs r1, cpsr \n\t"		// switch to undef mode
		   "and r1, r1, #0xFFFFFFE0\n\t"
		   "orr r1, r1, #0x1B\n\t"
		   "msr cpsr,r1 \n\t"

		   "str r13, [%0,#148] \n\t"	// R13_UND
		   "str r14, [%0,#152] \n\t"	// R14_UND
		   "mrs r1, spsr \n\t"		// SPSR_UND
		   "str r1, [%0,#156] \n\t"

		   /* restore to SVC mode */
		   "mrs r1, cpsr \n\t"		// switch to undef mode
		   "and r1, r1, #0xFFFFFFE0\n\t"
		   "orr r1, r1, #0x13\n\t"
		   "msr cpsr,r1 \n\t"
		   
	    :		/* output */
	    : "r"(regs)	/* input */
	    : "%r1"	/* clobbered register */
	    );

	return;
}

EXPORT_SYMBOL(kernel_sec_get_core_reg_dump);

int kernel_sec_get_mmu_reg_dump(t_kernel_sec_mmu_info * mmu_info)
{
// CR DAC TTBR0 TTBR1 TTBCR
	asm("mrc    p15, 0, r1, c1, c0, 0 \n\t"	//SCTLR
	    "str r1, [%0] \n\t"
	    "mrc    p15, 0, r1, c2, c0, 0 \n\t"	//TTBR0
	    "str r1, [%0,#4] \n\t"
	    "mrc    p15, 0, r1, c2, c0,1 \n\t"	//TTBR1
	    "str r1, [%0,#8] \n\t"
	    "mrc    p15, 0, r1, c2, c0,2 \n\t"	//TTBCR
	    "str r1, [%0,#12] \n\t"
	    "mrc    p15, 0, r1, c3, c0,0 \n\t"	//DACR
	    "str r1, [%0,#16] \n\t"

	    "mrc    p15, 0, r1, c5, c0,0 \n\t"	//DFSR
	    "str r1, [%0,#20] \n\t"
	    "mrc    p15, 0, r1, c6, c0,0 \n\t"	//DFAR
	    "str r1, [%0,#24] \n\t"
	    "mrc    p15, 0, r1, c5, c0,1 \n\t"	//IFSR
	    "str r1, [%0,#28] \n\t"
	    "mrc    p15, 0, r1, c6, c0,2 \n\t"	//IFAR
	    "str r1, [%0,#32] \n\t"
	    /*Dont populate DAFSR and RAFSR */
	    "mrc    p15, 0, r1, c10, c2,0 \n\t"	//PMRRR
	    "str r1, [%0,#44] \n\t"
	    "mrc    p15, 0, r1, c10, c2,1 \n\t"	//NMRRR
	    "str r1, [%0,#48] \n\t"
	    "mrc    p15, 0, r1, c13, c0,0 \n\t"	//FCSEPID
	    "str r1, [%0,#52] \n\t"
	    "mrc    p15, 0, r1, c13, c0,1 \n\t"	//CONTEXT
	    "str r1, [%0,#56] \n\t"
	    "mrc    p15, 0, r1, c13, c0,2 \n\t"	//URWTPID
	    "str r1, [%0,#60] \n\t"
	    "mrc    p15, 0, r1, c13, c0,3 \n\t"	//UROTPID
	    "str r1, [%0,#64] \n\t"
	    "mrc    p15, 0, r1, c13, c0,4 \n\t"	//POTPIDR
	    "str r1, [%0,#68] \n\t"

	    :			/* output */
	    : "r"(mmu_info)	/* input */
	    : "%r1", "memory"	/* clobbered register */
	    );
	return 0;
}

EXPORT_SYMBOL(kernel_sec_get_mmu_reg_dump);

void kernel_sec_save_final_context(void)
{
/*
	if (kernel_sec_get_mmu_reg_dump(&kernel_sec_mmu_reg_dump) < 0) {
		printk(KERN_EMERG
		       "(kernel_sec_save_final_context) kernel_sec_get_mmu_reg_dump faile.\n");
	}
	kernel_sec_get_core_reg_dump(&kernel_sec_core_reg_dump);
	printk(KERN_EMERG
	       "(kernel_sec_save_final_context) Final context was saved before the system reset.\n");
*/
//	unsigned long flags;
	
//	local_irq_save(flags);
	
	kernel_sec_get_mmu_reg_dump(&per_cpu
				(kernel_sec_mmu_reg_dump, smp_processor_id()));	
	kernel_sec_get_core_reg_dump(&per_cpu
				(kernel_sec_core_reg_dump, smp_processor_id()));
	printk(KERN_EMERG
	       "(%s) context saved(CPU:%d)\n", __func__, smp_processor_id());
	       
//	local_irq_restore(flags);
}

EXPORT_SYMBOL(kernel_sec_save_final_context);

#endif	/* CONFIG_SAMSUNG_KERNEL_DEBUG */

/**
 *	panic - halt the system
 *	@fmt: The text string to print
 *
 *	Display a message, then perform cleanups.
 *
 *	This function never returns.
 */
NORET_TYPE void panic(const char * fmt, ...)
{
	static char buf[1024];
	va_list args;
	long i, i_next = 0;
	int state = 0;

	/*
	 * It's possible to come here directly from a panic-assertion and
	 * not have preempt disabled. Some functions called from here want
	 * preempt to be disabled. No point enabling it later though...
	 */
	preempt_disable();

	console_verbose();
	bust_spinlocks(1);
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printk(KERN_EMERG "Kernel panic - not syncing: %s\n",buf);
#ifdef CONFIG_DEBUG_BUGVERBOSE
	dump_stack();
#endif

	atomic_notifier_call_chain(&panic_notifier_list, 0, buf);

	/*
	 * If we have crashed and we have a crash kernel loaded let it handle
	 * everything else.
	 * Do we want to call this before we try to display a message?
	 */
	crash_kexec(NULL);

	kmsg_dump(KMSG_DUMP_PANIC);

	/*
	 * Note smp_send_stop is the usual smp shutdown function, which
	 * unfortunately means it may not be hardened to work in a panic
	 * situation.
	 */
	smp_send_stop();

#ifdef CONFIG_SAMSUNG_KERNEL_DEBUG
	
		/*kernel_sec_save_final_context();*/

	/* L1 & L2 cache management */
	flush_cache_all();

	if (cpu_is_u8500())
		ux500_clean_l2_cache_all();

	if (0 == strcmp(fmt,"__do_user_fault"))
	{
		printk(KERN_INFO "machine_restart: userlockup\n" ) ;
		machine_restart("UserLockUp");
	}
	else if (0 == strcmp(fmt, "__forced_upload"))
	{
		printk(KERN_INFO "machine_restart: __forced_upload\n" ) ;
		machine_restart("ForcedUpload");
	}
	else if (0 == strcmp(fmt, "__MMDSP_forced_upload"))
	{
		printk(KERN_INFO "machine_restart: __MMDSP_forced_upload\n" ) ;
		machine_restart("MMDSP_ForcedUpload");
	}
	else
	{
		printk(KERN_INFO "machine_restart: lockup\n" ) ;
		machine_restart("LockUp");
	}

	for (i = 0; i < 1*1000; i++) {
		touch_nmi_watchdog();
		mdelay(1);
	}
#endif /* CONFIG_SAMSUNG_KERNEL_DEBUG */

	bust_spinlocks(0);

	if (!panic_blink)
		panic_blink = no_blink;

	if (panic_timeout > 0) {
		/*
		 * Delay timeout seconds before rebooting the machine.
		 * We can't use the "normal" timers since we just panicked.
		 */
		printk(KERN_EMERG "Rebooting in %d seconds..", panic_timeout);

		for (i = 0; i < panic_timeout * 1000; i += PANIC_TIMER_STEP) {
			touch_nmi_watchdog();
			if (i >= i_next) {
				i += panic_blink(state ^= 1);
				i_next = i + 3600 / PANIC_BLINK_SPD;
			}
			mdelay(PANIC_TIMER_STEP);
		}
	}
	if (panic_timeout != 0) {
		/*
		 * This will not be a clean reboot, with everything
		 * shutting down.  But if there is a chance of
		 * rebooting the system it will be rebooted.
		 */
		emergency_restart();
	}
#ifdef __sparc__
	{
		extern int stop_a_enabled;
		/* Make sure the user can actually press Stop-A (L1-A) */
		stop_a_enabled = 1;
		printk(KERN_EMERG "Press Stop-A (L1-A) to return to the boot prom\n");
	}
#endif
#if defined(CONFIG_S390)
	{
		unsigned long caller;

		caller = (unsigned long)__builtin_return_address(0);
		disabled_wait(caller);
	}
#endif
	local_irq_enable();
	for (i = 0; ; i += PANIC_TIMER_STEP) {
		touch_softlockup_watchdog();
		if (i >= i_next) {
			i += panic_blink(state ^= 1);
			i_next = i + 3600 / PANIC_BLINK_SPD;
		}
		mdelay(PANIC_TIMER_STEP);
	}
}

EXPORT_SYMBOL(panic);


struct tnt {
	u8	bit;
	char	true;
	char	false;
};

static const struct tnt tnts[] = {
	{ TAINT_PROPRIETARY_MODULE,	'P', 'G' },
	{ TAINT_FORCED_MODULE,		'F', ' ' },
	{ TAINT_UNSAFE_SMP,		'S', ' ' },
	{ TAINT_FORCED_RMMOD,		'R', ' ' },
	{ TAINT_MACHINE_CHECK,		'M', ' ' },
	{ TAINT_BAD_PAGE,		'B', ' ' },
	{ TAINT_USER,			'U', ' ' },
	{ TAINT_DIE,			'D', ' ' },
	{ TAINT_OVERRIDDEN_ACPI_TABLE,	'A', ' ' },
	{ TAINT_WARN,			'W', ' ' },
	{ TAINT_CRAP,			'C', ' ' },
	{ TAINT_FIRMWARE_WORKAROUND,	'I', ' ' },
	{ TAINT_OOT_MODULE,		'O', ' ' },
};

/**
 *	print_tainted - return a string to represent the kernel taint state.
 *
 *  'P' - Proprietary module has been loaded.
 *  'F' - Module has been forcibly loaded.
 *  'S' - SMP with CPUs not designed for SMP.
 *  'R' - User forced a module unload.
 *  'M' - System experienced a machine check exception.
 *  'B' - System has hit bad_page.
 *  'U' - Userspace-defined naughtiness.
 *  'D' - Kernel has oopsed before
 *  'A' - ACPI table overridden.
 *  'W' - Taint on warning.
 *  'C' - modules from drivers/staging are loaded.
 *  'I' - Working around severe firmware bug.
 *  'O' - Out-of-tree module has been loaded.
 *
 *	The string is overwritten by the next call to print_tainted().
 */
const char *print_tainted(void)
{
	static char buf[ARRAY_SIZE(tnts) + sizeof("Tainted: ") + 1];

	if (tainted_mask) {
		char *s;
		int i;

		s = buf + sprintf(buf, "Tainted: ");
		for (i = 0; i < ARRAY_SIZE(tnts); i++) {
			const struct tnt *t = &tnts[i];
			*s++ = test_bit(t->bit, &tainted_mask) ?
					t->true : t->false;
		}
		*s = 0;
	} else
		snprintf(buf, sizeof(buf), "Not tainted");

	return buf;
}

int test_taint(unsigned flag)
{
	return test_bit(flag, &tainted_mask);
}
EXPORT_SYMBOL(test_taint);

unsigned long get_taint(void)
{
	return tainted_mask;
}

void add_taint(unsigned flag)
{
	/*
	 * Can't trust the integrity of the kernel anymore.
	 * We don't call directly debug_locks_off() because the issue
	 * is not necessarily serious enough to set oops_in_progress to 1
	 * Also we want to keep up lockdep for staging development and
	 * post-warning case.
	 */
	switch (flag) {
	case TAINT_CRAP:
	case TAINT_WARN:
	case TAINT_FIRMWARE_WORKAROUND:
		break;

	default:
		if (__debug_locks_off())
			printk(KERN_WARNING "Disabling lock debugging due to kernel taint\n");
	}

	set_bit(flag, &tainted_mask);
}
EXPORT_SYMBOL(add_taint);

static void spin_msec(int msecs)
{
	int i;

	for (i = 0; i < msecs; i++) {
		touch_nmi_watchdog();
		mdelay(1);
	}
}

/*
 * It just happens that oops_enter() and oops_exit() are identically
 * implemented...
 */
static void do_oops_enter_exit(void)
{
	unsigned long flags;
	static int spin_counter;

	if (!pause_on_oops)
		return;

	spin_lock_irqsave(&pause_on_oops_lock, flags);
	if (pause_on_oops_flag == 0) {
		/* This CPU may now print the oops message */
		pause_on_oops_flag = 1;
	} else {
		/* We need to stall this CPU */
		if (!spin_counter) {
			/* This CPU gets to do the counting */
			spin_counter = pause_on_oops;
			do {
				spin_unlock(&pause_on_oops_lock);
				spin_msec(MSEC_PER_SEC);
				spin_lock(&pause_on_oops_lock);
			} while (--spin_counter);
			pause_on_oops_flag = 0;
		} else {
			/* This CPU waits for a different one */
			while (spin_counter) {
				spin_unlock(&pause_on_oops_lock);
				spin_msec(1);
				spin_lock(&pause_on_oops_lock);
			}
		}
	}
	spin_unlock_irqrestore(&pause_on_oops_lock, flags);
}

/*
 * Return true if the calling CPU is allowed to print oops-related info.
 * This is a bit racy..
 */
int oops_may_print(void)
{
	return pause_on_oops_flag == 0;
}

/*
 * Called when the architecture enters its oops handler, before it prints
 * anything.  If this is the first CPU to oops, and it's oopsing the first
 * time then let it proceed.
 *
 * This is all enabled by the pause_on_oops kernel boot option.  We do all
 * this to ensure that oopses don't scroll off the screen.  It has the
 * side-effect of preventing later-oopsing CPUs from mucking up the display,
 * too.
 *
 * It turns out that the CPU which is allowed to print ends up pausing for
 * the right duration, whereas all the other CPUs pause for twice as long:
 * once in oops_enter(), once in oops_exit().
 */
void oops_enter(void)
{
	tracing_off();
	/* can't trust the integrity of the kernel anymore: */
	debug_locks_off();
	do_oops_enter_exit();
}

/*
 * 64-bit random ID for oopses:
 */
static u64 oops_id;

static int init_oops_id(void)
{
	if (!oops_id)
		get_random_bytes(&oops_id, sizeof(oops_id));
	else
		oops_id++;

	return 0;
}
late_initcall(init_oops_id);

void print_oops_end_marker(void)
{
	init_oops_id();

	if (mach_panic_string)
		printk(KERN_WARNING "Board Information: %s\n",
		       mach_panic_string);

	printk(KERN_WARNING "---[ end trace %016llx ]---\n",
		(unsigned long long)oops_id);
}

/*
 * Called when the architecture exits its oops handler, after printing
 * everything.
 */
void oops_exit(void)
{
	do_oops_enter_exit();
	print_oops_end_marker();
	kmsg_dump(KMSG_DUMP_OOPS);
}

#ifdef WANT_WARN_ON_SLOWPATH
struct slowpath_args {
	const char *fmt;
	va_list args;
};

static void warn_slowpath_common(const char *file, int line, void *caller,
				 unsigned taint, struct slowpath_args *args)
{
	const char *board;

	printk(KERN_WARNING "------------[ cut here ]------------\n");
	printk(KERN_WARNING "WARNING: at %s:%d %pS()\n", file, line, caller);
	board = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (board)
		printk(KERN_WARNING "Hardware name: %s\n", board);

	if (args)
		vprintk(args->fmt, args->args);

	print_modules();
	dump_stack();
	print_oops_end_marker();
	add_taint(taint);
}

void warn_slowpath_fmt(const char *file, int line, const char *fmt, ...)
{
	struct slowpath_args args;

	args.fmt = fmt;
	va_start(args.args, fmt);
	warn_slowpath_common(file, line, __builtin_return_address(0),
			     TAINT_WARN, &args);
	va_end(args.args);
}
EXPORT_SYMBOL(warn_slowpath_fmt);

void warn_slowpath_fmt_taint(const char *file, int line,
			     unsigned taint, const char *fmt, ...)
{
	struct slowpath_args args;

	args.fmt = fmt;
	va_start(args.args, fmt);
	warn_slowpath_common(file, line, __builtin_return_address(0),
			     taint, &args);
	va_end(args.args);
}
EXPORT_SYMBOL(warn_slowpath_fmt_taint);

void warn_slowpath_null(const char *file, int line)
{
	warn_slowpath_common(file, line, __builtin_return_address(0),
			     TAINT_WARN, NULL);
}
EXPORT_SYMBOL(warn_slowpath_null);
#endif

#ifdef CONFIG_CC_STACKPROTECTOR

/*
 * Called when gcc's -fstack-protector feature is used, and
 * gcc detects corruption of the on-stack canary value
 */
void __stack_chk_fail(void)
{
	panic("stack-protector: Kernel stack is corrupted in: %p\n",
		__builtin_return_address(0));
}
EXPORT_SYMBOL(__stack_chk_fail);

#endif

core_param(panic, panic_timeout, int, 0644);
core_param(pause_on_oops, pause_on_oops, int, 0644);

static int __init oops_setup(char *s)
{
	if (!s)
		return -EINVAL;
	if (!strcmp(s, "panic"))
		panic_on_oops = 1;
	return 0;
}
early_param("oops", oops_setup);
