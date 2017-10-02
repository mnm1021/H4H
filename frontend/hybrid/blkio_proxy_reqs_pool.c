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

#include <linux/init.h>
#include <linux/module.h>

#include "h4h_drv.h"
#include "umemory.h"
#include "debug.h"

#include "blkio_proxy.h"
#include "blkio_proxy_ioctl.h"
#include "blkio_proxy_reqs_pool.h"


typedef struct {
	struct list_head list;
	h4h_blkio_proxy_req_t* mmap_req;
} pool_item_t;

h4h_proxy_reqs_pool_t* h4h_proxy_reqs_pool_create (
	int64_t nr_reqs, 
	h4h_blkio_proxy_req_t* reqs)
{
	h4h_proxy_reqs_pool_t* pool = NULL;
	int64_t i;

	/*h4h_track ();*/
	
	/* are input arguments correct? */
	if (nr_reqs <= 0 || reqs == NULL) {
		h4h_warning ("invalid parameters (nr_reqs = %lld, buff = %p)", nr_reqs, reqs);
		return NULL;
	}

	h4h_msg ("parameters (nr_reqs = %lld, buff = %p)", nr_reqs, reqs);

	/* initialize proxy_reqs pool */
	if ((pool = h4h_malloc (sizeof (h4h_proxy_reqs_pool_t))) == NULL) {
		h4h_error ("h4h_malloc () failed");
		return pool;
	}
	h4h_spin_lock_init (&pool->lock);
	INIT_LIST_HEAD (&pool->used_list);
	INIT_LIST_HEAD (&pool->free_list);
	pool->nr_reqs = nr_reqs;

	/*h4h_track ();*/

	/* add items to the free list */
	for (i = 0; i < nr_reqs; i++) {
		pool_item_t* item = NULL;
		/*h4h_track ();*/
		if ((item = (pool_item_t*)h4h_malloc 
				(sizeof (pool_item_t))) == NULL) {
			h4h_error ("h4h_malloc () failed");
			goto fail;
		}
		item->mmap_req = &reqs[i];
		item->mmap_req->id = i;
		item->mmap_req->stt = REQ_STT_FREE;
		list_add_tail (&item->list, &pool->free_list);
	}
	/*h4h_track ();*/

	/* ok.. */
	return pool;

fail:
	/*h4h_track ();*/

	if (pool) {
		h4h_spin_lock_destory (&pool->lock);
		h4h_free (pool);
		pool = NULL;
	}
	return NULL;
}

void h4h_proxy_reqs_pool_destroy (h4h_proxy_reqs_pool_t* pool)
{
	struct list_head* next = NULL;
	struct list_head* temp = NULL;
	pool_item_t* item = NULL;
	uint64_t count = 0;

	if (!pool) return;

	/* free & remove items from the used_list */
	list_for_each_safe (next, temp, &pool->used_list) {
		item = list_entry (next, pool_item_t, list);
		list_del (&item->list);
		h4h_free (item);
		count++;
	}

	/* free & remove items from the free_list */
	list_for_each_safe (next, temp, &pool->free_list) {
		item = list_entry (next, pool_item_t, list);
		list_del (&item->list);
		h4h_free (item);
		count++;
	}

	if (count != pool->nr_reqs) {
		h4h_warning ("oops! count != pool->nr_reqs (%lld != %lld)",
			count, pool->nr_reqs);
	}

	/* free other stuff */
	h4h_spin_lock_destory (&pool->lock);
	h4h_free (pool);
}

h4h_blkio_proxy_req_t* h4h_proxy_reqs_pool_alloc_item (
	h4h_proxy_reqs_pool_t* pool)
{
	struct list_head* pos = NULL;
	pool_item_t* item = NULL;

	/* see if there are free items in the free_list */
	list_for_each (pos, &pool->free_list) {
		item = list_entry (pos, pool_item_t, list);
		break;
	}

	if (item) {
		/* move it to the used_list */
		h4h_bug_on (item->mmap_req->stt != REQ_STT_FREE);
		list_del (&item->list);
		list_add_tail (&item->list, &pool->used_list);
	} else {
		/* oops! there is no free item */
		return NULL;
	}

	return item->mmap_req;
}

void h4h_proxy_reqs_pool_free_item (
	h4h_proxy_reqs_pool_t* pool,
	h4h_blkio_proxy_req_t* req)
{
	struct list_head* pos = NULL;
	pool_item_t* item = NULL;

	/*h4h_track ();*/

	/* see if there are free items in the free_list.
	 * NOTE: there are only few items in the lists, so it does not affect the
	 * overall performance.  But, if it is necessary, I will use HASH to
	 * improve its look-up speed */
	list_for_each (pos, &pool->used_list) {
		item = list_entry (pos, pool_item_t, list);
		if (item && item->mmap_req) {
			if (item->mmap_req->id == req->id) {
				/*h4h_track ();*/
				h4h_bug_on (item->mmap_req != req);
				h4h_bug_on (item->mmap_req->stt == REQ_STT_FREE);
				/* found it! */
				break;
			}
		}
		item = NULL;
	}

	if (item) {
		/*h4h_track ();*/
		/* move it to the used_list */
		item->mmap_req->stt = REQ_STT_FREE;
		list_del (&item->list);
		list_add_tail (&item->list, &pool->free_list);
	} else {
		/*h4h_track ();*/
		/* oops! there is no free item */
		h4h_warning ("oops! cannot find the request in the used_list (%u)", req->id);
	}
}
