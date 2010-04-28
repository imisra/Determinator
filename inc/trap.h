#if LAB >= 1
#ifndef PIOS_INC_TRAP_H
#define PIOS_INC_TRAP_H

#include <cdefs.h>


// Trap numbers
// These are processor defined:
#define T_DIVIDE	0	// divide error
#define T_DEBUG		1	// debug exception
#define T_NMI		2	// non-maskable interrupt
#define T_BRKPT		3	// breakpoint
#define T_OFLOW		4	// overflow
#define T_BOUND		5	// bounds check
#define T_ILLOP		6	// illegal opcode
#define T_DEVICE	7	// device not available 
#define T_DBLFLT	8	// double fault
/* #define T_COPROC	9 */	// reserved (not generated by recent processors)
#define T_TSS		10	// invalid task switch segment
#define T_SEGNP		11	// segment not present
#define T_STACK		12	// stack exception
#define T_GPFLT		13	// general protection fault
#define T_PGFLT		14	// page fault
/* #define T_RES	15 */	// reserved
#define T_FPERR		16	// floating point error
#define T_ALIGN		17	// aligment check
#define T_MCHK		18	// machine check
#define T_SIMDERR	19	// SIMD floating point error

#define T_IRQ0		32	// Legacy ISA hardware interrupts: IRQ0-15.

// The rest are arbitrarily chosen, but with care not to overlap
// processor defined exceptions or ISA hardware interrupt vectors.
#define T_SYSCALL	48	// System call

// We use these vectors to receive local per-CPU interrupts
#define T_LTIMER	49	// Local APIC timer interrupt
#define T_LERROR	50	// Local APIC error interrupt
#define T_PERFCTR	51	// Performance counter overflow interrupt

#define T_DEFAULT	500	// Unused trap vectors produce this value
#define T_ICNT		501	// Child process instruction count expired

// ISA hardware IRQ numbers. We receive these as (T_IRQ0 + IRQ_WHATEVER)
#define IRQ_TIMER	0	// 8253 Programmable Interval Timer (PIT)
#define IRQ_KBD		1	// Keyboard interrupt
#define IRQ_SERIAL	4	// Serial (COM) interrupt
#define IRQ_SPURIOUS	7	// Spurious interrupt
#define IRQ_IDE		14	// IDE disk controller interrupt

#ifndef __ASSEMBLER__

#include <inc/types.h>


// General registers in the format pushed by PUSHA instruction.
// We use this instruction to push the general registers only for convenience:
// modern kernels generally avoid it and save the registers manually,
// because that's just as fast or faster and they get to choose
// exactly which registers to save and where.
typedef struct pushregs {
	uint32_t reg_edi;
	uint32_t reg_esi;
	uint32_t reg_ebp;
	uint32_t reg_oesp;		/* Useless */
	uint32_t reg_ebx;
	uint32_t reg_edx;
	uint32_t reg_ecx;
	uint32_t reg_eax;
} pushregs;


// This struct represents the format of the trap frames
// that get pushed on the kernel stack by the processor
// in conjunction with the interrupt/trap entry code in trapasm.S.
// All interrupts and traps use this same format,
// although not all fields are always used:
// e.g., the error code (tf_err) applies only to some traps,
// and the processor pushes tf_esp and tf_ss
// only when taking a trap from user mode (privilege level >0).
typedef struct trapframe {

	// registers and other info we push manually in trapasm.S
	pushregs tf_regs;
	uint16_t tf_gs;		uint16_t tf_padding_gs;
	uint16_t tf_fs; 	uint16_t tf_padding_fs;
	uint16_t tf_es;		uint16_t tf_padding_es;
	uint16_t tf_ds; 	uint16_t tf_padding_ds;
	uint32_t tf_trapno;

	// format from here on determined by x86 hardware architecture
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs; 	uint16_t tf_padding_cs;
	uint32_t tf_eflags;

	// rest included only when crossing rings, e.g., user to kernel
	uintptr_t tf_esp;
	uint16_t tf_ss;		uint16_t tf_padding_ss;
} trapframe;

// size of trapframe pushed when called from user and kernel mode, respectively
#define trapframe_usize sizeof(trapframe)	// full trapframe struct
#define trapframe_ksize (sizeof(trapframe) - 8)	// no esp, ss, padding4


// Floating-point/MMX/XMM register save area format,
// in the layout defined by the processor's FXSAVE/FXRSTOR instructions.
typedef gcc_aligned(16) struct fxsave {
	uint16_t	fcw;			// byte 0
	uint16_t	fsw;
	uint16_t	ftw;
	uint16_t	fop;
	uint32_t	fpu_ip;
	uint16_t	cs;
	uint16_t	reserved1;
	uint32_t	fpu_dp;			// byte 16
	uint16_t	ds;
	uint16_t	reserved2;
	uint32_t	mxcsr;
	uint32_t	mxcsr_mask;
	uint8_t		st_mm[8][16];		// byte 32: x87/MMX registers
	uint8_t		xmm[8][16];		// byte 160: XMM registers
	uint8_t		reserved3[11][16];	// byte 288: reserved area
	uint8_t		available[3][16];	// byte 464: available to OS
} fxsave;


#endif /* !__ASSEMBLER__ */

// Must equal 'sizeof(struct trapframe)'.
// A static_assert in kern/trap.c checks this.
#define SIZEOF_STRUCT_TRAPFRAME	0x4c

#endif /* !PIOS_INC_TRAP_H */
#endif /* LAB >= 1 */
