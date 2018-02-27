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

#if defined(KERNEL_MODE)
#include <linux/module.h>
#include <linux/blkdev.h>

#elif defined(USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "debug.h"
#include "params.h"
#include "h4h_drv.h"
#include "hlm_reqs_pool.h"
#include "umemory.h"


#define DEFAULT_POOL_SIZE		128
#define DEFAULT_POOL_INC_SIZE	DEFAULT_POOL_SIZE / 5

h4h_hlm_reqs_pool_t* h4h_hlm_reqs_pool_create (
	int32_t mapping_unit_size, 
	int32_t io_unit_size)
{
	h4h_hlm_reqs_pool_t* pool = NULL;
	int in_place_rmw = 0;
	int i = 0;

	/* check input arguments */
	if (mapping_unit_size > io_unit_size) {
		h4h_error ("oops! mapping_unit_size > io_unit_size (%d > %d)", mapping_unit_size, io_unit_size);
		return NULL;
	}
	if (mapping_unit_size < io_unit_size && mapping_unit_size != KPAGE_SIZE) {
		h4h_error ("oops! mapping_unit_size is not equal to KERNEL_PAGE_SIZE (%d)", mapping_unit_size);
		return NULL;
	}
	if (io_unit_size > KPAGE_SIZE && mapping_unit_size == io_unit_size) {
		in_place_rmw = 1;
	}

	/* create a pool structure */
	if ((pool = h4h_malloc (sizeof (h4h_hlm_reqs_pool_t))) == NULL) {
		h4h_error ("h4h_malloc () failed");
		return NULL;
	}

	/* initialize variables */
	h4h_spin_lock_init (&pool->lock);
	INIT_LIST_HEAD (&pool->used_list);
	INIT_LIST_HEAD (&pool->free_list);
	pool->pool_size = DEFAULT_POOL_SIZE;
	pool->map_unit = mapping_unit_size;
	pool->io_unit = io_unit_size;
	pool->in_place_rmw = in_place_rmw;

	/* add hlm_reqs to the free-list */
	for (i = 0; i < DEFAULT_POOL_SIZE; i++) {
		h4h_hlm_req_t* item = NULL;
		if ((item = (h4h_hlm_req_t*)h4h_malloc (sizeof (h4h_hlm_req_t))) == NULL) {
			h4h_error ("h4h_malloc () failed");
			goto fail;
		}
		hlm_reqs_pool_allocate_llm_reqs (item->llm_reqs, H4H_BLKIO_MAX_VECS, RP_MEM_VIRT);
		h4h_sema_init (&item->done);
		list_add_tail (&item->list, &pool->free_list);
	}

	return pool;

fail:
	/* oops! it failed */
	if (pool) {
		struct list_head* next = NULL;
		struct list_head* temp = NULL;
		h4h_hlm_req_t* item = NULL;
		list_for_each_safe (next, temp, &pool->free_list) {
			item = list_entry (next, h4h_hlm_req_t, list);
			list_del (&item->list);
			hlm_reqs_pool_release_llm_reqs (item->llm_reqs, H4H_BLKIO_MAX_VECS, RP_MEM_VIRT);
			h4h_sema_free (&item->done);
			h4h_free (item);
		}
		h4h_spin_lock_destory (&pool->lock);
		h4h_free (pool);
		pool = NULL;
	}
	return NULL;
}

void h4h_hlm_reqs_pool_destroy (
	h4h_hlm_reqs_pool_t* pool)
{
	struct list_head* next = NULL;
	struct list_head* temp = NULL;
	h4h_hlm_req_t* item = NULL;
	int32_t count = 0;

	if (!pool) return;

	/* free & remove items from the used_list */
	list_for_each_safe (next, temp, &pool->used_list) {
		item = list_entry (next, h4h_hlm_req_t, list);
		list_del (&item->list);
		hlm_reqs_pool_release_llm_reqs (item->llm_reqs, H4H_BLKIO_MAX_VECS, RP_MEM_VIRT);
		h4h_sema_free (&item->done);
		h4h_free (item);
		count++;
	}

	/* free & remove items from the free_list */
	list_for_each_safe (next, temp, &pool->free_list) {
		item = list_entry (next, h4h_hlm_req_t, list);
		list_del (&item->list);
		hlm_reqs_pool_release_llm_reqs (item->llm_reqs, H4H_BLKIO_MAX_VECS, RP_MEM_VIRT);
		h4h_sema_free (&item->done);
		h4h_free (item);
		count++;
	}

	if (count != pool->pool_size) {
		h4h_warning ("oops! count != pool->pool_size (%d != %d)",
			count, pool->pool_size);
	}

	/* free other stuff */
	h4h_spin_lock_destory (&pool->lock);
	h4h_free (pool);
}

h4h_hlm_req_t* h4h_hlm_reqs_pool_get_item (
	h4h_hlm_reqs_pool_t* pool)
{
	struct list_head* pos = NULL;
	h4h_hlm_req_t* item = NULL;

	h4h_spin_lock (&pool->lock);

again:
	/* see if there are free items in the free_list */
	list_for_each (pos, &pool->free_list) {
		item = list_entry (pos, h4h_hlm_req_t, list);
		break;
	}

	/* oops! there are no free items in the free-list */
	if (item == NULL) {
		int i = 0;
		h4h_msg ("size of pool: %u", pool->pool_size + DEFAULT_POOL_INC_SIZE);
		/* add more items to the free-list */
		for (i = 0; i < DEFAULT_POOL_INC_SIZE; i++) {
			h4h_hlm_req_t* item = NULL;
			if ((item = (h4h_hlm_req_t*)h4h_malloc (sizeof (h4h_hlm_req_t))) == NULL) {
				h4h_error ("h4h_malloc () failed");
				goto fail;
			}
			hlm_reqs_pool_allocate_llm_reqs (item->llm_reqs, H4H_BLKIO_MAX_VECS, RP_MEM_VIRT);
			h4h_sema_init (&item->done);
			list_add_tail (&item->list, &pool->free_list);
		}
		/* increase the size of the pool */
		pool->pool_size += DEFAULT_POOL_INC_SIZE;

		/* try it again */
		goto again;
	}

	if (item == NULL)
		goto fail;

	/* move it to the used_list */
	list_del (&item->list);
	list_add_tail (&item->list, &pool->used_list);

	h4h_spin_unlock (&pool->lock);
	return item;

fail:

	h4h_spin_unlock (&pool->lock);
	return NULL;
}

void h4h_hlm_reqs_pool_free_item (
	h4h_hlm_reqs_pool_t* pool, 
	h4h_hlm_req_t* item)
{
	h4h_sema_unlock (&item->done);

	h4h_spin_lock (&pool->lock);
	list_del (&item->list);
	list_add_tail (&item->list, &pool->free_list);
	h4h_spin_unlock (&pool->lock);
}

static int __hlm_reqs_pool_create_trim_req  (
	h4h_hlm_reqs_pool_t* pool, 
	h4h_hlm_req_t* hr,
	h4h_blkio_req_t* br)
{
	int64_t sec_start, sec_end;

	/* trim boundary sectors */
	sec_start = H4H_ALIGN_UP (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = H4H_ALIGN_DOWN (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));

	/* initialize variables */
	hr->req_type = br->bi_rw;
	h4h_stopwatch_start (&hr->sw);
	if (sec_start < sec_end) {
		hr->lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->len = (sec_end - sec_start) / NR_KSECTORS_IN(pool->map_unit);
	} else {
		hr->lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->len = 0;
	}
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	return 0;
}

void hlm_reqs_pool_allocate_llm_reqs (
	h4h_llm_req_t* llm_reqs, 
	int32_t nr_llm_reqs,
	h4h_rp_mem flag)
{
	int i = 0, j = 0;
	h4h_flash_page_main_t* fm = NULL;
	h4h_flash_page_oob_t* fo = NULL;

	/* setup main page */
	for (i = 0; i < nr_llm_reqs; i++) {
		fm = &llm_reqs[i].fmain;
		fo = &llm_reqs[i].foob;
		for (j = 0; j < H4H_MAX_PAGES; j++)
			if (flag == RP_MEM_PHY)
				//fm->kp_pad[j] = (uint8_t*)h4h_malloc_phy (KPAGE_SIZE);
				fm->kp_pad[j] = NULL;
			else 
				//fm->kp_pad[j] = (uint8_t*)h4h_malloc (KPAGE_SIZE);
				fm->kp_pad[j] = NULL;
		/*
		if (flag == RP_MEM_PHY)
			fo->data = (uint8_t*)h4h_malloc_phy (8*H4H_MAX_PAGES);
		else
			fo->data = (uint8_t*)h4h_malloc (8*H4H_MAX_PAGES);
		*/
	}
}

void hlm_reqs_pool_release_llm_reqs (
	h4h_llm_req_t* llm_reqs, 
	int32_t nr_llm_reqs,
	h4h_rp_mem flag)
{
	int i = 0, j = 0;
	h4h_flash_page_main_t* fm = NULL;
	h4h_flash_page_oob_t* fo = NULL;

	/* setup main page */
	for (i = 0; i < nr_llm_reqs; i++) {
		fm = &llm_reqs[i].fmain;
		fo = &llm_reqs[i].foob;
		for (j = 0; j < H4H_MAX_PAGES; j++) {
			if (fm->kp_pad[j] == NULL)
				continue;
			if (flag == RP_MEM_PHY)
				h4h_free_phy (fm->kp_pad[j]);
			else
				h4h_free (fm->kp_pad[j]);
		}
		/*
		if (flag == RP_MEM_PHY)
			h4h_free_phy (fo->data);
		else
			h4h_free (fo->data);
		*/
	}
}

void hlm_reqs_pool_reset_fmain (h4h_flash_page_main_t* fmain)
{
	int i = 0;
	while (i < H4H_MAX_PAGES) {
		fmain->kp_stt[i] = KP_STT_HOLE;
		fmain->kp_ptr[i] = fmain->kp_pad[i]; 
		/* Note that fmain->kp_pad[i] could be NULL; In that case, kp_pad[i]
		 * must be mapped to memory later by calling
		 * hlm_reqs_pool_alloc_fmain_pad () */ 
		i++;
	}
}

void hlm_reqs_pool_alloc_fmain_pad (h4h_flash_page_main_t* fmain)
{
	int i = 0;
	while (i < H4H_MAX_PAGES) {
		if (fmain->kp_stt[i] == KP_STT_HOLE && fmain->kp_pad[i] == NULL) {
			fmain->kp_pad[i] = h4h_malloc (KPAGE_SIZE);
			fmain->kp_ptr[i] = fmain->kp_pad[i];
		}
		i++;
	}
}

void hlm_reqs_pool_reset_logaddr (h4h_logaddr_t* logaddr)
{
	int i = 0;
	while (i < H4H_MAX_PAGES) {
		logaddr->lpa[i] = -1;
		i++;
	}
	logaddr->ofs = 0;
}

static int __hlm_reqs_pool_create_write_req (
	h4h_hlm_reqs_pool_t* pool, 
	h4h_hlm_req_t* hr,
	h4h_blkio_req_t* br)
{
	int64_t sec_start, sec_end, pg_start, pg_end;
	int64_t i = 0, j = 0, k = 0;
	int64_t hole = 0, bvec_cnt = 0, nr_llm_reqs;
	h4h_flash_page_main_t* ptr_fm = NULL;
	h4h_llm_req_t* ptr_lr = NULL;

	/* expand boundary sectors */
	sec_start = H4H_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = H4H_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));
	h4h_bug_on (sec_start >= sec_end);

	pg_start = H4H_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = H4H_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	h4h_bug_on (pg_start >= pg_end);

	/* build llm_reqs */
	nr_llm_reqs = H4H_ALIGN_UP ((sec_end - sec_start), NR_KSECTORS_IN(pool->io_unit)) / NR_KSECTORS_IN(pool->io_unit);
	h4h_bug_on (nr_llm_reqs > H4H_BLKIO_MAX_VECS);

	ptr_lr = &hr->llm_reqs[0];
	for (i = 0; i < nr_llm_reqs; i++) {
		int fm_ofs = 0;

		ptr_fm = &ptr_lr->fmain;
		hlm_reqs_pool_reset_fmain (ptr_fm);
		hlm_reqs_pool_reset_logaddr (&ptr_lr->logaddr);

		/* build mapping-units */
		for (j = 0, hole = 0; j < pool->io_unit / pool->map_unit; j++) {
			/* build kernel-pages */
			ptr_lr->logaddr.lpa[j] = sec_start / NR_KSECTORS_IN(pool->map_unit);
			for (k = 0; k < NR_KPAGES_IN(pool->map_unit); k++) {
				uint64_t pg_off = sec_start / NR_KSECTORS_IN(KPAGE_SIZE);

				if (pg_off >= pg_start && pg_off < pg_end) {
					h4h_bug_on (bvec_cnt >= br->bi_bvec_cnt);
					if (bvec_cnt >= br->bi_bvec_cnt) {
						h4h_msg ("%lld %lld", bvec_cnt, br->bi_bvec_cnt);
					}
					ptr_fm->kp_stt[fm_ofs] = KP_STT_DATA;
					ptr_fm->kp_ptr[fm_ofs] = br->bi_bvec_ptr[bvec_cnt++]; /* assign actual data */
				} else {
					hole = 1;
				}

				/* go to the next */
				sec_start += NR_KSECTORS_IN(KPAGE_SIZE);
				fm_ofs++;
			}

			if (sec_start >= sec_end)
				break;
		}

		/* if there are holes, they must be filled up with valid memory */
		hlm_reqs_pool_alloc_fmain_pad (ptr_fm);

		/* decide the reqtype for llm_req */
		ptr_lr->req_type = br->bi_rw;
		if (hole == 1 && pool->in_place_rmw && br->bi_rw == REQTYPE_WRITE) {
			/* NOTE: if there are holes and map-unit is equal to io-unit, we
			 * should perform old-fashioned RMW operations */
			ptr_lr->req_type = REQTYPE_RMW_READ;
		}

		/* go to the next */
		ptr_lr->ptr_hlm_req = (void*)hr;
		ptr_lr++;
	}

	h4h_bug_on (bvec_cnt != br->bi_bvec_cnt);

	/* intialize hlm_req */
	hr->req_type = br->bi_rw;
	h4h_stopwatch_start (&hr->sw);
	hr->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hr->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hr->done);
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	return 0;
}

static int __hlm_reqs_pool_create_read_req (
	h4h_hlm_reqs_pool_t* pool, 
	h4h_hlm_req_t* hr,
	h4h_blkio_req_t* br)
{
	int64_t pg_start, pg_end, i = 0;
	int64_t offset = 0, bvec_cnt = 0, nr_llm_reqs;
	h4h_llm_req_t* ptr_lr = NULL;

	pg_start = H4H_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = H4H_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	h4h_bug_on (pg_start >= pg_end);

	/* build llm_reqs */
	nr_llm_reqs = pg_end - pg_start;

	ptr_lr = &hr->llm_reqs[0];
	for (i = 0; i < nr_llm_reqs; i++) {
		offset = pg_start % NR_KPAGES_IN(pool->map_unit);

		if (pool->in_place_rmw == 0)
			h4h_bug_on (offset != 0);

		hlm_reqs_pool_reset_fmain (&ptr_lr->fmain);
		ptr_lr->fmain.kp_stt[offset] = KP_STT_DATA;
		ptr_lr->fmain.kp_ptr[offset] = br->bi_bvec_ptr[bvec_cnt++];
		hlm_reqs_pool_alloc_fmain_pad (&ptr_lr->fmain);

		hlm_reqs_pool_reset_logaddr (&ptr_lr->logaddr);
		ptr_lr->req_type = br->bi_rw;
		ptr_lr->logaddr.lpa[0] = pg_start / NR_KPAGES_IN(pool->map_unit);
		if (pool->in_place_rmw == 1) 
			ptr_lr->logaddr.ofs = 0;		/* offset in llm is already decided */
		else
			ptr_lr->logaddr.ofs = offset;	/* it must be adjusted after getting physical locations */
		ptr_lr->ptr_hlm_req = (void*)hr;

		/* go to the next */
		pg_start++;
		ptr_lr++;
	}

	h4h_bug_on (bvec_cnt != br->bi_bvec_cnt);

	/* intialize hlm_req */
	hr->req_type = br->bi_rw;
	h4h_stopwatch_start (&hr->sw);
	hr->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hr->nr_llm_reqs_done, 0);
	h4h_sema_lock (&hr->done);
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	return 0;
}

int h4h_hlm_reqs_pool_build_req (
	h4h_hlm_reqs_pool_t* pool, 
	h4h_hlm_req_t* hr,
	h4h_blkio_req_t* br)
{
	int ret = 1;

	/* create a hlm_req using a bio */
	if (br->bi_rw == REQTYPE_TRIM) {
		ret = __hlm_reqs_pool_create_trim_req (pool, hr, br);
	} else if (br->bi_rw == REQTYPE_WRITE) {
		ret = __hlm_reqs_pool_create_write_req (pool, hr, br);
	} else if (br->bi_rw == REQTYPE_READ) {
		ret = __hlm_reqs_pool_create_read_req (pool, hr, br);
	}

	/* are there any errors? */
	if (ret != 0) {
		h4h_error ("oops! invalid request type: (%llx)", br->bi_rw);
		return 1;
	}

	/* inherit data hotness of blkio_req */
	hr->data_hotness = br->data_hotness;

	return 0;
}

void hlm_reqs_pool_relocate_kp (h4h_llm_req_t* lr, uint64_t new_sp_ofs)
{
	if (new_sp_ofs != lr->logaddr.ofs) {
		lr->fmain.kp_stt[new_sp_ofs] = KP_STT_DATA;
		lr->fmain.kp_ptr[new_sp_ofs] = lr->fmain.kp_ptr[lr->logaddr.ofs];
		lr->fmain.kp_stt[lr->logaddr.ofs] = KP_STT_HOLE;
		lr->fmain.kp_ptr[lr->logaddr.ofs] = lr->fmain.kp_pad[lr->logaddr.ofs];
	}
}

void hlm_reqs_pool_write_compaction (
	h4h_hlm_req_gc_t* dst, 
	h4h_hlm_req_gc_t* src, 
	h4h_device_params_t* np)
{
	uint64_t dst_loop = 0, dst_kp = 0, src_kp = 0, i = 0;
	uint64_t nr_punits = np->nr_chips_per_channel * np->nr_channels;

	h4h_llm_req_t* dst_r = NULL;
	h4h_llm_req_t* src_r = NULL;

	dst->nr_llm_reqs = 0;
	for (i = 0; i < nr_punits * np->nr_pages_per_block; i++)
		hlm_reqs_pool_reset_fmain (&dst->llm_reqs[i].fmain);

	dst_r = &dst->llm_reqs[0];
	dst->nr_llm_reqs = 1;
	for (i = 0; i < nr_punits * np->nr_pages_per_block; i++) {
		src_r = &src->llm_reqs[i];

		for (src_kp = 0; src_kp < np->nr_subpages_per_page; src_kp++) {
			if (src_r->fmain.kp_stt[src_kp] == KP_STT_DATA) {
				/* if src has data, copy it to dst */
				dst_r->fmain.kp_stt[dst_kp] = src_r->fmain.kp_stt[src_kp];
				dst_r->fmain.kp_ptr[dst_kp] = src_r->fmain.kp_ptr[src_kp];
				dst_r->logaddr.lpa[dst_kp] = src_r->logaddr.lpa[src_kp];
				((int64_t*)dst_r->foob.data)[dst_kp] = ((int64_t*)src_r->foob.data)[src_kp];
			} else {
				/* otherwise, skip it */
				continue;
			}

			/* goto the next llm if all kps are full */
			dst_kp++;
			if (dst_kp == np->nr_subpages_per_page) {
				dst_kp = 0;
				dst_loop++;
				dst_r++;
				dst->nr_llm_reqs++;
			}
		}
	}

	for (i = 0; i < nr_punits * np->nr_pages_per_block; i++)
		hlm_reqs_pool_alloc_fmain_pad (&dst->llm_reqs[i].fmain);
}
