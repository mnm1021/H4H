/*
The MIT License (MIT)

Copyright (c) 2014-2015 CSAIL, MIT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#if defined (KERNEL_MODE)
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/log2.h>

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>
#include "uilog.h"
#include "upage.h"

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "h4h_drv.h"
#include "params.h"
#include "debug.h"
#include "utime.h"
#include "ufile.h"
#include "umemory.h"
#include "hlm_reqs_pool.h"

#include "algo/abm.h"
#include "algo/page_ftl.h"


/* FTL interface */
h4h_ftl_inf_t _ftl_page_ftl = {
	.ptr_private = NULL,
	.create = h4h_page_ftl_create,
	.destroy = h4h_page_ftl_destroy,
	.get_free_ppa = h4h_page_ftl_get_free_ppa,
	.get_ppa = h4h_page_ftl_get_ppa,
	.map_lpa_to_ppa = h4h_page_ftl_map_lpa_to_ppa,
	.invalidate_lpa = h4h_page_ftl_invalidate_lpa,
	.do_gc = h4h_page_ftl_do_gc,
	.is_gc_needed = h4h_page_ftl_is_gc_needed,
	.scan_badblocks = h4h_page_badblock_scan,
	/*.load = h4h_page_ftl_load,*/
	/*.store = h4h_page_ftl_store,*/
	/*.get_segno = NULL,*/

	.get_free_ppas = h4h_page_ftl_get_free_ppas,
};


/* data structures for block-level FTL */
enum H4H_PFTL_PAGE_STATUS {
	PFTL_PAGE_NOT_ALLOCATED = 0,
	PFTL_PAGE_VALID,
	PFTL_PAGE_INVALID,
	PFTL_PAGE_INVALID_ADDR = -1ULL,
};

typedef struct {
	uint8_t status; /* H4H_PFTL_PAGE_STATUS */
	h4h_phyaddr_t phyaddr; /* physical location */
	uint8_t sp_off;
} h4h_page_mapping_entry_t;

typedef struct {
	h4h_abm_info_t* bai;
	h4h_page_mapping_entry_t* ptr_mapping_table;
	h4h_spinlock_t ftl_lock;
	uint64_t nr_punits;
	uint64_t nr_punits_pages;

	/* for the management of active blocks */
	uint64_t curr_puid;
	uint64_t curr_page_ofs;
	h4h_abm_block_t** ac_bab;

	/* reserved for gc (reused whenever gc is invoked) */
	h4h_abm_block_t** gc_bab;
	h4h_hlm_req_gc_t gc_hlm;
	h4h_hlm_req_gc_t gc_hlm_w;

	/* for bad-block scanning */
	h4h_sema_t badblk;
} h4h_page_ftl_private_t;


h4h_page_mapping_entry_t* __h4h_page_ftl_create_mapping_table (
	h4h_device_params_t* np)
{
	h4h_page_mapping_entry_t* me;
	uint64_t loop;

	/* create a page-level mapping table */
	if ((me = (h4h_page_mapping_entry_t*)h4h_zmalloc 
			(sizeof (h4h_page_mapping_entry_t) * np->nr_subpages_per_ssd)) == NULL) {
		return NULL;
	}

	/* initialize a page-level mapping table */
	for (loop = 0; loop < np->nr_subpages_per_ssd; loop++) {
		me[loop].status = PFTL_PAGE_NOT_ALLOCATED;
		me[loop].phyaddr.channel_no = PFTL_PAGE_INVALID_ADDR;
		me[loop].phyaddr.chip_no = PFTL_PAGE_INVALID_ADDR;
		me[loop].phyaddr.block_no = PFTL_PAGE_INVALID_ADDR;
		me[loop].phyaddr.page_no = PFTL_PAGE_INVALID_ADDR;
		me[loop].sp_off = -1;
	}

	/* return a set of mapping entries */
	return me;
}


void __h4h_page_ftl_destroy_mapping_table (
	h4h_page_mapping_entry_t* me)
{
	if (me == NULL)
		return;
	h4h_free (me);
}

uint32_t __h4h_page_ftl_get_active_blocks (
	h4h_device_params_t* np,
	h4h_abm_info_t* bai,
	h4h_abm_block_t** bab)
{
	uint64_t i, j;

	/* get a set of free blocks for active blocks */
	for (i = 0; i < np->nr_channels; i++) {
		for (j = 0; j < np->nr_chips_per_channel; j++) {
			/* prepare & commit free blocks */
			if ((*bab = h4h_abm_get_free_block_prepare (bai, i, j))) {
				h4h_abm_get_free_block_commit (bai, *bab);
				/* h4h_msg ("active blk = %p", *bab); */
				bab++;
			} else {
				h4h_error ("h4h_abm_get_free_block_prepare failed");
				return 1;
			}
		}
	}

	return 0;
}

h4h_abm_block_t** __h4h_page_ftl_create_active_blocks (
	h4h_device_params_t* np,
	h4h_abm_info_t* bai)
{
	uint64_t nr_punits;
	h4h_abm_block_t** bab = NULL;

	nr_punits = np->nr_chips_per_channel * np->nr_channels;

	/* create a set of active blocks */
	if ((bab = (h4h_abm_block_t**)h4h_zmalloc 
			(sizeof (h4h_abm_block_t*) * nr_punits)) == NULL) {
		h4h_error ("h4h_zmalloc failed");
		goto fail;
	}

	/* get a set of free blocks for active blocks */
	if (__h4h_page_ftl_get_active_blocks (np, bai, bab) != 0) {
		h4h_error ("__h4h_page_ftl_get_active_blocks failed");
		goto fail;
	}

	return bab;

fail:
	if (bab)
		h4h_free (bab);
	return NULL;
}

void __h4h_page_ftl_destroy_active_blocks (
	h4h_abm_block_t** bab)
{
	if (bab == NULL)
		return;

	/* TODO: it might be required to save the status of active blocks 
	 * in order to support rebooting */
	h4h_free (bab);
}

uint32_t h4h_page_ftl_create (h4h_drv_info_t* bdi)
{
	h4h_page_ftl_private_t* p = NULL;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	int i;

	/* create a private data structure */
	if ((p = (h4h_page_ftl_private_t*)h4h_zmalloc 
			(sizeof (h4h_page_ftl_private_t))) == NULL) {
		h4h_error ("h4h_malloc failed");
		return 1;
	}
	p->curr_puid = 0;
	p->curr_page_ofs = 0;
	p->nr_punits = np->nr_chips_per_channel * np->nr_channels;
	p->nr_punits_pages = p->nr_punits * np->nr_pages_per_block;
	h4h_spin_lock_init (&p->ftl_lock);
	_ftl_page_ftl.ptr_private = (void*)p;

	/* create 'h4h_abm_info' with pst */
	if ((p->bai = h4h_abm_create (np, 1)) == NULL) {
		h4h_error ("h4h_abm_create failed");
		h4h_page_ftl_destroy (bdi);
		return 1;
	}

	/* create a mapping table */
	if ((p->ptr_mapping_table = __h4h_page_ftl_create_mapping_table (np)) == NULL) {
		h4h_error ("__h4h_page_ftl_create_mapping_table failed");
		h4h_page_ftl_destroy (bdi);
		return 1;
	}

	/* allocate active blocks */
	if ((p->ac_bab = __h4h_page_ftl_create_active_blocks (np, p->bai)) == NULL) {
		h4h_error ("__h4h_page_ftl_create_active_blocks failed");
		h4h_page_ftl_destroy (bdi);
		return 1;
	}

	/* allocate gc stuff */
	if ((p->gc_bab = (h4h_abm_block_t**)h4h_zmalloc 
			(sizeof (h4h_abm_block_t*) * p->nr_punits)) == NULL) {
		h4h_error ("h4h_zmalloc failed");
		h4h_page_ftl_destroy (bdi);
		return 1;
	}

	if ((p->gc_hlm.llm_reqs = (h4h_llm_req_t*)h4h_zmalloc
			(sizeof (h4h_llm_req_t) * p->nr_punits_pages)) == NULL) {
		h4h_error ("h4h_zmalloc failed");
		h4h_page_ftl_destroy (bdi);
		return 1;
	}
	h4h_sema_init (&p->gc_hlm.done);
	hlm_reqs_pool_allocate_llm_reqs (p->gc_hlm.llm_reqs, p->nr_punits_pages, RP_MEM_PHY);

	if ((p->gc_hlm_w.llm_reqs = (h4h_llm_req_t*)h4h_zmalloc
			(sizeof (h4h_llm_req_t) * p->nr_punits_pages)) == NULL) {
		h4h_error ("h4h_zmalloc failed");
		h4h_page_ftl_destroy (bdi);
		return 1;
	}
	h4h_sema_init (&p->gc_hlm_w.done);
	hlm_reqs_pool_allocate_llm_reqs (p->gc_hlm_w.llm_reqs, p->nr_punits_pages, RP_MEM_PHY);

	return 0;
}

void h4h_page_ftl_destroy (h4h_drv_info_t* bdi)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;

	if (!p)
		return;
	if (p->gc_hlm_w.llm_reqs) {
		hlm_reqs_pool_release_llm_reqs (p->gc_hlm_w.llm_reqs, p->nr_punits_pages, RP_MEM_PHY);
		h4h_sema_free (&p->gc_hlm_w.done);
		h4h_free (p->gc_hlm_w.llm_reqs);
	}
	if (p->gc_hlm.llm_reqs) {
		hlm_reqs_pool_release_llm_reqs (p->gc_hlm.llm_reqs, p->nr_punits_pages, RP_MEM_PHY);
		h4h_sema_free (&p->gc_hlm.done);
		h4h_free (p->gc_hlm.llm_reqs);
	}
	if (p->gc_bab)
		h4h_free (p->gc_bab);
	if (p->ac_bab)
		__h4h_page_ftl_destroy_active_blocks (p->ac_bab);
	if (p->ptr_mapping_table)
		__h4h_page_ftl_destroy_mapping_table (p->ptr_mapping_table);
	if (p->bai)
		h4h_abm_destroy (p->bai);
	h4h_free (p);
}

uint32_t h4h_page_ftl_get_free_ppa (
	h4h_drv_info_t* bdi, 
	int64_t lpa,
	h4h_phyaddr_t* ppa)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_abm_block_t* b = NULL;
	uint64_t curr_channel;
	uint64_t curr_chip;

	/* get the channel & chip numbers */
	curr_channel = p->curr_puid % np->nr_channels;
	curr_chip = p->curr_puid / np->nr_channels;

	/* get the physical offset of the active blocks */
	b = p->ac_bab[curr_channel * np->nr_chips_per_channel + curr_chip];
	ppa->channel_no =  b->channel_no;
	ppa->chip_no = b->chip_no;
	ppa->block_no = b->block_no;
	ppa->page_no = p->curr_page_ofs;
	ppa->punit_id = H4H_GET_PUNIT_ID (bdi, ppa);

	/* check some error cases before returning the physical address */
	h4h_bug_on (ppa->channel_no != curr_channel);
	h4h_bug_on (ppa->chip_no != curr_chip);
	h4h_bug_on (ppa->page_no >= np->nr_pages_per_block);

	/* go to the next parallel unit */
	if ((p->curr_puid + 1) == p->nr_punits) {
		p->curr_puid = 0;
		p->curr_page_ofs++;	/* go to the next page */

		/* see if there are sufficient free pages or not */
		if (p->curr_page_ofs == np->nr_pages_per_block) {
			/* get active blocks */
			if (__h4h_page_ftl_get_active_blocks (np, p->bai, p->ac_bab) != 0) {
				h4h_error ("__h4h_page_ftl_get_active_blocks failed");
				return 1;
			}
			/* ok; go ahead with 0 offset */
			/*h4h_msg ("curr_puid = %llu", p->curr_puid);*/
			p->curr_page_ofs = 0;
		}
	} else {
		/*h4h_msg ("curr_puid = %llu", p->curr_puid);*/
		p->curr_puid++;
	}

	return 0;
}

/**
 * allocate sequential ppas on same block.
 * returns the size of free ppas.
 */
int32_t h4h_page_ftl_get_free_ppas (
	h4h_drv_info_t* bdi,
	int64_t lpa,
	uint32_t size,
	h4h_phyaddr_t* start_ppa)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_abm_block_t* b = NULL;
	uint64_t curr_channel;
	uint64_t curr_chip;
	int32_t ret_size;
	int start_puid;
	int retry = 1;

	start_puid = p->curr_puid;

	/* get the channel & chip numbers */
	curr_channel = p->curr_puid % np->nr_channels;
	curr_chip = p->curr_puid / np->nr_channels;
		
	/* get active block */
	b = p->ac_bab[curr_channel * np->nr_chips_per_channel + curr_chip];

	while (b == NULL)
	{
		/* search next block for current puid */
		b = h4h_abm_get_free_block_prepare (p->bai, curr_channel, curr_chip);
		if (b != NULL)
		{
			h4h_abm_get_free_block_commit (p->bai, b);
			p->ac_bab[curr_channel * np->nr_chips_per_channel + curr_chip] = b;
		}

		/* switch to next puid */
		p->curr_puid = (p->curr_puid + 1) % p->nr_punits;
		curr_channel = p->curr_puid % np->nr_channels;
		curr_chip = p->curr_puid / np->nr_channels;
		b = p->ac_bab[curr_channel * np->nr_chips_per_channel + curr_chip];

		if (p->curr_puid == start_puid)
		{
			if (retry)
			{
				retry = 0;
				continue;
			}
			h4h_msg ("[get_free_ppas] %d total blks, %d free blks, %d dirty blks", p->bai->nr_total_blks, p->bai->nr_free_blks, p->bai->nr_dirty_blks);
			h4h_msg ("[get_free_ppas] no block is available");
			return -1;
		}
	}

	/* get physical offset of the active blocks */
	start_ppa->channel_no = b->channel_no;
	start_ppa->chip_no = b->chip_no;
	start_ppa->block_no = b->block_no;
	start_ppa->page_no = b->offset;
	start_ppa->punit_id = H4H_GET_PUNIT_ID (bdi, start_ppa);

	/* check the offset of page: match the size */
	if (start_ppa->page_no + size > np->nr_pages_per_block)
	{
		ret_size = np->nr_pages_per_block - start_ppa->page_no;
		b->offset = np->nr_pages_per_block;
	}
	else
	{
		ret_size = size;
		b->offset += size;
	}

	/* get next free block if block is full */
	if (b->offset == np->nr_pages_per_block)
	{
		b = h4h_abm_get_free_block_prepare (p->bai, curr_channel, curr_chip);

		if (b != NULL)
		{
			h4h_abm_get_free_block_commit (p->bai, b);
		}
//		else
//		{
//			h4h_error ("h4h_abm_get_free_block_prepare failed");
//			return -1;
//		}

		p->ac_bab[curr_channel * np->nr_chips_per_channel + curr_chip] = b;

		/* advance to next puid */
		p->curr_puid = (p->curr_puid + 1) % p->nr_punits;
	}

	return ret_size;
}

uint32_t h4h_page_ftl_map_lpa_to_ppa (
	h4h_drv_info_t* bdi, 
	h4h_logaddr_t* logaddr,
	h4h_phyaddr_t* phyaddr)
{
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_page_mapping_entry_t* me = NULL;
	int k;

	/* is it a valid logical address */
	for (k = 0; k < np->nr_subpages_per_page; k++) {
		if (logaddr->lpa[k] == -1) {
			/* the correpsonding subpage must be set to invalid for gc */
			h4h_abm_invalidate_page (
				p->bai, 
				phyaddr->channel_no, 
				phyaddr->chip_no,
				phyaddr->block_no,
				phyaddr->page_no,
				k
			);
			continue;
		}

		if (logaddr->lpa[k] >= np->nr_subpages_per_ssd) {
			h4h_error ("LPA is beyond logical space (%llX)", logaddr->lpa[k]);
			return 1;
		}

		/* get the mapping entry for lpa */
		me = &p->ptr_mapping_table[logaddr->lpa[k]];
		h4h_bug_on (me == NULL);

		/* update the mapping table */
		if (me->status == PFTL_PAGE_VALID) {
			h4h_abm_invalidate_page (
				p->bai, 
				me->phyaddr.channel_no, 
				me->phyaddr.chip_no,
				me->phyaddr.block_no,
				me->phyaddr.page_no,
				me->sp_off
			);
		}
		me->status = PFTL_PAGE_VALID;
		me->phyaddr.channel_no = phyaddr->channel_no;
		me->phyaddr.chip_no = phyaddr->chip_no;
		me->phyaddr.block_no = phyaddr->block_no;
		me->phyaddr.page_no = phyaddr->page_no;
		me->sp_off = k;
	}

	return 0;
}

uint32_t h4h_page_ftl_get_ppa (
	h4h_drv_info_t* bdi, 
	int64_t lpa,
	h4h_phyaddr_t* phyaddr,
	uint64_t* sp_off)
{
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_page_mapping_entry_t* me = NULL;
	uint32_t ret;

	/* is it a valid logical address */
	if (lpa >= np->nr_subpages_per_ssd) {
		h4h_error ("A given lpa is beyond logical space (%llu)", lpa);
		return 1;
	}

	/* get the mapping entry for lpa */
	me = &p->ptr_mapping_table[lpa];

	/* NOTE: sometimes a file system attempts to read 
	 * a logical address that was not written before.
	 * in that case, we return 'address 0' */
	if (me->status != PFTL_PAGE_VALID) {
		phyaddr->channel_no = 0;
		phyaddr->chip_no = 0;
		phyaddr->block_no = 0;
		phyaddr->page_no = 0;
		phyaddr->punit_id = 0;
		*sp_off = 0;
		ret = 1;
	} else {
		phyaddr->channel_no = me->phyaddr.channel_no;
		phyaddr->chip_no = me->phyaddr.chip_no;
		phyaddr->block_no = me->phyaddr.block_no;
		phyaddr->page_no = me->phyaddr.page_no;
		phyaddr->punit_id = H4H_GET_PUNIT_ID (bdi, phyaddr);
		*sp_off = me->sp_off;
		ret = 0;
	}

	return ret;
}

uint32_t h4h_page_ftl_invalidate_lpa (
	h4h_drv_info_t* bdi, 
	int64_t lpa, 
	uint64_t len)
{	
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_page_mapping_entry_t* me = NULL;
	uint64_t loop;

	/* check the range of input addresses */
	if ((lpa + len) > np->nr_subpages_per_ssd) {
		h4h_warning ("LPA is beyond logical space (%llu = %llu+%llu) %llu", 
			lpa+len, lpa, len, np->nr_subpages_per_ssd);
		return 1;
	}

	/* make them invalid */
	for (loop = lpa; loop < (lpa + len); loop++) {
		me = &p->ptr_mapping_table[loop];
		if (me->status == PFTL_PAGE_VALID) {
			h4h_abm_invalidate_page (
				p->bai, 
				me->phyaddr.channel_no, 
				me->phyaddr.chip_no,
				me->phyaddr.block_no,
				me->phyaddr.page_no,
				me->sp_off
			);
			me->status = PFTL_PAGE_INVALID;
		}
	}

	return 0;
}

uint8_t h4h_page_ftl_is_gc_needed (h4h_drv_info_t* bdi, int64_t lpa)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	uint64_t nr_total_blks = h4h_abm_get_nr_total_blocks (p->bai);
	uint64_t nr_free_blks = h4h_abm_get_nr_free_blocks (p->bai);

	/* invoke gc when remaining free blocks are less than 1% of total blocks */
	if ((nr_free_blks * 100 / nr_total_blks) <= 10) {
		return 1;
	}

	/* invoke gc when there is only one dirty block (for debugging) */
	/*
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	if (h4h_abm_get_nr_dirty_blocks (p->bai) > 1) {
		return 1;
	}
	*/

	return 0;
}

/* VICTIM SELECTION - First Selection:
 * select the first dirty block in a list */
h4h_abm_block_t* __h4h_page_ftl_victim_selection (
	h4h_drv_info_t* bdi,
	uint64_t channel_no,
	uint64_t chip_no)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_abm_block_t* a = NULL;
	h4h_abm_block_t* b = NULL;
	struct list_head* pos = NULL;

	a = p->ac_bab[channel_no*np->nr_chips_per_channel + chip_no];
	h4h_abm_list_for_each_dirty_block (pos, p->bai, channel_no, chip_no) {
		b = h4h_abm_fetch_dirty_block (pos);
		if (a != b)
			break;
		b = NULL;
	}

	return b;
}

/* VICTIM SELECTION - Greedy:
 * select a dirty block with a small number of valid pages */
h4h_abm_block_t* __h4h_page_ftl_victim_selection_greedy (
	h4h_drv_info_t* bdi,
	uint64_t channel_no,
	uint64_t chip_no)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_abm_block_t* a = NULL;
	h4h_abm_block_t* b = NULL;
	h4h_abm_block_t* v = NULL;
	struct list_head* pos = NULL;

	a = p->ac_bab[channel_no*np->nr_chips_per_channel + chip_no];

	h4h_abm_list_for_each_dirty_block (pos, p->bai, channel_no, chip_no) {
		b = h4h_abm_fetch_dirty_block (pos);
		if (a == b)
			continue;
		if (b->nr_invalid_subpages == np->nr_subpages_per_block) {
			v = b;
			break;
		}
		if (v == NULL) {
			v = b;
			continue;
		}
		if (b->nr_invalid_subpages > v->nr_invalid_subpages)
			v = b;
	}

	return v;
}

/* TODO: need to improve it for background gc */
#if 0
uint32_t h4h_page_ftl_do_gc (h4h_drv_info_t* bdi)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_hlm_req_gc_t* hlm_gc = &p->gc_hlm;
	uint64_t nr_gc_blks = 0;
	uint64_t nr_llm_reqs = 0;
	uint64_t nr_punits = 0;
	uint64_t i, j, k;
	h4h_stopwatch_t sw;

	nr_punits = np->nr_channels * np->nr_chips_per_channel;

	/* choose victim blocks for individual parallel units */
	h4h_memset (p->gc_bab, 0x00, sizeof (h4h_abm_block_t*) * nr_punits);
	h4h_stopwatch_start (&sw);
	for (i = 0, nr_gc_blks = 0; i < np->nr_channels; i++) {
		for (j = 0; j < np->nr_chips_per_channel; j++) {
			h4h_abm_block_t* b; 
			if ((b = __h4h_page_ftl_victim_selection_greedy (bdi, i, j))) {
				p->gc_bab[nr_gc_blks] = b;
				nr_gc_blks++;
			}
		}
	}
	if (nr_gc_blks < nr_punits) {
		/* TODO: we need to implement a load balancing feature to avoid this */
		/*h4h_warning ("TODO: this warning will be removed with load-balancing");*/
		return 0;
	}

	/* build hlm_req_gc for reads */
	for (i = 0, nr_llm_reqs = 0; i < nr_gc_blks; i++) {
		h4h_abm_block_t* b = p->gc_bab[i];
		if (b == NULL)
			break;
		for (j = 0; j < np->nr_pages_per_block; j++) {
			h4h_llm_req_t* r = &hlm_gc->llm_reqs[nr_llm_reqs];
			int has_valid = 0;
			/* are there any valid subpages in a block */
			hlm_reqs_pool_reset_fmain (&r->fmain);
			hlm_reqs_pool_reset_logaddr (&r->logaddr);
			for (k = 0; k < np->nr_subpages_per_page; k++) {
				if (b->pst[j*np->nr_subpages_per_page+k] != H4H_ABM_SUBPAGE_INVALID) {
					has_valid = 1;
					r->logaddr.lpa[k] = -1; /* the subpage contains new data */
					r->fmain.kp_stt[k] = KP_STT_DATA;
				} else {
					r->logaddr.lpa[k] = -1;	/* the subpage contains obsolate data */
					r->fmain.kp_stt[k] = KP_STT_HOLE;
				}
			}
			/* if it is, selects it as the gc candidates */
			if (has_valid) {
				r->req_type = REQTYPE_GC_READ;
				r->phyaddr.channel_no = b->channel_no;
				r->phyaddr.chip_no = b->chip_no;
				r->phyaddr.block_no = b->block_no;
				r->phyaddr.page_no = j;
				r->phyaddr.punit_id = H4H_GET_PUNIT_ID (bdi, (&r->phyaddr));
				r->ptr_hlm_req = (void*)hlm_gc;
				r->ret = 0;
				nr_llm_reqs++;
			}
		}
	}

	/*
	h4h_msg ("----------------------------------------------");
	h4h_msg ("gc-victim: %llu pages, %llu blocks, %llu us", 
		nr_llm_reqs, nr_gc_blks, h4h_stopwatch_get_elapsed_time_us (&sw));
	*/

	/* wait until Q in llm becomes empty 
	 * TODO: it might be possible to further optimize this */
	bdi->ptr_llm_inf->flush (bdi);

	if (nr_llm_reqs == 0) 
		goto erase_blks;

	/* send read reqs to llm */
	hlm_gc->req_type = REQTYPE_GC_READ;
	hlm_gc->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hlm_gc->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc->done);
	for (i = 0; i < nr_llm_reqs; i++) {
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc->done);
	h4h_sema_unlock (&hlm_gc->done);

	/* build hlm_req_gc for writes */
	for (i = 0; i < nr_llm_reqs; i++) {
		h4h_llm_req_t* r = &hlm_gc->llm_reqs[i];
		r->req_type = REQTYPE_GC_WRITE;	/* change to write */
		for (k = 0; k < np->nr_subpages_per_page; k++) {
			/* move subpages that contain new data */
			if (r->fmain.kp_stt[k] == KP_STT_DATA) {
				r->logaddr.lpa[k] = ((uint64_t*)r->foob.data)[k];
			} else if (r->fmain.kp_stt[k] == KP_STT_HOLE) {
				((uint64_t*)r->foob.data)[k] = -1;
				r->logaddr.lpa[k] = -1;
			} else {
				h4h_bug_on (1);
			}
		}
		if (h4h_page_ftl_get_free_ppa (bdi, &r->phyaddr) != 0) {
			h4h_error ("h4h_page_ftl_get_free_ppa failed");
			h4h_bug_on (1);
		}
		if (h4h_page_ftl_map_lpa_to_ppa (bdi, &r->logaddr, &r->phyaddr) != 0) {
			h4h_error ("h4h_page_ftl_map_lpa_to_ppa failed");
			h4h_bug_on (1);
		}
	}

	/* send write reqs to llm */
	hlm_gc->req_type = REQTYPE_GC_WRITE;
	hlm_gc->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hlm_gc->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc->done);
	for (i = 0; i < nr_llm_reqs; i++) {
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc->done);
	h4h_sema_unlock (&hlm_gc->done);

	/* erase blocks */
erase_blks:
	for (i = 0; i < nr_gc_blks; i++) {
		h4h_abm_block_t* b = p->gc_bab[i];
		h4h_llm_req_t* r = &hlm_gc->llm_reqs[i];
		r->req_type = REQTYPE_GC_ERASE;
		r->logaddr.lpa[0] = -1ULL; /* lpa is not available now */
		r->phyaddr.channel_no = b->channel_no;
		r->phyaddr.chip_no = b->chip_no;
		r->phyaddr.block_no = b->block_no;
		r->phyaddr.page_no = 0;
		r->phyaddr.punit_id = H4H_GET_PUNIT_ID (bdi, (&r->phyaddr));
		r->ptr_hlm_req = (void*)hlm_gc;
		r->ret = 0;
	}

	/* send erase reqs to llm */
	hlm_gc->req_type = REQTYPE_GC_ERASE;
	hlm_gc->nr_llm_reqs = p->nr_punits;
	atomic64_set (&hlm_gc->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc->done);
	for (i = 0; i < nr_gc_blks; i++) {
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc->done);
	h4h_sema_unlock (&hlm_gc->done);

	/* FIXME: what happens if block erasure fails */
	for (i = 0; i < nr_gc_blks; i++) {
		uint8_t ret = 0;
		h4h_abm_block_t* b = p->gc_bab[i];
		if (hlm_gc->llm_reqs[i].ret != 0) 
			ret = 1;	/* bad block */
		h4h_abm_erase_block (p->bai, b->channel_no, b->chip_no, b->block_no, ret);
	}

	return 0;
}
#endif

uint32_t h4h_page_ftl_do_gc (h4h_drv_info_t* bdi, int64_t lpa)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_hlm_req_gc_t* hlm_gc = &p->gc_hlm;
	h4h_hlm_req_gc_t* hlm_gc_w = &p->gc_hlm_w;
	uint64_t nr_gc_blks = 0;
	uint64_t nr_llm_reqs = 0;
	uint64_t nr_punits = 0;
	uint64_t i, j, k;
	h4h_stopwatch_t sw;

	nr_punits = np->nr_channels * np->nr_chips_per_channel;

	/* choose victim blocks for individual parallel units */
	h4h_memset (p->gc_bab, 0x00, sizeof (h4h_abm_block_t*) * nr_punits);
	h4h_stopwatch_start (&sw);
	for (i = 0, nr_gc_blks = 0; i < np->nr_channels; i++) {
		for (j = 0; j < np->nr_chips_per_channel; j++) {
			h4h_abm_block_t* b; 
			if ((b = __h4h_page_ftl_victim_selection_greedy (bdi, i, j))) {
				p->gc_bab[nr_gc_blks] = b;
				nr_gc_blks++;
			}
		}
	}

//	if (nr_gc_blks < nr_punits) {
	if (nr_gc_blks * 20 <= nr_punits) { /* 5% of the whole punits */
		/* TODO: we need to implement a load balancing feature to avoid this */
		/*h4h_warning ("TODO: this warning will be removed with load-balancing");*/
		return 0;
	}

	/* TEMP */
	for (i = 0; i < nr_punits * np->nr_pages_per_block; i++) {
		hlm_reqs_pool_reset_fmain (&hlm_gc->llm_reqs[i].fmain);
		/* The memory for pads must be allocated for GC 
		 * because there are no buffers from bio */
		hlm_reqs_pool_alloc_fmain_pad (&hlm_gc->llm_reqs[i].fmain);
	}
	/* TEMP */

	/* build hlm_req_gc for reads */
	for (i = 0, nr_llm_reqs = 0; i < nr_gc_blks; i++) {
		h4h_abm_block_t* b = p->gc_bab[i];
		if (b == NULL)
			continue;
		for (j = 0; j < np->nr_pages_per_block; j++) {
			h4h_llm_req_t* r = &hlm_gc->llm_reqs[nr_llm_reqs];
			int has_valid = 0;
			/* are there any valid subpages in a block */
			hlm_reqs_pool_reset_fmain (&r->fmain);
			hlm_reqs_pool_reset_logaddr (&r->logaddr);
			for (k = 0; k < np->nr_subpages_per_page; k++) {
				if (b->pst[j*np->nr_subpages_per_page+k] != H4H_ABM_SUBPAGE_INVALID) {
					has_valid = 1;
					r->logaddr.lpa[k] = -1; /* the subpage contains new data */
					r->fmain.kp_stt[k] = KP_STT_DATA;
				} else {
					r->logaddr.lpa[k] = -1;	/* the subpage contains obsolate data */
					r->fmain.kp_stt[k] = KP_STT_HOLE;
				}
			}
			hlm_reqs_pool_alloc_fmain_pad (&r->fmain);
			/* if it is, selects it as the gc candidates */
			if (has_valid) {
				r->req_type = REQTYPE_GC_READ;
				r->phyaddr.channel_no = b->channel_no;
				r->phyaddr.chip_no = b->chip_no;
				r->phyaddr.block_no = b->block_no;
				r->phyaddr.page_no = j;
				r->phyaddr.punit_id = H4H_GET_PUNIT_ID (bdi, (&r->phyaddr));
				r->ptr_hlm_req = (void*)hlm_gc;
				r->ret = 0;
				nr_llm_reqs++;
			}
		}
	}

	/*
	h4h_msg ("----------------------------------------------");
	h4h_msg ("gc-victim: %llu pages, %llu blocks, %llu us", 
		nr_llm_reqs, nr_gc_blks, h4h_stopwatch_get_elapsed_time_us (&sw));
	*/

	/* wait until Q in llm becomes empty 
	 * TODO: it might be possible to further optimize this */
	bdi->ptr_llm_inf->flush (bdi);

	if (nr_llm_reqs == 0)
		goto erase_blks;

	/* send read reqs to llm */
	hlm_gc->req_type = REQTYPE_GC_READ;
	hlm_gc->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hlm_gc->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc->done);
	for (i = 0; i < nr_llm_reqs; i++) {
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc->done);
	h4h_sema_unlock (&hlm_gc->done);

#if 0
	/* perform write compaction for gc */
#include "hlm_reqs_pool.h"

	/* build hlm_req_gc for writes */
	for (i = 0; i < nr_llm_reqs; i++) {
		h4h_llm_req_t* r = &hlm_gc->llm_reqs[i];
		r->req_type = REQTYPE_GC_WRITE;	/* change to write */
		for (k = 0; k < np->nr_subpages_per_page; k++) {
			/* move subpages that contain new data */
			if (r->fmain.kp_stt[k] == KP_STT_DATA) {
				r->logaddr.lpa[k] = ((uint64_t*)r->foob.data)[k];
			} else if (r->fmain.kp_stt[k] == KP_STT_HOLE) {
				((uint64_t*)r->foob.data)[k] = -1;
				r->logaddr.lpa[k] = -1;
			} else {
				h4h_bug_on (1);
			}
		}
		if (h4h_page_ftl_get_free_ppa (bdi, &r->phyaddr) != 0) {
			h4h_error ("h4h_page_ftl_get_free_ppa failed");
			h4h_bug_on (1);
		}
		if (h4h_page_ftl_map_lpa_to_ppa (bdi, &r->logaddr, &r->phyaddr) != 0) {
			h4h_error ("h4h_page_ftl_map_lpa_to_ppa failed");
			h4h_bug_on (1);
		}
	}

	/* send write reqs to llm */
	hlm_gc->req_type = REQTYPE_GC_WRITE;
	hlm_gc->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hlm_gc->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc->done);
	for (i = 0; i < nr_llm_reqs; i++) {
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc->done);
	h4h_sema_unlock (&hlm_gc->done);
#else

	/* perform write compaction for gc */
#include "hlm_reqs_pool.h"
	hlm_reqs_pool_write_compaction (hlm_gc_w, hlm_gc, np);

	/*h4h_msg ("compaction: %llu => %llu", nr_llm_reqs, hlm_gc_w->nr_llm_reqs);*/

	nr_llm_reqs = hlm_gc_w->nr_llm_reqs;

	/* get_free_ppas for GC writes */
	h4h_phyaddr_t* phyaddrs = NULL;
	h4h_phyaddr_t start_ppa, reset_ppa;
	h4h_abm_block_t* reset_block;
	int32_t alloc_size, total_size = 0;
	phyaddrs = h4h_malloc (sizeof(h4h_phyaddr_t) * nr_llm_reqs);
	i = 0;

	while (total_size < nr_llm_reqs)
	{
		alloc_size = h4h_page_ftl_get_free_ppas (bdi, 0, nr_llm_reqs - total_size, &start_ppa);
		if (alloc_size < 0)
		{
			/* erase previous block offset */
			for (j = 0; j < i; ++j)
			{
				reset_ppa = phyaddrs[j];
				reset_block = h4h_abm_get_block (p->bai, reset_ppa.channel_no, reset_ppa.chip_no, reset_ppa.block_no);
				reset_block->offset -= 1;
			}
			h4h_free (phyaddrs);

			/* no free space, so just erase blocks only which are full of invalid pages */
			h4h_msg ("[do_gc] no free space for GC copy, just erasing fully-lnvalid blocks");
			j = 0;
			for (i = 0; i < nr_gc_blks; ++i)
			{
				if (p->gc_bab[i]->nr_invalid_subpages != np->nr_subpages_per_block)
				{
					p->gc_bab[i] = NULL;
					++j;
				}
			}
			nr_gc_blks -= j;

			goto erase_blks;
		}
		total_size += alloc_size;
		for (; i < total_size; ++i)
		{
			h4h_memcpy (&phyaddrs[i], &start_ppa, sizeof(h4h_phyaddr_t));
			start_ppa.page_no += 1;
		}
	}

	/* build hlm_req_gc for writes */
	for (i = 0; i < nr_llm_reqs; i++) {
		h4h_llm_req_t* r = &hlm_gc_w->llm_reqs[i];
		r->req_type = REQTYPE_GC_WRITE;	/* change to write */
		for (k = 0; k < np->nr_subpages_per_page; k++) {
			/* move subpages that contain new data */
			if (r->fmain.kp_stt[k] == KP_STT_DATA) {
				r->logaddr.lpa[k] = ((uint64_t*)r->foob.data)[k];
			} else if (r->fmain.kp_stt[k] == KP_STT_HOLE) {
				((uint64_t*)r->foob.data)[k] = -1;
				r->logaddr.lpa[k] = -1;
			} else {
				h4h_bug_on (1);
			}
		}
		r->ptr_hlm_req = (void*)hlm_gc_w;
//		if (h4h_page_ftl_get_free_ppa (bdi, 0, &r->phyaddr) != 0) {
//			h4h_error ("h4h_page_ftl_get_free_ppa failed");
//			h4h_bug_on (1);
//		}
		h4h_memcpy (&r->phyaddr, &phyaddrs[i], sizeof(h4h_phyaddr_t));
		if (h4h_page_ftl_map_lpa_to_ppa (bdi, &r->logaddr, &r->phyaddr) != 0) {
			h4h_error ("h4h_page_ftl_map_lpa_to_ppa failed");
			h4h_bug_on (1);
		}
	}
	h4h_free (phyaddrs);

	/* send write reqs to llm */
	hlm_gc_w->req_type = REQTYPE_GC_WRITE;
	hlm_gc_w->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hlm_gc_w->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc_w->done);
	for (i = 0; i < nr_llm_reqs; i++) {
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc_w->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc_w->done);
	h4h_sema_unlock (&hlm_gc_w->done);
#endif

	/* erase blocks */
erase_blks:
	for (i = 0; i < nr_punits; i++) {
		h4h_abm_block_t* b = p->gc_bab[i];
		if (b == NULL)
			continue;
		h4h_llm_req_t* r = &hlm_gc->llm_reqs[i];
		r->req_type = REQTYPE_GC_ERASE;
		r->logaddr.lpa[0] = -1ULL; /* lpa is not available now */
		r->phyaddr.channel_no = b->channel_no;
		r->phyaddr.chip_no = b->chip_no;
		r->phyaddr.block_no = b->block_no;
		r->phyaddr.page_no = 0;
		r->phyaddr.punit_id = H4H_GET_PUNIT_ID (bdi, (&r->phyaddr));
		r->ptr_hlm_req = (void*)hlm_gc;
		r->ret = 0;
	}

	/* send erase reqs to llm */
	hlm_gc->req_type = REQTYPE_GC_ERASE;
//	hlm_gc->nr_llm_reqs = p->nr_punits;
	hlm_gc->nr_llm_reqs = nr_gc_blks;
	atomic64_set (&hlm_gc->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc->done);
	for (i = 0; i < nr_punits; i++) {
		if (p->gc_bab[i] == NULL)
			continue;
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc->done);
	h4h_sema_unlock (&hlm_gc->done);

	/* FIXME: what happens if block erasure fails */
	for (i = 0; i < nr_punits; i++) {
		uint8_t ret = 0;
		h4h_abm_block_t* b = p->gc_bab[i];
		if (b == NULL)
			continue;
		if (hlm_gc->llm_reqs[i].ret != 0) 
			ret = 1;	/* bad block */
		h4h_abm_erase_block (p->bai, b->channel_no, b->chip_no, b->block_no, ret);
	}

	return 0;
}


/* for snapshot */
uint32_t h4h_page_ftl_load (h4h_drv_info_t* bdi, const char* fn)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_page_mapping_entry_t* me;
	h4h_file_t fp = 0;
	uint64_t i, pos = 0;

	/* step1: load abm */
	if (h4h_abm_load (p->bai, "/usr/share/h4h_drv/abm.dat") != 0) {
		h4h_error ("h4h_abm_load failed");
		return 1;
	}

	/* step2: load mapping table */
	if ((fp = h4h_fopen (fn, O_RDWR, 0777)) == 0) {
		h4h_error ("h4h_fopen failed");
		return 1;
	}

	me = p->ptr_mapping_table;
	for (i = 0; i < np->nr_subpages_per_ssd; i++) {
		pos += h4h_fread (fp, pos, (uint8_t*)&me[i], sizeof (h4h_page_mapping_entry_t));
		if (me[i].status != PFTL_PAGE_NOT_ALLOCATED &&
			me[i].status != PFTL_PAGE_VALID &&
			me[i].status != PFTL_PAGE_INVALID &&
			me[i].status != PFTL_PAGE_INVALID_ADDR) {
			h4h_msg ("snapshot: invalid status = %u", me[i].status);
		}
	}

	/* step3: get active blocks */
	if (__h4h_page_ftl_get_active_blocks (np, p->bai, p->ac_bab) != 0) {
		h4h_error ("__h4h_page_ftl_get_active_blocks failed");
		h4h_fclose (fp);
		return 1;
	}
	p->curr_puid = 0;
	p->curr_page_ofs = 0;

	h4h_fclose (fp);

	return 0;
}

uint32_t h4h_page_ftl_store (h4h_drv_info_t* bdi, const char* fn)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_page_mapping_entry_t* me;
	h4h_abm_block_t* b = NULL;
	h4h_file_t fp = 0;
	uint64_t pos = 0;
	uint64_t i, j, k;
	uint32_t ret;

	/* step1: make active blocks invalid (it's ugly!!!) */
	if ((fp = h4h_fopen (fn, O_CREAT | O_WRONLY, 0777)) == 0) {
		h4h_error ("h4h_fopen failed");
		return 1;
	}

	while (1) {
		/* get the channel & chip numbers */
		i = p->curr_puid % np->nr_channels;
		j = p->curr_puid / np->nr_channels;

		/* get the physical offset of the active blocks */
		b = p->ac_bab[i*np->nr_chips_per_channel + j];

		/* invalidate remaining pages */
		for (k = 0; k < np->nr_subpages_per_page; k++) {
			h4h_abm_invalidate_page (
				p->bai, 
				b->channel_no, 
				b->chip_no, 
				b->block_no, 
				p->curr_page_ofs, 
				k);
		}
		h4h_bug_on (b->channel_no != i);
		h4h_bug_on (b->chip_no != j);

		/* go to the next parallel unit */
		if ((p->curr_puid + 1) == p->nr_punits) {
			p->curr_puid = 0;
			p->curr_page_ofs++;	/* go to the next page */

			/* see if there are sufficient free pages or not */
			if (p->curr_page_ofs == np->nr_pages_per_block) {
				p->curr_page_ofs = 0;
				break;
			}
		} else {
			p->curr_puid++;
		}
	}

	/* step2: store mapping table */
	me = p->ptr_mapping_table;
	for (i = 0; i < np->nr_subpages_per_ssd; i++) {
		pos += h4h_fwrite (fp, pos, (uint8_t*)&me[i], sizeof (h4h_page_mapping_entry_t));
	}
	h4h_fsync (fp);
	h4h_fclose (fp);

	/* step3: store abm */
	ret = h4h_abm_store (p->bai, "/usr/share/h4h_drv/abm.dat");

	return ret;
}

void __h4h_page_badblock_scan_eraseblks (
	h4h_drv_info_t* bdi,
	uint64_t block_no)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_hlm_req_gc_t* hlm_gc = &p->gc_hlm;
	uint64_t i, j;

	/* setup blocks to erase */
	h4h_memset (p->gc_bab, 0x00, sizeof (h4h_abm_block_t*) * p->nr_punits);
	for (i = 0; i < np->nr_channels; i++) {
		for (j = 0; j < np->nr_chips_per_channel; j++) {
			h4h_abm_block_t* b = NULL;
			h4h_llm_req_t* r = NULL;
			uint64_t punit_id = i*np->nr_chips_per_channel+j;

			if ((b = h4h_abm_get_block (p->bai, i, j, block_no)) == NULL) {
				h4h_error ("oops! h4h_abm_get_block failed");
				h4h_bug_on (1);
			}
			p->gc_bab[punit_id] = b;

			r = &hlm_gc->llm_reqs[punit_id];
			r->req_type = REQTYPE_GC_ERASE;
			r->logaddr.lpa[0] = -1ULL; /* lpa is not available now */
			r->phyaddr.channel_no = b->channel_no;
			r->phyaddr.chip_no = b->chip_no;
			r->phyaddr.block_no = b->block_no;
			r->phyaddr.page_no = 0;
			r->phyaddr.punit_id = H4H_GET_PUNIT_ID (bdi, (&r->phyaddr));
			r->ptr_hlm_req = (void*)hlm_gc;
			r->ret = 0;
		}
	}

	/* send erase reqs to llm */
	hlm_gc->req_type = REQTYPE_GC_ERASE;
	hlm_gc->nr_llm_reqs = p->nr_punits;
	atomic64_set (&hlm_gc->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hlm_gc->done);
	for (i = 0; i < p->nr_punits; i++) {
		if ((bdi->ptr_llm_inf->make_req (bdi, &hlm_gc->llm_reqs[i])) != 0) {
			h4h_error ("llm_make_req failed");
			h4h_bug_on (1);
		}
	}
	h4h_sema_lock (&hlm_gc->done);
	h4h_sema_unlock (&hlm_gc->done);

	for (i = 0; i < p->nr_punits; i++) {
		uint8_t ret = 0;
		h4h_abm_block_t* b = p->gc_bab[i];

		if (hlm_gc->llm_reqs[i].ret != 0) {
			ret = 1; /* bad block */
		}

		h4h_abm_erase_block (p->bai, b->channel_no, b->chip_no, b->block_no, ret);
	}

	/* measure gc elapsed time */
}

static void __h4h_page_mark_it_dead (
	h4h_drv_info_t* bdi,
	uint64_t block_no)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	int i, j;

	for (i = 0; i < np->nr_channels; i++) {
		for (j = 0; j < np->nr_chips_per_channel; j++) {
			h4h_abm_block_t* b = NULL;

			if ((b = h4h_abm_get_block (p->bai, i, j, block_no)) == NULL) {
				h4h_error ("oops! h4h_abm_get_block failed");
				h4h_bug_on (1);
			}

			h4h_abm_set_to_dirty_block (p->bai, i, j, block_no);
		}
	}
}

uint32_t h4h_page_badblock_scan (h4h_drv_info_t* bdi)
{
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_page_mapping_entry_t* me = NULL;
	uint64_t i = 0;
	uint32_t ret = 0;

	h4h_msg ("[WARNING] 'h4h_page_badblock_scan' is called! All of the flash blocks will be erased!!!");

	/* step1: reset the page-level mapping table */
	h4h_msg ("step1: reset the page-level mapping table");
	me = p->ptr_mapping_table;
	for (i = 0; i < np->nr_subpages_per_ssd; i++) {
		me[i].status = PFTL_PAGE_NOT_ALLOCATED;
		me[i].phyaddr.channel_no = PFTL_PAGE_INVALID_ADDR;
		me[i].phyaddr.chip_no = PFTL_PAGE_INVALID_ADDR;
		me[i].phyaddr.block_no = PFTL_PAGE_INVALID_ADDR;
		me[i].phyaddr.page_no = PFTL_PAGE_INVALID_ADDR;
		me[i].sp_off = -1;
	}

	/* step2: erase all the blocks */
	bdi->ptr_llm_inf->flush (bdi);
	for (i = 0; i < np->nr_blocks_per_chip; i++) {
		__h4h_page_badblock_scan_eraseblks (bdi, i);
	}

	/* step3: store abm */
	if ((ret = h4h_abm_store (p->bai, "/usr/share/h4h_drv/abm.dat"))) {
		h4h_error ("h4h_abm_store failed");
		return 1;
	}

	/* step4: get active blocks */
	h4h_msg ("step2: get active blocks");
	if (__h4h_page_ftl_get_active_blocks (np, p->bai, p->ac_bab) != 0) {
		h4h_error ("__h4h_page_ftl_get_active_blocks failed");
		return 1;
	}
	p->curr_puid = 0;
	p->curr_page_ofs = 0;

	h4h_msg ("done");
	 
	return 0;

#if 0
	/* TEMP: on-demand format */
	h4h_page_ftl_private_t* p = _ftl_page_ftl.ptr_private;
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	h4h_page_mapping_entry_t* me = NULL;
	uint64_t i = 0;
	uint32_t ret = 0;
	uint32_t erased_blocks = 0;

	h4h_msg ("[WARNING] 'h4h_page_badblock_scan' is called! All of the flash blocks will be dirty!!!");

	/* step1: reset the page-level mapping table */
	h4h_msg ("step1: reset the page-level mapping table");
	me = p->ptr_mapping_table;
	for (i = 0; i < np->nr_pages_per_ssd; i++) {
		me[i].status = PFTL_PAGE_NOT_ALLOCATED;
		me[i].phyaddr.channel_no = PFTL_PAGE_INVALID_ADDR;
		me[i].phyaddr.chip_no = PFTL_PAGE_INVALID_ADDR;
		me[i].phyaddr.block_no = PFTL_PAGE_INVALID_ADDR;
		me[i].phyaddr.page_no = PFTL_PAGE_INVALID_ADDR;
	}

	/* step2: erase all the blocks */
	bdi->ptr_llm_inf->flush (bdi);
	for (i = 0; i < np->nr_blocks_per_chip; i++) {
		if (erased_blocks <= p->nr_punits)
			__h4h_page_badblock_scan_eraseblks (bdi, i);
		else 
			__h4h_page_mark_it_dead (bdi, i);
		erased_blocks += np->nr_channels;
	}

	/* step3: store abm */
	if ((ret = h4h_abm_store (p->bai, "/usr/share/h4h_drv/abm.dat"))) {
		h4h_error ("h4h_abm_store failed");
		return 1;
	}

	/* step4: get active blocks */
	h4h_msg ("step2: get active blocks");
	if (__h4h_page_ftl_get_active_blocks (np, p->bai, p->ac_bab) != 0) {
		h4h_error ("__h4h_page_ftl_get_active_blocks failed");
		return 1;
	}
	p->curr_puid = 0;
	p->curr_page_ofs = 0;

	h4h_msg ("[summary] Total: %llu, Free: %llu, Clean: %llu, Dirty: %llu",
		h4h_abm_get_nr_total_blocks (p->bai),
		h4h_abm_get_nr_free_blocks (p->bai),
		h4h_abm_get_nr_clean_blocks (p->bai),
		h4h_abm_get_nr_dirty_blocks (p->bai)
	);
#endif
	h4h_msg ("done");
	 
	return 0;

}
