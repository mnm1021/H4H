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

#ifndef _H4H_FTL_PAGEFTL_H
#define _H4H_FTL_PAGEFTL_H

extern h4h_ftl_inf_t _ftl_page_ftl;

#if 0
uint32_t h4h_page_ftl_create (h4h_drv_info_t* bdi);
void h4h_page_ftl_destroy (h4h_drv_info_t* bdi);
uint32_t h4h_page_ftl_get_free_ppa (h4h_drv_info_t* bdi, uint64_t lpa, h4h_phyaddr_t* ppa);
uint32_t h4h_page_ftl_get_ppa (h4h_drv_info_t* bdi, uint64_t lpa, h4h_phyaddr_t* ppa);
uint32_t h4h_page_ftl_map_lpa_to_ppa (h4h_drv_info_t* bdi, uint64_t lpa, h4h_phyaddr_t* ptr_phyaddr);
uint32_t h4h_page_ftl_invalidate_lpa (h4h_drv_info_t* bdi, uint64_t lpa, uint64_t len);
uint8_t h4h_page_ftl_is_gc_needed (h4h_drv_info_t* bdi);
uint32_t h4h_page_ftl_do_gc (h4h_drv_info_t* bdi);
uint32_t h4h_page_badblock_scan (h4h_drv_info_t* bdi);
uint32_t h4h_page_ftl_load (h4h_drv_info_t* bdi, const char* fn);
uint32_t h4h_page_ftl_store (h4h_drv_info_t* bdi, const char* fn);
#endif

uint32_t h4h_page_ftl_create (h4h_drv_info_t* bdi);
void h4h_page_ftl_destroy (h4h_drv_info_t* bdi);
uint32_t h4h_page_ftl_get_free_ppa (h4h_drv_info_t* bdi, int64_t lpa, h4h_phyaddr_t* ppa);
uint32_t h4h_page_ftl_get_ppa (h4h_drv_info_t* bdi, int64_t lpa, h4h_phyaddr_t* ppa, uint64_t* sp_off);
uint32_t h4h_page_ftl_map_lpa_to_ppa (h4h_drv_info_t* bdi, h4h_logaddr_t* logaddr, h4h_phyaddr_t* ppa);
uint32_t h4h_page_ftl_invalidate_lpa (h4h_drv_info_t* bdi, int64_t lpa, uint64_t len);
uint8_t h4h_page_ftl_is_gc_needed (h4h_drv_info_t* bdi, int64_t lpa);
uint32_t h4h_page_ftl_do_gc (h4h_drv_info_t* bdi, int64_t lpa);
uint32_t h4h_page_badblock_scan (h4h_drv_info_t* bdi);
uint32_t h4h_page_ftl_load (h4h_drv_info_t* bdi, const char* fn);
uint32_t h4h_page_ftl_store (h4h_drv_info_t* bdi, const char* fn);

int32_t h4h_page_ftl_get_free_ppas (h4h_drv_info_t* bdi, int64_t lpa, uint32_t size, h4h_phyaddr_t* start_ppa, uint8_t data_hotness);


#endif /* _H4H_FTL_BLOCKFTL_H */

