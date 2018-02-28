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

#define ALBSIZE 20992	// 82MB	; 8960 for 35MB
#define FLBSIZE 8960
//#define TOTAL_BLOCKS 16 * 2 * 64		// 16 channels 2 chips 64 blocks 
#define TOTAL_BLOCKS 8 * 2 * 32			// 8 channels 2 chips 32 blocks 
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

	for (i = 0; i < ALBSIZE/16; i++) {
		/* build blkio req */
		if ( type == 'W') blkio_req->bi_rw = REQTYPE_WRITE;
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
	blk_lbn_io ( 'W', 0 );
	blk_lbn_io ( 'I', 0 );
	blk_lbn_io ( 'W', 1 );
	blk_lbn_io ( 'W', 2 );
	blk_lbn_io ( 'I', 1 );
	blk_lbn_io ( 'I', 2 );
	blk_lbn_io ( 'W', 3 );
	blk_lbn_io ( 'W', 4 );
	blk_lbn_io ( 'W', 5 );
	blk_lbn_io ( 'I', 3 );
	blk_lbn_io ( 'I', 4 );
	blk_lbn_io ( 'I', 5 );
	blk_lbn_io ( 'W', 6 );
	blk_lbn_io ( 'W', 7 );
	blk_lbn_io ( 'W', 8 );
	blk_lbn_io ( 'W', 9 );
	blk_lbn_io ( 'I', 6 );
	blk_lbn_io ( 'I', 7 );
	blk_lbn_io ( 'I', 8 );
	blk_lbn_io ( 'I', 9 );
	blk_lbn_io ( 'W', 10 );
	blk_lbn_io ( 'W', 11 );
	blk_lbn_io ( 'W', 12 );
	blk_lbn_io ( 'W', 13 );
	blk_lbn_io ( 'W', 14 );
	blk_lbn_io ( 'I', 10 );
	blk_lbn_io ( 'I', 11 );
	blk_lbn_io ( 'I', 12 );
	blk_lbn_io ( 'I', 13 );
	blk_lbn_io ( 'I', 14 );
	blk_lbn_io ( 'W', 15 );
	blk_lbn_io ( 'W', 16 );
	blk_lbn_io ( 'W', 17 );
	blk_lbn_io ( 'W', 18 );
	blk_lbn_io ( 'W', 19 );
	blk_lbn_io ( 'W', 20 );
	blk_lbn_io ( 'I', 15 );
	blk_lbn_io ( 'I', 16 );
	blk_lbn_io ( 'I', 17 );
	blk_lbn_io ( 'I', 18 );
	blk_lbn_io ( 'I', 19 );
	blk_lbn_io ( 'I', 20 );
	blk_lbn_io ( 'W', 21 );
	blk_lbn_io ( 'W', 22 );
	blk_lbn_io ( 'W', 23 );
	blk_lbn_io ( 'W', 24 );
	blk_lbn_io ( 'W', 25 );
	blk_lbn_io ( 'W', 26 );
	blk_lbn_io ( 'W', 27 );
	blk_lbn_io ( 'I', 21 );
	blk_lbn_io ( 'I', 22 );
	blk_lbn_io ( 'I', 23 );
	blk_lbn_io ( 'I', 24 );
	blk_lbn_io ( 'I', 25 );
	blk_lbn_io ( 'I', 26 );
	blk_lbn_io ( 'I', 27 );
	blk_lbn_io ( 'W', 28 );
	blk_lbn_io ( 'W', 29 );
	blk_lbn_io ( 'W', 30 );
	blk_lbn_io ( 'W', 31 );
	blk_lbn_io ( 'W', 32 );
	blk_lbn_io ( 'W', 33 );
	blk_lbn_io ( 'W', 34 );
	blk_lbn_io ( 'W', 35 );
	blk_lbn_io ( 'I', 28 );
	blk_lbn_io ( 'I', 29 );
	blk_lbn_io ( 'I', 30 );
	blk_lbn_io ( 'I', 31 );
	blk_lbn_io ( 'I', 32 );
	blk_lbn_io ( 'I', 33 );
	blk_lbn_io ( 'I', 34 );
	blk_lbn_io ( 'I', 35 );
	blk_lbn_io ( 'W', 36 );
	blk_lbn_io ( 'W', 37 );
	blk_lbn_io ( 'W', 38 );
	blk_lbn_io ( 'W', 39 );
	blk_lbn_io ( 'W', 40 );
	blk_lbn_io ( 'W', 41 );
	blk_lbn_io ( 'W', 42 );
	blk_lbn_io ( 'W', 43 );
	blk_lbn_io ( 'W', 44 );
	blk_lbn_io ( 'W', 45 );
	blk_lbn_io ( 'I', 45 );
	blk_lbn_io ( 'W', 46 );
	blk_lbn_io ( 'W', 47 );
	blk_lbn_io ( 'I', 46 );
	blk_lbn_io ( 'I', 47 );
	blk_lbn_io ( 'W', 48 );
	blk_lbn_io ( 'W', 49 );
	blk_lbn_io ( 'W', 50 );
	blk_lbn_io ( 'I', 48 );
	blk_lbn_io ( 'I', 49 );
	blk_lbn_io ( 'I', 50 );
	blk_lbn_io ( 'W', 51 );
	blk_lbn_io ( 'W', 52 );
	blk_lbn_io ( 'W', 53 );
	blk_lbn_io ( 'W', 54 );
	blk_lbn_io ( 'I', 51 );
	blk_lbn_io ( 'I', 52 );
	blk_lbn_io ( 'I', 53 );
	blk_lbn_io ( 'I', 54 );
	blk_lbn_io ( 'W', 55 );
	blk_lbn_io ( 'W', 56 );
	blk_lbn_io ( 'W', 57 );
	blk_lbn_io ( 'W', 58 );
	blk_lbn_io ( 'W', 59 );
	blk_lbn_io ( 'I', 55 );
	blk_lbn_io ( 'I', 56 );
	blk_lbn_io ( 'I', 57 );
	blk_lbn_io ( 'I', 58 );
	blk_lbn_io ( 'I', 59 );
	blk_lbn_io ( 'W', 60 );
	blk_lbn_io ( 'W', 61 );
	blk_lbn_io ( 'W', 62 );
	blk_lbn_io ( 'W', 63 );
	blk_lbn_io ( 'W', 64 );
	blk_lbn_io ( 'W', 65 );
	blk_lbn_io ( 'I', 60 );
	blk_lbn_io ( 'I', 61 );
	blk_lbn_io ( 'I', 62 );
	blk_lbn_io ( 'I', 63 );
	blk_lbn_io ( 'I', 64 );
	blk_lbn_io ( 'I', 65 );
	blk_lbn_io ( 'W', 66 );
	blk_lbn_io ( 'W', 67 );
	blk_lbn_io ( 'W', 68 );
	blk_lbn_io ( 'W', 69 );
	blk_lbn_io ( 'W', 70 );
	blk_lbn_io ( 'W', 71 );
	blk_lbn_io ( 'W', 72 );
	blk_lbn_io ( 'I', 66 );
	blk_lbn_io ( 'I', 67 );
	blk_lbn_io ( 'I', 68 );
	blk_lbn_io ( 'I', 69 );
	blk_lbn_io ( 'I', 70 );
	blk_lbn_io ( 'I', 71 );
	blk_lbn_io ( 'I', 72 );
	blk_lbn_io ( 'W', 73 );
	blk_lbn_io ( 'W', 74 );
	blk_lbn_io ( 'W', 75 );
	blk_lbn_io ( 'W', 76 );
	blk_lbn_io ( 'W', 77 );
	blk_lbn_io ( 'W', 78 );
	blk_lbn_io ( 'W', 79 );
	blk_lbn_io ( 'W', 80 );
	blk_lbn_io ( 'I', 73 );
	blk_lbn_io ( 'I', 74 );
	blk_lbn_io ( 'I', 75 );
	blk_lbn_io ( 'I', 76 );
	blk_lbn_io ( 'I', 77 );
	blk_lbn_io ( 'I', 78 );
	blk_lbn_io ( 'I', 79 );
	blk_lbn_io ( 'I', 80 );
	blk_lbn_io ( 'W', 81 );
	blk_lbn_io ( 'W', 82 );
	blk_lbn_io ( 'W', 83 );
	blk_lbn_io ( 'W', 84 );
	blk_lbn_io ( 'W', 85 );
	blk_lbn_io ( 'W', 86 );
	blk_lbn_io ( 'W', 87 );
	blk_lbn_io ( 'W', 88 );
	blk_lbn_io ( 'W', 89 );
	blk_lbn_io ( 'I', 81 );
	blk_lbn_io ( 'I', 36 );
	blk_lbn_io ( 'I', 37 );
	blk_lbn_io ( 'W', 90 );
	blk_lbn_io ( 'I', 82 );
	blk_lbn_io ( 'W', 91 );
	blk_lbn_io ( 'I', 83 );
	blk_lbn_io ( 'I', 38 );
	blk_lbn_io ( 'I', 39 );
	blk_lbn_io ( 'W', 92 );
	blk_lbn_io ( 'W', 93 );
	blk_lbn_io ( 'W', 94 );
	blk_lbn_io ( 'I', 84 );
	blk_lbn_io ( 'W', 95 );
	blk_lbn_io ( 'I', 85 );
	blk_lbn_io ( 'I', 40 );
	blk_lbn_io ( 'W', 96 );
	blk_lbn_io ( 'W', 97 );
	blk_lbn_io ( 'I', 86 );
	blk_lbn_io ( 'I', 41 );
	blk_lbn_io ( 'W', 98 );
	blk_lbn_io ( 'W', 99 );
	blk_lbn_io ( 'I', 87 );
	blk_lbn_io ( 'I', 42 );
	blk_lbn_io ( 'W', 100 );
	blk_lbn_io ( 'W', 101 );
	blk_lbn_io ( 'I', 88 );
	blk_lbn_io ( 'I', 43 );
	blk_lbn_io ( 'W', 102 );
	blk_lbn_io ( 'W', 103 );
	blk_lbn_io ( 'I', 89 );
	blk_lbn_io ( 'I', 44 );
	blk_lbn_io ( 'W', 104 );
	blk_lbn_io ( 'W', 105 );
	blk_lbn_io ( 'W', 106 );
	blk_lbn_io ( 'W', 107 );
	blk_lbn_io ( 'W', 108 );
	blk_lbn_io ( 'I', 108 );
	blk_lbn_io ( 'W', 109 );
	blk_lbn_io ( 'W', 110 );
	blk_lbn_io ( 'I', 109 );
	blk_lbn_io ( 'I', 110 );
	blk_lbn_io ( 'W', 111 );
	blk_lbn_io ( 'W', 112 );
	blk_lbn_io ( 'W', 113 );
	blk_lbn_io ( 'I', 111 );
	blk_lbn_io ( 'I', 112 );
	blk_lbn_io ( 'I', 113 );
	blk_lbn_io ( 'W', 114 );
	blk_lbn_io ( 'W', 115 );
	blk_lbn_io ( 'W', 116 );
	blk_lbn_io ( 'W', 117 );
	blk_lbn_io ( 'I', 114 );
	blk_lbn_io ( 'I', 115 );
	blk_lbn_io ( 'I', 116 );
	blk_lbn_io ( 'I', 117 );
	blk_lbn_io ( 'W', 118 );
	blk_lbn_io ( 'W', 119 );
	blk_lbn_io ( 'W', 120 );
	blk_lbn_io ( 'W', 121 );
	blk_lbn_io ( 'W', 122 );
	blk_lbn_io ( 'I', 118 );
	blk_lbn_io ( 'I', 119 );
	blk_lbn_io ( 'I', 120 );
	blk_lbn_io ( 'I', 121 );
	blk_lbn_io ( 'I', 122 );
	blk_lbn_io ( 'W', 123 );
	blk_lbn_io ( 'W', 124 );
	blk_lbn_io ( 'W', 125 );
	blk_lbn_io ( 'W', 126 );
	blk_lbn_io ( 'W', 127 );
	blk_lbn_io ( 'W', 128 );
	blk_lbn_io ( 'I', 123 );
	blk_lbn_io ( 'I', 124 );
	blk_lbn_io ( 'I', 125 );
	blk_lbn_io ( 'I', 126 );
	blk_lbn_io ( 'I', 127 );
	blk_lbn_io ( 'I', 128 );
	blk_lbn_io ( 'W', 129 );
	blk_lbn_io ( 'W', 130 );
	blk_lbn_io ( 'W', 131 );
	blk_lbn_io ( 'W', 132 );
	blk_lbn_io ( 'W', 133 );
	blk_lbn_io ( 'W', 134 );
	blk_lbn_io ( 'W', 135 );
	blk_lbn_io ( 'I', 129 );
	blk_lbn_io ( 'I', 130 );
	blk_lbn_io ( 'I', 131 );
	blk_lbn_io ( 'I', 132 );
	blk_lbn_io ( 'I', 133 );
	blk_lbn_io ( 'I', 134 );
	blk_lbn_io ( 'I', 135 );
	blk_lbn_io ( 'W', 136 );
	blk_lbn_io ( 'W', 137 );
	blk_lbn_io ( 'W', 138 );
	blk_lbn_io ( 'W', 139 );
	blk_lbn_io ( 'W', 140 );
	blk_lbn_io ( 'W', 141 );
	blk_lbn_io ( 'W', 142 );
	blk_lbn_io ( 'W', 143 );
	blk_lbn_io ( 'I', 136 );
	blk_lbn_io ( 'I', 137 );
	blk_lbn_io ( 'I', 138 );
	blk_lbn_io ( 'I', 139 );
	blk_lbn_io ( 'I', 140 );
	blk_lbn_io ( 'I', 141 );
	blk_lbn_io ( 'I', 142 );
	blk_lbn_io ( 'I', 143 );
	blk_lbn_io ( 'W', 144 );
	blk_lbn_io ( 'W', 145 );
	blk_lbn_io ( 'W', 146 );
	blk_lbn_io ( 'W', 147 );
	blk_lbn_io ( 'W', 148 );
	blk_lbn_io ( 'W', 149 );
	blk_lbn_io ( 'W', 150 );
	blk_lbn_io ( 'W', 151 );
	blk_lbn_io ( 'W', 152 );
	blk_lbn_io ( 'I', 144 );
	blk_lbn_io ( 'I', 90 );
	blk_lbn_io ( 'I', 91 );
	blk_lbn_io ( 'I', 92 );
	blk_lbn_io ( 'W', 153 );
	blk_lbn_io ( 'W', 154 );
	blk_lbn_io ( 'I', 145 );
	blk_lbn_io ( 'I', 93 );
	blk_lbn_io ( 'I', 94 );
	blk_lbn_io ( 'W', 155 );
	blk_lbn_io ( 'W', 156 );
	blk_lbn_io ( 'W', 157 );
	blk_lbn_io ( 'I', 146 );
	blk_lbn_io ( 'I', 95 );
	blk_lbn_io ( 'I', 96 );
	blk_lbn_io ( 'W', 158 );
	blk_lbn_io ( 'W', 159 );
	blk_lbn_io ( 'W', 160 );
	blk_lbn_io ( 'I', 147 );
	blk_lbn_io ( 'I', 97 );
	blk_lbn_io ( 'I', 98 );
	blk_lbn_io ( 'W', 161 );
	blk_lbn_io ( 'W', 162 );
	blk_lbn_io ( 'W', 163 );
	blk_lbn_io ( 'I', 148 );
	blk_lbn_io ( 'I', 99 );
	blk_lbn_io ( 'I', 100 );
	blk_lbn_io ( 'W', 164 );
	blk_lbn_io ( 'W', 165 );
	blk_lbn_io ( 'W', 166 );
	blk_lbn_io ( 'I', 149 );
	blk_lbn_io ( 'I', 101 );
	blk_lbn_io ( 'I', 102 );
	blk_lbn_io ( 'W', 167 );
	blk_lbn_io ( 'W', 168 );
	blk_lbn_io ( 'W', 169 );
	blk_lbn_io ( 'I', 150 );
	blk_lbn_io ( 'I', 103 );
	blk_lbn_io ( 'I', 104 );
	blk_lbn_io ( 'W', 170 );
	blk_lbn_io ( 'W', 171 );
	blk_lbn_io ( 'W', 172 );
	blk_lbn_io ( 'I', 151 );
	blk_lbn_io ( 'I', 105 );
	blk_lbn_io ( 'I', 106 );
	blk_lbn_io ( 'W', 173 );
	blk_lbn_io ( 'W', 174 );
	blk_lbn_io ( 'W', 175 );
	blk_lbn_io ( 'I', 152 );
	blk_lbn_io ( 'I', 107 );
	blk_lbn_io ( 'W', 176 );
	blk_lbn_io ( 'W', 177 );
	blk_lbn_io ( 'W', 178 );
	blk_lbn_io ( 'W', 179 );
	blk_lbn_io ( 'W', 180 );
	blk_lbn_io ( 'I', 180 );
	blk_lbn_io ( 'W', 181 );
	blk_lbn_io ( 'W', 182 );
	blk_lbn_io ( 'I', 181 );
	blk_lbn_io ( 'I', 182 );
	blk_lbn_io ( 'W', 183 );
	blk_lbn_io ( 'W', 184 );
	blk_lbn_io ( 'W', 185 );
	blk_lbn_io ( 'I', 183 );
	blk_lbn_io ( 'I', 184 );
	blk_lbn_io ( 'I', 185 );
	blk_lbn_io ( 'W', 186 );
	blk_lbn_io ( 'W', 187 );
	blk_lbn_io ( 'W', 188 );
	blk_lbn_io ( 'W', 189 );
	blk_lbn_io ( 'I', 186 );
	blk_lbn_io ( 'I', 187 );
	blk_lbn_io ( 'I', 188 );
	blk_lbn_io ( 'I', 189 );
	blk_lbn_io ( 'W', 190 );
	blk_lbn_io ( 'W', 191 );
	blk_lbn_io ( 'W', 192 );
	blk_lbn_io ( 'W', 193 );
	blk_lbn_io ( 'W', 194 );
	blk_lbn_io ( 'I', 190 );
	blk_lbn_io ( 'I', 191 );
	blk_lbn_io ( 'I', 192 );
	blk_lbn_io ( 'I', 193 );
	blk_lbn_io ( 'I', 194 );
	blk_lbn_io ( 'W', 195 );
	blk_lbn_io ( 'W', 196 );
	blk_lbn_io ( 'W', 197 );
	blk_lbn_io ( 'W', 198 );
	blk_lbn_io ( 'W', 199 );
	blk_lbn_io ( 'W', 200 );
	blk_lbn_io ( 'I', 195 );
	blk_lbn_io ( 'I', 196 );
	blk_lbn_io ( 'I', 197 );
	blk_lbn_io ( 'I', 198 );
	blk_lbn_io ( 'I', 199 );
	blk_lbn_io ( 'I', 200 );
	blk_lbn_io ( 'W', 201 );
	blk_lbn_io ( 'W', 202 );
	blk_lbn_io ( 'W', 203 );
	blk_lbn_io ( 'W', 204 );
	blk_lbn_io ( 'W', 205 );
	blk_lbn_io ( 'W', 206 );
	blk_lbn_io ( 'W', 207 );
	blk_lbn_io ( 'I', 201 );
	blk_lbn_io ( 'I', 202 );
	blk_lbn_io ( 'I', 203 );
	blk_lbn_io ( 'I', 204 );
	blk_lbn_io ( 'I', 205 );
	blk_lbn_io ( 'I', 206 );
	blk_lbn_io ( 'I', 207 );
	blk_lbn_io ( 'W', 208 );
	blk_lbn_io ( 'W', 209 );
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

