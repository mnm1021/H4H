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
#include <string.h>

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

#define NUM_THREADS	1

/*#define NUM_THREADS	20*/
/*#define NUM_THREADS	10*/
/*#define NUM_THREADS	2*/

#include "h4h_drv.h"
#include "uatomic64.h"

#define ALBSIZE 164	// 82MB	; 8960 for 35MB
#define FLBSIZE 70
//#define TOTAL_BLOCKS 16 * 2 * 64		// 16 channels 2 chips 64 blocks 
#define TOTAL_BLOCKS 8 * 8 * 38			// 8 channels 2 chips 32 blocks 
#define BLK_IO_SIZE 8 * 16 	// default 8 = 4KB

int num;

void blk_lbn_io (char type, int lbn) { 
	
	h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));
	int i = 0, j = 0;
	int offset = ALBSIZE * lbn * 8;
	uint8_t* bvec_ptr = (uint8_t*)h4h_malloc (4096);

	int total_alloc_size = 0, alloc_size;
	h4h_logaddr_t logaddr;
	h4h_phyaddr_t start_ppa;

	h4h_ftl_inf_t* ftl = H4H_GET_FTL_INF (_bdi);

	/* allocate request by sequential address if write */
	if (type == 'W')
	{
		total_alloc_size = 0;
		logaddr.lpa[0] = ALBSIZE * lbn;
		logaddr.ofs = 0;
		
		while (total_alloc_size < ALBSIZE)
		{
			alloc_size = ftl->get_free_ppas (_bdi, 0, ALBSIZE - total_alloc_size, &start_ppa);
			if (alloc_size < 0)
			{
				h4h_error ("'ftl->get_free_ppas' failed");
				return;
			}

			total_alloc_size += alloc_size;
			for (; i < total_alloc_size; ++i) /* i not initialized to continuously alloc */
			{
				if (ftl->map_lpa_to_ppa (_bdi, &logaddr, &start_ppa) != 0)
				{
					h4h_error ("'ftl->map_lpa_to_ppa' failed");
					return;
				}
				start_ppa.page_no += 1;
				logaddr.lpa[0] += 1;
			}
		}
	}

	for (i = 0; i < ALBSIZE / 16; i++) {
		/* build blkio req */
		if (type == 'W') blkio_req->bi_rw = REQTYPE_WRITE;
		else if ( type == 'I') blkio_req->bi_rw = REQTYPE_TRIM;
		blkio_req->bi_offset = offset;
		blkio_req->bi_size = BLK_IO_SIZE;
		blkio_req->bi_bvec_cnt = BLK_IO_SIZE / 8;
		for (j = 0; j < blkio_req->bi_bvec_cnt; j++) {
			blkio_req->bi_bvec_ptr[j] = bvec_ptr;
			blkio_req->bi_bvec_ptr[j][0] = 0x0A;
			blkio_req->bi_bvec_ptr[j][1] = 0x0B;
			blkio_req->bi_bvec_ptr[j][2] = 0x0C;
		}
		/* send req to ftl */
		_bdi->ptr_host_inf->make_req (_bdi, blkio_req);

		/* increase offset */
		offset += BLK_IO_SIZE;
	}
}


void host_thread_fn_LSM_trace (void* data) 
{	
	FILE* fp;
	char op;
	int num;
	int level;

	fp = fopen ("data.txt", "r");

	while (fscanf (fp, "%c %d", &op, &num) == 2)
	{
		if (op == 'W')
		{
			fscanf (fp, " %d\n", &level);
		}
		else
		{
			fscanf (fp, "\n");
		}

		if (op != 'R')
		{
			blk_lbn_io (op, num);
		}
	}
}

void host_thread_fn_write (void *data) //	for full page write 
{
	int i = 0, j = 0;
	int offset = 0; /* sector (512B) */
	int size = 8; /* 512B * 8 = 4 KB */
	h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));
	uint8_t* bvec_ptr = (uint8_t*)h4h_malloc (4096);

	for (i = 0; i < FLBSIZE * TOTAL_BLOCKS; i++) {

		/* build blkio req */
		blkio_req->bi_rw = REQTYPE_WRITE;
		blkio_req->bi_offset = offset;
		blkio_req->bi_size = size;
		blkio_req->bi_bvec_cnt = size / 8;
		for (j = 0; j < blkio_req->bi_bvec_cnt; j++) {
			blkio_req->bi_bvec_ptr[j] = bvec_ptr;
			blkio_req->bi_bvec_ptr[j][0] = 0x0A;
			blkio_req->bi_bvec_ptr[j][1] = 0x0B;
			blkio_req->bi_bvec_ptr[j][2] = 0x0C;
		}
		/* send req to ftl */
		_bdi->ptr_host_inf->make_req (_bdi, blkio_req);

		/* increase offset */
		offset += size;
	}

}

void host_thread_fn_trim (void *data) //	for full page trim 
{
	int i = 0, j = 0;
	int offset = 0; /* sector (512B) */
	int size = 8; /* 512B * 8 = 4 KB */
	h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));
	uint8_t* bvec_ptr = (uint8_t*)h4h_malloc (4096);

	for (i = 0; i < FLBSIZE * TOTAL_BLOCKS; i++) {

		/* build blkio req */
		blkio_req->bi_rw = REQTYPE_TRIM;
		blkio_req->bi_offset = offset;
		blkio_req->bi_size = size;
		blkio_req->bi_bvec_cnt = size / 8;
		for (j = 0; j < blkio_req->bi_bvec_cnt; j++) {
			blkio_req->bi_bvec_ptr[j] = bvec_ptr;
			blkio_req->bi_bvec_ptr[j][0] = 0x0A;
			blkio_req->bi_bvec_ptr[j][1] = 0x0B;
			blkio_req->bi_bvec_ptr[j][2] = 0x0C;
		}
		/* send req to ftl */
		_bdi->ptr_host_inf->make_req (_bdi, blkio_req);

		/* increase offset */
		offset += size;
	}

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

//		h4h_msg ("[main] start full page writes");
//		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
//			thread_args[loop_thread] = loop_thread;
//			pthread_create (&thread[loop_thread], NULL, 
//				(void*)&host_thread_fn_write, 
//				(void*)&thread_args[loop_thread]);
//		}
//
//		h4h_msg ("[main] wait for threads to end...");
//		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
//			pthread_join (thread[loop_thread], NULL);
//		}
//
//		h4h_msg ("[main] start full page trims");
//		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
//			thread_args[loop_thread] = loop_thread;
//			pthread_create (&thread[loop_thread], NULL, 
//				(void*)&host_thread_fn_trim, 
//				(void*)&thread_args[loop_thread]);
//		}
//
//		h4h_msg ("[main] wait for threads to end...");
//		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
//			pthread_join (thread[loop_thread], NULL);
//		}
		
		h4h_msg ("[main] start LSM evaluation");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			thread_args[loop_thread] = loop_thread;
			pthread_create (&thread[loop_thread], NULL, 
				(void*)&host_thread_fn_LSM_trace, 
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

