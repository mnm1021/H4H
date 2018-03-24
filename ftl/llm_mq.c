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

#include "debug.h"
#include "umemory.h"
#include "params.h"
#include "h4h_drv.h"
#include "uthread.h"
#include "pmu.h"
#include "utime.h"

#include "queue/queue.h"
#include "queue/prior_queue.h"

#include "llm_mq.h"

/* NOTE: This serializes all of the requests from the host file system; 
 * it is useful for debugging */
/*#define ENABLE_SEQ_DBG*/


/* llm interface */
h4h_llm_inf_t _llm_mq_inf = {
	.ptr_private = NULL,
	.create = llm_mq_create,
	.destroy = llm_mq_destroy,
	.make_req = llm_mq_make_req,
	.flush = llm_mq_flush,
	.end_req = llm_mq_end_req,
};

/* private */
struct h4h_llm_mq_private {
	uint64_t nr_punits;
	h4h_sema_t* punit_locks;
	h4h_prior_queue_t* q;

	/* for debugging */
#if defined(ENABLE_SEQ_DBG)
	h4h_sema_t dbg_seq;
#endif	

	/* for thread management */
	h4h_thread_t* llm_thread;
};

int __llm_mq_thread (void* arg)
{
	h4h_drv_info_t* bdi = (h4h_drv_info_t*)arg;
	struct h4h_llm_mq_private* p = (struct h4h_llm_mq_private*)H4H_LLM_PRIV(bdi);
	uint64_t loop;
	uint64_t cnt = 0;

	if (p == NULL || p->q == NULL || p->llm_thread == NULL) {
		h4h_msg ("invalid parameters (p=%p, p->q=%p, p->llm_thread=%p",
			p, p->q, p->llm_thread);
		return 0;
	}

	for (;;) {
		/* give a chance to other processes if Q is empty */
		if (h4h_prior_queue_is_all_empty (p->q)) {
			h4h_thread_schedule_setup (p->llm_thread);
			if (h4h_prior_queue_is_all_empty (p->q)) {
				/* ok... go to sleep */
				if (h4h_thread_schedule_sleep (p->llm_thread) == SIGKILL)
					break;
			} else {
				/* there are items in Q; wake up */
				h4h_thread_schedule_cancel (p->llm_thread);
			}
		}

		/* send reqs until Q becomes empty */
		for (loop = 0; loop < p->nr_punits; loop++) {
			h4h_prior_queue_item_t* qitem = NULL;
			h4h_llm_req_t* r = NULL;

			/* if pu is busy, then go to the next pnit */
			if (!h4h_sema_try_lock (&p->punit_locks[loop]))
				continue;
			
			if ((r = (h4h_llm_req_t*)h4h_prior_queue_dequeue (p->q, loop, &qitem)) == NULL) {
				h4h_sema_unlock (&p->punit_locks[loop]);
				continue;
			}

			r->ptr_qitem = qitem;

			pmu_update_q (bdi, r);

			if (cnt % 10000000 == 0) {
				h4h_msg ("llm_make_req: %llu, %llu", cnt, h4h_prior_queue_get_nr_items (p->q));
			}

			if (bdi->ptr_dm_inf->make_req (bdi, r)) {
				h4h_sema_unlock (&p->punit_locks[loop]);

				/* TODO: I do not check whether it works well or not */
				bdi->ptr_llm_inf->end_req (bdi, r);
				h4h_warning ("oops! make_req failed");
			}

			cnt++;
		}
	}

	return 0;
}

uint32_t llm_mq_create (h4h_drv_info_t* bdi)
{
	struct h4h_llm_mq_private* p;
	uint64_t loop;

	/* create a private info for llm_nt */
	if ((p = (struct h4h_llm_mq_private*)h4h_malloc_atomic
			(sizeof (struct h4h_llm_mq_private))) == NULL) {
		h4h_error ("h4h_malloc_atomic failed");
		return -1;
	}

	/* get the total number of parallel units */
	p->nr_punits = H4H_GET_NR_PUNITS (bdi->parm_dev);

	/* create queue */
	if ((p->q = h4h_prior_queue_create (p->nr_punits, INFINITE_QUEUE)) == NULL) {
		h4h_error ("h4h_prior_queue_create failed");
		goto fail;
	}

	/* create completion locks for parallel units */
	if ((p->punit_locks = (h4h_sema_t*)h4h_malloc_atomic
			(sizeof (h4h_sema_t) * p->nr_punits)) == NULL) {
		h4h_error ("h4h_malloc_atomic failed");
		goto fail;
	}
	for (loop = 0; loop < p->nr_punits; loop++) {
		h4h_sema_init (&p->punit_locks[loop]);
	}

	/* keep the private structures for llm_nt */
	bdi->ptr_llm_inf->ptr_private = (void*)p;

	/* create & run a thread */
	if ((p->llm_thread = h4h_thread_create (
			__llm_mq_thread, bdi, "__llm_mq_thread")) == NULL) {
		h4h_error ("kthread_create failed");
		goto fail;
	}
	h4h_thread_run (p->llm_thread);

#if defined(ENABLE_SEQ_DBG)
	h4h_sema_init (&p->dbg_seq);
#endif

	return 0;

fail:
	if (p->punit_locks)
		h4h_free_atomic (p->punit_locks);
	if (p->q)
		h4h_prior_queue_destroy (p->q);
	if (p)
		h4h_free_atomic (p);
	return -1;
}

/* NOTE: we assume that all of the host requests are completely served.
 * the host adapter must be first closed before this function is called.
 * if not, it would work improperly. */
void llm_mq_destroy (h4h_drv_info_t* bdi)
{
	uint64_t loop;
	struct h4h_llm_mq_private* p = (struct h4h_llm_mq_private*)H4H_LLM_PRIV(bdi);

	if (p == NULL)
		return;

	/* wait until Q becomes empty */
	while (!h4h_prior_queue_is_all_empty (p->q)) {
		h4h_msg ("llm items = %llu", h4h_prior_queue_get_nr_items (p->q));
		h4h_thread_msleep (1);
	}

	/* kill kthread */
	h4h_thread_stop (p->llm_thread);

	for (loop = 0; loop < p->nr_punits; loop++) {
		h4h_sema_lock (&p->punit_locks[loop]);
	}

	/* release all the relevant data structures */
	if (p->q)
		h4h_prior_queue_destroy (p->q);
	if (p) 
		h4h_free_atomic (p);
	h4h_msg ("done");
}

uint32_t llm_mq_make_req (h4h_drv_info_t* bdi, h4h_llm_req_t* r)
{
	uint32_t ret;
	struct h4h_llm_mq_private* p = (struct h4h_llm_mq_private*)H4H_LLM_PRIV(bdi);

#if defined(ENABLE_SEQ_DBG)
	h4h_sema_lock (&p->dbg_seq);
#endif

	/* obtain the elapsed time taken by FTL algorithms */
	pmu_update_sw (bdi, r);

	/* wait until there are enough free slots in Q */
	//while (h4h_prior_queue_get_nr_items (p->q) >= 96) {
	/*while (h4h_prior_queue_get_nr_items (p->q) >= 262144) {*/
	while (h4h_prior_queue_get_nr_items (p->q) >= 20480) {
		h4h_thread_yield ();
	}

	/* put a request into Q */
	if (h4h_is_rmw (r->req_type) && h4h_is_read (r->req_type)) {
		/* step 1: put READ first */
		r->phyaddr = r->phyaddr_src;
		if ((ret = h4h_prior_queue_enqueue (p->q, r->phyaddr_src.punit_id, r->logaddr.lpa[0], (void*)r))) {
			h4h_msg ("h4h_prior_queue_enqueue failed");
		}
		/* step 2: put WRITE second with the same LPA */
		if ((ret = h4h_prior_queue_enqueue (p->q, r->phyaddr_dst.punit_id, r->logaddr.lpa[0], (void*)r))) {
			h4h_msg ("h4h_prior_queue_enqueue failed");
		}
	} else if (h4h_is_rmw (r->req_type) && h4h_is_read (r->req_type)) {
		h4h_bug_on (1);
	} else {
		if ((ret = h4h_prior_queue_enqueue (p->q, r->phyaddr.punit_id, r->logaddr.lpa[0], (void*)r))) {
			h4h_msg ("h4h_prior_queue_enqueue failed");
		}
	}

	/* wake up thread if it sleeps */
	h4h_thread_wakeup (p->llm_thread);

	return ret;
}

void llm_mq_flush (h4h_drv_info_t* bdi)
{
	struct h4h_llm_mq_private* p = (struct h4h_llm_mq_private*)H4H_LLM_PRIV(bdi);

	while (h4h_prior_queue_is_all_empty (p->q) != 1) {
		/*cond_resched ();*/
		h4h_thread_yield ();
	}
}

/* structs required for making that the block is actually written to media. */
#include "../../ftl/algo/abm.h"
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

	/* 3 different allocator for data separation */
	h4h_abm_block_t* cold_blk;
	h4h_abm_block_t* warm_blk;
	h4h_abm_block_t* hot_blk;
} h4h_page_ftl_private_t;

void llm_mq_end_req (h4h_drv_info_t* bdi, h4h_llm_req_t* r)
{
	struct h4h_llm_mq_private* p = (struct h4h_llm_mq_private*)H4H_LLM_PRIV(bdi);
	h4h_prior_queue_item_t* qitem = (h4h_prior_queue_item_t*)r->ptr_qitem;

	/* at the write to last page of certain block, we assume that this block is fully written. */
//	if (h4h_is_write(r->req_type) &&
//			r->phyaddr.page_no == H4H_GET_DEVICE_PARAMS (bdi)->nr_pages_per_block - 1)
	{
		h4h_abm_block_t* block = h4h_abm_get_block (
				((h4h_page_ftl_private_t *)H4H_FTL_PRIV (bdi))->bai,
				r->phyaddr.channel_no,
				r->phyaddr.chip_no,
				r->phyaddr.block_no
				);
//		block->is_full = 1;
		atomic64_inc (&block->is_full);
	}

	if (h4h_is_rmw (r->req_type) && h4h_is_read(r->req_type)) {
		/* get a parallel unit ID */
		/*h4h_msg ("unlock: %lld", r->phyaddr.punit_id);*/
		h4h_sema_unlock (&p->punit_locks[r->phyaddr.punit_id]);

		/*h4h_msg ("LLM Done: lpa=%llu", r->logaddr.lpa[0]);*/

		pmu_inc (bdi, r);

		/* change its type to WRITE if req_type is RMW */
		r->req_type = REQTYPE_RMW_WRITE;
		r->phyaddr = r->phyaddr_dst;

		/* remove it from the Q; this automatically triggers another request to be sent to NAND flash */
		h4h_prior_queue_remove (p->q, qitem);

		/* wake up thread if it sleeps */
		h4h_thread_wakeup (p->llm_thread);
	} else {
		/* get a parallel unit ID */
		h4h_prior_queue_remove (p->q, qitem);

		/* complete a lock */
		/*h4h_msg ("unlock: %lld", r->phyaddr.punit_id);*/
		h4h_sema_unlock (&p->punit_locks[r->phyaddr.punit_id]);

		/* update the elapsed time taken by NAND devices */
		pmu_update_tot (bdi, r);
		pmu_inc (bdi, r);

		/* finish a request */
		bdi->ptr_hlm_inf->end_req (bdi, r);


#if defined(ENABLE_SEQ_DBG)
		h4h_sema_unlock (&p->dbg_seq);
#endif
	}
}
