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

#ifndef _H4H_PRIOR_QUEUE_MQ_H
#define _H4H_PRIOR_QUEUE_MQ_H

#include "../3rd/uthash.h"

enum H4H_PRIOR_QUEUE_SIZE {
	INFINITE_PRIOR_QUEUE = -1,
};

typedef struct {
	struct list_head list; /* list header */
	void* ptr_req;
	uint64_t lpa;
	uint64_t tag;
	uint8_t lock;
} h4h_prior_queue_item_t;

typedef struct {
	struct list_head list;	/* list header */
	uint64_t lpa;
	uint64_t cur_tag;
	uint64_t max_tag;
	UT_hash_handle hh;	/* hash header */
} h4h_prior_lpa_item_t;

typedef struct {
	uint64_t nr_queues;
	int64_t max_size;
	int64_t qic; /* queue item count */
	h4h_spinlock_t lock; /* queue lock */
	struct list_head* qlh; /* queue list header */
 	h4h_prior_lpa_item_t* hash_lpa;	/* lpa hash */
} h4h_prior_queue_t;

h4h_prior_queue_t* h4h_prior_queue_create (uint64_t nr_queues, int64_t size);
void h4h_prior_queue_destroy (h4h_prior_queue_t* mq);
uint8_t h4h_prior_queue_enqueue (h4h_prior_queue_t* mq, uint64_t qid, uint64_t lpa, void* req);
void* h4h_prior_queue_dequeue (h4h_prior_queue_t* mq, uint64_t qid, h4h_prior_queue_item_t** out_q);
uint8_t h4h_prior_queue_remove (h4h_prior_queue_t* mq, h4h_prior_queue_item_t* q);
uint8_t h4h_prior_queue_move (h4h_prior_queue_t* mq, uint64_t quid, h4h_prior_queue_item_t* q);
uint8_t h4h_prior_queue_is_full (h4h_prior_queue_t* mq);
uint8_t h4h_prior_queue_is_empty (h4h_prior_queue_t* mq, uint64_t qid);
uint8_t h4h_prior_queue_is_all_empty (h4h_prior_queue_t* mq);
uint64_t h4h_prior_queue_get_nr_items (h4h_prior_queue_t* mq);

#endif
