/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * mm-memphy.c  —  Physical memory device implementation
 *
 * Implements the lowest layer of the memory subsystem:
 *   - init_memphy()        : initialise a RAM or SWAP device
 *   - MEMPHY_get_freefp()  : pop one free frame from the free list
 *   - MEMPHY_put_freefp()  : push a frame back onto the free list
 *   - MEMPHY_read()        : read one byte from a physical address
 *   - MEMPHY_write()       : write one byte to a physical address
 *   - MEMPHY_dump()        : debug dump of the storage array
 */

#ifdef MM_PAGING

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * init_memphy - initialise a physical memory device
 * @mp       : pointer to the memphy_struct to initialise
 * @max_size : total capacity in bytes
 * @randomflg: 1 = random-access device (RAM), 0 = sequential (SWAP)
 *
 * Allocates the backing storage array and builds the initial free-frame
 * list.  Every frame of size PAGING_PAGESZ is added to free_fp_list in
 * order from frame 0 upward.
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
    if (mp == NULL)
        return -1;

    /* Allocate the raw storage */
    mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
    if (mp->storage == NULL)
        return -1;

    memset(mp->storage, 0, max_size);

    mp->maxsz   = max_size;
    mp->rdmflg  = randomflg;
    mp->cursor  = 0;

    mp->free_fp_list = NULL;
    mp->used_fp_list = NULL;

    /* Build the free-frame list: one node per frame */
    int num_frames = max_size / PAGING_PAGESZ;
    for (int i = num_frames - 1; i >= 0; i--) {
        struct framephy_struct *node =
            (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
        if (node == NULL)
            return -1;
        node->fpn     = (addr_t)i;
        node->owner   = NULL;
        node->fp_next = mp->free_fp_list;
        mp->free_fp_list = node;
    }

    return 0;
}

/*
 * MEMPHY_get_freefp - allocate one free physical frame
 * @mp  : physical memory device
 * @fpn : output – frame number of the allocated frame
 *
 * Pops the head of free_fp_list and moves it to used_fp_list.
 * Returns 0 on success, -1 if no frames are available.
 */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *fpn)
{
    if (mp == NULL || fpn == NULL)
        return -1;

    struct framephy_struct *fp = mp->free_fp_list;
    if (fp == NULL)
        return -1;   /* out of physical memory */

    *fpn = fp->fpn;
    mp->free_fp_list = fp->fp_next;

    /* Track it in used list */
    fp->fp_next      = mp->used_fp_list;
    mp->used_fp_list = fp;

    return 0;
}

/*
 * MEMPHY_put_freefp - return a physical frame to the free pool
 * @mp  : physical memory device
 * @fpn : frame number being freed
 *
 * Searches used_fp_list for the matching frame, unlinks it, and
 * prepends it to free_fp_list.
 */
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
    if (mp == NULL)
        return -1;

    struct framephy_struct *prev = NULL;
    struct framephy_struct *cur  = mp->used_fp_list;

    /* Find the frame in the used list */
    while (cur != NULL) {
        if (cur->fpn == fpn)
            break;
        prev = cur;
        cur  = cur->fp_next;
    }

    if (cur == NULL) {
        /*
         * Frame not found in used list — likely a double-free or an
         * untracked frame.  Warn loudly so the bug surfaces during
         * debugging rather than silently corrupting the free list.
         */
        fprintf(stderr,
                "[MEMPHY_put_freefp] WARNING: fpn=%lu not in used_fp_list"
                " — possible double-free or untracked frame\n",
                (unsigned long)fpn);
        /* Defensive fallback: still return it to the free pool */
        struct framephy_struct *node =
            (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
        if (node == NULL)
            return -1;
        node->fpn     = fpn;
        node->owner   = NULL;
        node->fp_next = mp->free_fp_list;
        mp->free_fp_list = node;
        return 0;
    }

    /* Unlink from used list */
    if (prev == NULL)
        mp->used_fp_list = cur->fp_next;
    else
        prev->fp_next = cur->fp_next;

    /* Prepend to free list */
    cur->fp_next     = mp->free_fp_list;
    cur->owner       = NULL;
    mp->free_fp_list = cur;

    return 0;
}

/*
 * MEMPHY_read - read one byte from a physical byte address
 * @mp    : physical memory device
 * @addr  : byte address (fpn * PAGING_PAGESZ + offset)
 * @value : output byte
 *
 * For sequential devices the cursor must be advanced to addr first.
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (mp == NULL || value == NULL)
        return -1;
    if ((int)addr >= mp->maxsz)
        return -1;

    if (mp->rdmflg) {
        /* Random access – direct indexing */
        *value = mp->storage[addr];
    } else {
        /* Sequential access – advance cursor */
        if (mp->cursor != (int)addr) {
            /* Fast-forward cursor */
            mp->cursor = (int)addr;
        }
        *value = mp->storage[mp->cursor];
        mp->cursor++;
    }

    return 0;
}

/*
 * MEMPHY_write - write one byte to a physical byte address
 * @mp   : physical memory device
 * @addr : byte address
 * @data : byte value to write
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
    if (mp == NULL)
        return -1;
    if ((int)addr >= mp->maxsz)
        return -1;

    if (mp->rdmflg) {
        mp->storage[addr] = data;
    } else {
        if (mp->cursor != (int)addr)
            mp->cursor = (int)addr;
        mp->storage[mp->cursor] = data;
        mp->cursor++;
    }

    return 0;
}

/*
 * MEMPHY_dump - print the entire storage array (debug)
 */
int MEMPHY_dump(struct memphy_struct *mp)
{
    if (mp == NULL)
        return -1;

    printf("MEMPHY dump (maxsz=%d):\n", mp->maxsz);
    for (int i = 0; i < mp->maxsz; i++) {
        if (mp->storage[i] != 0)
            printf("  [%d] = 0x%02x\n", i, (unsigned char)mp->storage[i]);
    }
    return 0;
}

/*-----------------------------------------------------------------------
 * Helper list-printing utilities used by other mm modules
 *---------------------------------------------------------------------*/

int print_list_fp(struct framephy_struct *fp)
{
    printf("print_list_fp: ");
    while (fp != NULL) {
        printf("[fpn=" FORMAT_ADDR "] -> ", fp->fpn);
        fp = fp->fp_next;
    }
    printf("NULL\n");
    return 0;
}

int print_list_rg(struct vm_rg_struct *rg)
{
    printf("print_list_rg: ");
    while (rg != NULL) {
        printf("[" FORMAT_ADDR "-" FORMAT_ADDR "] -> ", rg->rg_start, rg->rg_end);
        rg = rg->rg_next;
    }
    printf("NULL\n");
    return 0;
}

int print_list_vma(struct vm_area_struct *vma)
{
    printf("print_list_vma: ");
    while (vma != NULL) {
        printf("[vm_id=%lu start=" FORMAT_ADDR " end=" FORMAT_ADDR "] -> ",
               vma->vm_id, vma->vm_start, vma->vm_end);
        vma = vma->vm_next;
    }
    printf("NULL\n");
    return 0;
}

int print_list_pgn(struct pgn_t *pgn)
{
    printf("print_list_pgn: ");
    while (pgn != NULL) {
        printf("[pgn=" FORMAT_ADDR "] -> ", pgn->pgn);
        pgn = pgn->pg_next;
    }
    printf("NULL\n");
    return 0;
}

#endif  /* MM_PAGING */