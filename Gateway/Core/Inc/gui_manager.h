/*
 * gui_manager.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Home dashboard: one tile per radio, as described in section 8 of the
 *  architecture document.
 *
 *  The GUI reads the gateway state only - sLoRaGtwConfParam under its
 *  mutex, and the read-only statistics from radio_manager. It never
 *  touches a LoRa register or an SPI handle. Screens designed later in
 *  SquareLine Studio drop in beside this file and use the same interface.
 *
 *  Call from Task_LVGL only, after Fn_LVGL_Port_Init().
 */

#ifndef INC_GUI_MANAGER_H_
#define INC_GUI_MANAGER_H_

#include "utils.h"

/* Why the last calibration attempt ended the way it did. Kept so a
 * rejection can be reported without the GUI layer owning a UART. */
typedef enum
{
    en_Cal_OK = 0,
    en_Cal_Timeout,         /* a crosshair was never touched          */
    en_Cal_EdgeMismatch,    /* two points on one edge disagreed       */
    en_Cal_SpreadTooSmall,  /* corner to corner range implausible     */
} EnumCalResult;

typedef struct
{
    EnumCalResult enResult;
    uint16_t      u16RawX[4];
    uint16_t      u16RawY[4];
    uint16_t      u16Edge[4];   /* left, right, top, bottom mismatch */
    uint16_t      u16SpreadA;
    uint16_t      u16SpreadB;
    bool          bSwap;
    uint8_t       u8Points;     /* corners captured before giving up */
} StructCalDebug;

void Fn_GUI_Get_Cal_Debug (StructCalDebug *psDbg);

/* Four point touch calibration. Draws a crosshair in each corner, waits
 * for a press on each, works out the axis mapping and applies it.
 *
 * Blocking, and it pumps LVGL itself, so call it from Task_LVGL only.
 * Returns false if the user never touched the panel (30 s per point),
 * in which case the built-in defaults stay in force. */
bool Fn_GUI_Calibrate (void);

/* Builds the home screen and starts its refresh timer. */
void Fn_GUI_Init (void);

#endif /* INC_GUI_MANAGER_H_ */
