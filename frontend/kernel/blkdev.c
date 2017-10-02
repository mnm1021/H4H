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
#include <linux/blkdev.h> /* bio */
#include <linux/hdreg.h>
#include <linux/kthread.h>
#include <linux/delay.h> /* mdelay */

#include "h4h_drv.h"
#include "debug.h"
#include "blkdev.h"
#include "blkdev_ioctl.h"
#include "umemory.h"


int h4h_blk_ioctl (struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg);
int h4h_blk_getgeo (struct block_device *bdev, struct hd_geometry* geo);

static struct h4h_device_t {
	struct gendisk *gd;
	struct request_queue *queue;
} h4h_device;

static uint32_t h4h_device_major_num = 0;
static struct block_device_operations bdops = {
	.owner = THIS_MODULE,
	.ioctl = h4h_blk_ioctl,
	.getgeo = h4h_blk_getgeo,
};

extern h4h_drv_info_t* _bdi;

DECLARE_COMPLETION (task_completion);
static struct task_struct *task = NULL;


int badblock_scan_thread_fn (void* arg) 
{
	h4h_ftl_inf_t* ftl = NULL;
	uint32_t ret;

	/* get the ftl */
	if ((ftl = _bdi->ptr_ftl_inf) == NULL) {
		h4h_warning ("ftl is not created");
		goto exit;
	}

	/* run the bad-block scan */
	if ((ret = ftl->scan_badblocks (_bdi))) {
		h4h_msg ("scan_badblocks failed (%u)", ret);
	}

exit:
	complete (&task_completion);
	return 0;
}

int h4h_blk_getgeo (struct block_device *bdev, struct hd_geometry* geo)
{
	h4h_device_params_t* np = H4H_GET_DEVICE_PARAMS (_bdi);
	int nr_sectors = np->device_capacity_in_byte >> 9;

	/* NOTE: Heads * Cylinders * Sectors = # of sectors (512B) in SSDs */
	geo->heads = 16;
	geo->cylinders = 1024;
	geo->sectors = nr_sectors / (geo->heads * geo->cylinders);
	if (geo->heads * geo->cylinders * geo->sectors != nr_sectors) {
		h4h_warning ("h4h_blk_getgeo: heads=%d, cylinders=%d, sectors=%d (total sectors=%d)",
			geo->heads, 
			geo->cylinders, 
			geo->sectors,
			nr_sectors);
		return 1;
	}
	return 0;
}

int h4h_blk_ioctl (
	struct block_device *bdev, 
	fmode_t mode, 
	unsigned cmd, 
	unsigned long arg)
{
	struct hd_geometry geo;
	struct gendisk *disk = bdev->bd_disk;
	int ret;

	switch (cmd) {
	case HDIO_GETGEO:
	case HDIO_GETGEO_BIG:
	case HDIO_GETGEO_BIG_RAW:
		if (!arg) {
			h4h_warning ("invalid argument");
			return -EINVAL;
		}
		if (!disk->fops->getgeo) {
			h4h_warning ("disk->fops->getgeo is NULL");
			return -ENOTTY;
		}

		h4h_memset(&geo, 0, sizeof(geo));
		geo.start = get_start_sect(bdev);
		ret = disk->fops->getgeo(bdev, &geo);
		if (ret) {
			h4h_warning ("disk->fops->getgeo returns (%d)", ret);
			return ret;
		}
		if (copy_to_user((struct hd_geometry __user *)arg, &geo, sizeof(geo))) {
			h4h_warning ("copy_to_user failed");
			return -EFAULT;
		}
		break;

	case H4H_BADBLOCK_SCAN:
		h4h_msg ("Get a H4H_BADBLOCK_SCAN command: %u (%X)", cmd, cmd);

		if (task != NULL) {
			h4h_msg ("badblock_scan_thread is running");
		} else {
			/* create thread */
			if ((task = kthread_create (badblock_scan_thread_fn, NULL, "badblock_scan_thread")) == NULL) {
				h4h_msg ("badblock_scan_thread failed to create");
			} else {
				wake_up_process (task);
			}
		}
		break;

	case H4H_BADBLOCK_SCAN_CHECK:
		/* check the status of the thread */
		if (task == NULL) {
			h4h_msg ("badblock_scan_thread is not created...");
			ret = 1; /* done */
			copy_to_user ((int*)arg, &ret, sizeof (int));
			break;
		}

		/* is it still running? */
		if (!h4h_try_wait_for_completion (task_completion)) {
			ret = 0; /* still running */
			copy_to_user ((int*)arg, &ret, sizeof (int));
			break;
		}
		ret = 1; /* done */
		
		/* reinit some variables */
		task = NULL;
		copy_to_user ((int*)arg, &ret, sizeof (int));
		h4h_reinit_completion (task_completion);
		break;

#if 0
	case H4H_GET_PHYADDR:
		break;
#endif

	default:
		/*h4h_msg ("unknown bdm_blk_ioctl: %u (%X)", cmd, cmd);*/
		break;
	}

	return 0;
}

uint32_t host_blkdev_register_device (h4h_drv_info_t* bdi, make_request_fn* fn)
{
	/* create a blk queue */
	if (!(h4h_device.queue = blk_alloc_queue (GFP_KERNEL))) {
		h4h_error ("blk_alloc_queue failed");
		return -ENOMEM;
	}
	blk_queue_make_request (h4h_device.queue, fn);
	blk_queue_logical_block_size (h4h_device.queue, bdi->parm_ftl.kernel_sector_size);
	blk_queue_io_min (h4h_device.queue, bdi->parm_dev.page_main_size);
	blk_queue_io_opt (h4h_device.queue, bdi->parm_dev.page_main_size);
	blk_queue_max_segment_size (h4h_device.queue, 4096);
	/*blk_queue_max_hw_sectors (h4h_device.queue, 16);*/

	/* see if a TRIM command is used or not */
	if (bdi->parm_ftl.trim == TRIM_ENABLE) {
		h4h_device.queue->limits.discard_granularity = KERNEL_PAGE_SIZE;
		h4h_device.queue->limits.max_discard_sectors = UINT_MAX;
		/*h4h_device.queue->limits.discard_zeroes_data = 1;*/
		queue_flag_set_unlocked (QUEUE_FLAG_DISCARD, h4h_device.queue);
		h4h_msg ("TRIM is enabled");
	} else {
		h4h_msg ("TRIM is disabled");
	}

	/* register a blk device */
	if ((h4h_device_major_num = register_blkdev (h4h_device_major_num, "H4H")) < 0) {
		h4h_msg ("register_blkdev failed (%d)", h4h_device_major_num);
		return h4h_device_major_num;
	}
	if (!(h4h_device.gd = alloc_disk (1))) {
		h4h_msg ("alloc_disk failed");
		unregister_blkdev (h4h_device_major_num, "H4H");
		return -ENOMEM;
	}
	h4h_device.gd->major = h4h_device_major_num;
	h4h_device.gd->first_minor = 0;
	h4h_device.gd->fops = &bdops;
	h4h_device.gd->queue = h4h_device.queue;
	h4h_device.gd->private_data = NULL;
	strcpy (h4h_device.gd->disk_name, "H4H");

	{
		uint64_t capacity;
		//capacity = bdi->parm_dev.device_capacity_in_byte * 0.9;
		capacity = bdi->parm_dev.device_capacity_in_byte;
		capacity = (capacity / KERNEL_PAGE_SIZE) * KERNEL_PAGE_SIZE;
		capacity = capacity - capacity / 10;
		set_capacity (h4h_device.gd, capacity / KERNEL_SECTOR_SIZE);
	}
	add_disk (h4h_device.gd);

	return 0;
}

void host_blkdev_unregister_block_device (h4h_drv_info_t* bdi)
{
	/* unregister a h4h device driver */
	del_gendisk (h4h_device.gd);
	blk_cleanup_queue (h4h_device.gd->queue);
	put_disk (h4h_device.gd);
	unregister_blkdev (h4h_device_major_num, "H4H");
}

