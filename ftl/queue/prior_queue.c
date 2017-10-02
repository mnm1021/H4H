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
#include <linux/list.h>

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "h4h_drv.h"
#include "debug.h"
#include "umemory.h"
#include "prior_queue.h"


/*static uint64_t max_queue_items = 0;*/

static uint64_t get_highest_priority_tag (
	h4h_prior_queue_t* mq, 
	uint64_t lpa)
{
	h4h_prior_lpa_item_t* q = NULL;

	HASH_FIND_INT (mq->hash_lpa, &lpa, q);
	if (q)
		return q->cur_tag;
	return -1;
}

static void remove_highest_priority_tag (
	h4h_prior_queue_t* mq, 
	uint64_t lpa)
{
	h4h_prior_lpa_item_t* q = NULL;

	HASH_FIND_INT (mq->hash_lpa, &lpa, q);
	if (q && q->lpa == lpa) {
		if (q->max_tag == q->cur_tag) {
			HASH_DEL (mq->hash_lpa, q);
			h4h_free (q);
			q = NULL;
		} else if (q->max_tag > q->cur_tag)
			q->cur_tag++;
		else {
			h4h_error ("oops!!!");
			h4h_bug_on (1);
		}
	}
}

static uint64_t get_new_priority_tag (
	h4h_prior_queue_t* mq, 
	uint64_t lpa)
{
	h4h_prior_lpa_item_t* q = NULL;

	HASH_FIND_INT (mq->hash_lpa, &lpa, q);
	if (q == NULL) {
		if ((q = (h4h_prior_lpa_item_t*)h4h_malloc
				(sizeof (h4h_prior_lpa_item_t))) == NULL) {
			h4h_error ("h4h_malloc failed");
			h4h_bug_on (1);
		}
		q->lpa = lpa;
		q->max_tag = 1;
		q->cur_tag = 1;
		HASH_ADD_INT (mq->hash_lpa, lpa, q);
	} else
		q->max_tag++;

	return q->max_tag;
}

h4h_prior_queue_t* h4h_prior_queue_create (
	uint64_t nr_queues, 
	int64_t max_size)
{
	h4h_prior_queue_t* mq;
	uint64_t loop;

	/* create a private structure */
	if ((mq = h4h_malloc (sizeof (h4h_prior_queue_t))) == NULL) {
		h4h_msg ("h4h_malloc_alloc failed");
		return NULL;
	}
	mq->nr_queues = nr_queues;
	mq->max_size = max_size;
	mq->qic = 0;
	h4h_spin_lock_init (&mq->lock);

	/* create linked-lists */
	if ((mq->qlh = h4h_malloc (sizeof (struct list_head) * mq->nr_queues)) == NULL) {
		h4h_msg ("h4h_malloc_alloc failed");
		h4h_free (mq);
		return NULL;
	}
	for (loop = 0; loop < mq->nr_queues; loop++)
		INIT_LIST_HEAD (&mq->qlh[loop]);

	/* create hash */
	mq->hash_lpa = NULL;

	return mq;
}

/* NOTE: it must be called when mq is empty. */
void h4h_prior_queue_destroy (h4h_prior_queue_t* mq)
{
	h4h_prior_lpa_item_t *c, *tmp;

	if (mq == NULL)
		return;

	HASH_ITER (hh, mq->hash_lpa, c, tmp) {
		h4h_warning ("hmm.. there are still some items in the hash table");
		HASH_DEL (mq->hash_lpa, c);
		h4h_free (c);
	}
	h4h_free (mq->qlh);
	h4h_free (mq);
}

uint8_t h4h_prior_queue_enqueue (
	h4h_prior_queue_t* mq, 
	uint64_t qid, 
	uint64_t lpa, 
	void* req)
{
	uint32_t ret = 1;

	if (qid >= mq->nr_queues) {
		h4h_error ("qid is invalid (%llu)", qid);
		return 1;
	}

	h4h_spin_lock (&mq->lock);
	if (mq->max_size == INFINITE_PRIOR_QUEUE || mq->qic < mq->max_size) {
		h4h_prior_queue_item_t* q = NULL;
		if ((q = h4h_malloc (sizeof (h4h_prior_queue_item_t))) == NULL) {
			h4h_error ("h4h_malloc failed");
			h4h_bug_on (1);
		} else {
			q->tag = get_new_priority_tag (mq, lpa);;
			q->lpa = lpa;
			q->lock = 0;
			q->ptr_req = (void*)req;
			list_add_tail (&q->list, &mq->qlh[qid]);	/* add to tail */
			mq->qic++;
			ret = 0;

			/*
			if (mq->qic > max_queue_items) {
				max_queue_items = mq->qic;
				h4h_msg ("max queue items: %llu", max_queue_items);
			}
			*/
		}
	}
	h4h_spin_unlock (&mq->lock);

	return ret;
}

uint8_t h4h_prior_queue_is_empty (
	h4h_prior_queue_t* mq, 
	uint64_t qid)
{
	struct list_head* pos = NULL;
	h4h_prior_queue_item_t* q = NULL;
	uint8_t ret = 1;

	h4h_spin_lock (&mq->lock);
	if (mq->qic > 0) {
		list_for_each (pos, &mq->qlh[qid]) {
			q = list_entry (pos, h4h_prior_queue_item_t, list);
			if (q && q->lock == 0)
				break;
			q = NULL;
			break; /* [CAUSION] it could incur a dead-lock problem */
		}
		if (q != NULL)
			ret = 0;
	}
	h4h_spin_unlock (&mq->lock);

	return ret;
}

void* h4h_prior_queue_dequeue (
	h4h_prior_queue_t* mq, 
	uint64_t qid,
	h4h_prior_queue_item_t** oq)
{
	struct list_head* pos = NULL;
	h4h_prior_queue_item_t* q = NULL;
	void* req = NULL;

	h4h_spin_lock (&mq->lock);
	if (mq->qic > 0) {
		list_for_each (pos, &mq->qlh[qid]) {
			if ((q = list_entry (pos, h4h_prior_queue_item_t, list))) {
				if (q->lock == 0) {
					uint64_t highest_tag = get_highest_priority_tag (mq, q->lpa);
					if (highest_tag == q->tag)
						break;
				}
			}
			q = NULL;
			break; /* [CAUSION] it could incur a dead-lock problem */
		}
		if (q != NULL) {
			q->lock = 1;	/* mark it use */
			req = q->ptr_req;
			*oq= q;
		}
	}
	h4h_spin_unlock (&mq->lock);

	return req;
}

uint8_t h4h_prior_queue_remove (
	h4h_prior_queue_t* mq, 
	h4h_prior_queue_item_t* q)
{
	h4h_spin_lock (&mq->lock);
	if (q) {
		remove_highest_priority_tag (mq, q->lpa);
		list_del (&q->list);
		h4h_free (q);
		mq->qic--;
		/*h4h_msg ("[QUEUE] # of items in queue = %llu", mq->qic);*/
	}
	h4h_spin_unlock (&mq->lock);

	return 0;
}

uint8_t h4h_prior_queue_move (
	h4h_prior_queue_t* mq, 
	uint64_t qid,
	h4h_prior_queue_item_t* q)
{
	h4h_spin_lock (&mq->lock);
	if (q) {
		list_del (&q->list); /* remove from the list */
		q->lock = 0;
		list_add (&q->list, &mq->qlh[qid]);	/* add to tail */
	}
	h4h_spin_unlock (&mq->lock);

	return 0;
}

uint8_t h4h_prior_queue_is_full (h4h_prior_queue_t* mq)
{
	uint8_t ret = 0;

	h4h_spin_lock (&mq->lock);
	if (mq->max_size != INFINITE_PRIOR_QUEUE) {
		if (mq->qic > mq->max_size) {
 			h4h_error ("oops!!!");
			h4h_bug_on (mq->qic > mq->max_size);
		}
		if (mq->qic == mq->max_size)
			ret = 1;
	}
	h4h_spin_unlock (&mq->lock);

	return ret;
}

uint8_t h4h_prior_queue_is_all_empty (h4h_prior_queue_t* mq)
{
	uint8_t ret = 0;

	if (mq == NULL)
		return 1;

	h4h_spin_lock (&mq->lock);
	if (mq->qic == 0)
		ret = 1;	/* q is empty */
	h4h_spin_unlock (&mq->lock);

	return ret;
}

uint64_t h4h_prior_queue_get_nr_items (h4h_prior_queue_t* mq)
{
	uint64_t nr_items = 0;

	h4h_spin_lock (&mq->lock);
	nr_items = mq->qic;
	h4h_spin_unlock (&mq->lock);

	return nr_items;
}
