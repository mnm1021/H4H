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

#ifndef _H4H_FTL_DFTL_H
#define _H4H_FTL_DFTL_H

extern h4h_ftl_inf_t _ftl_dftl;

uint32_t h4h_dftl_create (h4h_drv_info_t* bdi);
void h4h_dftl_destroy (h4h_drv_info_t* bdi);
uint32_t h4h_dftl_get_free_ppa (h4h_drv_info_t* bdi, uint64_t lpa, h4h_phyaddr_t* ppa);
uint32_t h4h_dftl_get_ppa (h4h_drv_info_t* bdi, uint64_t lpa, h4h_phyaddr_t* ppa);
uint32_t h4h_dftl_map_lpa_to_ppa (h4h_drv_info_t* bdi, uint64_t lpa, h4h_phyaddr_t* ptr_phyaddr);
uint32_t h4h_dftl_invalidate_lpa (h4h_drv_info_t* bdi, uint64_t lpa, uint64_t len);
uint8_t h4h_dftl_is_gc_needed (h4h_drv_info_t* bdi);
uint32_t h4h_dftl_do_gc (h4h_drv_info_t* bdi);

uint32_t h4h_dftl_badblock_scan (h4h_drv_info_t* bdi);
uint32_t h4h_dftl_load (h4h_drv_info_t* bdi, const char* fn);
uint32_t h4h_dftl_store (h4h_drv_info_t* bdi, const char* fn);

uint8_t h4h_dftl_check_mapblk (h4h_drv_info_t* bdi, uint64_t lpa);
h4h_llm_req_t* h4h_dftl_prepare_mapblk_eviction (h4h_drv_info_t* bdi);
void h4h_dftl_finish_mapblk_eviction (h4h_drv_info_t* bdi, h4h_llm_req_t* r);
h4h_llm_req_t* h4h_dftl_prepare_mapblk_load (h4h_drv_info_t* bdi, uint64_t lpa);
void h4h_dftl_finish_mapblk_load (h4h_drv_info_t* bdi, h4h_llm_req_t* r);

void h4h_dftl_finish_mapblk_load_2 (
	h4h_drv_info_t* bdi, 
	h4h_llm_req_t* r);

#endif /* _H4H_FTL_DFTL_H */

