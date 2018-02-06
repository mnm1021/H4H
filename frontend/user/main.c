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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/slab.h>

#elif defined(USER_MODE)
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#endif

#include "h4h_drv.h"
#include "umemory.h"
#include "params.h"
#include "ftl_params.h"
#include "debug.h"
#include "userio.h"
#include "ufile.h"

#include "llm_noq.h"
#include "llm_mq.h"
#include "hlm_nobuf.h"
#include "hlm_buf.h"
#include "hlm_dftl.h"
#include "hlm_rsd.h"
#include "devices.h"
#include "pmu.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "algo/dftl.h"

/* main data structure */
h4h_drv_info_t* _bdi = NULL;

#define NUM_THREADS	20
/*#define NUM_THREADS	20*/
/*#define NUM_THREADS	10*/
/*#define NUM_THREADS	1*/

#include "h4h_drv.h"
#include "uatomic64.h"

void host_thread_fn_write (void *data) 
{
	int i = 0, j = 0;
	int offset = 0; /* sector (512B) */
	int size = 8 * 82; /* 512B * 8 * 32 = 128 KB */

	for (i = 0; i < 155; i++) {
		h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));

		/* build blkio req */
		blkio_req->bi_rw = REQTYPE_WRITE;
		blkio_req->bi_offset = offset;
		blkio_req->bi_size = size;
		blkio_req->bi_bvec_cnt = size / 8;
		for (j = 0; j < blkio_req->bi_bvec_cnt; j++) {
			blkio_req->bi_bvec_ptr[j] = (uint8_t*)h4h_malloc (4096);
			blkio_req->bi_bvec_ptr[j][0] = 0x0A;
			blkio_req->bi_bvec_ptr[j][1] = 0x0B;
			blkio_req->bi_bvec_ptr[j][2] = 0x0C;
		}

		/* send req to ftl */
		_bdi->ptr_host_inf->make_req (_bdi, blkio_req);

		/* increase offset */
		offset += size;
	}

	pthread_exit (0);
}

void host_thread_fn_read (void *data) 
{
	int i = 0, j = 0;
	int offset = 0; /* sector (512B) */
	int size = 8 * 82; /* 512B * 8 * 32 = 128 KB */

	for (i = 0; i < 155; i++) {
		h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));

		/* build blkio req */
		blkio_req->bi_rw = REQTYPE_READ;
		blkio_req->bi_offset = offset;
		blkio_req->bi_size = size;
		blkio_req->bi_bvec_cnt = size / 8;
		for (j = 0; j < blkio_req->bi_bvec_cnt; j++) {
			blkio_req->bi_bvec_ptr[j] = (uint8_t*)h4h_malloc (4096);
			if (blkio_req->bi_bvec_ptr[j] == NULL) {
				h4h_msg ("h4h_malloc () failed");
				exit (-1);
			}
			//h4h_msg ("[main] %d %p", j, blkio_req->bi_bvec_ptr[j]);
		}

		/* send req to ftl */
		/*h4h_msg ("[main] lpa: %llu", blkio_req->bi_offset);*/
		_bdi->ptr_host_inf->make_req (_bdi, blkio_req);

		/* increase offset */
		offset += size;
	}

	pthread_exit (0);
}

int main (int argc, char** argv)
{
	int loop_thread;

	pthread_t thread[NUM_THREADS];
	int thread_args[NUM_THREADS];

	h4h_msg ("[main] run ftlib... (%d)", sizeof (h4h_llm_req_t));

	h4h_msg ("[user-main] initialize h4h_drv");
	if ((_bdi = h4h_drv_create ()) == NULL) {
		h4h_error ("[kmain] h4h_drv_create () failed");
		return -1;
	}

	if (h4h_dm_init (_bdi) != 0) {
		h4h_error ("[kmain] h4h_dm_init () failed");
		return -1;
	}

	h4h_drv_setup (_bdi, &_userio_inf, h4h_dm_get_inf (_bdi));
	h4h_drv_run (_bdi);

	do {
		h4h_msg ("[main] start writes");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			thread_args[loop_thread] = loop_thread;
			pthread_create (&thread[loop_thread], NULL, 
				(void*)&host_thread_fn_write, 
				(void*)&thread_args[loop_thread]);
		}

		h4h_msg ("[main] wait for threads to end...");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			pthread_join (thread[loop_thread], NULL);
		}

		h4h_msg ("[main] start reads");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			thread_args[loop_thread] = loop_thread;
			pthread_create (&thread[loop_thread], NULL, 
				(void*)&host_thread_fn_read, 
				(void*)&thread_args[loop_thread]);
		}

		h4h_msg ("[main] wait for threads to end...");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			pthread_join (thread[loop_thread], NULL);
		}

	} while (0);

	h4h_msg ("[main] destroy h4h_drv");
	h4h_drv_close (_bdi);
	h4h_dm_exit (_bdi);
	h4h_drv_destroy (_bdi);

	h4h_msg ("[main] done");

	return 0;
}

