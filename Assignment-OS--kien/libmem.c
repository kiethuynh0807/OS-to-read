/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
 pthread_mutex_lock(&mmvm_lock);
  
  /* FIX: Access mm_struct through caller->mm */
  struct mm_struct *mm = caller->krnl->mram; 
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
  
  if (!cur_vma) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
  }

  struct vm_rg_struct rgnode;

  /* 1. Try to find a hole in the existing free regions list */
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    /* FIX: Correctly index symrgtbl in mm_struct */
    mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    mm->symrgtbl[rgid].vmaid = vmaid;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* 2. Heap Expansion Logic */
  addr_t old_sbrk = cur_vma->sbrk;
  addr_t inc_sz;

#ifdef MM64
  /* FIX: Ensure PAGING_PAGESZ is used if PAGING64_PAGE_ALIGNSZ isn't defined */
  inc_sz = PAGING_PAGE_ALIGNSZ(size); 
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif

  /* 3. Invoke System Call to increase the limit */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP; 
  regs.a2 = vmaid;
  regs.a3 = inc_sz;        
  
  /* FIX: Ensure caller->krnl is the correct path to the kernel instance */
  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1; 
  }

  /* 4. Update the symbol table with the new region */
  mm->symrgtbl[rgid].rg_start = old_sbrk;
  mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  mm->symrgtbl[rgid].vmaid = vmaid;

  /* 5. Update the VMA's sbrk */
  cur_vma->sbrk = old_sbrk + size;

  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mram, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t addr;
  
  /* * Call the internal allocator. 
   * vmaid is hardcoded to 0 for standard process heap allocation.
   */
  int val = __alloc(proc, 0, reg_index, size, &addr);
  
  if (val == -1)
  {
    /* Allocation failed (Out of memory or overlap) */
    return -1;
  }

#ifdef IODUMP
  printf("--- [IODUMP: liballoc] ---\n");
  printf("Process PID: %d | Region Index: %d | Size: %lu\n", proc->pid, reg_index, size);
  printf("Allocated Address: " FORMATX_ADDR "\n", addr);

#ifdef PAGETBL_DUMP
  /* * Print the page table. 
   * Using 0 to -1 (max address) as requested to trace the entire mapping.
   */
  print_pgtbl(proc, 0, -1); 
#endif

  printf("--------------------------\n");
#endif

  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  /* * Call internal __free. 
   * vmaid is 0 by default for standard heap regions.
   */
  int val = __free(proc, 0, reg_index);
  
  if (val == -1)
  {
    /* Return error if the region index is invalid or already free */
    return -1;
  }

  // Debug trace for execution flow
  printf("%s:%d - Successfully freed region index %d\n", __func__, __LINE__, reg_index);

#ifdef IODUMP
  printf("--- [IODUMP: libfree] ---\n");
  printf("Process PID: %d | Freed Region Index: %d\n", proc->pid, reg_index);

#ifdef PAGETBL_DUMP
  /* * Dump the page table after freeing to verify if entries 
   * were cleared or marked as invalid/swapped.
   */
  print_pgtbl(proc, 0, -1); 
#endif

  printf("-------------------------\n");
#endif

  return 0;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { 
    /* Page is not in RAM. We need to bring it in. */
    addr_t vicpgn;
    addr_t swpfpn; 
    uint32_t vic_pte;

    /* 1. Find a victim page to evict from RAM (FIFO) */
    if (find_victim_page(caller->krnl->mram, &vicpgn) == -1)
      return -1;

    /* 2. Get the physical frame number (FPN) the victim is currently using */
    vic_pte = pte_get_entry(caller, vicpgn);
    addr_t vicfpn = PAGING_PTE_FPN(vic_pte);

    /* 3. Get a free slot in the SWAP device */
    if (MEMPHY_get_freefp(caller->krnl->mswp[0], &swpfpn) == -1)
      return -1;

    /* 4. Data Movement: Swap out the victim
     * Copy data from RAM (vicfpn) to SWAP (swpfpn)
     */
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->mswp[0], swpfpn);

    /* 5. Update Victim's Page Table Entry
     * It's no longer in RAM; mark it as swapped and store its SWAP location.
     */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* 6. Reuse the now-free RAM frame (vicfpn) for our new page */
    if (PAGING_PAGE_SWAPPED(pte)) {
        /* If our target page was in SWAP, bring it back */
        addr_t tgt_swpfpn = PAGING_PTE_SWPOFF(pte);
        __swap_cp_page(caller->krnl->mswp[0], tgt_swpfpn, caller->krnl->mram, vicfpn);
        MEMPHY_put_freefp(caller->krnl->mswp[0], tgt_swpfpn); // Free the swap slot
    } else {
        /* If it's a brand new page, zero it out or initialize it */
        vmap_pgd_memset(caller, pgn << PAGING_ADDR_PGN_LOBIT, 1);
    }

    /* 7. Update target Page Table Entry to point to the RAM frame */
    pte_set_fpn(caller, pgn, vicfpn);

    /* 8. Add the new page to the FIFO queue for future replacement */
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
  }

  /* Return the FPN now that the page is guaranteed to be in RAM */
  *fpn = PAGING_PTE_FPN(pte_get_entry(caller, pgn));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  /* 1. Extract Page Number (PGN) and Offset from the virtual address */
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* 2. Ensure the page is in RAM. 
   * pg_getpage handles page faults and swapping automatically.
   */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* Invalid page access (e.g., segment fault) */

  /* 3. Calculate the Physical Address in RAM:
   * Physical Address = (Frame Number * Page Size) + Offset
   */
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) | off;

  /* 4. Perform the read operation via System Call 
   * In many of these simulators, memory I/O is wrapped in a syscall
   * to ensure synchronization and proper access control.
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ; // Read operation code
  regs.a2 = phyaddr;        // Target physical address
  // regs.a3 = ...           // Could be used for data pointer or size

  /* Invoke the syscall to read from caller->mram */
  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
  {
      return -1;
  }

  /* 5. Alternatively, if your simulation allows direct MEMPHY access:
   * MEMPHY_read(caller->krnl->mram, phyaddr, data); 
   */
  MEMPHY_read(caller->krnl->mram, phyaddr, data);

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
 /* 1. Extract Page Number (PGN) and Offset */
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* 2. Bring the page into RAM if it's swapped or not yet allocated.
   * pg_getpage ensures we have a valid physical frame (fpn).
   */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* Segment Fault: Invalid memory access */

  /* 3. Calculate the Physical Address (FPN + Offset) */
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) | off;

  /* 4. Execute the Write Operation
   * Using direct MEMPHY_write to update the simulated RAM storage.
   */
  MEMPHY_write(caller->krnl->mram, phyaddr, value);

  /* 5. Optional: Syscall-based Write (if your kernel requires it for sync)
   * struct sc_regs regs;
   * regs.a1 = SYSMEM_IO_WRITE; 
   * regs.a2 = phyaddr;
   * regs.a3 = value; 
   * _syscall(caller->krnl, caller->pid, 17, &regs);
   */

  /* 6. Mark the page as DIRTY (since we just modified its content)
   * This is crucial so that if the page is later evicted, 
   * the OS knows it MUST write it back to SWAP.
   */
  uint32_t pte = pte_get_entry(caller, pgn);
  SETBIT(pte, PAGING_PTE_DIRTY_MASK);
  pte_set_entry(caller, pgn, pte);

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* 1. Retrieve the region from the symbolic table using rgid */
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  /* 2. Validation: Check if the region exists and if the offset is within bounds */
  if (currg == NULL || currg->vmaid != vmaid)
  {
    /* ERROR: Region not found or belongs to a different VMA */
    return -1;
  }

  /* Check if the offset exceeds the size of the allocated region 
   * (rg_start + offset) must be less than rg_end
   */
  if (currg->rg_start + offset >= currg->rg_end)
  {
    /* ERROR: Offset out of range (Segmentation Fault) */
    return -1;
  }

  /* 3. Perform the actual read using the paging system
   * Translation: Virtual Address = Base Address of region + Offset
   */
  addr_t target_vaddr = currg->rg_start + offset;
  
  if (pg_getval(caller->krnl->mram, target_vaddr, data, caller) != 0)
  {
    /* ERROR: Paging system failed to retrieve the value */
    return -1;
  }

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
 BYTE data;
  
  /* * Call the internal __read function.
   * Default vmaid is 0. 
   * 'source' acts as the rgid (Region ID) in the symbol table.
   */
  int val = __read(proc, 0, source, offset, &data);

  if (val == 0)
  {
    /* * Success: Store the byte into the destination register.
     * We cast to uint32_t to match the register size.
     */
    *destination = (uint32_t)data;
  }
  else
  {
    /* Error: Likely a Segmentation Fault or invalid Region ID */
    return -1;
  }

#ifdef IODUMP
  printf("--- [IODUMP: libread] ---\n");
  printf("PID: %d | Source (rgid): %u | Offset: " FORMAT_ADDR " | Data: %02x\n", 
          proc->pid, source, offset, data);

#ifdef PAGETBL_DUMP
  /* Print the full page table to track the translation during this read */
  print_pgtbl(proc, 0, -1); 
#endif

  printf("-------------------------\n");
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  /* 1. Call the internal __write function.
   * - vmaid is 0 (default heap/data area).
   * - 'destination' is used as the rgid (Region ID) to look up the base address.
   */
  int val = __write(proc, 0, destination, offset, data);

  if (val == -1)
  {
    /* Write failed: Likely a Segmentation Fault or invalid Region ID */
    return -1;
  }

#ifdef IODUMP
  printf("--- [IODUMP: libwrite] ---\n");
  printf("PID: %d | Destination (rgid): %u | Offset: " FORMAT_ADDR " | Data: %02x\n", 
          proc->pid, destination, offset, data);

#ifdef PAGETBL_DUMP
  /* * Dump the page table to see if the write triggered a page fault 
   * and to confirm the 'Dirty Bit' status.
   */
  print_pgtbl(proc, 0, -1); 
#endif

  printf("--------------------------\n");
#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  addr_t addr;
  
  /* 1. Forward the request to the internal kernel allocator helper (__kmalloc).
   * - We pass -1 for vmaid to distinguish this from a standard user VMA allocation.
   * - Kernel memory bypasses the standard VMA linked-list logic.
   */
  int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* 2. OS Level Validation
   * If the helper returns -1, it means the Kernel Cache Pool is exhausted 
   * or the requested size exceeds the maximum slab size.
   */
  if (val == -1)
  {
    /* Kernel Panics or returns error if kernel memory cannot be allocated */
    #ifdef IODUMP
       printf("KERNEL ERROR: libkmem_malloc failed for PID %d\n", caller->pid);
    #endif
    return -1;
  }

#ifdef IODUMP
  printf("--- [IODUMP: libkmem_malloc] ---\n");
  printf("Kernel Region Index: %u | Size: %u bytes\n", reg_index, size);
  printf("Kernel Physical Address: " FORMATX_ADDR "\n", addr);
  
#ifdef PAGETBL_DUMP
  /* Kernel memory is often identity-mapped or specially mapped. 
   * Dumping the table helps verify its location in the high-memory area.
   */
  print_pgtbl(caller, 0, -1);
#endif
  printf("--------------------------------\n");
#endif

  return 0;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);

  /* 1. Access the Kernel-level Memory structures.
   * Kernel memory is often managed globally or within a specific kernel MM context.
   */
  struct mm_struct *mm = caller->krnl->mm; 
  
  // Align size to page boundaries for the kernel
  addr_t inc_sz = PAGING_PAGE_ALIGNSZ(size);
  addr_t fpn;
  
  /* 2. Physical Allocation
   * Kernel memory is usually 'pinned' in RAM and not subject to FIFO eviction.
   * We grab a free frame directly from physical RAM.
   */
  if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0)
  {
     pthread_mutex_unlock(&mmvm_lock);
     return -1; // Kernel out of memory (Critical Error)
  }

  /* 3. Logical Mapping
   * Calculate a virtual address in the Kernel's reserved range.
   * For simplicity in this simulator, we often use identity mapping 
   * or a dedicated kernel sbrk.
   */
  addr_t k_vaddr = (fpn << PAGING_ADDR_FPN_LOBIT); 

  /* 4. Update the Page Table
   * Use vmap_page_range to map this physical frame into the PGD.
   * Note: Kernel entries should ideally have a 'Privileged' bit set.
   */
  vmap_page_range(caller, k_vaddr, inc_sz, fpn, NULL);

  /* 5. Update the Symbolic Table
   * Store the mapping in the symbol table so the kernel can find it by ID.
   */
  mm->symrgtbl[rgid].rg_start = k_vaddr;
  mm->symrgtbl[rgid].rg_end = k_vaddr + size;
  mm->symrgtbl[rgid].vmaid = vmaid; // -1 for kernel

  /* 6. Return the allocated address */
  *alloc_addr = k_vaddr;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* 1. Allocate the management structure for the cache pool */
  struct kcache_pool_struct *new_pool = malloc(sizeof(struct kcache_pool_struct));
  if (!new_pool) return -1;

  /* 2. Configure the pool properties */
  new_pool->size = size;   // Total bytes in the pool
  new_pool->align = align; // Size of each individual slot (e.g., 32 bytes)

  /* 3. Allocate physical backing for the pool
   * Since this is Kernel Memory, we grab a block of frames.
   * For simplicity, we allocate 'size' via the kernel's internal allocator.
   */
  addr_t pool_vaddr;
  if (__kmalloc(caller, -1, 0, size, &pool_vaddr) != 0)
  {
      free(new_pool);
      return -1;
  }

  /* 4. Link the physical storage to the pool structure 
   * In MM64, this is a 64-bit address pointer.
   */
  new_pool->storage = pool_vaddr;

  /* 5. Register the pool in the process's mm_struct
   * Note: You might need to extend mm_struct to hold an array of pools 
   * if cache_pool_id is used as an index.
   */
  caller->krnl->mm->kcpooltbl = new_pool;

#ifdef IODUMP
  printf("--- [IODUMP: Kernel Cache Created] ---\n");
  printf("Pool ID: %u | Total Size: %u | Slot Align: %u\n", cache_pool_id, size, align);
  printf("Base Virtual Address: " FORMATX_ADDR "\n", pool_vaddr);
  printf("--------------------------------------\n");
#endif

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  addr_t addr;

  /* 1. Forward the request to the internal helper __kmem_cache_alloc.
   * This helper finds the pool associated with cache_pool_id, 
   * finds a free slot (using a bitmask or free list), and returns its address.
   */
  int val = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);

  /* 2. OS Level Management & Validation
   * If val is -1, the specific cache pool is full. 
   * In a real OS, this might trigger the allocation of a new slab.
   */
  if (val != 0)
  {
#ifdef IODUMP
    printf("KERNEL ERROR: Cache pool %u is full or invalid for PID %d\n", cache_pool_id, proc->pid);
#endif
    return -1;
  }

  /* 3. Symbolic Table Registration
   * Even though this is a kernel cache slot, we store it in the symrgtbl
   * so the kernel can access it via pg_getval/pg_setval using reg_index.
   */
  // Note: Most of this registration happens inside the __kmem_cache_alloc helper.

#ifdef IODUMP
  printf("--- [IODUMP: Kernel Cache Slot Allocated] ---\n");
  printf("Pool ID: %u | Region Index: %u\n", cache_pool_id, reg_index);
  printf("Slot Physical Address: " FORMATX_ADDR "\n", addr);
  printf("---------------------------------------------\n");
#endif

  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);

  struct mm_struct *mm = caller->krnl->mm;
  struct kcache_pool_struct *pool = mm->kcpooltbl;

  /* 1. Validation: Ensure the cache pool exists and is initialized */
  if (pool == NULL || pool->storage == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* 2. Slot Discovery Logic
   * In a real kernel, we would use a bitmap or a "free list" of slots.
   * For this simulation, we'll find an available slot within the 'storage' area.
   */
  addr_t slot_vaddr = 0;
  int found = 0;

  /* Simple linear search for a free slot based on alignment 
   * (Ideally replaced by a bitmask for performance)
   */
  for (int offset = 0; offset < pool->size; offset += pool->align)
  {
      addr_t potential_addr = pool->storage + offset;
      
      /* Check if this address is already registered in the symbol table */
      int is_used = 0;
      for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
          if (mm->symrgtbl[i].rg_start == potential_addr && mm->symrgtbl[i].vmaid != -1) {
              is_used = 1;
              break;
          }
      }

      if (!is_used) {
          slot_vaddr = potential_addr;
          found = 1;
          break;
      }
  }

  if (!found)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1; // Cache pool exhausted
  }

  /* 3. Register the slot in the Symbolic Table 
   * This allows the kernel to use pg_getval/pg_setval on this cache object.
   */
  mm->symrgtbl[rgid].rg_start = slot_vaddr;
  mm->symrgtbl[rgid].rg_end = slot_vaddr + pool->align;
  mm->symrgtbl[rgid].vmaid = vmaid; // Usually -1 for kernel cache

  /* 4. Return the address */
  *alloc_addr = slot_vaddr;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* * 1. OS Level Management: Retrieve symbolic regions
   * source: User-space rgid
   * destination: Kernel-space rgid
   */
  struct vm_rg_struct *user_rg = get_symrg_byid(caller->krnl->mm, source);
  struct vm_rg_struct *kern_rg = get_symrg_byid(caller->krnl->mm, destination);

  /* 2. Validation: Ensure both symbolic regions are valid and allocated */
  if (user_rg == NULL || kern_rg == NULL) {
      return -1;
  }

  /* 3. Bounds Checking
   * Ensure source access is within user region and destination can hold the size.
   */
  if (offset + size > (user_rg->rg_end - user_rg->rg_start) ||
      size > (kern_rg->rg_end - kern_rg->rg_start)) {
      return -1; // Prevent buffer overflow or invalid memory access
  }

  /* 4. Data Transfer Loop
   * Iterate byte-by-byte to copy data from User Space to Kernel Space.
   * This uses the internal __read and __write to handle translation and swapping.
   */
  for (uint32_t i = 0; i < size; i++) {
      BYTE data;
      
      /* Read from User memory (vmaid 0) 
       * source: user rgid, offset: user-provided offset + loop index
       */
      if (__read(caller, 0, source, offset + i, &data) != 0)
          return -1;

      /* Write to Kernel memory (vmaid -1) 
       * destination: kernel rgid, offset: loop index (starts at beginning of kernel buffer)
       */
      if (__write(caller, -1, destination, i, data) != 0)
          return -1;
  }

#ifdef IODUMP
  printf("--- [IODUMP: libkmem_copy_from_user] ---\n");
  printf("PID: %d | Size: %u | Offset: %u\n", caller->pid, size, offset);
  printf("User Region (Source): %u -> Kernel Region (Destination): %u\n", source, destination);
  printf("----------------------------------------\n");
#endif

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
 /* 1. OS Level Management: Retrieve symbolic regions 
   * source: Kernel rgid (-1 vmaid)
   * destination: User rgid (0 vmaid)
   */
  struct vm_rg_struct *kern_rg = get_symrg_byid(caller->krnl->mm, source);
  struct vm_rg_struct *user_rg = get_symrg_byid(caller->krnl->mm, destination);

  /* 2. Validation: Ensure both symbolic regions are valid */
  if (kern_rg == NULL || user_rg == NULL) {
      return -1;
  }

  /* 3. Bounds Checking
   * Ensure kernel source has enough data and user destination has enough room at the offset.
   */
  if (size > (kern_rg->rg_end - kern_rg->rg_start) ||
      offset + size > (user_rg->rg_end - user_rg->rg_start)) {
      return -1; 
  }

  /* 4. Data Transfer Loop (Kernel Space -> User Space)
   * We iterate through the requested size, pulling from the kernel and pushing to the user.
   */
  for (uint32_t i = 0; i < size; i++) {
      BYTE data;
      
      /* Read from Kernel memory (vmaid -1) 
       * source: kernel rgid, offset: i (start from beginning of kernel buffer)
       */
      if (__read(caller, -1, source, i, &data) != 0)
          return -1;

      /* Write to User memory (vmaid 0) 
       * destination: user rgid, offset: user-provided offset + loop index
       */
      if (__write(caller, 0, destination, offset + i, data) != 0)
          return -1;
  }

#ifdef IODUMP
  printf("--- [IODUMP: libkmem_copy_to_user] ---\n");
  printf("PID: %d | Size: %u | User Offset: %u\n", caller->pid, size, offset);
  printf("Kernel Region (Source): %u -> User Region (Destination): %u\n", source, destination);
  printf("--------------------------------------\n");
#endif

  return 0; // Return 0 on success
}


/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
 /* 1. Retrieve the region from the symbolic table.
   * For kernel memory, vmaid is typically passed as -1.
   */
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  /* 2. Validation: Ensure the kernel region exists and matches the vmaid */
  if (currg == NULL || currg->vmaid != vmaid)
  {
    return -1;
  }

  /* 3. Bounds Checking
   * Ensure the offset does not exceed the size of the kernel region.
   */
  if (offset >= (currg->rg_end - currg->rg_start))
  {
    return -1; // Kernel access violation
  }

  /* 4. Address Translation
   * Calculate the Virtual Address in the kernel space.
   */
  addr_t target_vaddr = currg->rg_start + offset;

  /* 5. Memory Retrieval
   * Use pg_getval to access the data. 
   * Note: In MM64, this will use the kernel's mapping in the PGD.
   */
  if (pg_getval(caller->krnl->mm, target_vaddr, data, caller) != 0)
  {
      return -1;
  }

  return 0;
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* 1. Retrieve the kernel region from the symbolic table 
   * Kernel regions are identified by rgid and are typically associated with vmaid = -1.
   */
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  /* 2. Validation: Ensure the region is valid and belongs to kernel space */
  if (currg == NULL || currg->vmaid != vmaid)
  {
    return -1;
  }

  /* 3. Bounds Checking
   * Ensure the write offset does not exceed the allocated size of the kernel region.
   */
  if (offset >= (currg->rg_end - currg->rg_start))
  {
    return -1; // Kernel memory access violation (overflow)
  }

  /* 4. Logical Address Calculation
   * target_vaddr = Kernel Base Address + Offset
   */
  addr_t target_vaddr = currg->rg_start + offset;

  /* 5. Execution of the Write
   * Use pg_setval to handle translation and physical memory update.
   * This also handles setting the 'Dirty Bit' if the kernel memory is swappable.
   */
  if (pg_setval(caller->krnl->mm, target_vaddr, value, caller) != 0)
  {
      return -1;
  }

  return 0;
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
 /* 1. Retrieve the region from the symbolic table 
   * User memory regions are typically associated with vmaid >= 0 (e.g., Heap, Stack).
   */
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  /* 2. Validation: Ensure the region exists and belongs to a user VMA */
  if (currg == NULL || currg->vmaid < 0 || currg->vmaid != vmaid)
  {
    return -1; // Access denied or invalid region
  }

  /* 3. Bounds Checking
   * Ensure the requested offset is within the allocated size of this symbolic region.
   */
  if (offset >= (currg->rg_end - currg->rg_start))
  {
    return -1; // Segmentation fault: offset out of bounds
  }

  /* 4. Logical Address Calculation
   * target_vaddr = Region Start + Offset
   */
  addr_t target_vaddr = currg->rg_start + offset;

  /* 5. Execution: Fetch data using the Paging System
   * pg_getval handles the Page Table walk (PGD -> PUD -> PMD -> PTE)
   * and triggers swapping if the page is currently on disk.
   */
  if (pg_getval(caller->krnl->mm, target_vaddr, data, caller) != 0)
  {
      return -1;
  }

  return 0;
}


/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* 1. Retrieve the symbolic region
   * User memory regions are associated with a vmaid >= 0.
   */
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  /* 2. Validation: Ensure the region exists and belongs to a user-level VMA */
  if (currg == NULL || currg->vmaid < 0 || currg->vmaid != vmaid)
  {
    return -1; // Unauthorized access or invalid region ID
  }

  /* 3. Bounds Checking
   * Ensure the write offset does not exceed the allocated size of the region.
   */
  if (offset >= (currg->rg_end - currg->rg_start))
  {
    return -1; // Segmentation fault: writing out of bounds
  }

  /* 4. Logical Address Calculation
   * target_vaddr = Virtual Base Address of the region + offset
   */
  addr_t target_vaddr = currg->rg_start + offset;

  /* 5. Memory Write via Paging System
   * pg_setval performs the translation using caller->mm->pgd.
   * It also ensures the page is in RAM and sets the 'Dirty Bit'.
   */
  if (pg_setval(caller->krnl->mm, target_vaddr, value, caller) != 0)
  {
      return -1;
  }

  return 0;
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
 struct pgn_t *pg = mm->fifo_pgn;

  /* 1. Validation: If the list is empty, there are no pages to evict */
  if (!pg)
  {
    return -1;
  }

  /* 2. Traverse to the end of the FIFO list.
   * The 'tail' of the list represents the oldest page in memory.
   */
  struct pgn_t *prev = NULL;
  
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }

  /* 3. Extract the Page Number (pgn) to be returned */
  *retpgn = pg->pgn;

  /* 4. Update the FIFO list pointers */
  if (prev == NULL)
  {
    /* Case: Only one page was in the list. 
     * The list head now becomes NULL.
     */
    mm->fifo_pgn = NULL;
  }
  else
  {
    /* Case: Multiple pages in list. 
     * Disconnect the tail.
     */
    prev->pg_next = NULL;
  }

  /* 5. Clean up the management structure to prevent memory leaks */
  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
