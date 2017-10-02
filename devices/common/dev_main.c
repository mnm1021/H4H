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

#if defined (KERNEL_MODE)
#include <linux/kernel.h>
#include <linux/module.h>
#include "dev_stub.h"

#elif defined (USER_MODE)
#include <stdio.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "h4h_drv.h"
#include "debug.h"

extern h4h_dm_inf_t _h4h_dm_inf; /* exported by the device implementation module */
h4h_drv_info_t* _bdi_dm = NULL; /* for Connectal & RAMSSD */


#if defined (KERNEL_MODE)
static int __init risa_dev_init (void)
{
	/* initialize dm_stub_proxy for user-level apps */
	h4h_dm_stub_init ();
	return 0;
}

static void __exit risa_dev_exit (void)
{
	/* initialize dm_stub_proxy for user-level apps */
	h4h_dm_stub_exit ();
}
#endif

int h4h_dm_init (h4h_drv_info_t* bdi)
{
	/* see if bdi is valid or not */
	if (bdi == NULL) {
		h4h_warning ("bdi is NULL");
		return 1;
	}

	if (_bdi_dm != NULL) {
		h4h_warning ("dm_stub is already used by other clients");
		return 1;
	}

	/* initialize global variables */
	_bdi_dm = bdi;

	return 0;
}

void h4h_dm_exit (h4h_drv_info_t* bdi)
{
	_bdi_dm = NULL;
}

/* NOTE: Export dm_inf to kernel or user applications.
 * This is only supported when both the FTL and the device manager (dm) are compiled 
 * in the same mode (i.e., both KERNEL_MODE or USER_MODE) */
h4h_dm_inf_t* h4h_dm_get_inf (h4h_drv_info_t* bdi)
{
	if (_bdi_dm == NULL) {
		h4h_warning ("_bdi_dm is not initialized yet");
		return NULL;
	}

	return &_h4h_dm_inf;
}

#if defined (KERNEL_MODE)
EXPORT_SYMBOL (h4h_dm_init);
EXPORT_SYMBOL (h4h_dm_exit);
EXPORT_SYMBOL (h4h_dm_get_inf);

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("RISA Device Wrapper");
MODULE_LICENSE ("GPL");

module_init (risa_dev_init);
module_exit (risa_dev_exit);
#endif
