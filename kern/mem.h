#if LAB >= 1
// Physical memory management definitions.
// See COPYRIGHT for copyright information.
#ifndef PIOS_KERN_MEM_H
#define PIOS_KERN_MEM_H
#ifndef PIOS_KERNEL
# error "This is a PIOS kernel header; user programs should not #include it"
#endif


// At PHYS_IOMEM (640K) there is a 384K hole for I/O.  From the kernel,
// PHYS_IOMEM can be addressed at VM_KERNLO + PHYS_IOMEM.  The hole ends
// at physical address PHYS_EXTMEM.
#define MEM_IOMEM	0x0A0000
#define MEM_EXTMEM	0x100000


// Given a physical address,
// return a C pointer the kernel can use to access it.
// This macro does nothing in PIOS because physical memory
// is mapped into the kernel's virtual address space at address 0,
// but this is not the case for many other systems such as JOS or Linux,
// which must do some translation here (usually just adding an offset).
#define mem_ptr(physaddr)	((void*)(physaddr))


// A pageinfo struct holds metadata on how a particular physical page is used.
// On boot we allocate a big array of pageinfo structs, one per physical page.
// This could be a union instead of a struct,
// since only one member is used for a given page state (free, allocated) -
// but that might make debugging a bit more challenging.
typedef struct pageinfo {
	struct pageinfo	*free_next;	// Next page number on free list
	uint32_t	refs;		// Reference count on allocated pages
} pageinfo;


// The pmem module sets up the following globals during mem_init().
extern size_t mem_max;		// Maximum physical address
extern size_t mem_npage;	// Total number of physical memory pages
extern pageinfo *mem_pageinfo;	// Metadata array indexed by page number


// Detect available physical memory and initialize the mem_pageinfo array.
void mem_init(void);

// Allocate a physical page and return a pointer to its pageinfo struct.
// Returns NULL if no more physical pages are available.
pageinfo *mem_alloc(void);

// Return a physical page to the free list.
void mem_free(pageinfo *pi);

// Check the physical page allocator (mem_alloc(), mem_free())
// for correct operation after initialization via mem_init().
void mem_check(void);

#endif /* !PIOS_KERN_MEM_H */
#endif // LAB >= 1
