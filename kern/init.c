/* See COPYRIGHT for copyright information. */

#include <inc/asm.h>
#include <inc/string.h>
#if LAB >= 3
#include <kern/trap.h>
#endif
#include <kern/pmap.h>
#include <kern/env.h>
#include <kern/console.h>
#include <kern/printf.h>
#include <kern/picirq.h>
#include <kern/kclock.h>
#if LAB >= 3
#include <kern/sched.h>
#endif

void
i386_init(void)
{
	// can't call printf until after cons_init()
	cons_init();

	printf("6828 decimal is %o octal!\n", 6828);

	// Lab 2 initialization functions
	i386_detect_memory();
	i386_vm_init();
	page_init();
	page_check();

#if LAB >= 3
	// Lab 3 initialization functions
	idt_init();
	pic_init();
	kclock_init();
	env_init();
#endif

#if LAB >= 6
	// Should always have an idle process as first one.
	ENV_CREATE(user_idle);

	// Start fs.
	ENV_CREATE(fs_fs);

	// Start init
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE2(TEST, TESTSIZE);
#else
	// Touch all you want.
	ENV_CREATE(user_icode);
	// ENV_CREATE(user_pipereadeof);
	// ENV_CREATE(user_pipewriteeof);
#endif
#elif LAB >= 5
	// Should always have an idle process as first one.
	ENV_CREATE(user_idle);

	// Start fs.
	ENV_CREATE(fs_fs);

	// Start init
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE2(TEST, TESTSIZE);
#else
	// Touch all you want.
	// ENV_CREATE(user_writemotd);
	// ENV_CREATE(user_testfsipc);
	// ENV_CREATE(user_icode);
#endif

#elif LAB >= 4
	// Should always have an idle process as first one.
	ENV_CREATE(user_idle);

#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE2(TEST, TESTSIZE)
#elif defined(TEST_PINGPONG2)
	// Don't touch -- used by grading script!
	ENV_CREATE(user_pingpong2);
	ENV_CREATE(user_pingpong2);
#else
	// Touch all you want.
	ENV_CREATE(user_hello);
#endif // TEST*
#elif LAB >= 3
	// Temporary test code specific to LAB 3
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST)
#elif defined(TEST_ALICEBOB)
	// Don't touch -- used by grading script!
	ENV_CREATE(alice);
	ENV_CREATE(bob);
#else
	// Touch all you want.
	ENV_CREATE(spin);
#endif // TEST*
#endif // LAB5, LAB4, LAB3

#if LAB >= 6
	// Should not be necessary - drain keyboard because interrupt has given up.
	kbd_intr();

#endif
#if LAB >= 3
	sched_yield();
#endif
	panic("init.c: end of i386_init() reached!");
}


// copy a memory block (doesn't handle overlapping src/dst)
void *
memcpy(void *dst, const void *src, size_t len)
{
	void *orig_dst = dst;
	void *max;

	max = dst + len;
	// copy machine words while possible
	while (dst + 3 < max)
		*(int *)dst++ = *(int *)src++;
	// finish remaining 0-3 bytes
	while (dst < max)
		*(char *)dst++ = *(char *)src++;

	return orig_dst;
}

void *
memset(void *b, int c, size_t len)
{
	void *orig = b;
	void *max;

	c &= 0xff;
	c |= c << 8;
	c |= c << 16;

	max = b + len;

	// zero machine words while possible
	while (b + 3 < max)
		*(int *)b++ = c;

	// finish remaining 0-3 bytes
	while (b < max)
		*(char *)b++ = c;

	return orig;
}

// Ignore from here on down.  The functions below here are never
// called.  They are just here to get around a linking problem.

void
abort(void)
{
	panic("abort");
}

void *
malloc(size_t size)
{
	panic("malloc: size %d", size);
}

void
free(void *ptr)
{
	panic("free: ptr %p", ptr);
}

int
atexit(void(*function)(void))
{
	panic("atexit: function %p", function);
}

