/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
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
 * Region / page list helpers
 *=====================================================================*/

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
    struct vm_rg_struct *rgnode =
        (struct vm_rg_struct *)malloc(sizeof(struct vm_rg_struct));
    if (rgnode == NULL)
        return NULL;
    rgnode->rg_start = rg_start;
    rgnode->rg_end   = rg_end;
    rgnode->vmaid    = 0;
    rgnode->rg_next  = NULL;
    return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
    if (rglist == NULL || rgnode == NULL)
        return -1;
    rgnode->rg_next = *rglist;
    *rglist = rgnode;
    return 0;
}

int enlist_pgn_node(struct pgn_t **pgnlist, addr_t pgn)
{
    if (pgnlist == NULL)
        return -1;
    struct pgn_t *node = (struct pgn_t *)malloc(sizeof(struct pgn_t));
    if (node == NULL)
        return -1;
    node->pgn     = pgn;
    node->pg_next = *pgnlist;
    *pgnlist = node;
    return 0;
}

/*=======================================================================
 * Physical frame allocation
 *=====================================================================*/

addr_t alloc_pages_range(struct pcb_t *caller, int incpgnum,
                         struct framephy_struct **frm_lst)
{
    if (caller == NULL || caller->krnl == NULL || frm_lst == NULL)
        return -1;

    *frm_lst = NULL;
    int allocated = 0;

    struct memphy_struct *mram = caller->krnl->mram;
    if (mram == NULL)
        return 0;

    for (int i = 0; i < incpgnum; i++) {
        addr_t fpn;
        if (MEMPHY_get_freefp(mram, &fpn) != 0)
            break;   /* RAM exhausted */

        struct framephy_struct *node =
            (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
        if (node == NULL) {
            MEMPHY_put_freefp(mram, fpn);
            break;
        }
        node->fpn     = fpn;
        node->owner   = caller->krnl->mm;   /* FIX: krnl->mm */
        node->fp_next = *frm_lst;
        *frm_lst = node;
        allocated++;
    }

    return (addr_t)allocated;
}

/*=======================================================================
 * Page-table range operations
 *=====================================================================*/

addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
                       struct framephy_struct *frames,
                       struct vm_rg_struct *ret_rg)
{
    if (caller == NULL || caller->krnl == NULL
        || caller->krnl->mm == NULL || frames == NULL)
        return 0;

    struct mm_struct *mm = caller->krnl->mm;   /* FIX */
    struct framephy_struct *fnode = frames;
    addr_t cur_addr = addr;

    for (int i = 0; i < pgnum && fnode != NULL; i++) {
#ifdef MM64
        addr_t pgn = cur_addr / PAGING64_PAGESZ;
#else
        addr_t pgn = PAGING_PGN(cur_addr);
#endif
        pte_set_fpn(caller, pgn, fnode->fpn);
        enlist_pgn_node(&mm->fifo_pgn, pgn);

        fnode = fnode->fp_next;
#ifdef MM64
        cur_addr += PAGING64_PAGESZ;
#else
        cur_addr += PAGING_PAGESZ;
#endif
    }

    if (ret_rg != NULL) {
        ret_rg->rg_start = addr;
        ret_rg->rg_end   = cur_addr;
    }

    return cur_addr;
}

/*
 * vmap_pgd_memset  –  dummy-populate PTEs for the 64-bit syscall test
 * FIX: mm via caller->krnl->mm
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

#ifdef MM64
    addr_t cur = addr;
    for (int i = 0; i < pgnum; i++) {
        addr_t pgn = cur / PAGING64_PAGESZ;
        /* Mark present+dirty as a dummy marker (no real frame) */
        uint32_t pte = (uint32_t)(PAGING_PTE_PRESENT_MASK | PAGING_PTE_DIRTY_MASK);
        pte_set_entry(caller, pgn, pte);
        cur += PAGING64_PAGESZ;
    }
#else
    (void)addr; (void)pgnum;
#endif
    return 0;
}

addr_t vm_map_range(struct pcb_t *caller,
                    addr_t astart, addr_t aend, addr_t mapstart,
                    int incpgnum, struct vm_rg_struct *ret_rg)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return 0;

    struct framephy_struct *frm_lst = NULL;
    addr_t got = alloc_pages_range(caller, incpgnum, &frm_lst);
    if ((int)got < incpgnum || frm_lst == NULL)
        return 0;

    (void)astart; (void)aend;
    addr_t end = vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

    struct framephy_struct *fp = frm_lst;
    while (fp != NULL) {
        struct framephy_struct *next = fp->fp_next;
        free(fp);
        fp = next;
    }

    return end;
}

addr_t vm_map_kernel(struct pcb_t *caller,
                     addr_t astart, addr_t aend, addr_t mapstart,
                     int incpgnum, struct vm_rg_struct *ret_rg)
{
    return vm_map_range(caller, astart, aend, mapstart, incpgnum, ret_rg);
}

/*=======================================================================
 * VMA limit management
 *=====================================================================*/

int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct mm_struct *mm = caller->krnl->mm;   /* FIX */
    struct vm_area_struct *vma = get_vma_by_num(mm, vmaid);
    if (vma == NULL)
        return -1;

#ifdef MM64
    addr_t pgsz = PAGING64_PAGESZ;
#else
    addr_t pgsz = PAGING_PAGESZ;
#endif

    addr_t inc_pages = DIV_ROUND_UP(inc_sz, pgsz);
    addr_t inc_bytes = inc_pages * pgsz;

    addr_t old_sbrk = vma->sbrk;
    addr_t new_sbrk = old_sbrk + inc_bytes;

    /*
     * FIX: if new_sbrk would exceed the current vm_end, extend vm_end
     * to accommodate it rather than failing.  This allows the heap to
     * grow dynamically, which is the expected behaviour.
     */
    if (new_sbrk > vma->vm_end)
        vma->vm_end = new_sbrk;

    /* Allocate and map the new physical pages */
    struct framephy_struct *frm_lst = NULL;
    addr_t got = alloc_pages_range(caller, (int)inc_pages, &frm_lst);
    if ((int)got < (int)inc_pages || frm_lst == NULL)
        return -1;

    struct vm_rg_struct mapped_rg;
    vmap_page_range(caller, old_sbrk, (int)inc_pages, frm_lst, &mapped_rg);

    struct framephy_struct *fp = frm_lst;
    while (fp != NULL) {
        struct framephy_struct *next = fp->fp_next;
        free(fp);
        fp = next;
    }

    /* Add the newly mapped region to the free list */
    struct vm_rg_struct *new_rg = init_vm_rg(old_sbrk, new_sbrk);
    if (new_rg == NULL)
        return -1;
    new_rg->vmaid = vmaid;
    enlist_vm_rg_node(&vma->vm_freerg_list, new_rg);

    vma->sbrk = new_sbrk;
    return 0;
}

/*
 * get_free_vmrg_area  –  first-fit search in a VMA's free list
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size,
                       struct vm_rg_struct *newrg)
{
    if (caller == NULL || caller->krnl == NULL
        || caller->krnl->mm == NULL || newrg == NULL)
        return -1;

    struct vm_area_struct *vma = get_vma_by_num(caller->krnl->mm, vmaid);
    if (vma == NULL)
        return -1;

    struct vm_rg_struct *prev = NULL;
    struct vm_rg_struct *cur  = vma->vm_freerg_list;

    while (cur != NULL) {
        addr_t rg_sz = cur->rg_end - cur->rg_start;
        if ((addr_t)size <= rg_sz) {
            newrg->rg_start = cur->rg_start;
            newrg->rg_end   = cur->rg_start + (addr_t)size;
            newrg->vmaid    = vmaid;

            if (rg_sz == (addr_t)size) {
                if (prev == NULL)
                    vma->vm_freerg_list = cur->rg_next;
                else
                    prev->rg_next = cur->rg_next;
                free(cur);
            } else {
                cur->rg_start += (addr_t)size;
            }
            return 0;
        }
        prev = cur;
        cur  = cur->rg_next;
    }

    return -1;
}

/*=======================================================================
 * Page replacement
 *=====================================================================*/

int find_victim_page(struct mm_struct *mm, addr_t *pgn)
{
    if (mm == NULL || pgn == NULL || mm->fifo_pgn == NULL)
        return -1;

    /* Walk to the tail (oldest entry = FIFO victim) */
    struct pgn_t *prev = NULL;
    struct pgn_t *cur  = mm->fifo_pgn;
    while (cur->pg_next != NULL) {
        prev = cur;
        cur  = cur->pg_next;
    }

    *pgn = cur->pgn;

    if (prev == NULL)
        mm->fifo_pgn = NULL;
    else
        prev->pg_next = NULL;

    free(cur);
    return 0;
}

/*=======================================================================
 * Page swap copy
 *=====================================================================*/

int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
    if (mpsrc == NULL || mpdst == NULL)
        return -1;

#ifdef MM64
    addr_t pgsz = PAGING64_PAGESZ;
#else
    addr_t pgsz = PAGING_PAGESZ;
#endif

    addr_t src_base = srcfpn * pgsz;
    addr_t dst_base = dstfpn * pgsz;

    for (addr_t i = 0; i < pgsz; i++) {
        BYTE val = 0;
        if (MEMPHY_read(mpsrc,  src_base + i, &val) != 0) return -1;
        if (MEMPHY_write(mpdst, dst_base + i,  val) != 0) return -1;
    }
    return 0;
}

/*
 * __mm_swap_page  –  swap one virtual page out to the active swap device
 *
 * Called by sys_mem.c (syscall SYSMEM_SWP_OP) as:
 *   __mm_swap_page(caller, vicfpn, swpfpn)
 *
 * Copies the frame at vicfpn in RAM to swpfpn in the active swap device.
 * Uses caller->krnl->mram and caller->krnl->active_mswp.
 */
int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn, addr_t swpfpn)
{
    if (caller == NULL || caller->krnl == NULL)
        return -1;
    return __swap_cp_page(caller->krnl->mram,
                          vicfpn,
                          caller->krnl->active_mswp,
                          swpfpn);
}

/*=======================================================================
 * Kernel memory allocators
 * NOTE: __kmem_cache_create is NOT declared in mm.h (upstream omission).
 * libmem.c's libkmem_cache_pool_create must call __kmem_cache_create().
 * Add this forward declaration to mm.h, or add it locally in libmem.c:
 *   addr_t __kmem_cache_create(struct pcb_t*, int, addr_t, addr_t);
 *=====================================================================*/

addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid,
                 addr_t size, addr_t *alloc_addr)
{
    if (caller == NULL || caller->krnl == NULL
        || caller->krnl->mm == NULL || alloc_addr == NULL)
        return (addr_t)-1;

    struct mm_struct *mm = caller->krnl->mm;   /* FIX */
    struct vm_rg_struct newrg;
    memset(&newrg, 0, sizeof(newrg));

    if (get_free_vmrg_area(caller, vmaid, (int)size, &newrg) != 0) {
        if (inc_vma_limit(caller, vmaid, size) != 0)
            return (addr_t)-1;
        if (get_free_vmrg_area(caller, vmaid, (int)size, &newrg) != 0)
            return (addr_t)-1;
    }

    if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
        mm->symrgtbl[rgid].rg_start = newrg.rg_start;
        mm->symrgtbl[rgid].rg_end   = newrg.rg_end;
        mm->symrgtbl[rgid].vmaid    = vmaid;
    }

    *alloc_addr = newrg.rg_start;
    return 0;
}

/*
 * __kmem_cache_create  –  allocate and register a slab cache pool.
 *
 * remain NULL and __kmem_cache_alloc to always return -1.
 *
 * @cache_pool_id : index into kcpooltbl[]
 * @size          : slab object size in bytes
 * @align         : alignment requirement
 */
#define PAGING_MAX_KCPOOL 8   /* max number of kernel cache pools */

addr_t __kmem_cache_create(struct pcb_t *caller, int cache_pool_id,
                            addr_t size, addr_t align)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return (addr_t)-1;

    struct mm_struct *mm = caller->krnl->mm;

    /* Allocate the pool table on first use */
    if (mm->kcpooltbl == NULL) {
        mm->kcpooltbl = (struct kcache_pool_struct *)
            calloc(PAGING_MAX_KCPOOL, sizeof(struct kcache_pool_struct));
        if (mm->kcpooltbl == NULL)
            return (addr_t)-1;
    }

    if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_KCPOOL)
        return (addr_t)-1;

    /* Allocate a contiguous physical region for the pool backing store */
    addr_t alloc_addr = 0;
    if (__kmalloc(caller, 1 /* kernel vmaid */, -1, size, &alloc_addr) != 0)
        return (addr_t)-1;

    mm->kcpooltbl[cache_pool_id].size    = (int)size;
    mm->kcpooltbl[cache_pool_id].align   = (int)align;
    mm->kcpooltbl[cache_pool_id].storage = alloc_addr;

    return 0;
}

/*
 * __kmem_cache_alloc  –  allocate from a slab cache pool
 */
addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid,
                          int cache_pool_id, addr_t *alloc_addr)
{
    if (caller == NULL || caller->krnl == NULL
        || caller->krnl->mm == NULL || alloc_addr == NULL)
        return (addr_t)-1;

    struct mm_struct *mm = caller->krnl->mm;

    if (mm->kcpooltbl == NULL)
        return (addr_t)-1;

    struct kcache_pool_struct *pool = &mm->kcpooltbl[cache_pool_id];
    if (pool->size <= 0)
        return (addr_t)-1;

    *alloc_addr = pool->storage;

    if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
        mm->symrgtbl[rgid].rg_start = pool->storage;
        mm->symrgtbl[rgid].rg_end   = pool->storage + (addr_t)pool->size;
        mm->symrgtbl[rgid].vmaid    = vmaid;
    }

    return 0;
}

#endif  /* MM_PAGING */
