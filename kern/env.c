#if LAB >= 3
/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/elf.h>

#include <kern/env.h>
#include <kern/pmap.h>
#if LAB >= 3
#include <kern/trap.h>
#include <kern/sched.h>
#endif

struct Env *envs = NULL;		// All environments
struct Env *curenv = NULL;	        // The current env

static struct Env_list env_free_list;	// Free list

//
// Calculates the envid for env e.  
//
static u_int
mkenvid(struct Env *e)
{
	static u_long next_env_id = 0;
	// lower bits of envid hold e's position in the envs array
	u_int idx = e - envs;
	// high bits of envid hold an increasing number
	return(++next_env_id << (1 + LOG2NENV)) | idx;
}

//
// Converts an envid to an env pointer.
//
// RETURNS
//   env pointer -- on success and sets *error = 0
//   NULL -- on failure, and sets *error = the error number
//
int
envid2env(u_int envid, struct Env **penv, int checkperm)
{
	struct Env *e;

	if (envid == 0) {
		*penv = curenv;
		return 0;
	}

	e = &envs[ENVX(envid)];
	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*penv = 0;
		return -E_BAD_ENV;
	}

	if (checkperm) {
#if SOL >= 4
		if (e != curenv && e->env_parent_id != curenv->env_id) {
			*penv = 0;
			return -E_BAD_ENV;
		}
#else
		// Your code here in Lab 4
		return -E_BAD_ENV;
#endif
	}
	*penv = e;
	return 0;
}

//
// Marks all environments in 'envs' as free and inserts them into 
// the env_free_list.  Insert in reverse order, so that
// the first call to env_alloc() returns envs[0].
//
void
env_init(void)
{
#if SOL >= 3
	int i;
	LIST_INIT (&env_free_list);
	for (i = NENV - 1; i >= 0; i--) {
		envs[i].env_status = ENV_FREE;    
		LIST_INSERT_HEAD (&env_free_list, &envs[i], env_link);
	}
#endif /* SOL >= 3 */
}

//
// Initializes the kernel virtual memory layout for environment e.
// Allocates a page directory and initializes
// the kernel portion of the new environment's address space.
// Also sets e->env_cr3 and e->env_pgdir accordingly.
// We do NOT (yet) map anything into the user portion
// of the environment's virtual address space.
//
// RETURNS
//   0 -- on sucess
//   <0 -- otherwise 
//
static int
env_setup_vm(struct Env *e)
{
	int i, r;
	struct Page *p = NULL;

	// Allocate a page for the page directory
	if ((r = page_alloc(&p)) < 0)
		return r;

	// Hint:
	//    - The VA space of all envs is identical above UTOP
	//      (except at VPT and UVPT).
	//    - Use boot_pgdir as a template.
	//    - You do not need to make any more calls to page_alloc.
	//    - Note: pp_ref is not maintained
	//	for physical pages mapped above UTOP.

#if SOL >= 3
	e->env_cr3 = page2pa(p);
	e->env_pgdir = (Pde*)page2kva(p);
	memset(e->env_pgdir, 0, BY2PG);

	// The VA space of all envs is identical above UTOP...
	static_assert(UTOP % PDMAP == 0);
	for (i = PDX(UTOP); i <= PDX(~0); i++)
		e->env_pgdir[i] = boot_pgdir[i];

#endif /* SOL >= 3 */

	// ...except at VPT and UVPT.  These map the env's own page table
	e->env_pgdir[PDX(VPT)]   = e->env_cr3 | PTE_P | PTE_W;
	e->env_pgdir[PDX(UVPT)]  = e->env_cr3 | PTE_P | PTE_U;

	return 0;
}

//
// Allocates and initializes a new env.
//
// RETURNS
//   0 -- on success, sets *new to point at the new env 
//   <0 -- on failure
//
int
env_alloc(struct Env **new, u_int parent_id)
{
	int r;
	struct Env *e;

	if (!(e = LIST_FIRST(&env_free_list)))
		return -E_NO_FREE_ENV;

	// Allocate and set up the page directory for this environment.
	if ((r = env_setup_vm(e)) < 0)
		return r;

	// Generate an env_id for this environment,
	// and set the basic status variables.
	e->env_id = mkenvid(e);
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;

	// Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and 
	// GD_UT is the user text segment selector (see inc/pmap.h).
	// The low 2 bits of each segment register
	// contains the Requestor Privilege Level (RPL);
	// 3 means user mode.
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;

#if SOL >= 4
	// Enable interrupts while in user mode.
	e->env_tf.tf_eflags = FL_IF; // interrupts enabled
#endif

	// You also need to set tf_eip to the correct value at some point.
	// Hint: see load_icode

	// Clear the page fault handler until user installs one.
	e->env_pgfault_handler = 0;

#if LAB >= 4
	e->env_ipc_recving = 0;
#endif

#if LAB >= 5
	// If this is the file server (e==&envs[1]) give it I/O privileges.
#if SOL >= 5
	if (e == &envs[1])
		e->env_tf.tf_eflags |= FL_IOPL0|FL_IOPL1;
#else
	//   (your code here)
#endif
#endif

	// commit the allocation
	LIST_REMOVE(e, env_link);
	*new = e;

#if LAB >= 5
	// printf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
#else
	printf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
#endif
	return 0;
}

// Allocate and map all required pages into an env's address space
// to cover virtual addresses va through va+len-1 inclusive,
// and initialize these pages so the range va through va+len-1
// contains the data from src to src+len-1 (in the kernel's address space).
// Make all mappings read/write and user-accessible.
// Panic if any allocation attempt fails.
//
// Warning: Neither va nor len are necessarily page-aligned!
// You may assume, however, that nothing is already mapped
// in the pages touched by the specified virtual address range.
static void
map_segment(struct Env *e, u_int va, u_int len, u_char *src)
{
#if SOL >= 3
	int r;
	struct Page *p;
	u_int endva, copylen;

	while (len > 0) {
		// Allocate and map a page covering virtual address va.
		if ((r = page_alloc(&p)) < 0)
			panic("map_segment: could not alloc page: %e\n", r);
		if ((r = page_insert(e->env_pgdir, p, va, PTE_P|PTE_W|PTE_U))
				< 0)
			panic("map_segment: could not insert page: %e\n", r);

		// Copy data from src into (the appropriate part of) this page,
		// up to 'len' bytes or to the end of the current page.
		endva = (va + BY2PG) & ~(BY2PG-1);
		if (endva > va+len)
			endva = va+len;
		copylen = endva-va;
		memcpy((void*)page2kva(p) + PGOFF(va), src, copylen);

		// Move on to the next page.
		va += copylen;
		src += copylen;
		len -= copylen;
	}	
#else
	// Your code here.
#endif
}

//
// Set up the the initial stack and program binary for a user process.
//
// This function loads the complete binary image, including elf header,
// into the environment's user memory starting at virtual address UTEXT,
// and maps one page for the program's initial stack
// at virtual address USTACKTOP - BY2PG.
//
// This function does not allocate or clear the bss of the loaded program,
// and all mappings are read/write including those of the text segment.
//
static void
load_icode(struct Env *e, u_char *binary, u_int size)
{
#if SOL >= 3
	int i, r;
	struct Elf *elf;
	struct Page *p;
	struct Proghdr *ph;

	// Check magic number on binary
	elf = (struct Elf*)binary;
	if (elf->e_magic != ELF_MAGIC)
		panic("load_icode: not an ELF binary");

	// Record entry for binary.
	e->env_tf.tf_eip = elf->e_entry;
	printf("load_icode: entry is 0x%x\n", e->env_tf.tf_eip);

	// Map segments as directed.
	ph = (struct Proghdr*)(binary + elf->e_phoff);
	for (i = 0; i < elf->e_phnum; i++, ph++) {
		if(ph->p_type != ELF_PROG_LOAD)
			continue;
		if(ph->p_va + ph->p_memsz < ph->p_va)
			panic("load_icode: overflow in elf header segment");
		if(ph->p_va + ph->p_memsz >= UTOP)
			panic("load_icode: icode wants to be loaded above UTOP");
		map_segment(e, ph->p_va, ph->p_memsz, binary+ph->p_offset);
	}

	// Give environment a stack
	if ((r = page_alloc(&p)) < 0)
		panic("load_icode: could not alloc page: %e\n", r);
	if ((r = page_insert(e->env_pgdir, p, USTACKTOP - BY2PG,
				PTE_P|PTE_W|PTE_U)) < 0)
		panic("load_icode: could not map page: %e\n", r);
#else /* not SOL >= 3 */
	// Hint: 
	//  Use map_segment() for loading the program segments.
	//  Only use segments with ph->p_type == ELF_PROG_LOAD.
	//  For mapping the stack, use page_alloc, page_insert, and
	//  e->env_pgdir.
	//  You must figure out which permissions you'll need for the
	//  different mappings you create.
#endif /* not SOL >= 3 */
}

//
// Allocates a new env and loads the elf binary into it.
//  - new env's parent env id is 0
void
env_create(u_char *binary, int size)
{
#if SOL >= 3
	int r;
	struct Env *e;
	if ((r = env_alloc(&e, 0)) < 0)
		panic("env_create: could not allocate env: %e\n", r);
	load_icode(e, binary, size);
#endif /* not SOL >= 3 */
}

//
// Frees env e and all memory it uses.
// 
void
env_free(struct Env *e)
{
	Pte *pt;
	u_int pdeno, pteno, pa;

	// Note the environment's demise.
#if LAB >= 5
	// printf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
#else
	printf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
#endif

	// Flush all mapped pages in the user portion of the address space
	static_assert(UTOP%PDMAP == 0);
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {

		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (Pte*)KADDR(pa);

		// unmap all PTEs in this page table
		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			if (pt[pteno] & PTE_P)
				page_remove(e->env_pgdir,
					(pdeno << PDSHIFT) |
					(pteno << PGSHIFT));
		}

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}

	// free the page directory
	pa = e->env_cr3;
	e->env_pgdir = 0;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));

	// return the environment to the free list
	e->env_status = ENV_FREE;
	LIST_INSERT_HEAD(&env_free_list, e, env_link);
}

//
// Frees env e.  And schedules a new env
// if e was the current env.
//
void
env_destroy(struct Env *e) 
{
	env_free(e);
	if (curenv == e) {
		curenv = NULL;
		sched_yield();
	}
}


//
// Restores the register values in the Trapframe
//  (does not return)
//
void
env_pop_tf(struct Trapframe *tf)
{
#if 0
	printf(" --> %d 0x%x\n", ENVX(curenv->env_id), tf->tf_eip);
#endif

	asm volatile("movl %0,%%esp\n"
		"\tpopal\n"
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		"\tiret"
		:: "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}

//
// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
//  (This function does not return.)
//
void
env_run(struct Env *e)
{
#if SOL >= 3
#if SOL >= 4
	// save the register state of the previously executing environment
	if (curenv)
		curenv->env_tf = *UTF;
#endif // SOL >= 4

	// keep track of which environment we're currently running
	curenv = e;

	// restore e's address space
	lcr3(e->env_cr3);

	// restore e's register state
#if LAB >= 6
	e->env_runs++;
#endif
	env_pop_tf(&e->env_tf);
#else /* not SOL >= 3 */
	// step 1: save the register state of old curenv.
	//	(Copy it from where trapentry.S saves it on the kernel stack
	//	into the trapframe portion of curenv.)
	// step 2: set curenv to the new environment to be run.
	// step 3: use lcr3 to switch to the new environment's address space.
	// step 4: use env_pop_tf() to restore the new environment's registers
	//	and drop into user mode in the new environment.

	// Hint: You may skip step 1 until Part 2,
	// where you start handling exceptions and system calls.
	// You don't need it for Part 1, and in Part 2 you'll better
	// understand what you need to do.
#endif /* not SOL >= 3 */
}


#endif /* LAB >= 3 */
