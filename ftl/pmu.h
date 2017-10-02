#ifndef __H4H_PMU_H
#define __H4H_PMU_H

#include "utime.h"
#include "umemory.h"

/* performance monitor functions */
void pmu_create (h4h_drv_info_t* bdi);
void pmu_destory (h4h_drv_info_t* bdi);
void pmu_display (h4h_drv_info_t* bdi);

void pmu_inc (h4h_drv_info_t* bdi, h4h_llm_req_t* llm_req);
void pmu_inc_read (h4h_drv_info_t* bdi);
void pmu_inc_write (h4h_drv_info_t* bdi);
void pmu_inc_rmw_read (h4h_drv_info_t* bdi);
void pmu_inc_rmw_write (h4h_drv_info_t* bdi);
void pmu_inc_gc (h4h_drv_info_t* bdi);
void pmu_inc_gc_erase (h4h_drv_info_t* bdi);
void pmu_inc_gc_read (h4h_drv_info_t* bdi);
void pmu_inc_gc_write (h4h_drv_info_t* bdi);
void pmu_inc_meta_read (h4h_drv_info_t* bdi);
void pmu_inc_meta_write (h4h_drv_info_t* bdi);
void pmu_inc_util_r (h4h_drv_info_t* bdi, uint64_t pid);
void pmu_inc_util_w (h4h_drv_info_t* bdi, uint64_t pid);

void pmu_update_sw (h4h_drv_info_t* bdi, h4h_llm_req_t* req);
void pmu_update_r_sw (h4h_drv_info_t* bdi, h4h_stopwatch_t* sw);
void pmu_update_w_sw (h4h_drv_info_t* bdi, h4h_stopwatch_t* sw);
void pmu_update_rmw_sw (h4h_drv_info_t* bdi, h4h_stopwatch_t* sw);
void pmu_update_gc_sw (h4h_drv_info_t* bdi, h4h_stopwatch_t* sw);

void pmu_update_q (h4h_drv_info_t* bdi, h4h_llm_req_t* req);
void pmu_update_r_q (h4h_drv_info_t* bdi, h4h_stopwatch_t* req);
void pmu_update_w_q (h4h_drv_info_t* bdi, h4h_stopwatch_t* req);
void pmu_update_rmw_q (h4h_drv_info_t* bdi, h4h_stopwatch_t* req);

void pmu_update_tot (h4h_drv_info_t* bdi, h4h_llm_req_t* req);
void pmu_update_r_tot (h4h_drv_info_t* bdi, h4h_stopwatch_t* req);
void pmu_update_w_tot (h4h_drv_info_t* bdi, h4h_stopwatch_t* req);
void pmu_update_rmw_tot (h4h_drv_info_t* bdi, h4h_stopwatch_t* req);
void pmu_update_gc_tot (h4h_drv_info_t* bdi, h4h_stopwatch_t* sw);

#endif
