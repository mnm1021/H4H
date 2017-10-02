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

#include <linux/kernel.h>

#include "params.h"
#include "raw-flash.h"
#include "umemory.h"
#include "debug.h"
#include "devices.h"

/*#define DEBUG_FLASH_RAW*/

typedef enum {
	FLASH_RAW_ERASE = 0x0010,
	FLASH_RAW_READ = 0x0020,
	FLASH_RAW_WRITE = 0x0030,
} h4h_raw_flash_io_t;

typedef enum {
	FLASH_RAW_PUNIT_IDLE = 0,
	FLASH_RAW_PUNIT_BUSY = 1,
} h4h_raw_flash_punit_status_t;

static void __dm_intr_handler (h4h_drv_info_t* bdi, h4h_llm_req_t* r);

h4h_llm_inf_t _h4h_llm_inf = {
	.ptr_private = NULL,
	.create = NULL,
	.destroy = NULL,
	.make_req = NULL,
	.flush = NULL,
	.end_req = __dm_intr_handler, /* 'dm' automatically calls 'end_req' when it gets acks from devices */
};

#if 0
h4h_params_t* read_driver_params (void)
{
	h4h_params_t* p = NULL;

	/* allocate the memory for parameters */
	if ((p = (h4h_params_t*)h4h_zmalloc (sizeof (h4h_params_t))) == NULL) {
		h4h_error ("failed to allocate the memory for params");
		return NULL;
	}

	/* setup driver parameters */
	/* NOTE: FTL-specific parameters must be decided by custom FTL
	 * implementation. For this reason, all the parameters for the FTL are set
	 * to zero by default */
	h4h_memset (&p->driver, 0x00, sizeof (h4h_ftl_params));

	return p;
}
#endif

static void __h4h_raw_flash_destory (h4h_raw_flash_t* rf)
{
	uint64_t i;

	if (!rf)
		return;

	/* wait for all the on-going jobs to finish */
	h4h_msg ("[%s] wait for all the on-going jobs to finish...", __FUNCTION__);
	if (rf->rr) {
		for (i = 0; i < rf->nr_punits; i++) {
			if (rf->rr[i].done) {
				h4h_sema_lock (rf->rr[i].done);
			}
		}
	}

	/* delete llm_req */
	if (rf->rr) {
		for (i = 0; i < rf->nr_punits; i++) {
			if (rf->rr[i].done) {
				h4h_sema_unlock (rf->rr[i].done);
				h4h_sema_free (rf->rr[i].done);
				h4h_free (rf->rr[i].done);
			}
#if 0
			if (rf->rr[i].pptr_kpgs) {
				h4h_free (rf->rr[i].pptr_kpgs);
			}
#endif
		}
	}

	if (rf->punit_status)
		h4h_free (rf->punit_status);

#if 0
	if (rf->bdi.ptr_h4h_params)
		h4h_free (rf->bdi.ptr_h4h_params);
#endif

	if (rf->rr)
		h4h_free (rf->rr);

	h4h_free (rf);
}

#if 0
static void __h4h_raw_flash_show_nand_params (h4h_params_t* p)
{
	h4h_msg ("=====================================================================");
	h4h_msg ("< NAND PARAMETERS >");
	h4h_msg ("=====================================================================");
	h4h_msg ("# of channels = %llu", p->nand.nr_channels);
	h4h_msg ("# of chips per channel = %llu", p->nand.nr_chips_per_channel);
	h4h_msg ("# of blocks per chip = %llu", p->nand.nr_blocks_per_chip);
	h4h_msg ("# of pages per block = %llu", p->nand.nr_pages_per_block);
	h4h_msg ("page main size  = %llu bytes", p->nand.page_main_size);
	h4h_msg ("page oob size = %llu bytes", p->nand.page_oob_size);
	h4h_msg ("SSD type = %u (0: ramdrive, 1: ramdrive with timing , 2: h4h(emul), 3: h4h)", p->nand.device_type);
	h4h_msg ("# of punits: %llu", H4H_GET_NR_PUNITS (p->nand));
	h4h_msg ("# of kernel pages per flash page: %llu", p->nand.page_main_size / KERNEL_PAGE_SIZE);
	h4h_msg ("");
}
#endif

static void __h4h_raw_flash_show_nand_params (h4h_device_params_t* parm_dev)
{
	h4h_msg ("=====================================================================");
	h4h_msg ("< NAND PARAMETERS >");
	h4h_msg ("=====================================================================");
	h4h_msg ("# of channels = %llu", parm_dev->nr_channels);
	h4h_msg ("# of chips per channel = %llu", parm_dev->nr_chips_per_channel);
	h4h_msg ("# of blocks per chip = %llu", parm_dev->nr_blocks_per_chip);
	h4h_msg ("# of pages per block = %llu", parm_dev->nr_pages_per_block);
	h4h_msg ("page main size  = %llu bytes", parm_dev->page_main_size);
	h4h_msg ("page oob size = %llu bytes", parm_dev->page_oob_size);
	h4h_msg ("SSD type = %u (0: ramdrive, 1: ramdrive with timing , 2: h4h(emul), 3: h4h)", parm_dev->device_type);
	h4h_msg ("# of punits: %llu", H4H_GET_NR_PUNITS ((*parm_dev)));
	h4h_msg ("# of kernel pages per flash page: %llu", parm_dev->page_main_size / KERNEL_PAGE_SIZE);
	h4h_msg ("");
}


h4h_raw_flash_t* h4h_raw_flash_init (void)
{
	h4h_raw_flash_t* rf = NULL;
	h4h_drv_info_t* bdi = NULL;
	h4h_dm_inf_t* dm = NULL;

	/* create h4h_raw_flash_t */
	if ((rf = h4h_zmalloc (sizeof (h4h_raw_flash_t))) == NULL) {
		h4h_error ("h4h_zmalloc () failed");
		goto fail;
	}
	bdi = &rf->bdi; /* for convenience */

#if 0
	/* create params_t */
	if ((bdi->ptr_h4h_params = (h4h_params_t*)h4h_zmalloc 
			(sizeof (h4h_params_t))) == NULL) {
		h4h_error ("h4h_zmalloc () failed");
		goto fail;
	}
#endif

	/* obtain dm_inf from the device */
	if (h4h_dm_init (bdi) != 0)  {
		h4h_error ("h4h_dm_init () failed");
		goto fail;
	}
	if ((dm = h4h_dm_get_inf (bdi)) == NULL) {
		h4h_error ("h4h_dm_get_inf () failed");
		goto fail;
	}
	bdi->ptr_dm_inf = dm;

	/* probe the device and get the device paramters */
#if 0
	if (dm->probe (bdi, &bdi->ptr_h4h_params->nand) != 0) {
#endif
	if (dm->probe (bdi, &bdi->parm_dev) != 0) {
		h4h_error ("dm->probe () failed");
		goto fail;
	} else {
		/* get the number of parallel units in the device;
		 * it is commonly obtained by # of channels * # of chips per channels */
		rf->np = H4H_GET_DEVICE_PARAMS (bdi);
		rf->nr_kp_per_fp = rf->np->page_main_size / KERNEL_PAGE_SIZE;
		rf->nr_punits = H4H_GET_NR_PUNITS ((*rf->np));

		/* NOTE: disply device parameters. note that these parameters can be
		 * set manually by modifying the "include/params.h" file. */
#if 0
		__h4h_raw_flash_show_nand_params (bdi->ptr_h4h_params);
#endif
		__h4h_raw_flash_show_nand_params (&bdi->parm_dev);
	}

	/* create llm_reqs */
	if ((rf->rr = (h4h_llm_req_t*)h4h_zmalloc 
			(sizeof (h4h_llm_req_t) * rf->nr_punits)) == NULL) {
		h4h_error ("h4h_zmalloc () failed");
		goto fail;
	} else {
		uint64_t i = 0;
#if 0
		uint32_t nr_kp_per_fp = 1;
#endif

		rf->punit_status = h4h_malloc (sizeof (atomic_t) * rf->nr_punits);
		for (i = 0; i < rf->nr_punits; i++) {
#if 0
			rf->rr[i].pptr_kpgs = h4h_zmalloc (nr_kp_per_fp * sizeof (uint8_t*));
#endif
			rf->rr[i].done = h4h_malloc (sizeof (h4h_sema_t));

			h4h_sema_init (rf->rr[i].done); /* start with unlock */
			atomic_set (&rf->punit_status[i], FLASH_RAW_PUNIT_IDLE); /* start with unused */
		}
	}

	/* setup function points; this is just to handle responses from the device */
	bdi->ptr_llm_inf = &_h4h_llm_inf;

	/* assign rf to bdi's private_data */
	bdi->private_data = (void*)rf;

	return rf;

fail:
	/* oops! it fails */
	__h4h_raw_flash_destory (rf);

	return NULL;
}

int h4h_raw_flash_open (h4h_raw_flash_t* rf)
{
	h4h_drv_info_t* bdi = &rf->bdi;
	h4h_dm_inf_t* dm = bdi->ptr_dm_inf;

	/* open the device */
	if (dm->open (bdi) != 0) {
		h4h_error ("dm->open () failed");
		return -1;
	}

	return 0;
}

static void __dm_intr_handler (
	h4h_drv_info_t* bdi, 
	h4h_llm_req_t* r)
{
	h4h_raw_flash_t* rf = (h4h_raw_flash_t*)bdi->private_data;

#ifdef DEBUG_FLASH_RAW
	h4h_msg ("[%s] punit_id = %llu", __FUNCTION__, r->phyaddr.punit_id);
#endif

	h4h_bug_on (r->phyaddr.punit_id >= H4H_GET_NR_PUNITS ((*rf->np)));
	h4h_bug_on (atomic_read (&rf->punit_status[r->phyaddr.punit_id]) != FLASH_RAW_PUNIT_BUSY);

#ifdef DEBUG_FLASH_RAW
	if (r->ret != 0) {
		h4h_msg ("oops: (%llu, %llu, %llu) ret = %u",
			r->phyaddr.channel_no,
			r->phyaddr.chip_no,
			r->phyaddr.block_no,
			r->ret);
	}
#endif

	/* free mutex & free punit */
	atomic_set (&rf->punit_status[r->phyaddr.punit_id], FLASH_RAW_PUNIT_IDLE); 
	h4h_sema_unlock (r->done);
}

static int __h4h_raw_flash_fill_phyaddr (
	h4h_raw_flash_t* rf,
	h4h_drv_info_t* bdi,
	uint64_t channel,
	uint64_t chip,
	uint64_t block,
	uint64_t page,
	h4h_phyaddr_t* phyaddr)
{
	h4h_bug_on (channel >= rf->np->nr_channels);
	h4h_bug_on (chip >= rf->np->nr_chips_per_channel);
	h4h_bug_on (block >= rf->np->nr_blocks_per_chip);
	h4h_bug_on (page >= rf->np->nr_pages_per_block);

	phyaddr->channel_no = channel;
	phyaddr->chip_no = chip;
	phyaddr->block_no = block;
	phyaddr->page_no = page;
	phyaddr->punit_id = H4H_GET_PUNIT_ID (bdi, phyaddr);

	return 0;
}

static h4h_llm_req_t* __h4h_raw_flash_get_llm_req (
	h4h_raw_flash_t* rf,
	h4h_phyaddr_t* phyaddr)
{
	/* get llm_req from rr */
	h4h_bug_on (phyaddr->punit_id >= H4H_GET_NR_PUNITS ((*rf->np)));
	
	return &rf->rr[phyaddr->punit_id];
}

int __h4h_raw_flash_rwe_async (
	h4h_raw_flash_t* rf,
	h4h_raw_flash_io_t io,
	uint64_t channel,
	uint64_t chip,
	uint64_t block,
	uint64_t page,
	uint64_t lpa,
	uint8_t* ptr_data,
	uint8_t* ptr_oob)
{
	h4h_phyaddr_t phyaddr;
	h4h_llm_req_t* r = NULL;
	h4h_drv_info_t* bdi = &rf->bdi;
	h4h_dm_inf_t* dm = NULL;
	uint64_t i = 0, ret = 0;

	/* fill up phyaddr */
	if ((__h4h_raw_flash_fill_phyaddr (rf, bdi, channel, chip, block, page, &phyaddr)) != 0)
		return 1; /* failed to fill up h4h_phyaddr_t */

	/* get llm_req */
	if ((r = __h4h_raw_flash_get_llm_req (rf, &phyaddr)) == NULL)
		return 2; /* failed to get an empty bdgm_llm_req_t */

	/* see if llm_req is busy or not */
	if (h4h_sema_try_lock (r->done) == 0) {
		/* oops! r is being used by other threads. The client should wait for
		 * the on-going request to finish */
		return 3; /* busy */
	} 

	/* get a lock for r; set the corresponding punit to busy */
	atomic_set (&rf->punit_status[phyaddr.punit_id], FLASH_RAW_PUNIT_BUSY); 

	/* let's fill up llm_req for READ */
	switch (io) {
	case FLASH_RAW_ERASE:
		r->req_type = REQTYPE_GC_ERASE;
		r->phyaddr = phyaddr;
		break;
	case FLASH_RAW_READ:
		r->req_type = REQTYPE_READ;
		r->phyaddr = phyaddr;
		break;
	case FLASH_RAW_WRITE:
		r->req_type = REQTYPE_WRITE;
		r->phyaddr = phyaddr;
		break;
	default:
		break;
	}
#if 0
	for (i = 0; i < rf->nr_kp_per_fp; i++)
		r->pptr_kpgs[i] = ptr_data + ((KERNEL_PAGE_SIZE) * i);
	r->ptr_oob = ptr_oob;
#endif
	for (i = 0; i < rf->nr_kp_per_fp; i++)
		r->fmain.kp_ptr[i] = ptr_data + ((KERNEL_PAGE_SIZE) * i);
	if (ptr_oob)
		h4h_memcpy (r->foob.data, ptr_oob, rf->np->page_oob_size);
	r->ret = 0; /* success by default */

	/* send the reqest to the device */
	dm = H4H_GET_DM_INF (bdi);
	h4h_bug_on (dm == NULL);

#ifdef DEBUG_FLASH_RAW
	h4h_msg ("[%s] submit - punit_id = %llu (status = %d)", 
		__FUNCTION__, 
		r->phyaddr.punit_id, 
		atomic_read (&rf->punit_status[r->phyaddr.punit_id]));
#endif

	if ((ret = dm->make_req (bdi, r)) != 0) {
		h4h_error ("[%s] dm->make_req () failed (ret = %llu)", __FUNCTION__, ret);
	
		/* free mutex & free the punit */
		atomic_set (&rf->punit_status[i], FLASH_RAW_PUNIT_IDLE); 
		h4h_sema_unlock (r->done);
	}

	return ret;
}

int __h4h_raw_flash_rwe (
	h4h_raw_flash_t* rf,
	h4h_raw_flash_io_t io,
	uint64_t channel,
	uint64_t chip,
	uint64_t block,
	uint64_t page,
	uint64_t lpa,
	uint8_t* ptr_data,
	uint8_t* ptr_oob,
	uint8_t* ptr_ret)
{
	int ret;

	/* (1) submit the request to the device */
#ifdef DEBUG_FLASH_RAW
	uint64_t punit_id = (channel * rf->np->nr_chips_per_channel) + chip;
	h4h_msg ("[%s] submit - punit_id = %llu", __FUNCTION__, punit_id);
#endif

	ret = __h4h_raw_flash_rwe_async (
			rf, 
			io, 
			channel, chip, block, page, lpa,
			ptr_data, ptr_oob);

	/* (2) is it successful? */
	if (ret != 0) {
#ifdef DEBUG_FLASH_RAW
		h4h_msg ("[%s] error - punit_id = %llu, ret = %d", __FUNCTION__, punit_id, ret);
#endif
		goto done;
	}

	/* (3) wait for the request to finish */
#ifdef DEBUG_FLASH_RAW
	h4h_msg ("[%s] wait - punit_id = %llu", __FUNCTION__, punit_id);
#endif
	ret = h4h_raw_flash_wait (rf, channel, chip, ptr_ret);

#ifdef DEBUG_FLASH_RAW
	h4h_msg ("[%s] done - punit_id = %llu", __FUNCTION__, punit_id);
#endif

done:
	return ret;
}

int h4h_raw_flash_wait (
	h4h_raw_flash_t* rf,
	uint64_t channel,
	uint64_t chip,
	uint8_t* ptr_ret)
{
	h4h_llm_req_t* r = NULL;
	uint64_t punit_id = (channel * rf->np->nr_chips_per_channel) + chip;

	h4h_bug_on (channel >= rf->np->nr_channels);
	h4h_bug_on (chip >= rf->np->nr_chips_per_channel);
	h4h_bug_on (punit_id >= H4H_GET_NR_PUNITS ((*rf->np)));

	r = &rf->rr[punit_id];

	/* wait... */
#ifdef DEBUG_FLASH_RAW
	h4h_msg ("[%s] wait - punit_id = %llu", __FUNCTION__, punit_id);
#endif
	h4h_sema_lock (r->done);

	/* do something */
	h4h_bug_on (atomic_read (&rf->punit_status[punit_id]) != FLASH_RAW_PUNIT_IDLE);
	*ptr_ret = r->ret;

#ifdef DEBUG_FLASH_RAW
	h4h_msg ("[%s] done - punit_id = %llu", __FUNCTION__, punit_id);
#endif
	/* we got it; unlock it again for future use */
	h4h_sema_unlock (r->done);

	return 0;
}

int h4h_raw_flash_is_done (
	h4h_raw_flash_t* rf,
	uint64_t channel,
	uint64_t chip)
{
	uint64_t punit_id = (channel * rf->np->nr_chips_per_channel) + chip;

	if (atomic_read (&rf->punit_status[punit_id]) == FLASH_RAW_PUNIT_IDLE) {
		return 0;
	}

	return 1;
}

int h4h_raw_flash_read_page_async (
	h4h_raw_flash_t* rf,
	uint64_t channel,
	uint64_t chip,
	uint64_t block,
	uint64_t page,
	uint64_t lpa,
	uint8_t* ptr_data,
	uint8_t* ptr_oob)
{
	return __h4h_raw_flash_rwe_async (rf, FLASH_RAW_READ, channel, chip, block, page, lpa, ptr_data, ptr_oob);
}

int h4h_raw_flash_read_page (
	h4h_raw_flash_t* rf,
	uint64_t channel,
	uint64_t chip,
	uint64_t block,
	uint64_t page,
	uint64_t lpa,
	uint8_t* ptr_data,
	uint8_t* ptr_oob,
	uint8_t* ptr_ret)
{
	return __h4h_raw_flash_rwe (rf, FLASH_RAW_READ, channel, chip, block, page, lpa, ptr_data, ptr_oob, ptr_ret);
}

int h4h_raw_flash_write_page_async (
	h4h_raw_flash_t* rf, 
	uint64_t channel, 
	uint64_t chip, 
	uint64_t block, 
	uint64_t page, 
	uint64_t lpa, 
	uint8_t* ptr_data, 
	uint8_t* ptr_oob)
{
	return __h4h_raw_flash_rwe_async (rf, FLASH_RAW_WRITE, channel, chip, block, page, lpa, ptr_data, ptr_oob);
}

int h4h_raw_flash_write_page (
	h4h_raw_flash_t* rf, 
	uint64_t channel, 
	uint64_t chip, 
	uint64_t block, 
	uint64_t page, 
	uint64_t lpa, 
	uint8_t* ptr_data, 
	uint8_t* ptr_oob,
	uint8_t* ptr_ret)
{
	return __h4h_raw_flash_rwe (rf, FLASH_RAW_WRITE, channel, chip, block, page, lpa, ptr_data, ptr_oob, ptr_ret);
}

int h4h_raw_flash_erase_block_async (
	h4h_raw_flash_t* rf, 
	uint64_t channel, 
	uint64_t chip, 
	uint64_t block)
{
	return __h4h_raw_flash_rwe_async (rf, FLASH_RAW_ERASE, channel, chip, block, 0, -1ULL, NULL, NULL);
}

int h4h_raw_flash_erase_block (
	h4h_raw_flash_t* rf, 
	uint64_t channel, 
	uint64_t chip, 
	uint64_t block,
	uint8_t* ptr_ret)
{
	return __h4h_raw_flash_rwe (rf, FLASH_RAW_ERASE, channel, chip, block, 0, -1ULL, NULL, NULL, ptr_ret);
}

h4h_device_params_t* h4h_raw_flash_get_nand_params (
	h4h_raw_flash_t* rf)
{
	return H4H_GET_DEVICE_PARAMS ((&rf->bdi));
}

void h4h_raw_flash_exit (h4h_raw_flash_t* rf)
{
	h4h_drv_info_t* bdi = NULL;
	h4h_dm_inf_t* dm = NULL;

	if (!rf) return;

	bdi = &rf->bdi;
	dm = bdi->ptr_dm_inf;

	/* close the device interface */
	bdi->ptr_dm_inf->close (bdi);

	/* close the device module */
	h4h_dm_exit (&rf->bdi);

	/* destory the raw-flash module */
	__h4h_raw_flash_destory (rf);
}

