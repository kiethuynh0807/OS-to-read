/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * mm64.c  —  64-bit 5-level page-table support
 *
 * Virtual address layout (bits):
 *   63–57  : canonical sign-extension (unused / mode indicator)
 *   56–48  : PGD index  (9 bits, 512 entries)
 *   47–39  : P4D index  (9 bits, 512 entries)
 *   38–30  : PUD index  (9 bits, 512 entries)
 *   29–21  : PMD index  (9 bits, 512 entries)
 *   20–12  : PT  index  (9 bits, 512 entries)
 *   11–0   : page offset (12 bits, 4 KB pages)
 *
 */

#ifdef MM_PAGING
#ifdef MM64

#include "mm.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Each paging level uses 9 bits → 512 entries per array */
#define PG64_ENTRIES 512

/*=======================================================================
 * Address decomposition
 *=====================================================================*/

/*
 * get_pd_from_address  –  decompose a 64-bit virtual address into
 * the five directory-level indices (PGD → P4D → PUD → PMD → PT).
 */
int get_pd_from_address(addr_t addr,
                        addr_t *pgd_idx,
                        addr_t *p4d_idx,
                        addr_t *pud_idx,
                        addr_t *pmd_idx,
                        addr_t *pt_idx)
{
    if (!pgd_idx || !p4d_idx || !pud_idx || !pmd_idx || !pt_idx)
        return -1;

    *pt_idx  = PAGING64_ADDR_PT(addr);
    *pmd_idx = PAGING64_ADDR_PMD(addr);
    *pud_idx = PAGING64_ADDR_PUD(addr);
    *p4d_idx = PAGING64_ADDR_P4D(addr);
    *pgd_idx = PAGING64_ADDR_PGD(addr);

    return 0;
}

/*
 * get_pd_from_pagenum  –  same as get_pd_from_address but accepts a
 * flat page number; reconstructs the virtual address by shifting left
 * by the page-offset width (12 bits).
 */
int get_pd_from_pagenum(addr_t pgn,
                        addr_t *pgd_idx,
                        addr_t *p4d_idx,
                        addr_t *pud_idx,
                        addr_t *pmd_idx,
                        addr_t *pt_idx)
{
    addr_t vaddr = pgn << PAGING64_ADDR_PT_SHIFT;   /* reconstruct VA */
    return get_pd_from_address(vaddr, pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx);
}

/*=======================================================================
 * Hierarchical tree helpers
 *
 * Tree structure:
 *   mm->pgd  [PG64_ENTRIES]   — root; mm owns this array
 *      pgd[i] → addr_t*       — pointer to a per-pgd p4d array
 *         p4d[j] → addr_t*    — pointer to a per-p4d pud array
 *            pud[k] → addr_t* — pointer to a per-pud pmd array
 *               pmd[l] → addr_t* — pointer to a per-pmd pt array
 *                  pt[m]       — leaf PTE value
 *
 * Each directory entry stores a child-array pointer cast to addr_t.
 * Zero means "subtree not yet allocated".
 *=====================================================================*/

/*
 * pg64_get_or_alloc  –  lazily allocate a 512-entry child array.
 * *slot is the parent directory entry.  If it is 0, a new zeroed array
 * is allocated and its address is written back into *slot.
 * Returns a pointer to the child array, or NULL on OOM.
 */
static addr_t *pg64_get_or_alloc(addr_t *slot)
{
    if (*slot == 0) {
        addr_t *arr = (addr_t *)calloc(PG64_ENTRIES, sizeof(addr_t));
        if (arr == NULL)
            return NULL;
        *slot = (addr_t)(uintptr_t)arr;
    }
    return (addr_t *)(uintptr_t)(*slot);
}

/*
 * pg64_walk  –  walk the 5-level tree to locate the leaf PTE for pgn.
 *
 * @mm     : process mm_struct (holds the root pgd array)
 * @pgn    : flat page number
 * @create : non-zero → allocate missing directory nodes on the way down
 *
 * Returns a pointer to the PTE slot (addr_t*), or NULL if the path
 * does not exist and create == 0, or on allocation failure.
 */
static addr_t *pg64_walk(struct mm_struct *mm, addr_t pgn, int create)
{
    if (mm == NULL)
        return NULL;

    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    /* ---- Level 1: PGD (root array owned by mm_struct) ---- */
    if (mm->pgd == NULL) {
        if (!create) return NULL;
        mm->pgd = (addr_t *)calloc(PG64_ENTRIES, sizeof(addr_t));
        if (mm->pgd == NULL) return NULL;
    }

    /* ---- Level 2: P4D ---- */
    addr_t *p4d;
    if (create) {
        p4d = pg64_get_or_alloc(&mm->pgd[pgd_idx]);
    } else {
        if (!mm->pgd[pgd_idx]) return NULL;
        p4d = (addr_t *)(uintptr_t)mm->pgd[pgd_idx];
    }
    if (p4d == NULL) return NULL;

    /* ---- Level 3: PUD ---- */
    addr_t *pud;
    if (create) {
        pud = pg64_get_or_alloc(&p4d[p4d_idx]);
    } else {
        if (!p4d[p4d_idx]) return NULL;
        pud = (addr_t *)(uintptr_t)p4d[p4d_idx];
    }
    if (pud == NULL) return NULL;

    /* ---- Level 4: PMD ---- */
    addr_t *pmd;
    if (create) {
        pmd = pg64_get_or_alloc(&pud[pud_idx]);
    } else {
        if (!pud[pud_idx]) return NULL;
        pmd = (addr_t *)(uintptr_t)pud[pud_idx];
    }
    if (pmd == NULL) return NULL;

    /* ---- Level 5: PT (leaf array) ---- */
    addr_t *pt;
    if (create) {
        pt = pg64_get_or_alloc(&pmd[pmd_idx]);
    } else {
        if (!pmd[pmd_idx]) return NULL;
        pt = (addr_t *)(uintptr_t)pmd[pmd_idx];
    }
    if (pt == NULL) return NULL;

    return &pt[pt_idx];   /* pointer to the leaf PTE slot */
}

/*=======================================================================
 * PTE manipulation
 *=====================================================================*/

/*
 * pte_set_fpn  –  install a present PTE: pgn → fpn
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct mm_struct *mm = caller->krnl->mm;

    addr_t *pte_slot = pg64_walk(mm, pgn, 1 /* create */);
    if (pte_slot == NULL)
        return -1;

    addr_t pte = 0;
    pte |= PAGING_PTE_PRESENT_MASK;
    /* embed FPN in bits 12:0 */
    pte  = (pte & ~(addr_t)PAGING_PTE_FPN_MASK)
         | (fpn  &  (addr_t)PAGING_PTE_FPN_MASK);

    *pte_slot = pte;
    return 0;
}

/*
 * pte_set_swap  –  mark a PTE as swapped-out (present=0, swapped=1)
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct mm_struct *mm = caller->krnl->mm;

    addr_t *pte_slot = pg64_walk(mm, pgn, 1);
    if (pte_slot == NULL)
        return -1;

    addr_t pte = 0;
    pte |= PAGING_PTE_SWAPPED_MASK;   /* present bit stays 0 */
    pte  = (pte & ~(addr_t)PAGING_PTE_SWPTYP_MASK)
         | ((addr_t)swptyp & PAGING_PTE_SWPTYP_MASK);
    pte  = (pte & ~(addr_t)PAGING_PTE_SWPOFF_MASK)
         | ((swpoff << PAGING_PTE_SWPOFF_LOBIT) & PAGING_PTE_SWPOFF_MASK);

    *pte_slot = pte;
    return 0;
}

/*
 * pte_get_entry  –  read the raw 32-bit PTE value for a page number
 */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return 0;

    struct mm_struct *mm = caller->krnl->mm;
    addr_t *pte_slot = pg64_walk(mm, pgn, 0 /* no create */);
    if (pte_slot == NULL)
        return 0;

    return (uint32_t)(*pte_slot & 0xFFFFFFFF);
}

/*
 * pte_set_entry  –  write a raw PTE value
 */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct mm_struct *mm = caller->krnl->mm;
    addr_t *pte_slot = pg64_walk(mm, pgn, 1);
    if (pte_slot == NULL)
        return -1;

    *pte_slot = (addr_t)pte_val;
    return 0;
}

/*=======================================================================
 * print_pgtbl  –  dump PTEs for a virtual address range
 *=====================================================================*/
int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct mm_struct *mm = caller->krnl->mm;

    printf("print_pgtbl:\n");

    /* Show the top-level directory pointer values (matches sample output) */
    addr_t p4d_sample = mm->pgd ? mm->pgd[0] : 0;
    addr_t pud_sample = p4d_sample
                        ? ((addr_t *)(uintptr_t)p4d_sample)[0] : 0;
    addr_t pmd_sample = pud_sample
                        ? ((addr_t *)(uintptr_t)pud_sample)[0] : 0;

    printf(" PDG=%p P4g=%p PUD=%p PMD=%p\n",
           (void *)mm->pgd,
           (void *)(uintptr_t)p4d_sample,
           (void *)(uintptr_t)pud_sample,
           (void *)(uintptr_t)pmd_sample);

    /* Walk each page in the requested range */
    addr_t pgn_start = start / PAGING64_PAGESZ;
    addr_t pgn_end   = (end   / PAGING64_PAGESZ) + 1;

    for (addr_t pgn = pgn_start; pgn < pgn_end; pgn++) {
        addr_t *pte_slot = pg64_walk(mm, pgn, 0);
        if (pte_slot == NULL || *pte_slot == 0)
            continue;
        addr_t pte = *pte_slot;
        if (pte & PAGING_PTE_PRESENT_MASK) {
            addr_t fpn = pte & PAGING_PTE_FPN_MASK;
            printf("  pgn=" FORMAT_ADDR " -> fpn=" FORMAT_ADDR " [present]\n",
                   pgn, fpn);
        } else if (pte & PAGING_PTE_SWAPPED_MASK) {
            printf("  pgn=" FORMAT_ADDR " [swapped]\n", pgn);
        }
    }
    return 0;
}

#endif  /* MM64 */
#endif  /* MM_PAGING */
