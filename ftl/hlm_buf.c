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
#include "hlm_nobuf.h"
#include "hlm_buf.h"
#include "uthread.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "queue/queue.h"


/* interface for hlm_buf */
h4h_hlm_inf_t _hlm_buf_inf = {
	.ptr_private = NULL,
	.create = hlm_buf_create,
	.destroy = hlm_buf_destroy,
	.make_req = hlm_buf_make_req,
	.end_req = hlm_buf_end_req,
};


/* data structures for hlm_buf */
struct h4h_hlm_buf_private {
	h4h_ftl_inf_t* ptr_ftl_inf;	/* for hlm_nobuff (it must be on top of this structure) */

	/* for thread management */
	h4h_queue_t* q;
	h4h_thread_t* hlm_thread;
};


/* kernel thread for _llm_q */
int __hlm_buf_thread (void* arg)
{
	h4h_drv_info_t* bdi = (h4h_drv_info_t*)arg;
	struct h4h_hlm_buf_private* p = (struct h4h_hlm_buf_private*)H4H_HLM_PRIV(bdi);
	h4h_hlm_req_t* r;

	for (;;) {
		if (h4h_queue_is_all_empty (p->q)) {
			if (h4h_thread_schedule (p->hlm_thread) == SIGKILL) {
				break;
			}
		}

		/* if nothing is in Q, then go to the next punit */
		while (!h4h_queue_is_empty (p->q, 0)) {
			if ((r = (h4h_hlm_req_t*)h4h_queue_dequeue (p->q, 0)) != NULL) {
				if (hlm_nobuf_make_req (bdi, r)) {
					/* if it failed, we directly call 'ptr_host_inf->end_req' */
					bdi->ptr_host_inf->end_req (bdi, r);
					h4h_warning ("oops! make_req failed");
					/* [CAUTION] r is now NULL */
				}
			} else {
				h4h_error ("r == NULL");
				h4h_bug_on (1);
			}
		} 
	}

	return 0;
}

/* interface functions for hlm_buf */
uint32_t hlm_buf_create (h4h_drv_info_t* bdi)
{
	struct h4h_hlm_buf_private* p;

	/* create private */
	if ((p = (struct h4h_hlm_buf_private*)h4h_malloc_atomic
			(sizeof(struct h4h_hlm_buf_private))) == NULL) {
		h4h_error ("h4h_malloc_atomic failed");
		return 1;
	}

	/* setup FTL function pointers */
	if ((p->ptr_ftl_inf = H4H_GET_FTL_INF (bdi)) == NULL) {
		h4h_error ("ftl is not valid");
		return 1;
	}

	/* create a single queue */
	if ((p->q = h4h_queue_create (1, INFINITE_QUEUE)) == NULL) {
		h4h_error ("h4h_queue_create failed");
		return -1;
	}

	/* keep the private structure */
	bdi->ptr_hlm_inf->ptr_private = (void*)p;

	/* create & run a thread */
	if ((p->hlm_thread = h4h_thread_create (
			__hlm_buf_thread, bdi, "__hlm_buf_thread")) == NULL) {
		h4h_error ("kthread_create failed");
		return -1;
	}
	h4h_thread_run (p->hlm_thread);

	return 0;
}

void hlm_buf_destroy (h4h_drv_info_t* bdi)
{
	struct h4h_hlm_buf_private* p = (struct h4h_hlm_buf_private*)bdi->ptr_hlm_inf->ptr_private;

	/* wait until Q becomes empty */
	while (!h4h_queue_is_all_empty (p->q)) {
		h4h_msg ("hlm items = %llu", h4h_queue_get_nr_items (p->q));
		h4h_thread_msleep (1);
	}

	/* kill kthread */
	h4h_thread_stop (p->hlm_thread);

	/* destroy queue */
	h4h_queue_destroy (p->q);

	/* free priv */
	h4h_free_atomic (p);
}

uint32_t hlm_buf_make_req (
	h4h_drv_info_t* bdi, 
	h4h_hlm_req_t* r)
{
	uint32_t ret;
	struct h4h_hlm_buf_private* p = (struct h4h_hlm_buf_private*)H4H_HLM_PRIV(bdi);

	if (h4h_queue_is_full (p->q)) {
		/* FIXME: wait unti queue has a enough room */
		h4h_error ("it should not be happened!");
		h4h_bug_on (1);
	} 

	while (h4h_queue_get_nr_items (p->q) >= 256) {
		h4h_thread_yield ();
	}
	
	/* put a request into Q */
	if ((ret = h4h_queue_enqueue (p->q, 0, (void*)r))) {
		h4h_msg ("h4h_queue_enqueue failed");
	}

	/* wake up thread if it sleeps */
	h4h_thread_wakeup (p->hlm_thread);

	return ret;
}

void hlm_buf_end_req (
	h4h_drv_info_t* bdi, 
	h4h_llm_req_t* r)
{
	hlm_nobuf_end_req (bdi, r);
}

