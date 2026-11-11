/*
 * ili9341.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  ILI9341 320x240 TFT on SPI5, landscape.
 *
 *    SCK   PF7     MOSI  PF9     MISO  PF8
 *    CS    PE4     DC    PE5     RESET PF2
 *
 *  Backlight is hard wired to 3V3, so there is no brightness control.
 *
 *  Bulk pixel writes go out on DMA2_Stream4 (SPI5_TX), which is what the
 *  LVGL flush callback will use. Commands and short parameter writes stay
 *  blocking: they are a few bytes each and happen only at start-up.
 */

#ifndef INC_ILI9341_H_
#define INC_ILI9341_H_

#include "utils.h"

#define ILI9341_WIDTH           320U
#define ILI9341_HEIGHT          240U

/* RGB565 */
#define ILI9341_BLACK           0x0000U
#define ILI9341_WHITE           0xFFFFU
#define ILI9341_RED             0xF800U
#define ILI9341_GREEN           0x07E0U
#define ILI9341_BLUE            0x001FU
#define ILI9341_YELLOW          0xFFE0U
#define ILI9341_CYAN            0x07FFU
#define ILI9341_MAGENTA         0xF81FU

/* Panel identification, read back from register 0xD3.
 * A healthy ILI9341 answers 0x009341. */
#define ILI9341_EXPECTED_ID     0x009341UL

void     Fn_ILI9341_Init        (void);
uint32_t Fn_ILI9341_Read_Id     (void);

void     Fn_ILI9341_Set_Window  (uint16_t u16X1, uint16_t u16Y1, uint16_t u16X2, uint16_t u16Y2);
void     Fn_ILI9341_Fill_Rect   (uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H, uint16_t u16Colour);
void     Fn_ILI9341_Fill_Screen (uint16_t u16Colour);

/* Push a prepared RGB565 block, byte swapped, over DMA. Returns false if a
 * previous transfer is still running. Fn_ILI9341_Is_Busy() clears when the
 * DMA completes, which is where LVGL will call lv_disp_flush_ready(). */
bool     Fn_ILI9341_Flush_DMA   (const uint8_t *pu8Data, uint32_t u32Len);
bool     Fn_ILI9341_Is_Busy     (void);

/* Called from the SPI5 DMA complete interrupt once CS has been released.
 * The LVGL port uses it to call lv_disp_flush_ready(). Keep it short. */
void     Fn_ILI9341_Set_Flush_Done_Cb (void (*pfnDone)(void));

#endif /* INC_ILI9341_H_ */
