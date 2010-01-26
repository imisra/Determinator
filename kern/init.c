/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/gcc.h>
#include <inc/syscall.h>

#include <kern/init.h>
#include <kern/console.h>
#include <kern/debug.h>
#include <kern/mem.h>
#include <kern/cpu.h>
#include <kern/trap.h>
#if LAB >= 2
#include <kern/mp.h>
#include <kern/proc.h>
#endif	// LAB >= 2

#if LAB >= 2
#include <dev/lapic.h>
#endif	// LAB >= 2


// User-mode stack for user(), below, to run on.
static char gcc_aligned(16) user_stack[PAGESIZE];


// Called first from entry.S on the bootstrap processor,
// and later from boot/bootother.S on all other processors.
// As a rule, "init" functions in PIOS are called once on EACH processor.
void
init(void)
{
	extern char edata[], end[];

	// Before anything else, complete the ELF loading process.
	// Clear all uninitialized global data (BSS) in our program,
	// ensuring that all static/global variables start out zero.
	if (cpu_onboot())
		memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

#if LAB == 1
	// Lab 1: test cprintf and debug_trace
	cprintf("1234 decimal is %o octal!\n", 1234);
	debug_check();

#endif	// LAB == 1
	// Initialize and load the bootstrap CPU's GDT, TSS, and IDT.
	cpu_init();
	trap_init();
#if LAB >= 2
	lapic_init();		// setup this CPU's local APIC
#endif	// LAB >= 2

	// Physical memory detection/initialization.
	// Can't call mem_alloc until after we do this!
	mem_init();

#if LAB >= 2
	// Find and start other processors in a multiprocessor system
	mp_init();		// Find info about processors in system
	cpu_bootothers();	// Get other processors started
	cprintf("CPU %d (%s) has booted\n", cpu_cur()->id,
		cpu_onboot() ? "BP" : "AP");

	// Initialize the process management code and the root process.
	proc_init();
#endif

#if SOL >= 2
	// Create our first actual user-mode process
	// (though it'll still be sharing the kernel's address space).
	proc *root = proc_alloc(NULL, 0);
	root->tf.tf_eip = (uint32_t) user;
	root->tf.tf_esp = (uint32_t) &user_stack[PAGESIZE];
	proc_ready(root);	// make it ready
	proc_sched();		// run it
#elif SOL >= 1
	// Conjure up a trapframe and "return" to it to enter user mode.
	static trapframe utf = {
		tf_ds: CPU_GDT_UDATA | 3,
		tf_es: CPU_GDT_UDATA | 3,
		tf_eip: (uint32_t) user,
		tf_cs: CPU_GDT_UCODE | 3,
		tf_eflags: FL_IOPL_3,	// let user() output to console
		tf_esp: (uint32_t) &user_stack[PAGESIZE],
		tf_ss: CPU_GDT_UDATA | 3,
	};
	trap_return(&utf);
#else
	// Lab 1: change this so it enters user() in user mode,
	// running on the user_stack declared above,
	// instead of just calling user() directly.
	user();
#endif
}

#if LAB == 2
static void child(int n);
static void grandchild(int n);
#endif

// This is the first function that gets run in user mode (ring 3).
// It acts as PIOS's "root process",
// of which all other processes are descendants.
void
user()
{
	cprintf("in user()\n");
	assert(read_esp() > (uint32_t) &user_stack[0]);
	assert(read_esp() < (uint32_t) &user_stack[sizeof(user_stack)]);

#if LAB == 1
	// Check that we're in user mode and can handle traps from there.
	trap_check(1);
#endif
#if LAB == 2
	// Spawn to child processes, executing on statically allocated stacks.
	static struct cpustate state;
	static char gcc_aligned(16) child_stack[2][PAGESIZE];

	int i;
	for (i = 0; i < 2; i++) {
		// Setup register state for child
		uint32_t *esp = (uint32_t*) &child_stack[i][PAGESIZE];
		*--esp = i;	// push argument to child() function
		*--esp = 0;	// fake return address
		state.tf.tf_eip = (uint32_t) child;
		state.tf.tf_esp = (uint32_t) esp;

		// Use PUT syscall to create and start it
		cprintf("spawning child %d\n", i);
		sys_put(SYS_START | SYS_REGS, i, &state);
	}

	// now wait for both children
	for (i = 0; i < 2; i++) {
		cprintf("waiting for child %d\n", i);
		sys_get(SYS_REGS, i, &state);
	}

	cprintf("proc_check() succeeded!\n");
#endif

	done();
}

#if LAB == 2
static void child(int n)
{
	int i;
	for (i = 0; i < 10; i++)
		cprintf("in child %d count %d\n", n, i);
	sys_ret();

	done();
}

static void grandchild(int n)
{
}

#endif	// LAB == 2
void
done()
{
	while (1)
		;	// just spin
}

