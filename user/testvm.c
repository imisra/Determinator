#if LAB >= 3

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/vm.h>


#define STACKSIZE	PAGESIZE

#define ALLVA		((void*) VM_USERLO)
#define ALLSIZE		(VM_USERHI - VM_USERLO)

extern uint8_t start[], etext[], edata[], end[];

static uint8_t stack[2][STACKSIZE];


static void fork(int cmd, uint8_t child, void (*func)(int arg), int arg,
		uint8_t stack[STACKSIZE])
{

	// Push the function argument and fake return address on the stack
	uint32_t *esp = (uint32_t*)&stack[STACKSIZE];
	*--esp = arg;
	*--esp = (uint32_t) sys_ret;

	// Set up the register state for the child
	struct cpustate cs;
	memset(&cs, 0, sizeof(cs));
	cs.tf.tf_eip = (uint32_t) func;
	cs.tf.tf_esp = (uint32_t) esp;

	// Fork the child, copying our entire user address space into it.
	sys_put(cmd | SYS_REGS | SYS_COPY, child, &cs, ALLVA, ALLVA, ALLSIZE);
}

static void join(int cmd, uint8_t child, int trapexpect)
{
	// Wait for the child and retrieve its CPU state.
	// If merging, leave the highest 4MB containing the stack unmerged,
	// so that the stack acts as a "thread-private" memory area.
	struct cpustate cs;
	sys_get(cmd | SYS_REGS, child, &cs, ALLVA, ALLVA, ALLSIZE-PTSIZE);

	// Make sure the child exited with the expected trap number
	if (cs.tf.tf_trapno != trapexpect) {
		cprintf("  eip  0x%08x\n", cs.tf.tf_eip);
		cprintf("  esp  0x%08x\n", cs.tf.tf_esp);
		panic("join: unexpected trap %d, expecting %d\n",
			cs.tf.tf_trapno, trapexpect);
	}
}

static void trapchild(int arg)
{
	int bounds[2] = { 1, 3 };
	switch (arg) {
	case T_DIVIDE:
		asm volatile("divl %0,%0" : : "r" (0));
	case T_BRKPT:
		asm volatile("int3");
	case T_OFLOW:
		asm volatile("addl %0,%0; into" : : "r" (0x70000000));
	case T_BOUND:
		asm volatile("boundl %0,%1" : : "r" (0), "m" (bounds[0]));
	case T_ILLOP:
		asm volatile("ud2");	// guaranteed to be undefined
	case T_GPFLT:
		asm volatile("lidt %0" : : "m" (arg));
	}
}

static void trapcheck(int trapno)
{
	fork(SYS_START, 0, trapchild, trapno, stack[0]);
	join(0, 0, trapno);
}

static void readchild(int arg) {
	(void)(*(volatile int*)arg);
}
#define readfaulttest(va) \
	fork(SYS_START, 0, readchild, (int)va, stack[0]); \
	join(0, 0, T_PGFLT);

static void writefaultchild(int arg) {
	*(volatile int*)arg = 0xdeadbeef;
}
#define writefaulttest(va) \
	fork(SYS_START, 0, writefaultchild, (int)va, stack[0]); \
	join(0, 0, T_PGFLT);

static void cputsfaultchild(int arg) {
	sys_cputs((char*)arg);
}
#define cputsfaulttest(va) \
	fork(SYS_START, 0, cputsfaultchild, (int)va, stack[0]); \
	join(0, 0, T_PGFLT);

static void putfaultchild(int arg) {
	sys_put(SYS_REGS, 0, (cpustate*)arg, NULL, NULL, 0);
}
#define putfaulttest(va) \
	fork(SYS_START, 0, putfaultchild, (int)va, stack[0]); \
	join(0, 0, T_PGFLT);

static void getfaultchild(int arg) {
	sys_get(SYS_REGS, 0, (cpustate*)arg, NULL, NULL, 0);
}
#define getfaulttest(va) \
	fork(SYS_START, 0, getfaultchild, (int)va, stack[0]); \
	join(0, 0, T_PGFLT);

static void loadcheck()
{
	// Simple ELF loading test: make sure bss is mapped but cleared
	uint8_t *p;
	for (p = edata; p < end; p++) {
		if (*p != 0) cprintf("%x %d\n", p, *p);
		assert(*p == 0);
	}

	cprintf("testvm: loadcheck passed\n");
}

// Check forking of simple child processes and trap redirection (once more)
static void forkcheck()
{
	// Our first copy-on-write test: fork and execute a simple child.
	fork(SYS_START, 0, trapchild, T_SYSCALL, stack[0]);
	join(0, 0, T_SYSCALL);

	// Re-check trap handling and reflection from child processes
	trapcheck(T_DIVIDE);
	trapcheck(T_BRKPT);
	trapcheck(T_OFLOW);
	trapcheck(T_BOUND);
	trapcheck(T_ILLOP);
	trapcheck(T_GPFLT);

	// Make sure we can run several children using the same stack area
	// (since each child should get a separate logical copy)
	fork(SYS_START, 0, trapchild, T_SYSCALL, stack[0]);
	fork(SYS_START, 1, trapchild, T_DIVIDE, stack[0]);
	fork(SYS_START, 2, trapchild, T_BRKPT, stack[0]);
	fork(SYS_START, 3, trapchild, T_OFLOW, stack[0]);
	fork(SYS_START, 4, trapchild, T_BOUND, stack[0]);
	fork(SYS_START, 5, trapchild, T_ILLOP, stack[0]);
	fork(SYS_START, 6, trapchild, T_GPFLT, stack[0]);
	join(0, 0, T_SYSCALL);
	join(0, 1, T_DIVIDE);
	join(0, 2, T_BRKPT);
	join(0, 3, T_OFLOW);
	join(0, 4, T_BOUND);
	join(0, 5, T_ILLOP);
	join(0, 6, T_GPFLT);

	// Check that kernel address space is inaccessible to user code
	readfaulttest(0);
	readfaulttest(VM_USERLO-4);
	readfaulttest(VM_USERHI);
	readfaulttest(0-4);

	cprintf("testvm: forkcheck passed\n");
}

// Check for proper virtual memory protection
static void protcheck()
{
	// Copyin/copyout protection:
	// make sure we can't use cputs/put/get data in kernel space
	cputsfaulttest(0);
	cputsfaulttest(VM_USERLO-1);
	cputsfaulttest(VM_USERHI);
	cputsfaulttest(~0);
	putfaulttest(0);
	putfaulttest(VM_USERLO-1);
	putfaulttest(VM_USERHI);
	putfaulttest(~0);
	getfaulttest(0);
	getfaulttest(VM_USERLO-1);
	getfaulttest(VM_USERHI);
	getfaulttest(~0);

	// Check that unused parts of user space are also inaccessible
	readfaulttest(VM_USERLO+PTSIZE);
	readfaulttest(VM_USERHI-PTSIZE);
	readfaulttest(VM_USERHI-PTSIZE*2);
	cputsfaulttest(VM_USERLO+PTSIZE);
	cputsfaulttest(VM_USERHI-PTSIZE);
	cputsfaulttest(VM_USERHI-PTSIZE*2);
	putfaulttest(VM_USERLO+PTSIZE);
	putfaulttest(VM_USERHI-PTSIZE);
	putfaulttest(VM_USERHI-PTSIZE*2);
	getfaulttest(VM_USERLO+PTSIZE);
	getfaulttest(VM_USERHI-PTSIZE);
	getfaulttest(VM_USERHI-PTSIZE*2);

	// Check that our text segment is mapped read-only
	writefaulttest((int)start);
	writefaulttest((int)etext-4);
	getfaulttest((int)start);
	getfaulttest((int)etext-4);

	cprintf("testvm: protcheck passed\n");
}

// Test explicit memory management operations
static void memopcheck(void)
{
	// Test page permission changes
	void *va = (void*)VM_USERLO+PTSIZE+PAGESIZE;
	readfaulttest(va);
	sys_get(SYS_PERM | SYS_READ, 0, NULL, NULL, va, PAGESIZE);
	assert(*(volatile int*)va == 0);	// should be readable now
	writefaulttest(va);			// but not writable
	sys_get(SYS_PERM | SYS_READ | SYS_WRITE, 0, NULL, NULL, va, PAGESIZE);
	*(volatile int*)va = 0xdeadbeef;	// should be writable now
	sys_get(SYS_PERM, 0, NULL, NULL, va, PAGESIZE);	// revoke all perms
	readfaulttest(va);
	sys_get(SYS_PERM | SYS_READ, 0, NULL, NULL, va, PAGESIZE);
	assert(*(volatile int*)va == 0xdeadbeef);	// readable again
	writefaulttest(va);				// but not writable
	sys_get(SYS_PERM | SYS_READ | SYS_WRITE, 0, NULL, NULL, va, PAGESIZE);

	// Test SYS_ZERO with SYS_GET
	va = (void*)VM_USERLO+PTSIZE;	// 4MB-aligned
	sys_get(SYS_ZERO, 0, NULL, NULL, va, PTSIZE);
	readfaulttest(va);		// should be inaccessible again
	sys_get(SYS_PERM | SYS_READ, 0, NULL, NULL, va, PAGESIZE);
	assert(*(volatile int*)va == 0);	// and zeroed
	writefaulttest(va);			// but not writable
	sys_get(SYS_ZERO, 0, NULL, NULL, va, PTSIZE);
	readfaulttest(va);			// gone again
	sys_get(SYS_PERM | SYS_READ | SYS_WRITE, 0, NULL, NULL, va, PAGESIZE);
	*(volatile int*)va = 0xdeadbeef;	// writable now
	sys_get(SYS_ZERO, 0, NULL, NULL, va, PTSIZE);
	readfaulttest(va);			// gone again
	sys_get(SYS_PERM | SYS_READ, 0, NULL, NULL, va, PAGESIZE);
	assert(*(volatile int*)va == 0);	// and zeroed

	// Test SYS_COPY with SYS_GET - pull residual stuff out of child 0
	void *sva = (void*)VM_USERLO;
	void *dva = (void*)VM_USERLO+PTSIZE;
	sys_get(SYS_COPY, 0, NULL, sva, dva, PTSIZE);
	assert(memcmp(sva, dva, end - start) == 0);
	writefaulttest(dva);
	readfaulttest(dva + PTSIZE-4);

	// Test SYS_ZERO with SYS_PUT
	void *dva2 = (void*)VM_USERLO+PTSIZE*2;
	sys_put(SYS_ZERO, 0, NULL, NULL, dva, PTSIZE);
	sys_get(SYS_COPY, 0, NULL, dva, dva2, PTSIZE);
	readfaulttest(dva2);
	readfaulttest(dva2 + PTSIZE-4);
	sys_get(SYS_PERM | SYS_READ, 0, NULL, NULL, dva2, PTSIZE);
	assert(*(volatile int*)dva2 == 0);
	assert(*(volatile int*)(dva2+PTSIZE-4) == 0);

	// Test SYS_COPY with SYS_PUT
	sys_put(SYS_COPY, 0, NULL, sva, dva, PTSIZE);
	sys_get(SYS_COPY, 0, NULL, dva, dva2, PTSIZE);
	assert(memcmp(sva, dva2, end - start) == 0);
	writefaulttest(dva2);
	readfaulttest(dva2 + PTSIZE-4);

	// Hide an easter egg and make sure it survives the two copies
	sva = (void*)VM_USERLO; dva = sva+PTSIZE; dva2 = dva+PTSIZE;
	uint32_t ofs = PTSIZE-PAGESIZE;
	sys_get(SYS_PERM|SYS_READ|SYS_WRITE, 0, NULL, NULL, sva+ofs, PAGESIZE);
	*(volatile int*)(sva+ofs) = 0xdeadbeef;	// should be writable now
	sys_get(SYS_PERM, 0, NULL, NULL, sva+ofs, PAGESIZE);
	readfaulttest(sva+ofs);			// hide it
	sys_put(SYS_COPY, 0, NULL, sva, dva, PTSIZE);
	sys_get(SYS_COPY, 0, NULL, dva, dva2, PTSIZE);
	readfaulttest(dva2+ofs);		// stayed hidden?
	sys_get(SYS_PERM|SYS_READ, 0, NULL, NULL, dva2+ofs, PAGESIZE);
	assert(*(volatile int*)(dva2+ofs) == 0xdeadbeef);	// survived?

	cprintf("testvm: memopcheck passed\n");
}

int x, y;

void mergechild(int arg)
{
	if (arg)
		x = 0xdeadbeef;
	else
		y = 0xabadcafe;
}

void swapchild(int arg)
{
	if (arg)
		x = y;
	else
		y = x;
}

int randints[256] = {	// some random ints
	 20,726,926,682,210,585,829,491,612,744,753,405,346,189,669,416,
	 41,832,959,511,260,879,844,323,710,570,289,299,624,319,997,907,
	 56,545,122,497, 60,314,759,741,276,951,496,376,403,294,395, 96,
	372,402,468,866,782,524,739,273,462,920,965,225,164,687,628,127,
	998,957,973,212,801,790,254,855,215,979,229,234,194,755,174,793,
	367,865,458,479,117,471,113, 12,605,328,231,513,676,495,422,404,
	611,693, 32, 59,126,607,219,837,542,437,803,341,727,626,360,507,
	834,465,795,271,646,725,336,241, 42,353,438, 44,167,786, 51,873,
	874,994, 80,432,657,365,734,132,500,145,238,931,332,146,922,878,
	108,508,601, 38,749,606,565,642,261,767,312,410,239,476,498, 90,
	655,379,835,270,862,876,699,165,675,869,296,163,435,321, 88,575,
	233,745, 94,303,584,381,359, 50,766,534, 27,499,101,464,195,453,
	671, 87,139,123,544,560,679,616,705,494,733,678,927, 26, 14,114,
	140,777,250,564,596,802,723,383,808,817,  1,436,361,952,613,680,
	854,580, 76,891,888,721,204,989,882,141,448,286,964,130, 48,385,
	756,224,138,630,821,449,662,578,400, 74,477,275,272,392,747,394};
int sortints[256] = {	// sorted array of the same ints
	  1, 12, 14, 20, 26, 27, 32, 38, 41, 42, 44, 48, 50, 51, 56, 59,
	 60, 74, 76, 80, 87, 88, 90, 94, 96,101,108,113,114,117,122,123,
	126,127,130,132,138,139,140,141,145,146,163,164,165,167,174,189,
	194,195,204,210,212,215,219,224,225,229,231,233,234,238,239,241,
	250,254,260,261,270,271,272,273,275,276,286,289,294,296,299,303,
	312,314,319,321,323,328,332,336,341,346,353,359,360,361,365,367,
	372,376,379,381,383,385,392,394,395,400,402,403,404,405,410,416,
	422,432,435,436,437,438,448,449,453,458,462,464,465,468,471,476,
	477,479,491,494,495,496,497,498,499,500,507,508,511,513,524,534,
	542,544,545,560,564,565,570,575,578,580,584,585,596,601,605,606,
	607,611,612,613,616,624,626,628,630,642,646,655,657,662,669,671,
	675,676,678,679,680,682,687,693,699,705,710,721,723,725,726,727,
	733,734,739,741,744,745,747,749,753,755,756,759,766,767,777,782,
	786,790,793,795,801,802,803,808,817,821,829,832,834,835,837,844,
	854,855,862,865,866,869,873,874,876,878,879,882,888,891,907,920,
	922,926,927,931,951,952,957,959,964,965,973,979,989,994,997,998};

#define swapints(a,b) ({ int t = (a); (a) = (b); (b) = t; })

static void pqsort(int *lo, int *hi)
{
	if (lo >= hi)
		return;

	int pivot = *lo;	// yeah, bad way to choose pivot...
	int *l = lo+1, *h = hi;
	while (l <= h) {
		if (*l < pivot)
			l++;
		else if (*h > pivot)
			h--;
		else
			swapints(*h, *l), l++, h--;
	}
	swapints(*lo, l[-1]);

	// Now recursively sort the two halves
	pqsort(lo, l-2);
	pqsort(h+1, hi);
}

static void mergecheck()
{
	// Simple merge test: two children write two adjacent variables
	fork(SYS_START | SYS_SNAP, 0, mergechild, 0, stack[0]); \
	fork(SYS_START | SYS_SNAP, 1, mergechild, 1, stack[1]); \
	assert(x == 0); assert(y == 0);
	join(SYS_MERGE, 0, T_SYSCALL);
	join(SYS_MERGE, 1, T_SYSCALL);
	assert(x == 0xdeadbeef); assert(y == 0xabadcafe);

	// A Rube Goldberg approach to swapping two variables
	fork(SYS_START | SYS_SNAP, 0, swapchild, 0, stack[0]); \
	fork(SYS_START | SYS_SNAP, 1, swapchild, 1, stack[1]); \
	assert(x == 0xdeadbeef); assert(y == 0xabadcafe);
	join(SYS_MERGE, 0, T_SYSCALL);
	join(SYS_MERGE, 1, T_SYSCALL);
	assert(y == 0xdeadbeef); assert(x == 0xabadcafe);

	// Parallel quicksort with recursive processes!
	// (though probably not very efficient on arrays this small)
	pqsort(&randints[0], &randints[256-1]);
	assert(memcmp(randints, sortints, 256*sizeof(int)) == 0);

	cprintf("testvm: mergecheck passed\n");
}

void piosmain()
{
	cprintf("testvm: in piosmain()\n");

	loadcheck();
	forkcheck();
	protcheck();
	memopcheck();
	mergecheck();

	cprintf("testvm: all tests completed successfully!\n");
}

#endif