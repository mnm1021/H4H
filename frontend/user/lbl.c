/**
 * lbl.c: implementation for logical-block layer
 * Author: Yoohyun Jo
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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

#include "uatomic64.h"

#define NR_LOGICAL_BLOCKS 10
#define APP_BLOCK_SIZE 82
#define FLASH_BLOCK_SIZE 35

int valid[NR_LOGICAL_BLOCKS][APP_BLOCK_SIZE];
int allocated_size = 0;
int num;
int alb_inf_on = 1;

void LBL_inf_get_block(h4h_drv_info_t* bdi, int request_size) {
	//Logical Block Layer command interface - get_block
	
	h4h_ftl_inf_t* ftl = H4H_GET_FTL_INF(bdi);
	int i, j;
	int32_t alloc_size, total_alloc_size;
	h4h_logaddr_t logaddr;
	h4h_phyaddr_t start_ppa;
	int i_ppa;

	allocated_size = request_size;
	num = allocated_size;

	logaddr.lpa[0] = 0;
	logaddr.ofs = 0;

	for(i = 0; i < num / APP_BLOCK_SIZE; i++) {
		for(j = 0; j < APP_BLOCK_SIZE; j++) {
			/* set allocated page to clean */
			valid[i][j] = 0;

			/* get space from flash and map to FTL */
			total_alloc_size = 0;
			i_ppa = 0;
			while (total_alloc_size < APP_BLOCK_SIZE)
			{
				alloc_size = ftl->get_free_ppas (bdi, logaddr.lpa[0],
						APP_BLOCK_SIZE - total_alloc_size, &start_ppa);
				if (alloc_size < 0)
				{
					h4h_error ("'ftl->get_free_ppas' failed");
					return;
				}

				/* map given addresses to FTL */
				total_alloc_size += alloc_size;
				for (; i_ppa < total_alloc_size; ++i_ppa)
				{
					if (ftl->map_lpa_to_ppa (bdi, &logaddr, &start_ppa) != 0)
					{
						h4h_error ("'ftl->map_lpa_to_ppa' failed");
						return;
					}
					logaddr.lpa[0] += 1;
					start_ppa.page_no += 1;
				}
			}
		}
	}

	for(j = 0; j < num % APP_BLOCK_SIZE; j++) {
		valid[num / APP_BLOCK_SIZE][j] = 0;	
	}

	printf("\n[allocation done]\n");
	
	printf("Available Logical Block range : \n");
	printf("	-> 0 block's 0 offset to %d block's offset %d\n",
			num / APP_BLOCK_SIZE, num % APP_BLOCK_SIZE);
	
	if(alb_inf_on == 1) {
		printf("\n[app logical block information] (0:free 1:written -1:out of range)\n");
		for(i = 0; i <= num / APP_BLOCK_SIZE; i++) {
			for(j = 0; j < APP_BLOCK_SIZE; j++) {
				printf("%d ", valid[i][j]);
			}
			printf("\n");
		}
	}

	//scanf("%d", &num);
}

void LBL_inf_write(h4h_drv_info_t* bdi, int lbn, int offset, int length) {
	//Logical Block Layer command interface - write

	if(offset != 0 && valid[lbn][offset-1] == 0) {
		printf("Invalid : jump case\n");
		return;
	}
	if(valid[lbn][offset] == 1) {
		printf("Invalid : already written case\n");
		return;		
	}	
	if(valid[lbn][offset] == -1) {
		printf("Invalid : out of bound case\n");
		return;
	}
	int size = 0;	
	size = length * 2;
	int i = 0, j = 0;
	h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));

		/* build blkio req */
		blkio_req->bi_rw = REQTYPE_WRITE;
		blkio_req->bi_offset = (APP_BLOCK_SIZE * lbn + offset ) * 8;
		blkio_req->bi_size = size;
		blkio_req->bi_bvec_cnt = size / 8;
		for (j = 0; j < blkio_req->bi_bvec_cnt; j++) {
			blkio_req->bi_bvec_ptr[j] = (uint8_t*)h4h_malloc (4096);
			blkio_req->bi_bvec_ptr[j][0] = 0x0A;
			blkio_req->bi_bvec_ptr[j][1] = 0x0B;
			blkio_req->bi_bvec_ptr[j][2] = 0x0C;
		}
		/* send req to ftl */
		bdi->ptr_host_inf->make_req (bdi, blkio_req);

	for(i = offset; i < offset + length/4; i++) { 
		valid[lbn][i] = 1;
	}
	printf("\n[write done]\n");
	num = allocated_size;	
	if(alb_inf_on == 1) {
		printf("\napp logical block information (0:free 1:written -1:out of range)\n");
		for(i = 0; i <= num / APP_BLOCK_SIZE; i++) {
			for(j = 0; j < APP_BLOCK_SIZE; j++) {
				printf("%d ", valid[i][j]);
			}
			printf("\n");
		}
	}
	
	//scanf("%d", &num);
}

void LBL_inf_read(h4h_drv_info_t* bdi, int lbn, int offset, int length) {
	//Logical Block Layer command interface - read
	if(valid[lbn][offset] == 0) {
		printf("Invalid : not written case\n");
		return;
	}
	if(valid[lbn][offset] == -1) {
		printf("Invalid : out of bound case\n");
		return;
	}

	int size = 0;	
	size = length * 2;
	int i = 0, j = 0;
	h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));

		/* build blkio req */
		blkio_req->bi_rw = REQTYPE_READ;
		blkio_req->bi_offset = (APP_BLOCK_SIZE * lbn + offset) * 8;
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
		bdi->ptr_host_inf->make_req (bdi, blkio_req);

	printf("read done\n");
	//scanf("%d", &num);
}

void LBL_inf_trim(h4h_drv_info_t* bdi, int lbn) {
	//Logical Block Layer command interface - trim

	int length = 4 * APP_BLOCK_SIZE;
	int size = 0;
	size = length * 2;
	int i = 0, j = 0;
	h4h_blkio_req_t* blkio_req = (h4h_blkio_req_t*)h4h_malloc (sizeof (h4h_blkio_req_t));

		/* build blkio req */
		blkio_req->bi_rw = REQTYPE_TRIM;
		blkio_req->bi_offset = APP_BLOCK_SIZE * lbn;
		//printf("offset : %d\n", blkio_req->bi_offset); 
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
		bdi->ptr_host_inf->make_req (bdi, blkio_req);
	
	for(j = 0; j < 35; j++) {
		valid[lbn][j] = 0;
	}
	printf("\n[trim done]\n");
	num = allocated_size;
	if(alb_inf_on == 1) {
		printf("\napp logical block information (0:free 1:written -1:out of range)\n");
		for(i = 0; i <= num / APP_BLOCK_SIZE; i++) {
			for(j = 0; j < APP_BLOCK_SIZE; j++) {
				printf("%d ", valid[i][j]);
			}
			printf("\n");
		}
	}
	//scanf("%d", &num);
}

void LBL_inf (h4h_drv_info_t* bdi)
{
	int command_type = 0; // 0:default / 1:get_block / 2:write / 3:read / 4:trim / 5:end
	int end = 0;
	int i, j;

	for(i = 0; i < NR_LOGICAL_BLOCKS; i++) {
		for(j = 0; j < APP_BLOCK_SIZE; j++) {
			valid[i][j] = -1;
		}
	}

	do {
		char input[100];
		printf("\ntype commands\n");
		printf("ex) get_block(500)\n");
		printf("ex) W-L ( 3 ) -O ( 0 ) -L ( 4K )\n");
		printf("ex) R-L ( 3 ) -O ( 0 ) -L ( 4K )\n");
		printf("ex) Trim Logical block 3\n");
		printf("type end to end\n");		
		fseek(stdin,0,SEEK_END);
		fgets (input, 100, stdin);
		//scanf("%[^\n]s",input);
		//scanf("%s",input);
		char * pch;
		//printf ("Splitting string \"%s\" into tokens:\n",input);
		pch = strtok (input," 'KB''MB'()-");
		//if(pch == "get_block") command_type = 1;
		//printf("\n%s\n", pch);
		if(strcmp(pch, "get_block")==0) command_type = 1;
		if(strcmp(pch, "W")==0) command_type = 2;
		if(strcmp(pch, "R")==0) command_type = 3;
		if(strcmp(pch, "Trim")==0) command_type = 4;
		if(strcmp(pch, "end\n")==0) command_type = 5;
		//printf("\n%d\n", command_type);
		int count = 0;
		int lbn = 0;
		int offset = 0;
		int length = 0;
		int size = 0;
		int request_size = 0;
		while (pch != NULL) {
			//printf ("%s:%d ",pch, count);
			count++;
			pch = strtok (NULL," 'KB''MB'()-");
			if(command_type == 1 && count == 1) request_size = atoi(pch);

			if(command_type == 2 && count == 2) lbn = atoi(pch);
			if(command_type == 2 && count == 4) offset = atoi(pch);
			if(command_type == 2 && count == 6) length = atoi(pch);	
		
			if(command_type == 3 && count == 2) lbn = atoi(pch);
			if(command_type == 3 && count == 4) offset = atoi(pch);
			if(command_type == 3 && count == 6) length = atoi(pch);
		
			if(command_type == 4 && count == 3) lbn = atoi(pch);
		}
		if(command_type == 1) LBL_inf_get_block(bdi, request_size);
		if(command_type == 2) LBL_inf_write(bdi, lbn, offset, length);
		if(command_type == 3) LBL_inf_read(bdi, lbn, offset, length);
		if(command_type == 4) LBL_inf_trim(bdi, lbn);
		if(command_type == 5) end = 1;
	} while (end == 0);
	
	scanf("%d", &num);
}
