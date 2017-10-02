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

#include <linux/init.h>
#include <linux/module.h>

#include "h4h_drv.h"
#include "debug.h"
#include "blkio.h"
#include "devices.h"

h4h_drv_info_t* _bdi = NULL;

static int __init h4h_drv_init (void)
{
	/* create bdi with default parameters */
	if ((_bdi = h4h_drv_create ()) == NULL) {
		h4h_error ("[kmain] h4h_drv_create () failed");
		return -ENXIO;
	}

	/* open the device */
	if (h4h_dm_init (_bdi) != 0) {
		h4h_error ("[kmain] h4h_dm_init () failed");
		return -ENXIO;
	}

	/* attach the host & the device interface to the h4h */
	if (h4h_drv_setup (_bdi, &_blkio_inf, h4h_dm_get_inf (_bdi)) != 0) {
		h4h_error ("[kmain] h4h_drv_setup () failed");
		return -ENXIO;
	}

	/* run it */
	if (h4h_drv_run (_bdi) != 0) {
		h4h_error ("[kmain] h4h_drv_run () failed");
		return -ENXIO;
	}

	return 0;
}

static void __exit h4h_drv_exit(void)
{
	/* stop running layers */
	h4h_drv_close (_bdi);

	/* close the device */
	h4h_dm_exit (_bdi);

	/* remove h4h_drv */
	h4h_drv_destroy (_bdi);
}

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("h4h Device Driver");
MODULE_LICENSE ("GPL");

module_init (h4h_drv_init);
module_exit (h4h_drv_exit);
