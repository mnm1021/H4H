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

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "h4h_drv.h"
#include "params.h"
#include "umemory.h"
#include "debug.h"
#include "abm.h"
#include "ufile.h"


static inline 
uint64_t __get_channel_ofs (h4h_device_params_t* np, uint64_t blk_idx) {
	return (blk_idx / np->nr_blocks_per_channel);
}

static inline 
uint64_t __get_chip_ofs (h4h_device_params_t* np, uint64_t blk_idx) {
	return ((blk_idx % np->nr_blocks_per_channel) / np->nr_blocks_per_chip);
}

static inline 
uint64_t __get_block_ofs (h4h_device_params_t* np, uint64_t blk_idx) {
	return (blk_idx % np->nr_blocks_per_chip);
}

static inline
uint64_t __get_block_idx (h4h_device_params_t* np, uint64_t channel_no, uint64_t chip_no, uint64_t block_no) {
	return channel_no * np->nr_blocks_per_channel + 
		chip_no * np->nr_blocks_per_chip + 
		block_no;
}

static inline
void __h4h_abm_check_status (h4h_abm_info_t* bai)
{
	h4h_bug_on (bai->nr_total_blks != 
		bai->nr_free_blks + 
		bai->nr_free_blks_prepared + 
		bai->nr_clean_blks + 
		bai->nr_dirty_blks + 
		bai->nr_bad_blks);
}

static inline
void __h4h_abm_display_status (h4h_abm_info_t* bai) 
{
	h4h_msg ("[ABM] Total: %llu => Free:%llu, Free(prepare):%llu, Clean:%llu, Dirty:%llu, Bad:%llu",
		bai->nr_total_blks,
		bai->nr_free_blks, 
		bai->nr_free_blks_prepared, 
		bai->nr_clean_blks,
		bai->nr_dirty_blks,
		bai->nr_bad_blks);

	__h4h_abm_check_status (bai);
}

babm_abm_subpage_t* __h4h_abm_create_pst (h4h_device_params_t* np)
{
	babm_abm_subpage_t* pst = NULL;

	/* NOTE: pst is managed in the unit of subpage to support fine-grain
	 * mapping FTLs that are often used to avoid expensive read-modify-writes
	 * */
	pst = h4h_malloc (sizeof (babm_abm_subpage_t) * np->nr_subpages_per_block);
	h4h_memset (pst, BABM_ABM_SUBPAGE_NOT_INVALID, sizeof (babm_abm_subpage_t) * np->nr_subpages_per_block);

	return pst;
};

void __h4h_abm_destory_pst (babm_abm_subpage_t* pst) 
{
	if (pst)
		h4h_free (pst);
}

h4h_abm_info_t* h4h_abm_create (
	h4h_device_params_t* np,
	uint8_t use_pst)
{
	uint64_t loop;
	h4h_abm_info_t* bai = NULL;

	/* create 'h4h_abm_info' */
	if ((bai = (h4h_abm_info_t*)h4h_zmalloc (sizeof (h4h_abm_info_t))) == NULL) {
		h4h_error ("h4h_zmalloc fbailed");
		return NULL;
	}
	bai->np = np;

	/* create 'h4h_abm_block' */
	if ((bai->blocks = h4h_zmalloc 
			(sizeof (h4h_abm_block_t) * np->nr_blocks_per_ssd)) == NULL) {
		goto fail;
	}

	/* initialize 'h4h_abm_block' */
	for (loop = 0; loop < np->nr_blocks_per_ssd; loop++) {
		bai->blocks[loop].status = H4H_ABM_BLK_FREE;
		bai->blocks[loop].channel_no = __get_channel_ofs (np, loop);
		bai->blocks[loop].chip_no = __get_chip_ofs (np, loop);
		bai->blocks[loop].block_no = __get_block_ofs (np, loop);
		bai->blocks[loop].erase_count = 0;
		bai->blocks[loop].pst = NULL;
		bai->blocks[loop].nr_invalid_subpages = 0;
		bai->blocks[loop].offset = 0;
		/* create a 'page status table' (pst) if necessary */
		if (use_pst) {
			if ((bai->blocks[loop].pst = __h4h_abm_create_pst (np)) == NULL) {
				h4h_error ("__h4h_abm_create_pst failed");
				goto fail;
			}
		}
	}

	/* build linked-lists */
	bai->list_head_free = (struct list_head**)h4h_zmalloc (sizeof (struct list_head*) * np->nr_channels);
	bai->list_head_clean = (struct list_head**)h4h_zmalloc (sizeof (struct list_head*) * np->nr_channels);
	bai->list_head_dirty = (struct list_head**)h4h_zmalloc (sizeof (struct list_head*) * np->nr_channels);
	bai->list_head_bad = (struct list_head**)h4h_zmalloc (sizeof (struct list_head*) * np->nr_channels);
	if (bai->list_head_free == NULL || 
		bai->list_head_clean == NULL || 
		bai->list_head_dirty == NULL || 
		bai->list_head_bad == NULL) {
		h4h_error ("h4h_zmalloc failed");
		goto fail;
	}

	for (loop = 0; loop < np->nr_channels; loop++) {
		uint64_t subloop = 0;
		bai->list_head_free[loop] = (struct list_head*)h4h_zmalloc 
			(sizeof (struct list_head) * np->nr_chips_per_channel);
		bai->list_head_clean[loop] = (struct list_head*)h4h_zmalloc 
			(sizeof (struct list_head) * np->nr_chips_per_channel);
		bai->list_head_dirty[loop] = (struct list_head*)h4h_zmalloc 
			(sizeof (struct list_head) * np->nr_chips_per_channel);
		bai->list_head_bad[loop] = (struct list_head*)h4h_zmalloc 
			(sizeof (struct list_head) * np->nr_chips_per_channel);

		if (bai->list_head_free[loop] == NULL || 
			bai->list_head_clean[loop] == NULL || 
			bai->list_head_dirty[loop] == NULL ||
			bai->list_head_bad[loop] == NULL) {
			h4h_error ("h4h_zmalloc failed");
			goto fail;
		}
		for (subloop = 0; subloop < np->nr_chips_per_channel; subloop++) {
			INIT_LIST_HEAD (&bai->list_head_free[loop][subloop]);
			INIT_LIST_HEAD (&bai->list_head_clean[loop][subloop]);
			INIT_LIST_HEAD (&bai->list_head_dirty[loop][subloop]);
			INIT_LIST_HEAD (&bai->list_head_bad[loop][subloop]);
		}
	}

	/* add abm blocks into corresponding lists */
	for (loop = 0; loop < np->nr_blocks_per_ssd; loop++) {
		list_add_tail (&(bai->blocks[loop].list), 
			&(bai->list_head_free[bai->blocks[loop].channel_no][bai->blocks[loop].chip_no]));
	}

	/* initialize # of blocks according to their types */
	bai->nr_total_blks = np->nr_blocks_per_ssd;
	bai->nr_free_blks = bai->nr_total_blks;
	bai->nr_free_blks_prepared = 0;
	bai->nr_clean_blks = 0;
	bai->nr_dirty_blks = 0;
	bai->nr_bad_blks = 0;

	/* done */
	return bai;

fail:
	h4h_abm_destroy (bai);

	return NULL;
}

void h4h_abm_destroy (h4h_abm_info_t* bai) 
{
	uint64_t loop;

	if (bai == NULL)
		return;

	if (bai->list_head_free != NULL) {
		for (loop = 0; loop < bai->np->nr_channels; loop++)
			h4h_free (bai->list_head_free[loop]);
		h4h_free (bai->list_head_free);
	}
	if (bai->list_head_clean != NULL) {
		for (loop = 0; loop < bai->np->nr_channels; loop++)
			h4h_free (bai->list_head_clean[loop]);
		h4h_free (bai->list_head_clean);
	}
	if (bai->list_head_dirty != NULL) {
		for (loop = 0; loop < bai->np->nr_channels; loop++)
			h4h_free (bai->list_head_dirty[loop]);
		h4h_free (bai->list_head_dirty);
	}
	if (bai->list_head_bad != NULL) {
		for (loop = 0; loop < bai->np->nr_channels; loop++)
			h4h_free (bai->list_head_bad[loop]);
		h4h_free (bai->list_head_bad);
	}
	if (bai->blocks != NULL) {
		for (loop = 0; loop < bai->np->nr_blocks_per_ssd; loop++)
			__h4h_abm_destory_pst (bai->blocks[loop].pst);
		h4h_free (bai->blocks);
	}
	h4h_free (bai);
}

/* get a block using an index */
h4h_abm_block_t* h4h_abm_get_block (
	h4h_abm_info_t* bai,
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no) 
{
	uint64_t blk_idx = 
		__get_block_idx (bai->np, channel_no, chip_no, block_no);

	/* see if blk_idx is correct or not */
	if (blk_idx >= bai->np->nr_blocks_per_ssd) {
		h4h_error ("oops! blk_idx (%llu) is larger than # of blocks in SSD (%llu)",
			blk_idx, bai->np->nr_blocks_per_ssd);
		return NULL;
	}

	/* return */
	return &bai->blocks[blk_idx];
}

/* get a free block using lists */
h4h_abm_block_t* h4h_abm_get_free_block_prepare (
	h4h_abm_info_t* bai,
	uint64_t channel_no,
	uint64_t chip_no) 
{
	struct list_head* pos = NULL;
	h4h_abm_block_t* blk = NULL;
	uint32_t cnt = 0;

	list_for_each (pos, &(bai->list_head_free[channel_no][chip_no])) {
		cnt++;
		blk = list_entry (pos, h4h_abm_block_t, list);
		if (blk->status == H4H_ABM_BLK_FREE) {
			blk->status = H4H_ABM_BLK_FREE_PREPARE;

			/* check some error cases */
			if (bai->nr_free_blks == 0) {
				h4h_msg ("oops! bai->nr_free_blks == 0");
			}
			__h4h_abm_check_status (bai);

			/* change the number of blks */
			bai->nr_free_blks--;
			bai->nr_free_blks_prepared++;
			break;
		}
		/* ignore if the status of a block is 'H4H_ABM_BLK_FREE_PREPARE' */
		if (blk->status == H4H_ABM_BLK_CLEAN) {
			h4h_msg ("oops! blk->status == H4H_ABM_BLK_CLEAN");
		}
	}

	return blk;
}

void h4h_abm_get_free_block_rollback (
	h4h_abm_info_t* bai,
	h4h_abm_block_t* blk)
{
	h4h_bug_on (blk->status != H4H_ABM_BLK_FREE_PREPARE);

	/* change the status of a block */
	blk->status = H4H_ABM_BLK_FREE;

	/* check some error cases */
	h4h_bug_on (bai->nr_free_blks_prepared == 0);
	__h4h_abm_check_status (bai);

	/* change the number of blks */
	bai->nr_free_blks_prepared--;
	bai->nr_free_blks++;
}

void h4h_abm_get_free_block_commit (
	h4h_abm_info_t* bai,
	h4h_abm_block_t* blk)
{
	/* see if the status of a block is correct or not */
	h4h_bug_on (blk->status != H4H_ABM_BLK_FREE_PREPARE);

	/* change the status */
	blk->status = H4H_ABM_BLK_CLEAN;

	/* move it to 'clean_list' */
	list_del (&blk->list);
	list_add_tail (&blk->list, &(bai->list_head_clean[blk->channel_no][blk->chip_no]));

	/* check some error cases */
	h4h_bug_on (bai->nr_free_blks_prepared == 0);
	__h4h_abm_check_status (bai);

	/* change the number of blks */
	bai->nr_free_blks_prepared--;
	bai->nr_clean_blks++;
}

void h4h_abm_erase_block (
	h4h_abm_info_t* bai,
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no,
	uint8_t is_bad)
{
	h4h_abm_block_t* blk = NULL;
	uint64_t blk_idx = 
		__get_block_idx (bai->np, channel_no, chip_no, block_no);

	/* see if blk_idx is correct or not */
	if (blk_idx >= bai->np->nr_blocks_per_ssd) {
		h4h_msg ("%llu %llu %llu", channel_no, chip_no, block_no);
		h4h_error ("blk_idx (%llu) is larger than # of blocks in SSD (%llu)",
			blk_idx, bai->np->nr_blocks_per_ssd);
		return;
	} else {
		blk = &bai->blocks[blk_idx];
	}

	if (blk->channel_no != channel_no || blk->chip_no != chip_no || blk->block_no != block_no) {
		h4h_error ("wrong block is chosen (%llu,%llu,%llu) != (%llu,%llu,%llu)",
			blk->channel_no, blk->chip_no, blk->block_no,
			channel_no, chip_no, block_no);
		return;
	}

	/* check some error cases */
	__h4h_abm_check_status (bai);

	/* change # of blks */
	if (blk->status == H4H_ABM_BLK_CLEAN) {
		h4h_bug_on (bai->nr_clean_blks == 0);
		bai->nr_clean_blks--;
	} else if (blk->status == H4H_ABM_BLK_DIRTY) {
		h4h_bug_on (bai->nr_dirty_blks == 0);
		bai->nr_dirty_blks--;
	} else if (blk->status == H4H_ABM_BLK_FREE) {
		h4h_bug_on (bai->nr_free_blks == 0);
		bai->nr_free_blks--;
	} else if (blk->status == H4H_ABM_BLK_FREE_PREPARE) {
		h4h_bug_on (bai->nr_free_blks_prepared == 0);
		bai->nr_free_blks_prepared--;
	} else if (blk->status == H4H_ABM_BLK_BAD) {
		h4h_bug_on (bai->nr_bad_blks == 0);
		bai->nr_bad_blks--;
	} else {
		h4h_bug_on (1);
	}

	if (is_bad == 1) {
		/* move it to 'bad_list' */
		list_del (&blk->list);
		list_add_tail (&blk->list, &(bai->list_head_bad[blk->channel_no][blk->chip_no]));
		bai->nr_bad_blks++;
		blk->status = H4H_ABM_BLK_BAD;	/* mark it bad */

		h4h_msg ("[BAD-BLOCK - MARKED] b:%llu c:%llu b:%llu p/e:%u", 
			blk->channel_no, 
			blk->chip_no, 
			blk->block_no, 
			blk->erase_count);

		/*__h4h_abm_display_status (bai);*/
		__h4h_abm_check_status (bai);

	} else {
		/* move it to 'free_list' */
		list_del (&blk->list);
		list_add_tail (&blk->list, &(bai->list_head_free[blk->channel_no][blk->chip_no]));
		bai->nr_free_blks++;
		blk->status = H4H_ABM_BLK_FREE;
	}

	__h4h_abm_check_status (bai);

	/* reset the block */
	blk->erase_count++;
	blk->nr_invalid_subpages = 0;
	if (blk->pst) {
		h4h_memset (blk->pst, 
			BABM_ABM_SUBPAGE_NOT_INVALID, 
			sizeof (babm_abm_subpage_t) * bai->np->nr_subpages_per_block
		);
	}
}

void h4h_abm_set_to_dirty_block (
	h4h_abm_info_t* bai,
	uint64_t channel_no, 
	uint64_t chip_no, 
	uint64_t block_no)
{
	h4h_abm_block_t* blk = NULL;
	uint64_t blk_idx = 
		__get_block_idx (bai->np, channel_no, chip_no, block_no);

	/* see if blk_idx is correct or not */
	if (blk_idx >= bai->np->nr_blocks_per_ssd) {
		h4h_msg ("%llu %llu %llu", channel_no, chip_no, block_no);
		h4h_error ("blk_idx (%llu) is larger than # of blocks in SSD (%llu)",
			blk_idx, bai->np->nr_blocks_per_ssd);
		return;
	} else {
		blk = &bai->blocks[blk_idx];
	}

	if (blk->channel_no != channel_no || blk->chip_no != chip_no || blk->block_no != block_no) {
		h4h_error ("wrong block is chosen (%llu,%llu,%llu) != (%llu,%llu,%llu)",
			blk->channel_no, blk->chip_no, blk->block_no,
			channel_no, chip_no, block_no);
		return;
	}

	/* check some error cases */
	__h4h_abm_check_status (bai);

	/* change # of blks */
	if (blk->status == H4H_ABM_BLK_CLEAN) {
		h4h_bug_on (bai->nr_clean_blks == 0);
		bai->nr_clean_blks--;
	} else if (blk->status == H4H_ABM_BLK_DIRTY) {
		h4h_bug_on (bai->nr_dirty_blks == 0);
		bai->nr_dirty_blks--;
	} else if (blk->status == H4H_ABM_BLK_FREE) {
		h4h_bug_on (bai->nr_free_blks == 0);
		bai->nr_free_blks--;
	} else if (blk->status == H4H_ABM_BLK_FREE_PREPARE) {
		h4h_bug_on (bai->nr_free_blks_prepared == 0);
		bai->nr_free_blks_prepared--;
	} else if (blk->status == H4H_ABM_BLK_BAD) {
		h4h_bug_on (bai->nr_bad_blks == 0);
		bai->nr_bad_blks--;
	} else {
		h4h_bug_on (1);
	}

	/* move it to 'free_list' */
	list_del (&blk->list);
	list_add_tail (&blk->list, &(bai->list_head_dirty[blk->channel_no][blk->chip_no]));
	bai->nr_dirty_blks++;
	blk->status = H4H_ABM_BLK_DIRTY;

	__h4h_abm_check_status (bai);

	/* reset the block */
	blk->nr_invalid_subpages = bai->np->nr_subpages_per_block;
	if (blk->pst) {
		h4h_memset (blk->pst, 
			H4H_ABM_SUBPAGE_INVALID, 
			sizeof (babm_abm_subpage_t) * bai->np->nr_subpages_per_block
		);
	}

}

void h4h_abm_invalidate_page (
	h4h_abm_info_t* bai, 
	uint64_t channel_no, 
	uint64_t chip_no, 
	uint64_t block_no, 
	uint64_t page_no,
	uint64_t subpage_no)
{
	h4h_abm_block_t* b = NULL;
	uint64_t pst_off = 0;

	b = h4h_abm_get_block (bai, channel_no, chip_no, block_no);

	h4h_bug_on (b == NULL);
	h4h_bug_on (page_no >= bai->np->nr_pages_per_block);
	h4h_bug_on (b->channel_no != channel_no);
	h4h_bug_on (b->chip_no != chip_no);
	h4h_bug_on (b->block_no != block_no);
	h4h_bug_on (subpage_no >= bai->np->nr_subpages_per_page);

	/* get a subpage offst in pst */
 	pst_off = (page_no * bai->np->nr_subpages_per_page) + subpage_no;
	h4h_bug_on (pst_off >= bai->np->nr_subpages_per_block);

	/* if pst is NULL, ignore it */
	if (b->pst == NULL)
		return;

	if (b->pst[pst_off] == BABM_ABM_SUBPAGE_NOT_INVALID) {
		b->pst[pst_off] = H4H_ABM_SUBPAGE_INVALID;
		/* is the block clean? */
		if (b->nr_invalid_subpages == 0) {
			if (b->status != H4H_ABM_BLK_CLEAN) {
				h4h_msg ("b->status: %u (%llu %llu %llu) (%llu %llu)", 
					b->status, channel_no, chip_no, block_no, page_no, subpage_no);
				h4h_bug_on (b->status != H4H_ABM_BLK_CLEAN);
			}

			/* if so, its status is changed and then moved to a dirty list */
			b->status = H4H_ABM_BLK_DIRTY;
			list_del (&b->list);
			list_add_tail (&b->list, &(bai->list_head_dirty[b->channel_no][b->chip_no]));

			if (bai->nr_clean_blks > 0) {
				h4h_bug_on (bai->nr_clean_blks == 0);
				__h4h_abm_check_status (bai);

				bai->nr_clean_blks--;
				bai->nr_dirty_blks++;
			}
		}
		/* increase # of invalid pages in the block */
		b->nr_invalid_subpages++;
		h4h_bug_on (b->nr_invalid_subpages > bai->np->nr_subpages_per_block);
	} else {
		/* ignore if it was invalidated before */
	}
}


/* for snapshot */
uint32_t h4h_abm_load (h4h_abm_info_t* bai, const char* fn)
{
	/*struct file* fp = NULL;*/
	h4h_file_t fp = 0;
	uint64_t i, pos = 0;

	if ((fp = h4h_fopen (fn, O_RDWR, 0777)) == 0) {
		h4h_error ("h4h_fopen failed");
		return 1;
	}

	/* step1: load a set of h4h_abm_block_t */
	for (i = 0; i < bai->np->nr_blocks_per_ssd; i++) {
		pos += h4h_fread (fp, pos, (uint8_t*)&bai->blocks[i].status, sizeof(bai->blocks[i].status));
		pos += h4h_fread (fp, pos, (uint8_t*)&bai->blocks[i].channel_no, sizeof(bai->blocks[i].channel_no));
		pos += h4h_fread (fp, pos, (uint8_t*)&bai->blocks[i].chip_no, sizeof(bai->blocks[i].chip_no));
		pos += h4h_fread (fp, pos, (uint8_t*)&bai->blocks[i].block_no, sizeof(bai->blocks[i].block_no));
		pos += h4h_fread (fp, pos, (uint8_t*)&bai->blocks[i].erase_count, sizeof(bai->blocks[i].erase_count));
		pos += h4h_fread (fp, pos, (uint8_t*)&bai->blocks[i].nr_invalid_subpages, sizeof(bai->blocks[i].nr_invalid_subpages));
		if (bai->blocks[i].pst) {
			pos += h4h_fread (fp, pos, (uint8_t*)bai->blocks[i].pst, sizeof (babm_abm_subpage_t) * bai->np->nr_subpages_per_block);
		} else {
			pos += sizeof (babm_abm_subpage_t) * bai->np->nr_subpages_per_block;
		}
	}

	/* step2: build lists & # of blocks */
	bai->nr_free_blks = 0;
	bai->nr_free_blks_prepared = 0;
	bai->nr_clean_blks = 0;
	bai->nr_dirty_blks = 0;
	bai->nr_bad_blks = 0;

	for (i = 0; i < bai->np->nr_blocks_per_ssd; i++) {
		h4h_abm_block_t* b = &bai->blocks[i];
		list_del (&b->list);
		switch (b->status) {
		case H4H_ABM_BLK_FREE:
			list_add_tail (&b->list, &(bai->list_head_free[b->channel_no][b->chip_no]));
			bai->nr_free_blks++;
			break;
		case H4H_ABM_BLK_FREE_PREPARE:
			list_add_tail (&b->list, &(bai->list_head_free[b->channel_no][b->chip_no]));
			bai->nr_free_blks_prepared++;
			break;
		case H4H_ABM_BLK_CLEAN:
			list_add_tail (&b->list, &(bai->list_head_clean[b->channel_no][b->chip_no]));
			bai->nr_clean_blks++;
			break;
		case H4H_ABM_BLK_DIRTY:
			list_add_tail (&b->list, &(bai->list_head_dirty[b->channel_no][b->chip_no]));
			bai->nr_dirty_blks++;
			break;
		case H4H_ABM_BLK_BAD:
			list_add_tail (&b->list, &(bai->list_head_bad[b->channel_no][b->chip_no]));
			bai->nr_bad_blks++;
			break;
		default:
			h4h_error ("invalid block type: blk-id = %llu, blk-status = %u", i, b->status);
			break;
		}
	}

	/* step3: display */
	h4h_msg ("abm-load: free:%llu, free(prepare):%llu, clean:%llu, dirty:%llu, bad:%llu",
		bai->nr_free_blks, 
		bai->nr_free_blks_prepared, 
		bai->nr_clean_blks,
		bai->nr_dirty_blks,
		bai->nr_bad_blks);

	h4h_fclose (fp);

	return 0;
}

uint32_t h4h_abm_store (h4h_abm_info_t* bai, const char* fn)
{
	/*struct file* fp = NULL;*/
	h4h_file_t fp = 0;
	uint64_t i, pos = 0;

	if ((fp = h4h_fopen (fn,  O_CREAT | O_WRONLY, 0777)) == 0) {
		h4h_error ("h4h_fopen failed");
		return 1;
	}

	/* step1: store a set of h4h_abm_block_t */
	for (i = 0; i < bai->np->nr_blocks_per_ssd; i++) {
		pos += h4h_fwrite (fp, pos, (uint8_t*)&bai->blocks[i].status, sizeof(bai->blocks[i].status));
		pos += h4h_fwrite (fp, pos, (uint8_t*)&bai->blocks[i].channel_no, sizeof(bai->blocks[i].channel_no));
		pos += h4h_fwrite (fp, pos, (uint8_t*)&bai->blocks[i].chip_no, sizeof(bai->blocks[i].chip_no));
		pos += h4h_fwrite (fp, pos, (uint8_t*)&bai->blocks[i].block_no, sizeof(bai->blocks[i].block_no));
		pos += h4h_fwrite (fp, pos, (uint8_t*)&bai->blocks[i].erase_count, sizeof(bai->blocks[i].erase_count));
		pos += h4h_fwrite (fp, pos, (uint8_t*)&bai->blocks[i].nr_invalid_subpages, sizeof(bai->blocks[i].nr_invalid_subpages));
		if (bai->blocks[i].pst) {
			pos += h4h_fwrite (fp, pos, (uint8_t*)bai->blocks[i].pst, sizeof (babm_abm_subpage_t) * bai->np->nr_subpages_per_block);
		} else {
			pos += sizeof (babm_abm_subpage_t) * bai->np->nr_subpages_per_block;
		}
	}

	h4h_msg ("abm-store: free:%llu, free(prepare):%llu, clean:%llu, dirty:%llu, bad:%llu",
		bai->nr_free_blks, 
		bai->nr_free_blks_prepared, 
		bai->nr_clean_blks,
		bai->nr_dirty_blks,
		bai->nr_bad_blks);

	h4h_fsync (fp);
	h4h_fclose (fp);

	return 0;
}

