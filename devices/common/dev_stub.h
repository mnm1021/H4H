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
#elif defined (USER_MODE)
#include <sys/ioctl.h>
#include <unistd.h>
#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "h4h_drv.h"

#if 0
typedef struct {
	uint32_t req_type; /* read, write, or erase */
	uint64_t lpa; /* logical page address */
	
	uint64_t channel_no;
	uint64_t chip_no;
	uint64_t block_no;
	uint64_t page_no;

	uint8_t kpg_flags[16]; /* maximum page size is assumed to be 64KB */
	uint8_t ret; /* return value */
} h4h_llm_req_ioctl_t;
#endif

typedef struct {
	uint32_t req_type; /* read, write, or erase */
	uint8_t ret; /* return value */
	h4h_logaddr_t logaddr;
	h4h_phyaddr_t phyaddr;
	kp_stt_t kp_stt[32];
} h4h_llm_req_ioctl_t;


#define H4H_DM_IOCTL_NAME		"h4h_dm_stub"
#define H4H_DM_IOCTL_DEVNAME	"/dev/h4h_dm_stub"
#define H4H_DM_IOCTL_MAGIC		'X'

#define H4H_DM_IOCTL_PROBE		_IOWR (H4H_DM_IOCTL_MAGIC, 0, int)
#define H4H_DM_IOCTL_OPEN		_IOWR (H4H_DM_IOCTL_MAGIC, 1, int)
#define H4H_DM_IOCTL_CLOSE		_IOWR (H4H_DM_IOCTL_MAGIC, 2, int)
#define H4H_DM_IOCTL_MAKE_REQ	_IOWR (H4H_DM_IOCTL_MAGIC, 3, h4h_llm_req_ioctl_t*)
#define H4H_DM_IOCTL_END_REQ	_IOWR (H4H_DM_IOCTL_MAGIC, 4, int)
#define H4H_DM_IOCTL_LOAD		_IOWR (H4H_DM_IOCTL_MAGIC, 5, int)
#define H4H_DM_IOCTL_STORE		_IOWR (H4H_DM_IOCTL_MAGIC, 6, int)

int h4h_dm_stub_init (void);
void h4h_dm_stub_exit (void);

