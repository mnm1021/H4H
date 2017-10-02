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
#include <errno.h>	/* strerr, errno */
#include <fcntl.h> /* O_RDWR */
#include <unistd.h> /* close */
#include <poll.h> /* poll */
#include <sys/mman.h> /* mmap */

#include "h4h_drv.h"
#include "debug.h"
#include "umemory.h"
#include "userio.h"
#include "params.h"

#include "dev_proxy.h"

#include "utime.h"
#include "uthread.h"

#include "dm_df.h"


/* interface for dm */
h4h_dm_inf_t _h4h_dm_inf = {
	.ptr_private = NULL,
	.probe = dm_proxy_probe,
	.open = dm_proxy_open,
	.close = dm_proxy_close,
	.make_req = dm_proxy_make_req,
	.make_reqs = dm_proxy_make_reqs,
	.end_req = dm_proxy_end_req,
	.load = NULL,
	.store = NULL,
};

typedef struct {
	/* nothing now */
	int test;
} h4h_dm_proxy_t;


uint32_t dm_proxy_probe (
	h4h_drv_info_t* bdi, 
	h4h_device_params_t* params)
{
	h4h_dm_proxy_t* p = (h4h_dm_proxy_t*)H4H_DM_PRIV(bdi);
	int ret;

	h4h_msg ("dm_proxy_probe is called");

	ret = dm_df_probe (bdi, params);

	h4h_msg ("--------------------------------");
	h4h_msg ("probe (): ret = %d", ret);
	h4h_msg ("nr_channels: %u", (uint32_t)params->nr_channels);
	h4h_msg ("nr_chips_per_channel: %u", (uint32_t)params->nr_chips_per_channel);
	h4h_msg ("nr_blocks_per_chip: %u", (uint32_t)params->nr_blocks_per_chip);
	h4h_msg ("nr_pages_per_block: %u", (uint32_t)params->nr_pages_per_block);
	h4h_msg ("page_main_size: %u", (uint32_t)params->page_main_size);
	h4h_msg ("page_oob_size: %u", (uint32_t)params->page_oob_size);
	h4h_msg ("device_type: %u", (uint32_t)params->device_type);
	h4h_msg ("");

	return ret;
}

uint32_t dm_proxy_open (h4h_drv_info_t* bdi)
{
	h4h_msg ("dm_proxy_open is called");
	return dm_df_open (bdi);
}

void dm_proxy_close (h4h_drv_info_t* bdi)
{
	h4h_dm_proxy_t* p = (h4h_dm_proxy_t*)H4H_DM_PRIV(bdi);

	h4h_msg ("dm_proxy_close is called");
	return dm_df_close (bdi);
}

uint32_t dm_proxy_make_req (h4h_drv_info_t* bdi, h4h_llm_req_t* r)
{
	h4h_dm_proxy_t* p = (h4h_dm_proxy_t*)H4H_DM_PRIV(bdi);

	if (h4h_is_read (r->req_type)) {
		h4h_msg ("dm_proxy_make_req: \t[R] logical:%llu <= physical:%llu %llu %llu %llu",
			r->logaddr.lpa[0], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no, 
			r->phyaddr.page_no);
	} else if (h4h_is_write (r->req_type)) {
		h4h_msg ("dm_proxy_make_req: \t[W] logical:%llu => physical:%llu %llu %llu %llu",
			r->logaddr.lpa[0], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no, 
			r->phyaddr.page_no);
	} else {
		h4h_msg ("invalid req_type");
	}

	return dm_df_make_req (bdi, r);
}

uint32_t dm_proxy_make_reqs (h4h_drv_info_t* bdi, h4h_hlm_req_t* r)
{
	h4h_msg ("dm_proxy_make_reqs ()");
	return dm_df_make_reqs (bdi, r);
}

void dm_proxy_end_req (h4h_drv_info_t* bdi, h4h_llm_req_t* r)
{
	h4h_dm_proxy_t* p = (h4h_dm_proxy_t*)H4H_DM_PRIV(bdi);

	h4h_msg ("dm_proxy_end_req is called");
	bdi->ptr_llm_inf->end_req (bdi, r);
}


/* functions exported to clients */
int h4h_dm_init (h4h_drv_info_t* bdi)
{
	h4h_dm_proxy_t* p = NULL;

	if (_h4h_dm_inf.ptr_private != NULL) {
		h4h_warning ("_h4h_dm_inf is already open");
		return 1;
	}

	/* create and initialize private variables */
	if ((p = (h4h_dm_proxy_t*)h4h_zmalloc 
			(sizeof (h4h_dm_proxy_t))) == NULL) {
		h4h_error ("h4h_malloc failed");
		return 1;
	}

	_h4h_dm_inf.ptr_private = (void*)p;

	h4h_msg ("%p", p);

	return 0;
}

h4h_dm_inf_t* h4h_dm_get_inf (h4h_drv_info_t* bdi)
{
	return &_h4h_dm_inf;
}

void h4h_dm_exit (h4h_drv_info_t* bdi) 
{
	h4h_dm_proxy_t* p = (h4h_dm_proxy_t*)H4H_DM_PRIV(bdi);

	if (p == NULL) {
		h4h_warning ("_h4h_dm_inf is already closed");
		return;
	}

	h4h_free (p);
}

