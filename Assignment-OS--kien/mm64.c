/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#if defined(MM64)

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
//struct krnl_t *krnl = caller->krnl;

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
	
  // dummy pte alloc to avoid runtime error
  pte = malloc(sizeof(addr_t));
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;
#else
  pte = &krnl->mm->pgd[pgn];
#endif
	
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
//struct krnl_t *krnl = caller->krnl;

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
	
  // dummy pte alloc to avoid runtime error
  pte = malloc(sizeof(addr_t));
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;
#else
  pte = &krnl->mm->pgd[pgn];
#endif

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
//struct krnl_t *krnl = caller->krnl;
  uint32_t pte = 0;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t	pt=0;
	
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;	
	
  return pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
	struct krnl_t *krnl = caller->krnl;
	krnl->mm->pgd[pgn]=pte_val;
	
	return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  //int pgit = 0;
  //uint64_t pattern = 0xdeadbeef;

  /* TODO memset the page table with given pattern
   */

uint32_t *pgd = caller->krnl->mm->pgd; // Assuming 1st level page table
  uint64_t pattern = 0xdeadbeef;
  
  for (int pgit = 0; pgit < pgnum; pgit++)
  {
    /* 1. Calculate the virtual address for the current page */
    addr_t curr_vaddr = addr + (pgit * PAGING_PAGESZ);

    /* 2. Get a free frame from physical memory (MEMPHY) 
     * Note: Your kernel might use a specific allocator like get_free_frame()
     */
    uint32_t tgtfpn;
    if (get_free_frame(caller->krnl->mram, &tgtfpn) < 0) {
        return -1; // Out of physical memory
    }

    /* 3. Update the Page Table Entry (PTE)
     * This links the Virtual Address to the Physical Frame Number (FPN)
     */
    pg_set_entry(pgd, curr_vaddr, tgtfpn);

    /* 4. "Memset" the actual RAM content with the pattern 
     * We convert FPN to actual physical address to write the data
     */
    addr_t phys_addr = tgtfpn << PAGING_ADDR_FPN_LOBIT;
    for (int byte = 0; byte < PAGING_PAGESZ; byte += sizeof(uint64_t)) {
        // Writing the 'deadbeef' pattern to the physical RAM
        memcpy(&caller->krnl->mram->storage[phys_addr + byte], &pattern, sizeof(uint64_t));
    }
  }

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
//struct framephy_struct *fpit;
//int pgit = 0;
//addr_t pgn;

  /* TODO: update the rg_end and rg_start of ret_rg 
  //ret_rg->rg_end =  ....
  //ret_rg->rg_start = ...
  //ret_rg->vmaid = ...
  */

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->krnl->mm->pgd,
   *                    caller->krnl->mm->pud...
   *                    ...
   */

  /* Tracking for later page replacement activities (if needed)
   * Enqueue new usage page */
  //enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn64 + pgit);

  struct framephy_struct *fpit = frames;
    
    ret_rg->rg_start = addr;
    ret_rg->rg_end = addr + (pgnum * PAGING_PAGESZ);

    for (int pgit = 0; pgit < pgnum; pgit++)
    {
        if (!fpit) break;

        addr_t curr_vaddr = addr + (pgit * PAGING_PAGESZ);
        
        // Use your header's PGN extraction
        addr_t pgn = PAGING_PGN(curr_vaddr); 

        // Map the Frame Physical Number (FPN) to the Page Number (PGN)
        pte_set_fpn(caller, pgn, fpit->fpn);

        // Track for FIFO replacement
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);

        fpit = fpit->fp_next;
    }
    return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  //addr_t fpn;
  //int pgit;
  //struct framephy_struct *newfp_str = NULL;

  /* TODO: allocate the page 
  //caller-> ...
  //frm_lst-> ...
  */


/*
  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    // TODO: allocate the page 
    if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      newfp_str->fpn = fpn;
    }
    else
    { // TODO: ERROR CODE of obtaining somes but not enough frames
    }
  }
*/


  /* End TODO */

  
  int pgit;
  struct framephy_struct *head = NULL;
  struct framephy_struct *last = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    addr_t fpn;
    // Attempt to get a free frame from the physical RAM (mram/m_memphy)
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0)
    {
      // Create a new node for our frame list
      struct framephy_struct *newfp_str = malloc(sizeof(struct framephy_struct));
      if (!newfp_str) {
          // Allocation failed: typically would need to free what was gathered so far
          return -1; 
      }
      
      newfp_str->fpn = fpn;
      newfp_str->fp_next = NULL;

      // Link the node into the list
      if (head == NULL) {
          head = newfp_str;
      } else {
          last->fp_next = newfp_str;
      }
      last = newfp_str;
    }
    else
    { 
      /* ERROR CASE: Not enough frames in RAM.
       * In a real kernel, we would trigger page replacement (swap) here.
       * For now, we return an error. 
       */
      // Optional: Cleanup list before returning to avoid memory leaks
      *frm_lst = head; 
      return -1; 
    }
  }

  // Success! Assign the built list back to the pointer provided by the caller
  *frm_lst = head;
  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
//int pgnum = incpgnum;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  // ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
   vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  /* 1. Allocate the Global Page Directory (Level 1)
   * In 64-bit mode, entries are 8 bytes (addr_t) 
   */
  mm->pgd = malloc(PAGING_MAX_PGN * sizeof(addr_t));
  if (!mm->pgd) return -1;
  memset(mm->pgd, 0, PAGING_MAX_PGN * sizeof(addr_t));

  /* Initialize higher-level table pointers to NULL.
   * These are usually allocated dynamically during a page fault.
   */
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt  = NULL;

  /* 2. Initialize the first VMA */
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (!vma0) {
      free(mm->pgd);
      return -1;
  }

  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = 0;
  vma0->sbrk = 0;
  vma0->vm_mm = mm;
  vma0->vm_next = NULL;
  vma0->vm_freerg_list = NULL;

  /* 3. Setup the initial free region for the VMA */
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  if (first_rg) {
      enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);
  }

  /* 4. Map the VMA to the MM structure */
  mm->mmap = vma0;
  
  /* 5. Initialize the Symbolic Region Table (Fixed size array) */
  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
      mm->symrgtbl[i].vmaid = -1;
      mm->symrgtbl[i].rg_start = 0;
      mm->symrgtbl[i].rg_end = 0;
      mm->symrgtbl[i].rg_next = NULL;
  }

  /* 6. Initialize FIFO and Cache pointers */
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;

  // Link the process control block to this memory management unit
  caller->krnl->mram = mm;

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
printf("--- [PAGE TABLE DUMP: " FORMATX_ADDR " to " FORMATX_ADDR "] ---\n", start, end);

  // 1. Align start and end to page boundaries
  addr_t pgn_start = PAGING_PGN(start);
  addr_t pgn_end = PAGING_PGN(end);

  // 2. Iterate through every page number in the requested range
  for (addr_t pgn = pgn_start; pgn <= pgn_end; pgn++)
  {
    /* Retrieve the Page Table Entry (PTE) for this virtual page number */
    uint32_t pte = pte_get_entry(caller, pgn);

    /* 3. Only print entries that are actually mapped (Present or Swapped) */
    if (pte != 0) // If the entry is not empty
    {
      addr_t vaddr = pgn << PAGING_ADDR_PGN_LOBIT;
      
      printf("VA: " FORMATX_ADDR " -> ", vaddr);

      if (PAGING_PAGE_PRESENT(pte)) {
          /* Page is in RAM */
          addr_t fpn = PAGING_PTE_FPN(pte);
          printf("PTE: %08x [FPN: " FORMATX_ADDR "]\n", pte, fpn);
      } 
      else if (pte & PAGING_PTE_SWAPPED_MASK) {
          /* Page is in Swap Space */
          addr_t swp_off = PAGING_PTE_SWPOFF(pte);
          printf("PTE: %08x [SWAP OFF: " FORMATX_ADDR "]\n", pte, swp_off);
      }
      else {
          printf("PTE: %08x [RESERVED/OTHER]\n", pte);
      }
    }
  }

  printf("----------------------------------------------------------\n");
  return 0;
}

#endif  //def MM64
