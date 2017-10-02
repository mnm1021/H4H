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

uint32_t dm_proxy_probe (h4h_drv_info_t* bdi, h4h_device_params_t* params);
uint32_t dm_proxy_open (h4h_drv_info_t* bdi);
void dm_proxy_close (h4h_drv_info_t* bdi);
uint32_t dm_proxy_make_req (h4h_drv_info_t* bdi, h4h_llm_req_t* r);
void dm_proxy_end_req (h4h_drv_info_t* bdi, h4h_llm_req_t* r);
uint32_t dm_proxy_load (h4h_drv_info_t* bdi, const char* fn);
uint32_t dm_proxy_store (h4h_drv_info_t* bdi, const char* fn);

