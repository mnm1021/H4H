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

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>	/* strerr, errno */
#include <fcntl.h> /* O_RDWR */
#include <sys/mman.h> /* mmap */
#include <poll.h> /* poll */
#include <sys/ioctl.h> /* ioctl */

#include "h4h_drv.h"
#include "debug.h"
#include "umemory.h"
#include "params.h"
#include "utime.h"
#include "uthread.h"

#include "blkio_stub.h"
#include "blkio_proxy_ioctl.h"
#include "hlm_reqs_pool.h"

//#define ENABLE_DISPLAY

h4h_host_inf_t _blkio_stub_inf = {
	.ptr_private = NULL,
	.open = blkio_stub_open,
	.close = blkio_stub_close,
	.make_req = blkio_stub_make_req,
	.end_req = blkio_stub_end_req,
};

typedef struct {
	int fd;
	int stop;
	h4h_blkio_proxy_req_t* mmap_reqs;
	h4h_thread_t* host_stub_thread; /* polling the blockio proxy */
	atomic_t nr_host_reqs;
	h4h_sema_t host_lock;
	h4h_hlm_reqs_pool_t* hlm_reqs_pool;
} h4h_blkio_stub_private_t;


int __host_proxy_stub_thread (void* arg) 
{
	h4h_drv_info_t* bdi = (h4h_drv_info_t*)arg;
	h4h_device_params_t* np = (h4h_device_params_t*)H4H_GET_DEVICE_PARAMS (bdi);
	h4h_host_inf_t* host_inf = (h4h_host_inf_t*)H4H_GET_HOST_INF (bdi);
	h4h_blkio_stub_private_t* p = (h4h_blkio_stub_private_t*)H4H_HOST_PRIV (bdi);
	struct pollfd fds[1];
	int ret, i, j, sent;

	while (p->stop != 1) {
		h4h_thread_yield ();

		/* prepare arguments for poll */
		fds[0].fd = p->fd;
		fds[0].events = POLLIN;

		/* call poll () with 3 seconds timout */
		ret = poll (fds, 1, 3000);	/* p->ps is shared by kernel, but it is only updated by kernel when poll () is called */

		/* timeout: continue to check the device status */
		if (ret == 0)
			continue;

		/* error: poll () returns error for some reasones */
		if (ret < 0) 
			continue;

		/* success */
		if (ret > 0) {
			h4h_blkio_proxy_req_t* proxy_req = NULL;

			sent = 0;
			for (i = 0; i < H4H_PROXY_MAX_REQS; i++) {
				/* fetch the outstanding request from mmap */
				proxy_req = &p->mmap_reqs[i];
				if (proxy_req->id != i) {
					h4h_msg ("proxy_req->id: %llu i: %llu", proxy_req->id != i);
				}
				h4h_bug_on (proxy_req->id != i);

				/* are there any requests to send to the device? */
				if (proxy_req->stt == REQ_STT_KERN_SENT) {
					proxy_req->stt = REQ_STT_USER_PROG;
					/* setup blkio_req */
					for (j = 0; j < proxy_req->blkio_req.bi_bvec_cnt; j++)
						proxy_req->blkio_req.bi_bvec_ptr[j] = proxy_req->bi_bvec_ptr[j];
					/* send the request */
					host_inf->make_req (bdi, &proxy_req->blkio_req);
					sent++;
				}
			}

			/* how many outstanding requests were sent? */
			if (sent == 0) {
				h4h_warning ("hmm... this is an impossible case");
			}
		}
	}

	pthread_exit (0);

	return 0;
}

uint32_t blkio_stub_open (h4h_drv_info_t* bdi)
{
	h4h_blkio_stub_private_t* p = NULL;
	int mmap_size;
	int mapping_unit_size;

	/* create a private data for host_proxy */
	if ((p = h4h_malloc (sizeof (h4h_blkio_stub_private_t))) == NULL) {
		h4h_error ("h4h_malloc () failed");
		return 1;
	}
	p->fd = -1;
	p->stop = 0;
	p->mmap_reqs = NULL;
	atomic_set (&p->nr_host_reqs, 0);
	h4h_sema_init (&p->host_lock);
	bdi->ptr_host_inf->ptr_private = (void*)p;

	/* connect to blkio_proxy */
	if ((p->fd = open (H4H_BLOCKIO_PROXY_IOCTL_DEVNAME, O_RDWR)) < 0) {
		h4h_error ("open () failed (ret = %d)\n", p->fd);
		return 1;
	}

	/* create mmap_reqs */
	mmap_size = sizeof (h4h_blkio_proxy_req_t) * H4H_PROXY_MAX_REQS;
	if ((p->mmap_reqs = mmap (NULL,
			mmap_size,
			PROT_READ | PROT_WRITE, 
			MAP_SHARED, 
			p->fd, 0)) == NULL) {
		h4h_warning ("h4h_dm_proxy_mmap () failed");
		return 1;
	}

	/* create hlm_reqs pool */
	if (bdi->parm_dev.nr_subpages_per_page == 1)
		mapping_unit_size = bdi->parm_dev.page_main_size;
	else
		mapping_unit_size = KERNEL_PAGE_SIZE;

	if ((p->hlm_reqs_pool = h4h_hlm_reqs_pool_create (
			mapping_unit_size,	/* mapping unit */
			bdi->parm_dev.page_main_size	/* io unit */	
			)) == NULL) {
		h4h_warning ("h4h_hlm_reqs_pool_create () failed");
		return 1;
	}

	/* run a thread to poll the blockio proxy */
	if ((p->host_stub_thread = h4h_thread_create (
			__host_proxy_stub_thread, bdi, "__host_proxy_stub_thread")) == NULL) {
		h4h_warning ("h4h_thread_create failed");
		return 1;
	}
	h4h_thread_run (p->host_stub_thread);

	return 0;
}

void blkio_stub_close (h4h_drv_info_t* bdi)
{
	h4h_blkio_stub_private_t* p = H4H_HOST_PRIV (bdi); 

	/* stop the blkio_stub thread */
	p->stop = 1;
	h4h_thread_stop (p->host_stub_thread);

	/* wait until requests to finish */
	if (atomic_read (&p->nr_host_reqs) > 0) {
		h4h_thread_yield ();
	}

	if (p->hlm_reqs_pool) {
		h4h_hlm_reqs_pool_destroy (p->hlm_reqs_pool);
	}

	/* close the blkio_proxy */
	if (p->fd >= 0) {
		close (p->fd);
	}

	/* free stub */
	h4h_free (p);
}

static void __blkio_stub_finish (
	h4h_drv_info_t* bdi, 
	h4h_blkio_req_t* r)
{
	h4h_blkio_stub_private_t* p = H4H_HOST_PRIV(bdi);
	h4h_blkio_proxy_req_t* proxy_req = (h4h_blkio_proxy_req_t*)r;

	/* change the status of the request */
	proxy_req->stt = REQ_STT_USER_DONE;

	/* send a 'done' siganl to the proxy */
	ioctl (p->fd, H4H_BLOCKIO_PROXY_IOCTL_DONE, &proxy_req->id);
}

#ifdef ENABLE_DISPLAY
static void __blkio_display_req (
	h4h_drv_info_t* bdi, 
	h4h_hlm_req_t* hlm_req)
{
	h4h_ftl_inf_t* ftl = (h4h_ftl_inf_t*)H4H_GET_FTL_INF(bdi);
	uint64_t seg_no = 0;

	if (ftl->get_segno) {
		seg_no = ftl->get_segno (bdi, hlm_req->lpa);
	}

	switch (hlm_req->req_type) {
	case REQTYPE_TRIM:
		h4h_msg ("[%llu] TRIM\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);
		break;
	case REQTYPE_READ:
		h4h_msg ("[%llu] READ\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);
		break;
	case REQTYPE_WRITE:
		h4h_msg ("[%llu] WRITE\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);
		break;
	default:
		h4h_error ("invalid REQTYPE (%u)", hlm_req->req_type);
		break;
	}
}
#endif

void blkio_stub_make_req (h4h_drv_info_t* bdi, void* bio)
{
	h4h_blkio_stub_private_t* p = (h4h_blkio_stub_private_t*)H4H_HOST_PRIV(bdi);
	h4h_blkio_req_t* br = (h4h_blkio_req_t*)bio;
	h4h_hlm_req_t* hr = NULL;

	//h4h_msg ("make_req - [%llx] offset: %llu, size: %llu", br->bi_rw, br->bi_offset*512/4096, br->bi_size/8);

	/* get a free hlm_req from the hlm_reqs_pool */
	if ((hr = h4h_hlm_reqs_pool_get_item (p->hlm_reqs_pool)) == NULL) {
		h4h_error ("h4h_hlm_reqs_pool_get_item () failed");
		h4h_bug_on (1);
		return;
	}

	/* build hlm_req with bio */
	if (h4h_hlm_reqs_pool_build_req (p->hlm_reqs_pool, hr, br) != 0) {
		h4h_error ("h4h_hlm_reqs_pool_build_req () failed");
		h4h_bug_on (1);
		return;
	}

#ifdef ENABLE_DISPLAY
	__blkio_display_req (bdi, hr);
#endif

	/* if success, increase # of host reqs */
	atomic_inc (&p->nr_host_reqs);

	/* NOTE: it would be possible that 'hlm_req' becomes NULL 
	 * if 'bdi->ptr_hlm_inf->make_req' is success. */
	if (bdi->ptr_hlm_inf->make_req (bdi, hr) != 0) {
		/* oops! something wrong */
		h4h_error ("'bdi->ptr_hlm_inf->make_req' failed");

		/* cancel the request */
		__blkio_stub_finish (bdi, br);
		atomic_dec (&p->nr_host_reqs);
		h4h_hlm_reqs_pool_free_item (p->hlm_reqs_pool, hr);
	}
}

void blkio_stub_end_req (h4h_drv_info_t* bdi, h4h_hlm_req_t* req)
{
	h4h_blkio_stub_private_t* p = (h4h_blkio_stub_private_t*)H4H_HOST_PRIV(bdi);
	h4h_blkio_req_t* br = (h4h_blkio_req_t*)req->blkio_req;

	//h4h_msg ("end_req - [%llx] offset: %llu, size: %llu", br->bi_rw, br->bi_offset*512/4096, br->bi_size/8);

	/* finish the proxy request */
	__blkio_stub_finish (bdi, br);

	/* decreate # of reqs */
	atomic_dec (&p->nr_host_reqs);

	/* destroy hlm_req */
	h4h_hlm_reqs_pool_free_item (p->hlm_reqs_pool, req);
}
