/*
 * lvgl_port.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Binds LVGL 8.3.11 to the ILI9341 display and the XPT2046 touch panel.
 *
 *  Everything LVGL related runs in Task_LVGL only. No other task may call
 *  an lv_* function: LVGL 8.3 is not thread safe and this project keeps
 *  the GUI behind the gateway core instead of locking the whole library.
 *
 *  Flushing is asynchronous. flush_cb starts a DMA transfer and returns;
 *  lv_disp_flush_ready() is called from the SPI5 DMA complete interrupt.
 */

#ifndef INC_LVGL_PORT_H_
#define INC_LVGL_PORT_H_

#include "utils.h"

/* Partial draw buffer height in lines. Two buffers of
 * 320 x 24 x 2 bytes = 15 KB each, 30 KB together. */
#define LVGL_DRAW_BUF_LINES     24U

/* Initialises LVGL, the display driver and the touch input device.
 * Call once, from Task_LVGL, before any other lv_* call. */
void Fn_LVGL_Port_Init (void);

/* Last point LVGL's own input polling saw, and whether it was pressed.
 * Reports what the GUI is actually acting on, and costs no extra SPI
 * traffic because the panel has already been read. */
bool Fn_LVGL_Port_Get_Touch (int16_t *pi16X, int16_t *pi16Y);

#endif /* INC_LVGL_PORT_H_ */
