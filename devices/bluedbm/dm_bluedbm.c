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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/delay.h> /* mdelay */
#include <linux/hrtimer.h> /* hrtimer */
#include <linux/workqueue.h> /* workqueue */

#include "portal.h"  
#include "portalmem.h"
#include "dmaManager.h"
#include "GeneratedTypes.h" 

#include "h4h_drv.h"
#include "params.h"
#include "dev_params.h"
#include "debug.h"
#include "dm_bluedbm.h"
#include "umemory.h"
#include "uthread.h"
#include "dev_params.h"

//#define USE_TIMER

/*#define H4H_DBG*/
#define MAX_INDARRAY 4
#define	BLKOFS 256

/*h4h_dm_inf_t _dm_bluedbm_inf = {*/
h4h_dm_inf_t _h4h_dm_inf = {
	.ptr_private = NULL,
	.probe = dm_bluedbm_probe,
	.open = dm_bluedbm_open,
	.close = dm_bluedbm_close,
	.make_req = dm_bluedbm_make_req,
	.end_req = dm_bluedbm_end_req,
	.load = dm_bluedbm_load,
	.store = dm_bluedbm_store,
};

typedef struct {
	struct work_struct work; /* it must be at the end of structre */
	void* user;
} dm_bluedbm_wq_t;

struct dm_bluedbm_private {
	h4h_completion_t connectal_completion;
	h4h_completion_t connectal_completion_init;

	/* for Connectal */
	DmaManagerPrivate dma;
	PortalInternal intarr[MAX_INDARRAY];
	sem_t flash_sem;

	/* for thread management */
	h4h_completion_t event_handler_completion;
	struct task_struct *event_handler;

	struct hrtimer hrtimer;	/* hrtimer must be at the end of the structure */
	struct workqueue_struct *wq;
	dm_bluedbm_wq_t works;

	struct task_struct *connectal_handler;

	/* for tag management */
	h4h_spinlock_t lock;
	h4h_llm_req_t** llm_reqs;
	uint8_t** rbuf;
	uint8_t** wbuf;
};

extern h4h_drv_info_t* _bdi_dm;

void __copy_bio_to_dma (h4h_drv_info_t* bdi, h4h_llm_req_t* r);
void __copy_dma_to_bio (h4h_drv_info_t* bdi, h4h_llm_req_t* r);


/**
 * the implementation of call-back functions for Connectal 
 **/
void FlashIndicationreadDone_cb (struct PortalInternal *p, const uint32_t tag)
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (_bdi_dm);
	h4h_llm_req_t* r = NULL;

	if ((r = priv->llm_reqs[tag]) == NULL)
		h4h_bug_on (1);

	__copy_dma_to_bio (_bdi_dm, r);

	h4h_spin_lock (&priv->lock);
	priv->llm_reqs[tag] = NULL;
	h4h_spin_unlock (&priv->lock);

	_bdi_dm->ptr_dm_inf->end_req (_bdi_dm, r);
}

void FlashIndicationwriteDone_cb (  struct PortalInternal *p, const uint32_t tag )
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (_bdi_dm);
	h4h_llm_req_t* r = NULL;

	if ((r = priv->llm_reqs[tag]) == NULL)
		h4h_bug_on (1);

	h4h_spin_lock (&priv->lock);
	priv->llm_reqs[tag] = NULL;
	h4h_spin_unlock (&priv->lock);

	_bdi_dm->ptr_dm_inf->end_req (_bdi_dm, r);
}

void FlashIndicationeraseDone_cb (  struct PortalInternal *p, const uint32_t tag, const uint32_t status )
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (_bdi_dm);
	h4h_llm_req_t* r = NULL;

	if ((r = priv->llm_reqs[tag]) == NULL)
		h4h_bug_on (1);

	if (status != 0) {
		h4h_msg ("*** bad block detected! (%llu, %llu, %llu) ***", r->phyaddr.channel_no, r->phyaddr.chip_no, r->phyaddr.block_no);
		r->ret = 1; /* oops! it is a bad block */
	}

	h4h_spin_lock (&priv->lock);
	priv->llm_reqs[tag] = NULL;
	h4h_spin_unlock (&priv->lock);

	_bdi_dm->ptr_dm_inf->end_req (_bdi_dm, r);
}

void FlashIndicationdebugDumpResp_cb (  struct PortalInternal *p, const uint32_t debug0, const uint32_t debug1, const uint32_t debug2, const uint32_t debug3, const uint32_t debug4, const uint32_t debug5 )
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (_bdi_dm);

	sem_post (&priv->flash_sem);
}

void MMUIndicationWrapperidResponse_cb (  struct PortalInternal *p, const uint32_t sglId ) 
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (_bdi_dm);

	priv->dma.sglId = sglId;
	sem_post (&priv->dma.sglIdSem);
}

void MMUIndicationWrapperconfigResp_cb (  struct PortalInternal *p, const uint32_t pointer )
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (_bdi_dm);

	sem_post (&priv->dma.confSem);
}

void MMUIndicationWrappererror_cb (  struct PortalInternal *p, const uint32_t code, const uint32_t pointer, const uint64_t offset, const uint64_t extra ) 
{
	PORTAL_PRINTF ("cb: MMUConfigIndicationWrappererror_cb\n");
}

void manual_event (struct dm_bluedbm_private* priv)
{
	int i;
	for (i = 0; i < MAX_INDARRAY; i++)
		portalCheckIndication(&priv->intarr[i]);
}

#ifndef USE_TIMER
int event_handler_fn (void* arg) 
{
	h4h_drv_info_t* bdi = (h4h_drv_info_t*)arg;
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (bdi);

	while (1) {
		manual_event (priv);
		yield ();
		if (kthread_should_stop ()) {
			h4h_msg ("event_handler_fn ends");
			break;
		}
	}
	h4h_msg ("event_handler_fn is going to finish");
	h4h_complete (priv->event_handler_completion);
	return 0;
}
#else
static void __bluedbm_wq_handler (struct work_struct *w)
{
	dm_bluedbm_wq_t* work = (dm_bluedbm_wq_t*)w;
	manual_event ((struct dm_bluedbm_private*)work->user);
}

static enum hrtimer_restart __bluedbm_timing_hrtimer_cmd_done (struct hrtimer *ptr_hrtimer)
{
	ktime_t ktime;
	struct dm_bluedbm_private* priv;
	
	priv = (struct dm_bluedbm_private*)container_of (ptr_hrtimer, struct dm_bluedbm_private, hrtimer);

	/* run workqueue */
	queue_work (priv->wq, &priv->works.work);

	ktime = ktime_set (0, 50 * 1000);
	hrtimer_start (&priv->hrtimer, ktime, HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}
#endif

MMUIndicationCb MMUIndication_cbTable = {
	MMUIndicationWrapperidResponse_cb,
	MMUIndicationWrapperconfigResp_cb,
	MMUIndicationWrappererror_cb,
};

FlashIndicationCb FlashIndication_cbTable = {
	FlashIndicationreadDone_cb,
	FlashIndicationwriteDone_cb,
	FlashIndicationeraseDone_cb,
	FlashIndicationdebugDumpResp_cb,
};

int connectal_handler_fn (void* arg) 
{
	#define H4H_FLASH_PAGE_SIZE (8192*2)
	#define H4H_NUM_TAGS 128

	uint32_t t = 0;
	h4h_drv_info_t* bdi = (h4h_drv_info_t*)arg;
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (bdi);

	uint32_t src_alloc;
	uint32_t dst_alloc;
	size_t dst_alloc_sz = H4H_FLASH_PAGE_SIZE * H4H_NUM_TAGS *sizeof(uint8_t);
	size_t src_alloc_sz = H4H_FLASH_PAGE_SIZE * H4H_NUM_TAGS *sizeof(uint8_t);
	uint32_t ref_dst_alloc; 
	uint32_t ref_src_alloc; 
	uint32_t* dst_buffer;
	uint32_t* src_buffer;

	/* create portals in FPGA */
	init_portal_internal (&priv->intarr[2], IfcNames_HostMMURequest, NULL, NULL, NULL, NULL, MMURequest_reqinfo); // fpga3
	init_portal_internal (&priv->intarr[0], IfcNames_HostMMUIndication, MMUIndication_handleMessage, &MMUIndication_cbTable, NULL, NULL, MMUIndication_reqinfo); // fpga1
	init_portal_internal (&priv->intarr[3], IfcNames_FlashRequest, NULL, NULL, NULL, NULL, FlashRequest_reqinfo); // fpga4
	init_portal_internal (&priv->intarr[1], IfcNames_FlashIndication, FlashIndication_handleMessage, &FlashIndication_cbTable, NULL, NULL, FlashIndication_reqinfo); // fpga2

	DmaManager_init (&priv->dma, NULL, &priv->intarr[2]);
	sem_init (&priv->flash_sem, 0, 0);

	/* create and run a thread for message handling */
#ifndef USE_TIMER
	wake_up_process (priv->event_handler);
#else
	{
		/* create a timer */
		ktime_t ktime;
		hrtimer_init (&priv->hrtimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
		priv->hrtimer.function = __bluedbm_timing_hrtimer_cmd_done;
		ktime = ktime_set (0, 500 * 1000);
		hrtimer_start (&priv->hrtimer, ktime, HRTIMER_MODE_REL);

		/* create wq */
		priv->wq = create_singlethread_workqueue ("h4h_bluedbm_wq");
		priv->works.user = (void*)priv;
		INIT_WORK (&priv->works.work, __bluedbm_wq_handler);

		h4h_msg ("registered a timer for checking events from HW");
	}
#endif

	/* get the mapped-memory from Connectal */
	src_alloc = portalAlloc (src_alloc_sz);
	dst_alloc = portalAlloc (dst_alloc_sz);
	src_buffer = (uint32_t*)portalMmap (src_alloc, src_alloc_sz);
	dst_buffer = (uint32_t*)portalMmap (dst_alloc, dst_alloc_sz);

	portalDCacheFlushInval (dst_alloc, dst_alloc_sz, dst_buffer);
	portalDCacheFlushInval (src_alloc, src_alloc_sz, src_buffer);
	ref_dst_alloc = DmaManager_reference (&priv->dma, dst_alloc);
	ref_src_alloc = DmaManager_reference (&priv->dma, src_alloc);

	/* assign the mapped-memory to device */
	for (t = 0; t < H4H_NUM_TAGS; t++) {
		uint32_t byte_offset = t * H4H_FLASH_PAGE_SIZE;
		FlashRequest_addDmaWriteRefs (&priv->intarr[3], ref_dst_alloc, byte_offset, t);
		FlashRequest_addDmaReadRefs (&priv->intarr[3], ref_src_alloc, byte_offset, t);
		priv->rbuf[t] = (uint8_t*)(dst_buffer + byte_offset/sizeof(uint32_t));
		priv->wbuf[t] = (uint8_t*)(src_buffer + byte_offset/sizeof(uint32_t));
	}

	/* initialize a device */
	FlashRequest_start (&priv->intarr[3], 0);
	FlashRequest_setDebugVals (&priv->intarr[3], 0, 0);
	FlashRequest_debugDumpReq (&priv->intarr[3], 0);
	sem_wait (&priv->flash_sem);
	FlashRequest_debugDumpReq (&priv->intarr[3], 0);
	sem_wait (&priv->flash_sem);

	/* everything has done */
	h4h_daemonize ("connectal_handler_fn");
	allow_signal (SIGKILL); 
	h4h_complete (priv->connectal_completion_init);

	/* sleep until the device driver is unloaded */
	set_current_state (TASK_INTERRUPTIBLE);
	schedule ();	/* go to sleep */
	set_current_state (TASK_RUNNING);

	/* free mapped-memory */
	DmaManager_dereference (&priv->dma, ref_dst_alloc);
	DmaManager_dereference (&priv->dma, ref_src_alloc);
	portalmem_dmabuffer_destroy (src_alloc);
	portalmem_dmabuffer_destroy (dst_alloc);

	h4h_complete (priv->connectal_completion);

	return 0;
}


/**
 * the implementation of call-back functions for h4h_drv
 **/
static void __dm_setup_device_params (h4h_device_params_t* params)
{
	*params = get_default_device_params ();
	display_device_params (params);
}

uint32_t dm_bluedbm_probe (h4h_drv_info_t* bdi, h4h_device_params_t* params)
{
	struct dm_bluedbm_private* priv = NULL;
	uint32_t nr_punit;

	/* setup NAND parameters according to users' inputs */
	__dm_setup_device_params (params);
	nr_punit = params->nr_channels * params->nr_chips_per_channel;

	/* create a private for bluedbm */
	if ((priv = h4h_zmalloc (sizeof (struct dm_bluedbm_private))) == NULL)	{
		h4h_warning ("h4h_zmalloc failed");
		goto fail;
	}
	bdi->ptr_dm_inf->ptr_private = (void*)priv;

	/* initialize some internal data structures */
	h4h_spin_lock_init (&priv->lock);
	if ((priv->llm_reqs = (h4h_llm_req_t**)h4h_zmalloc (
			sizeof (h4h_llm_req_t*) * nr_punit)) == NULL) {
		h4h_warning ("h4h_zmalloc failed");
		goto fail;
	}
	if ((priv->rbuf = (uint8_t**)h4h_zmalloc (
			sizeof (uint8_t*) * nr_punit)) == NULL) {
		h4h_warning ("h4h_zmalloc failed");
		goto fail;
	}
	if ((priv->wbuf = (uint8_t**)h4h_zmalloc (
			sizeof (uint8_t*) * nr_punit)) == NULL) {
		h4h_warning ("h4h_zmalloc failed");
		goto fail;
	}
#ifndef USE_TIMER
	h4h_init_completion (priv->event_handler_completion);
#endif
	h4h_init_completion (priv->connectal_completion);
	h4h_init_completion (priv->connectal_completion_init);

	/* register a h4h device */
#ifndef USE_TIMER
	if ((priv->event_handler = kthread_create (
			event_handler_fn, (void*)bdi, "event_handler_fn")) == NULL) {
		h4h_error ("kthread_create failed");
		goto fail;
	}
#else
	/* TODO: create a timer */
#endif
	if ((priv->connectal_handler = kthread_create (
			connectal_handler_fn, (void*)bdi, "connectal_handler_fn")) == NULL) {
		h4h_error ("kthread_create failed");
		goto fail;
	}

	return 0;

fail:
	h4h_error ("dm_bluedbm_probe failed!");
	bdi->ptr_dm_inf->close (bdi);
	return 1;
}

/* NOTE: To prevent the mapped-memory from Connectal from being freed,
 * we have to run connectal_handler in a different thread */
uint32_t dm_bluedbm_open (h4h_drv_info_t* bdi)
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (bdi);

	/* start the connectal handler */
	wake_up_process (priv->connectal_handler);

	/* wait until the connectal handler finishes it jobs */
	wait_for_completion (&priv->connectal_completion_init);

	h4h_msg ("dm_bluedbm_open is initialized"); 

	return 0;
}

void dm_bluedbm_close (h4h_drv_info_t* bdi)
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (bdi);

	if (priv == NULL)
		return;

#ifndef USE_TIMER
	if (priv->event_handler) {
		kthread_stop (priv->event_handler);
		wait_for_completion_timeout (
			&priv->event_handler_completion, 
			msecs_to_jiffies(2000));
		h4h_msg ("event_handler done");
	}
#else
	hrtimer_cancel (&priv->hrtimer);
	if (priv->wq) {
		destroy_workqueue (priv->wq);
	}
	h4h_msg ("destoryed timer for bluedbm");
#endif

	if (priv->connectal_handler) {
		send_sig (SIGKILL, priv->connectal_handler, 0);
		wait_for_completion_timeout (
			&priv->connectal_completion, 
			msecs_to_jiffies(2000));
	}
	if (priv->llm_reqs)
		h4h_free (priv->llm_reqs);
	if (priv->rbuf)
		h4h_free (priv->rbuf);
	if (priv->wbuf)
		h4h_free (priv->wbuf);
	h4h_free (priv);

	h4h_msg ("dm_bluedbm_close is destroyed"); 
}

void __copy_dma_to_bio (
	h4h_drv_info_t* bdi,
	h4h_llm_req_t* r)
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (bdi);
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
	uint8_t* ptr_dma_addr = NULL;
	uint32_t nr_pages = 0;
	uint32_t loop = 0;

	nr_pages = np->page_main_size / KERNEL_PAGE_SIZE;
	ptr_dma_addr = (uint8_t*)priv->rbuf[r->phyaddr.punit_id];

	/* copy the main page data to a buffer */
	for (loop = 0; loop < nr_pages; loop++) {
		if (h4h_is_read (r->req_type) && h4h_is_rmw (r->req_type)) {
			/* skip reading the page if it is part of RMW */
			if (r->fmain.kp_stt[loop] == KP_STT_DATA)
				continue;
		}
		h4h_memcpy (r->fmain.kp_ptr[loop], ptr_dma_addr + KERNEL_PAGE_SIZE * loop, KPAGE_SIZE);
	}

	/* copy the OOB data to a buffer */
	if (h4h_is_read (r->req_type)) {
		if (!h4h_is_rmw (r->req_type)) {
			h4h_memcpy (r->foob.data, ptr_dma_addr + np->page_main_size, np->page_oob_size);
		}
	}
}

void __copy_bio_to_dma (
	h4h_drv_info_t* bdi,
	h4h_llm_req_t* r)
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (bdi);
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (bdi);
 	uint8_t* ptr_dma_addr = NULL;
	uint32_t nr_pages = 0;
	uint32_t loop = 0;

	nr_pages = np->page_main_size / KPAGE_SIZE;
	ptr_dma_addr = (uint8_t*)priv->wbuf[r->phyaddr.punit_id];

	/* copy the main page data to a buffer */
	for (loop = 0; loop < nr_pages; loop++) {
		h4h_memcpy (ptr_dma_addr + KPAGE_SIZE * loop, r->fmain.kp_ptr[loop], KPAGE_SIZE);
	}

	/* copy the OOB data to a buffer */
	if (h4h_is_write (r->req_type)) {
		h4h_memcpy (ptr_dma_addr + np->page_main_size, r->foob.data, np->page_oob_size);
	}
}

uint32_t dm_bluedbm_make_req (
	h4h_drv_info_t* bdi, 
	h4h_llm_req_t* r)
{
	struct dm_bluedbm_private* priv = H4H_DM_PRIV (bdi);
	uint32_t punit_id;

	if (r->req_type == REQTYPE_READ_DUMMY) {
		_bdi_dm->ptr_dm_inf->end_req (bdi, r);
		return 0;
	}

	/* check punit (= tags) */
	punit_id = r->phyaddr.punit_id;

	spin_lock (&priv->lock);
	if (priv->llm_reqs[punit_id] != NULL) {
		spin_unlock (&priv->lock);
		h4h_error ("punit_id (%u) is busy...", punit_id);
		h4h_bug_on (1);
	} else
		priv->llm_reqs[punit_id] = r;
	spin_unlock (&priv->lock);

#ifdef H4H_DBG
	switch (r->req_type) {
	case REQTYPE_WRITE:
		h4h_msg ("[W-NM] %llu (%llu,%llu,%llu,%llu)",
			r->lpa, r->phyaddr.channel_no, r->phyaddr.chip_no, r->phyaddr.block_no, r->phyaddr.page_no);
		break;
	case REQTYPE_RMW_WRITE:
		h4h_msg ("[W-RW] %llu (%llu,%llu,%llu,%llu)", 
			r->lpa, r->phyaddr.channel_no, r->phyaddr.chip_no, r->phyaddr.block_no, r->phyaddr.page_no);
		break;
	case REQTYPE_GC_WRITE:
		h4h_msg ("[W-GC] %llu (%llu,%llu,%llu,%llu)", 
			r->lpa, r->phyaddr->channel_no, r->phyaddr->chip_no, r->phyaddr->block_no, r->phyaddr->page_no);
		break;
	case REQTYPE_READ:
		h4h_msg ("[R-NM] %llu (%llu,%llu,%llu,%llu)", 
			r->lpa, r->phyaddr->channel_no, r->phyaddr->chip_no, r->phyaddr->block_no, r->phyaddr->page_no);
		break;
	case REQTYPE_RMW_READ:
		h4h_msg ("[R-RW] %llu (%llu,%llu,%llu,%llu)", 
			r->lpa, r->phyaddr->channel_no, r->phyaddr->chip_no, r->phyaddr->block_no, r->phyaddr->page_no);
		break;
	case REQTYPE_GC_READ:
		h4h_msg ("[R-GC] %llu (%llu,%llu,%llu,%llu)", 
			r->lpa, r->phyaddr->channel_no, r->phyaddr->chip_no, r->phyaddr->block_no, r->phyaddr->page_no);
		break;
	default:
		break;
	}
#endif

	/* submit reqs to the device */
	switch (r->req_type) {
	case REQTYPE_WRITE:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_META_WRITE:
		__copy_bio_to_dma (bdi, r);
		FlashRequest_writePage (
			&priv->intarr[3], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no+BLKOFS, 
			r->phyaddr.page_no, 
			punit_id);
		break;

	case REQTYPE_READ:
	case REQTYPE_RMW_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_META_READ:
		FlashRequest_readPage (
			&priv->intarr[3], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no+BLKOFS, 
			r->phyaddr.page_no, 
			punit_id);
		break;

	case REQTYPE_GC_ERASE:
		FlashRequest_eraseBlock (
			&priv->intarr[3], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no+BLKOFS, 
			punit_id);
		break;

	default:
		break;
	}

	return 0;
}

void dm_bluedbm_end_req (h4h_drv_info_t* bdi, h4h_llm_req_t* r)
{
	bdi->ptr_llm_inf->end_req (bdi, r);
}

uint32_t dm_bluedbm_load (h4h_drv_info_t* bdi, const char* fn)
{
	/* do nothing */
	return 0;
}

uint32_t dm_bluedbm_store (h4h_drv_info_t* bdi, const char* fn)
{
	/* do nothing */
	return 0;
}

