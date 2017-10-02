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

#ifndef _H4H_HOST_USER_H
#define _H4H_HOST_USER_H

extern h4h_host_inf_t _userio_inf;

#if 0
typedef struct {
	uint64_t uniq_id;
	uint32_t req_type; /* read, write, or erase */
	uint64_t lpa; /* logical page address */
	uint64_t len; /* legnth */
	uint8_t* data;
} h4h_host_req_t;
#endif

uint32_t userio_open (h4h_drv_info_t* bdi);
void userio_close (h4h_drv_info_t* bdi);
void userio_make_req (h4h_drv_info_t* bdi, void* bio);
void userio_end_req (h4h_drv_info_t* bdi, h4h_hlm_req_t* req);

#endif

