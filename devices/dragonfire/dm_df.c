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
#include <pthread.h>

#include "h4h_drv.h"
#include "debug.h"
#include "umemory.h"
#include "params.h"
#include "utime.h"

#include "dm_df.h"
#include "dev_params.h"

static void __dm_setup_device_params (h4h_device_params_t* params)
{
	*params = get_default_device_params ();
}

uint32_t dm_df_probe (h4h_drv_info_t* bdi, h4h_device_params_t* params)
{
	h4h_msg ("dm_df_prove is called");
	return 0;
}

uint32_t dm_df_open (h4h_drv_info_t* bdi)
{
	h4h_msg ("dm_df_open is called");
	return 0;
}

void dm_df_close (h4h_drv_info_t* bdi)
{
	h4h_msg ("dm_df_close is called");
}

uint32_t dm_df_make_req (h4h_drv_info_t* bdi, h4h_llm_req_t* ptr_llm_req)
{
	h4h_msg ("dm_df_make_req is called");
	dm_df_end_req (bdi, ptr_llm_req);
	return 0;
}

uint32_t dm_df_make_reqs (h4h_drv_info_t* bdi, h4h_hlm_req_t* hr)
{
#include "../../ftl/hlm_reqs_pool.h"

	uint32_t i;
	h4h_llm_req_t* lr = NULL;

	h4h_msg ("dm_df_make_reqs is called");

	/* TODO: do something for DF cards */

	h4h_hlm_for_each_llm_req (lr, hr, i) {
		h4h_msg ("%llu", i);
		dm_df_end_req (bdi, lr);
	}

	return 0;
}

void dm_df_end_req (h4h_drv_info_t* bdi, h4h_llm_req_t* ptr_llm_req)
{
	h4h_msg ("dm_df_end_req is called");
	bdi->ptr_dm_inf->end_req (bdi, ptr_llm_req);
}

/* for snapshot */
uint32_t dm_df_load (h4h_drv_info_t* bdi, const char* fn)
{	
	h4h_msg ("loading a DRAM snapshot...");
	return 0;
}

uint32_t dm_df_store (h4h_drv_info_t* bdi, const char* fn)
{
	h4h_msg ("storing a DRAM snapshot...");
	return 0;
}
