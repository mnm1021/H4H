/**
 * lbl.h: header for logical-block layer
 * Author: Yoohyun Jo
 */

#include "h4h_drv.h"

void LBL_inf (h4h_drv_info_t*);
void LBL_inf_get_block (h4h_drv_info_t*, int);
void LBL_inf_write (h4h_drv_info_t*, int, int, int);
void LBL_inf_read (h4h_drv_info_t*, int, int, int);
void LBL_inf_trim (h4h_drv_info_t*, int);
