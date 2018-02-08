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
#include "hlm_reqs_pool.h"
#include "utime.h"
#include "umemory.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"


/* interface for hlm_nobuf */
h4h_hlm_inf_t _hlm_nobuf_inf = {
	.ptr_private = NULL,
	.create = hlm_nobuf_create,
	.destroy = hlm_nobuf_destroy,
	.make_req = hlm_nobuf_make_req,
	.end_req = hlm_nobuf_end_req,
	/*.load = hlm_nobuf_load,*/
	/*.store = hlm_nobuf_store,*/
};

/* data structures for hlm_nobuf */
typedef struct {
	h4h_hlm_req_t tmp_hr;
} h4h_hlm_nobuf_private_t;


/* functions for hlm_nobuf */
uint32_t hlm_nobuf_create (h4h_drv_info_t* bdi)
{
	h4h_hlm_nobuf_private_t* p;

	/* create private */
	if ((p = (h4h_hlm_nobuf_private_t*)h4h_malloc
			(sizeof(h4h_hlm_nobuf_private_t))) == NULL) {
		h4h_error ("h4h_malloc failed");
		return 1;
	}

	/* keep the private structure */
	bdi->ptr_hlm_inf->ptr_private = (void*)p;

	return 0;
}

void hlm_nobuf_destroy (h4h_drv_info_t* bdi)
{
	h4h_hlm_nobuf_private_t* p = (h4h_hlm_nobuf_private_t*)H4H_HLM_PRIV(bdi);

	/* free priv */
	h4h_free (p);
}

uint32_t __hlm_nobuf_make_trim_req (h4h_drv_info_t* bdi, h4h_hlm_req_t* ptr_hlm_req)
{
	h4h_ftl_inf_t* ftl = (h4h_ftl_inf_t*)H4H_GET_FTL_INF(bdi);
	uint64_t i;

	for (i = 0; i < ptr_hlm_req->len; i++) {
		ftl->invalidate_lpa (bdi, ptr_hlm_req->lpa + i, 1);
	}

	return 0;
}

uint32_t __hlm_nobuf_make_rw_req (h4h_drv_info_t* bdi, h4h_hlm_req_t* hr)
{
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS(bdi);
	h4h_ftl_inf_t* ftl = H4H_GET_FTL_INF(bdi);
	h4h_llm_req_t* lr = NULL;
	uint64_t i = 0, j = 0, sp_ofs;

//	h4h_phyaddr_t start_ppa;
//	h4h_phyaddr_t* phyaddrs = NULL;
//	int32_t size, alloc_size, total_alloc_size;

//	/* allocate request by sequential address if write */
//	if (h4h_is_write (hr->req_type))
//	{
//		size = hr->nr_llm_reqs;
//		phyaddrs = h4h_malloc (sizeof(h4h_phyaddr_t) * size);
//
//		total_alloc_size = 0;
//		
//		while (total_alloc_size < size)
//		{
//			alloc_size = ftl->get_free_ppas (bdi, 0, size - total_alloc_size, &start_ppa);
//			if (alloc_size < 0)
//			{
//				h4h_error ("'ftl->get_free_ppas' failed");
//				h4h_free (phyaddrs);
//				goto fail;
//			}
//
//			total_alloc_size += alloc_size;
//			for (; i < total_alloc_size; ++i) /* i not initialized to continuously alloc */
//			{
//				h4h_memcpy (&phyaddrs[i], &start_ppa, sizeof(h4h_phyaddr_t));
//				start_ppa.page_no += 1;
//			}
//		}
//	}

	/* perform mapping with the FTL */
	h4h_hlm_for_each_llm_req (lr, hr, i) {
		/* (1) get the physical locations through the FTL */
		if (h4h_is_normal (lr->req_type)) {
			/* handling normal I/O operations */
			if (h4h_is_read (lr->req_type)) {
				if (ftl->get_ppa (bdi, lr->logaddr.lpa[0], &lr->phyaddr, &sp_ofs) != 0) {
					/* Note that there could be dummy reads (e.g., when the
					 * file-systems are initialized) */
					lr->req_type = REQTYPE_READ_DUMMY;
				} else {
					hlm_reqs_pool_relocate_kp (lr, sp_ofs);
				}
			} else if (h4h_is_write (lr->req_type)) {
				/*
				if (ftl->get_free_ppa (bdi, lr->logaddr.lpa[0], &lr->phyaddr) != 0) {
					h4h_error ("`ftl->get_free_ppa' failed");
					goto fail;
				}
				*/
//				h4h_memcpy (&lr->phyaddr, &phyaddrs[i], sizeof(h4h_phyaddr_t));

//				if (ftl->map_lpa_to_ppa (bdi, &lr->logaddr, &lr->phyaddr) != 0) {
//					h4h_error ("`ftl->map_lpa_to_ppa' failed");
//					goto fail;
//				}
				if (ftl->get_ppa (bdi, lr->logaddr.lpa[0], &lr->phyaddr, &sp_ofs) != 0) {
					h4h_error ("'ftl->get_ppa' failed: invalid write");
					goto fail;
				}
			} else {
				h4h_error ("oops! invalid type (%x)", lr->req_type);
				h4h_bug_on (1);
			}
		} else if (h4h_is_rmw (lr->req_type)) {
			h4h_phyaddr_t* phyaddr = &lr->phyaddr_src;

			/* finding the location of the previous data */ 
			if (ftl->get_ppa (bdi, lr->logaddr.lpa[0], phyaddr, &sp_ofs) != 0) {
				/* if it was not written before, change it to a write request */
				lr->req_type = REQTYPE_WRITE;
				phyaddr = &lr->phyaddr;
			} else {
				hlm_reqs_pool_relocate_kp (lr, sp_ofs);
				phyaddr = &lr->phyaddr_dst;
			}

			/* getting the location to which data will be written */
			if (ftl->get_free_ppa (bdi, lr->logaddr.lpa[0], phyaddr) != 0) {
				h4h_error ("`ftl->get_free_ppa' failed");
				goto fail;
			}
			if (ftl->map_lpa_to_ppa (bdi, &lr->logaddr, phyaddr) != 0) {
				h4h_error ("`ftl->map_lpa_to_ppa' failed");
				goto fail;
			}
		} else {
			h4h_error ("oops! invalid type (%x)", lr->req_type);
			h4h_bug_on (1);
		}

		/* (2) setup oob */
		for (j = 0; j < np->nr_subpages_per_page; j++) {
			((int64_t*)lr->foob.data)[j] = lr->logaddr.lpa[j];
		}
	}

	/* (3) send llm_req to llm */
	if (bdi->ptr_llm_inf->make_reqs == NULL) {
		/* send individual llm-reqs to llm */
		h4h_hlm_for_each_llm_req (lr, hr, i) {
			if (bdi->ptr_llm_inf->make_req (bdi, lr) != 0) {
				h4h_error ("oops! make_req () failed");
				h4h_bug_on (1);
			}
		}
	} else {
		/* send a bulk of llm-reqs to llm if make_reqs is supported */
		if (bdi->ptr_llm_inf->make_reqs (bdi, hr) != 0) {
			h4h_error ("oops! make_reqs () failed");
			h4h_bug_on (1);
		}
	}

	h4h_bug_on (hr->nr_llm_reqs != i);

//	if (phyaddrs != NULL)
//		h4h_free (phyaddrs);

	return 0;

fail:
	return 1;
}

/* TODO: it must be more general... */
void __hlm_nobuf_check_ondemand_gc (h4h_drv_info_t* bdi, h4h_hlm_req_t* hr)
{
	h4h_ftl_params* dp = H4H_GET_DRIVER_PARAMS (bdi);
	h4h_ftl_inf_t* ftl = (h4h_ftl_inf_t*)H4H_GET_FTL_INF(bdi);

	if (dp->mapping_type == MAPPING_POLICY_PAGE) {
		uint32_t loop;
		/* see if foreground GC is needed or not */
		for (loop = 0; loop < 10; loop++) {
			if (hr->req_type == REQTYPE_WRITE && 
				ftl->is_gc_needed != NULL && 
				ftl->is_gc_needed (bdi, 0)) {
				/* perform GC before sending requests */ 
				//h4h_msg ("[hlm_nobuf_make_req] trigger GC");
				ftl->do_gc (bdi, 0);
			} else
				break;
		}
	} else if (dp->mapping_type == MAPPING_POLICY_RSD ||
			   dp->mapping_type == MAPPING_POLICY_BLOCK) {
		/* perform mapping with the FTL */
		if (hr->req_type == REQTYPE_WRITE && ftl->is_gc_needed != NULL) {
			h4h_llm_req_t* lr = NULL;
			uint64_t i = 0;
			h4h_hlm_for_each_llm_req (lr, hr, i) {
				/* NOTE: segment-level ftl does not support fine-grain rmw */
				if (ftl->is_gc_needed (bdi, lr->logaddr.lpa[0])) {
					/* perform GC before sending requests */ 
					//h4h_msg ("[hlm_nobuf_make_req] trigger GC");
					ftl->do_gc (bdi, lr->logaddr.lpa[0]);
				}
			}
		}
	} else {
		/* do nothing */
	}
}

uint32_t hlm_nobuf_make_req (h4h_drv_info_t* bdi, h4h_hlm_req_t* hr)
{
	uint32_t ret;
	h4h_stopwatch_t sw;
	h4h_stopwatch_start (&sw);

	/* is req_type correct? */
	h4h_bug_on (!h4h_is_normal (hr->req_type));

#if 0
	/* trigger gc if necessary */
	if (dp->mapping_type != MAPPING_POLICY_DFTL) {
		h4h_ftl_inf_t* ftl = (h4h_ftl_inf_t*)H4H_GET_FTL_INF(bdi);
		/* see if foreground GC is needed or not */
		for (loop = 0; loop < 10; loop++) {
			if (hr->req_type == REQTYPE_WRITE && 
				ftl->is_gc_needed != NULL && 
				ftl->is_gc_needed (bdi, 0)) {
				/* perform GC before sending requests */ 
				h4h_msg ("[hlm_nobuf_make_req] trigger GC");
				ftl->do_gc (bdi, 0);
			} else
				break;
		}
	}
#endif

	/* perform i/o */
	if (h4h_is_trim (hr->req_type)) {
		if ((ret = __hlm_nobuf_make_trim_req (bdi, hr)) == 0) {
			/* call 'ptr_host_inf->end_req' directly */
			bdi->ptr_host_inf->end_req (bdi, hr);
			/* hr is now NULL */
		}
	} else {
		/* do we need to do garbage collection? */
		__hlm_nobuf_check_ondemand_gc (bdi, hr);

		ret = __hlm_nobuf_make_rw_req (bdi, hr);
	} 

	return ret;
}

void __hlm_nobuf_end_blkio_req (h4h_drv_info_t* bdi, h4h_llm_req_t* lr)
{
	h4h_hlm_req_t* hr = (h4h_hlm_req_t* )lr->ptr_hlm_req;

	/* increase # of reqs finished */
	atomic64_inc (&hr->nr_llm_reqs_done);
	lr->req_type |= REQTYPE_DONE;

	if (atomic64_read (&hr->nr_llm_reqs_done) == hr->nr_llm_reqs) {
		/* finish the host request */
		bdi->ptr_host_inf->end_req (bdi, hr);
	}
}

void __hlm_nobuf_end_gcio_req (h4h_drv_info_t* bdi, h4h_llm_req_t* lr)
{
	h4h_hlm_req_gc_t* hr_gc = (h4h_hlm_req_gc_t* )lr->ptr_hlm_req;

	atomic64_inc (&hr_gc->nr_llm_reqs_done);
	lr->req_type |= REQTYPE_DONE;

	if (atomic64_read (&hr_gc->nr_llm_reqs_done) == hr_gc->nr_llm_reqs) {
		h4h_sema_unlock (&hr_gc->done);
	}
}

void hlm_nobuf_end_req (h4h_drv_info_t* bdi, h4h_llm_req_t* lr)
{
	if (h4h_is_gc (lr->req_type)) {
		__hlm_nobuf_end_gcio_req (bdi, lr);
	} else {
		__hlm_nobuf_end_blkio_req (bdi, lr);
	}
}

