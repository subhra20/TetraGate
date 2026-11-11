/*
 * xpt2046.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  XPT2046 resistive touch controller on SPI6, its own bus.
 *
 *    T_CLK PG13    T_DIN PG14    T_DO PG12    T_CS PG11
 *
 *  PENIRQ is not wired to the MCU in this design, so touch is detected by
 *  measuring plate pressure (Z1/Z2) rather than by an interrupt. The LVGL
 *  input device polls this driver, which is what LVGL expects anyway.
 *
 *  SPI6 runs at 1.3 Mbit/s: the XPT2046 is rated to about 2.5 MHz.
 *
 *  Raw-to-pixel calibration defaults are rough. The touch calibration
 *  screen will replace them with measured values stored in the EEPROM.
 */

#ifndef INC_XPT2046_H_
#define INC_XPT2046_H_

#include "utils.h"

/* Pressure above which the panel counts as touched, in raw Z units where
 * Z = Z1 + (4095 - Z2).
 *
 * Measured on this panel: idle sits at 14 or below (Z1 = 1..3,
 * Z2 = 4077..4095); a press gives 389 or more. 150 sits clear of the
 * noise floor while still catching a light touch. */
#define XPT2046_Z_THRESHOLD     150U

typedef struct
{
    uint16_t u16RawXMin;
    uint16_t u16RawXMax;
    uint16_t u16RawYMin;
    uint16_t u16RawYMax;
    bool     bSwapXY;       /* panel axes vs display axes */
    bool     bInvertX;
    bool     bInvertY;
} StructTouchCal;

void Fn_XPT2046_Init (void);

/* Raw 12-bit reading. Returns false when the panel is not being touched. */
bool Fn_XPT2046_Read_Raw (uint16_t *pu16X, uint16_t *pu16Y, uint16_t *pu16Z);

/* Calibrated pixel coordinates, clipped to the panel. */
bool Fn_XPT2046_Read (uint16_t *pu16X, uint16_t *pu16Y);

/* Raw and calibrated from a single conversion, so the two can be
 * compared without the panel being read twice. */
bool Fn_XPT2046_Read_Both (uint16_t *pu16RawX, uint16_t *pu16RawY,
                           uint16_t *pu16PixX, uint16_t *pu16PixY);

/* Unconditional read of all four channels, with no pressure gating, for
 * bring-up and for the calibration screen. Shows what the controller
 * really returns even when the touch logic rejects it. */
void Fn_XPT2046_Read_Diag (uint16_t *pu16Z1, uint16_t *pu16Z2, uint16_t *pu16X, uint16_t *pu16Y);

void Fn_XPT2046_Set_Calibration (const StructTouchCal *psCal);
void Fn_XPT2046_Get_Calibration (StructTouchCal *psCal);

#endif /* INC_XPT2046_H_ */
