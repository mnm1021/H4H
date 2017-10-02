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

#ifndef _H4H_QUEUE_MQ_H
#define _H4H_QUEUE_MQ_H

enum H4H_QUEUE_SIZE {
	INFINITE_QUEUE = -1,
};

typedef struct {
	void* ptr_req;
	struct list_head list;
} h4h_queue_item_t;

typedef struct {
	uint64_t nr_queues;
	int64_t max_size;
	int64_t qic;			/* queue item count */
	h4h_spinlock_t lock;	/* queue lock */

	struct list_head* qlh;	/* queue list header */
} h4h_queue_t;

h4h_queue_t* h4h_queue_create (uint64_t nr_queues, int64_t size);
void h4h_queue_destroy (h4h_queue_t* mq);
uint8_t h4h_queue_enqueue (h4h_queue_t* mq, uint64_t qid, void* req);
uint8_t h4h_queue_enqueue_top (h4h_queue_t* mq, uint64_t qid, void* req);
void* h4h_queue_dequeue (h4h_queue_t* mq, uint64_t qid);
uint8_t h4h_queue_is_full (h4h_queue_t* mq);
uint8_t h4h_queue_is_empty (h4h_queue_t* mq, uint64_t qid);
uint8_t h4h_queue_is_all_empty (h4h_queue_t* mq);
uint64_t h4h_queue_get_nr_items (h4h_queue_t* mq);

#endif
