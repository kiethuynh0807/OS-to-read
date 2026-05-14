/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * mm.c  –  Core paging memory-management implementation
 */

#ifdef MM_PAGING

#include "mm.h"
#ifdef MM64
#include "mm64.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*=======================================================================
 * Page-table entry helpers
 *=====================================================================*/

/*
 * init_pte  –  compose and write a PTE bit-pattern into *pte
 *   pre    : 1 = present in RAM
 *   fpn    : physical frame number (when pre == 1)
 *   drt    : dirty bit
 *   swp    : 1 = swapped out
 *   swptyp : swap device type  (when swp == 1)
 *   swpoff : swap offset       (when swp == 1)
 */
int init_pte(addr_t *pte,
             int pre, addr_t fpn,
             int drt,
             int swp, int swptyp, addr_t swpoff)
{
    if (pte == NULL)
        return -1;

    *pte = 0;

    if (pre) {
        *pte |= PAGING_PTE_PRESENT_MASK;
        SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    if (drt)
        *pte |= PAGING_PTE_DIRTY_MASK;
    if (swp) {
        *pte |= PAGING_PTE_SWAPPED_MASK;
        SETVAL(*pte, (addr_t)swptyp, PAGING_PTE_SWPTYP_MASK,
               PAGING_PTE_SWPTYP_LOBIT);
        SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK,
               PAGING_PTE_SWPOFF_LOBIT);
    }

    return 0;
}

/*=======================================================================
 * mm_struct / VMA initialisation
 *=====================================================================*/

/*
 * init_mm  –  initialise the memory-management structure for a process
 *
 * Creates one initial user VMA (vmaid = 0) and one kernel VMA (vmaid = 1).
 * The VMA starts with sbrk == vm_start; no physical memory is committed yet.
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
    if (mm == NULL || caller == NULL)
        return -1;

    memset(mm, 0, sizeof(struct mm_struct));

    /* Page-table root is created lazily by pte_set_fpn (mm64.c) */
    mm->pgd      = NULL;
    mm->fifo_pgn = NULL;
    mm->kcpooltbl = NULL;

    /* ---- VMA 0: user heap/data segment ---- */
    struct vm_area_struct *uvma =
        (struct vm_area_struct *)malloc(sizeof(struct vm_area_struct));
    if (uvma == NULL)
        return -1;

    uvma->vm_id    = 0;
    uvma->vm_start = 0;
    /*
     * pages.  We set it equal to the maximum addressable byte so the
     * VMA can grow on demand via inc_vma_limit, but the initial sbrk
     * is 0 so no pages are actually mapped yet.
     */
#ifdef MM64
    uvma->vm_end = (addr_t)PAGING64_MAX_PGN * PAGING64_PAGESZ;
#else
    uvma->vm_end = (addr_t)(1 << PAGING_CPU_BUS_WIDTH);
#endif
    uvma->sbrk          = 0;
    uvma->vm_mm         = mm;
    uvma->vm_freerg_list = NULL;
    uvma->vm_next        = NULL;

    /* ---- VMA 1: kernel memory space ---- */
    struct vm_area_struct *kvma =
        (struct vm_area_struct *)malloc(sizeof(struct vm_area_struct));
    if (kvma == NULL) {
        free(uvma);
        return -1;
    }

    kvma->vm_id    = 1;
#ifdef MM64
    /* Kernel occupies the high-address canonical range.
     * For this simulation we use a flat offset above user space. */
    kvma->vm_start = (addr_t)PAGING64_MAX_PGN * PAGING64_PAGESZ;
    kvma->vm_end   = kvma->vm_start
                   + (addr_t)PAGING64_MAX_PGN * PAGING64_PAGESZ;
#else
    kvma->vm_start = (addr_t)(1 << PAGING_CPU_BUS_WIDTH);
    kvma->vm_end   = kvma->vm_start + (addr_t)(1 << PAGING_CPU_BUS_WIDTH);
#endif
    kvma->sbrk          = kvma->vm_start;
    kvma->vm_mm         = mm;
    kvma->vm_freerg_list = NULL;
    kvma->vm_next        = NULL;

    /* Link: uvma → kvma → NULL */
    uvma->vm_next = kvma;
    mm->mmap      = uvma;

    /* Initialise the symbol table */
    memset(mm->symrgtbl, 0, sizeof(mm->symrgtbl));

    return 0;
}

/*=======================================================================
 * VMA / symbol-table accessors
 * FIX: accept mm_struct* directly (mm lives in krnl_t)
 *=====================================================================*/

struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
    if (mm == NULL)
        return NULL;
    struct vm_area_struct *vma = mm->mmap;
    while (vma != NULL) {
        if ((int)vma->vm_id == vmaid)
            return vma;
        vma = vma->vm_next;
    }
    return NULL;
}

struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
    if (mm == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
        return NULL;
    return &mm->symrgtbl[rgid];
}

/*
 * validate_overlap_vm_area
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid,
                             addr_t vmastart, addr_t vmaend)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct vm_area_struct *vma = caller->krnl->mm->mmap;
    while (vma != NULL) {
        if ((int)vma->vm_id != vmaid) {
            /* Explicit overlap test, not the broken OVERLAP macro */
            if (vmastart < vma->vm_end && vmaend > vma->vm_start)
                return -1;
        }
        vma = vma->vm_next;
    }
    return 0;
}

/*=======================================================================
 * Core alloc / free
 *=====================================================================*/

/*
 * __alloc  –  allocate `size` bytes in VMA vmaid, record as region rgid
 *
 *   1. Search vm_freerg_list for a free region of at least `size` bytes.
 *   2. If none, call inc_vma_limit to grow sbrk and get new pages.
 *   3. Record in mm->symrgtbl[rgid].
 *   4. Return the region start in *alloc_addr.
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid,
            addr_t size, addr_t *alloc_addr)
{
    if (caller == NULL || caller->krnl == NULL
        || caller->krnl->mm == NULL || alloc_addr == NULL)
        return -1;

    struct mm_struct *mm = caller->krnl->mm;   /* FIX */
    struct vm_rg_struct newrg;
    memset(&newrg, 0, sizeof(newrg));

    if (get_free_vmrg_area(caller, vmaid, (int)size, &newrg) != 0) {
        if (inc_vma_limit(caller, vmaid, size) != 0)
            return -1;
        if (get_free_vmrg_area(caller, vmaid, (int)size, &newrg) != 0)
            return -1;
    }

    if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
        mm->symrgtbl[rgid].rg_start = newrg.rg_start;
        mm->symrgtbl[rgid].rg_end   = newrg.rg_end;
        mm->symrgtbl[rgid].vmaid    = vmaid;
    }

    *alloc_addr = newrg.rg_start;

#ifdef IODUMP
    printf("liballoc:%d\n", (int)newrg.rg_start);
    print_pgtbl(caller, newrg.rg_start, newrg.rg_end);
#endif

    return 0;
}

/*
 * __free  –  release the region recorded at symrgtbl[rgid]
 *
 * Physical frames are NOT immediately reclaimed (avoids holes);
 * the region is put back onto vm_freerg_list for re-use.
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;
    if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
        return -1;

    struct mm_struct *mm = caller->krnl->mm;   /* FIX */

    struct vm_rg_struct *rg = get_symrg_byid(mm, rgid);
    if (rg == NULL || (rg->rg_start == 0 && rg->rg_end == 0))
        return -1;

    struct vm_area_struct *vma = get_vma_by_num(mm, vmaid);
    if (vma == NULL)
        return -1;

    struct vm_rg_struct *freed = init_vm_rg(rg->rg_start, rg->rg_end);
    if (freed == NULL)
        return -1;
    freed->vmaid = vmaid;
    enlist_vm_rg_node(&vma->vm_freerg_list, freed);

    /* Clear the symbol table entry */
    rg->rg_start = 0;
    rg->rg_end   = 0;
    rg->vmaid    = -1;

#ifdef IODUMP
    printf("libfree:%d\n", rgid);
    print_pgtbl(caller, freed->rg_start, freed->rg_end);
#endif

    return 0;
}

/*=======================================================================
 * Address translation with swap support
 *=====================================================================*/

static int addr_to_physical(struct pcb_t *caller, addr_t vaddr,
                             addr_t *fpn, addr_t *off)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

#ifdef MM64
    addr_t pgsz = PAGING64_PAGESZ;
#else
    addr_t pgsz = PAGING_PAGESZ;
#endif

    addr_t pgn  = vaddr / pgsz;
    addr_t poff = vaddr % pgsz;

    uint32_t pte = pte_get_entry(caller, pgn);

    if (PAGING_PAGE_PRESENT(pte)) {
        *fpn = GETVAL(pte, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
        *off = poff;
        return 0;
    }

    if (pte & PAGING_PTE_SWAPPED_MASK) {
        int    swptyp = (int)GETVAL(pte, PAGING_PTE_SWPTYP_MASK,
                                    PAGING_PTE_SWPTYP_LOBIT);
        addr_t swpoff = GETVAL(pte, PAGING_PTE_SWPOFF_MASK,
                               PAGING_PTE_SWPOFF_LOBIT);

        /* FIX: mswp via krnl */
        struct memphy_struct **mswp = caller->krnl->mswp;
        struct memphy_struct  *mram = caller->krnl->mram;   /* FIX */

        if (mswp == NULL || mswp[swptyp] == NULL)
            return -1;

        addr_t new_fpn;
        if (MEMPHY_get_freefp(mram, &new_fpn) != 0) {
            /* RAM full – evict FIFO victim */
            addr_t victim_pgn;
            if (find_victim_page(caller->krnl->mm, &victim_pgn) != 0)
                return -1;

            uint32_t victim_pte = pte_get_entry(caller, victim_pgn);
            addr_t   victim_fpn = GETVAL(victim_pte, PAGING_PTE_FPN_MASK,
                                         PAGING_PTE_FPN_LOBIT);

            struct memphy_struct *active_swp =
                caller->krnl->mswp[caller->krnl->active_mswp_id];   /* FIX */
            addr_t swp_slot;
            if (MEMPHY_get_freefp(active_swp, &swp_slot) != 0)
                return -1;

            __swap_cp_page(mram, victim_fpn, active_swp, swp_slot);
            MEMPHY_put_freefp(mram, victim_fpn);
            pte_set_swap(caller, victim_pgn,
                         (int)caller->krnl->active_mswp_id, swp_slot);

            new_fpn = victim_fpn;
        }

        __swap_cp_page(mswp[swptyp], swpoff, mram, new_fpn);
        MEMPHY_put_freefp(mswp[swptyp], swpoff);
        pte_set_fpn(caller, pgn, new_fpn);
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);

        *fpn = new_fpn;
        *off = poff;
        return 0;
    }

    return -1;   /* unmapped page */
}

/*=======================================================================
 * __read / __write
 *=====================================================================*/

int __read(struct pcb_t *caller, int vmaid __attribute__((unused)), int rgid,
           addr_t offset, BYTE *data)
{
    if (caller == NULL || caller->krnl == NULL
        || caller->krnl->mm == NULL || data == NULL)
        return -1;

    struct vm_rg_struct *rg = get_symrg_byid(caller->krnl->mm, rgid);
    if (rg == NULL || rg->rg_start == rg->rg_end)
        return -1;

    addr_t vaddr = rg->rg_start + offset;
    if (vaddr >= rg->rg_end)
        return -1;

    addr_t fpn, off;
    if (addr_to_physical(caller, vaddr, &fpn, &off) != 0)
        return -1;

#ifdef MM64
    addr_t phyaddr = fpn * PAGING64_PAGESZ + off;
#else
    addr_t phyaddr = fpn * PAGING_PAGESZ + off;
#endif

    return MEMPHY_read(caller->krnl->mram, phyaddr, data);   /* FIX */
}

int __write(struct pcb_t *caller, int vmaid __attribute__((unused)), int rgid,
            addr_t offset, BYTE value)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct vm_rg_struct *rg = get_symrg_byid(caller->krnl->mm, rgid);
    if (rg == NULL || rg->rg_start == rg->rg_end)
        return -1;

    addr_t vaddr = rg->rg_start + offset;
    if (vaddr >= rg->rg_end)
        return -1;

    addr_t fpn, off;
    if (addr_to_physical(caller, vaddr, &fpn, &off) != 0)
        return -1;

#ifdef MM64
    addr_t phyaddr = fpn * PAGING64_PAGESZ + off;
#else
    addr_t phyaddr = fpn * PAGING_PAGESZ + off;
#endif

    /* Mark PTE dirty */
#ifdef MM64
    addr_t pgn = vaddr / PAGING64_PAGESZ;
#else
    addr_t pgn = vaddr / PAGING_PAGESZ;
#endif
    uint32_t pte = pte_get_entry(caller, pgn);
    pte |= PAGING_PTE_DIRTY_MASK;
    pte_set_entry(caller, pgn, pte);

    return MEMPHY_write(caller->krnl->mram, phyaddr, value);   /* FIX */
}

/*=======================================================================
 * User-space / kernel-space read-write wrappers
 *=====================================================================*/

int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid,
                    addr_t offset, BYTE *data)
{
    if (vmaid != 0) return -1;
    return __read(caller, vmaid, rgid, offset, data);
}

int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid,
                     addr_t offset, BYTE value)
{
    if (vmaid != 0) return -1;
    return __write(caller, vmaid, rgid, offset, value);
}

int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid,
                      addr_t offset, BYTE *data)
{
    if (vmaid < 1) return -1;
    return __read(caller, vmaid, rgid, offset, data);
}

int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid,
                       addr_t offset, BYTE value)
{
    if (vmaid < 1) return -1;
    return __write(caller, vmaid, rgid, offset, value);
}

/*=======================================================================
 * Top-level VM entry points declared in mm.h
 *
 * mm.h declares pgalloc / pgfree_data / pgread / pgwrite.
 * libmem.c provides the liballoc/libfree/libread/libwrite wrappers
 * that cpu.c calls; those wrappers delegate to __alloc/__free/__read/
 * __write.  We implement the pg* names here as required by mm.h.
 *
 * Note: libread's destination parameter is uint32_t* in libmem.h so
 * that the caller can receive the value; pgread keeps uint32_t (the
 * register index) as declared in mm.h — libmem.c passes &proc->regs[].
 *=====================================================================*/

/*
 * pgalloc  –  ALLOC instruction handler (mm.h declaration)
 */
int pgalloc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
    if (proc == NULL)
        return -1;

    addr_t addr = 0;
    if (__alloc(proc, 0, (int)reg_index, (addr_t)size, &addr) != 0)
        return -1;

    proc->regs[reg_index] = addr;
    return 0;
}

/*
 * pgfree_data  –  FREE instruction handler (mm.h declaration)
 */
int pgfree_data(struct pcb_t *proc, uint32_t reg_index)
{
    if (proc == NULL)
        return -1;
    return __free(proc, 0, (int)reg_index);
}

/*
 * pgread  –  READ instruction handler (mm.h declaration)
 */
int pgread(struct pcb_t *proc, uint32_t source,
           addr_t offset, uint32_t destination)
{
    if (proc == NULL)
        return -1;

    BYTE data = 0;
    int ret = __read_user_mem(proc, 0, (int)source, offset, &data);
    if (ret != 0)
        return ret;

    proc->regs[destination] = (addr_t)data;

#ifdef IODUMP
    if (proc->krnl && proc->krnl->mm) {
        printf("libread reg=%d offset=%lu data=%d\n",
               (int)destination, (unsigned long)offset, (int)data);
        print_pgtbl(proc,
                    proc->krnl->mm->symrgtbl[source].rg_start,
                    proc->krnl->mm->symrgtbl[source].rg_end);
    }
#endif

    return 0;
}

/*
 * pgwrite  –  WRITE instruction handler (mm.h declaration)
 */
int pgwrite(struct pcb_t *proc, BYTE data,
            uint32_t destination, addr_t offset)
{
    if (proc == NULL)
        return -1;

    int ret = __write_user_mem(proc, 0, (int)destination, offset, data);

#ifdef IODUMP
    if (proc->krnl && proc->krnl->mm) {
        printf("libwrite data=%d reg=%d offset=%lu\n",
               (int)data, (int)destination, (unsigned long)offset);
        print_pgtbl(proc,
                    proc->krnl->mm->symrgtbl[destination].rg_start,
                    proc->krnl->mm->symrgtbl[destination].rg_end);
    }
#endif

    return ret;
}

#endif  /* MM_PAGING */
