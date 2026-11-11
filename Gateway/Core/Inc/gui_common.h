/*
 * gui_common.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Shared look and layout for the gateway screens.
 *
 *  The panel is 320 x 240 with a 30 px tab bar, so every screen gets a
 *  320 x 210 content area and a finger - not a stylus - as the pointer.
 *  Touch targets are therefore never smaller than 28 px, and values the
 *  operator changes use stepper buttons rather than dropdowns: a
 *  dropdown list on a screen this size covers the value being set.
 */

#ifndef INC_GUI_COMMON_H_
#define INC_GUI_COMMON_H_

#include "utils.h"
#include "lvgl.h"

/*------------------------------------------------------------ Geometry --------------------------------------------------------------*/
#define GUI_W                   320
#define GUI_H                   240
#define GUI_TABBAR_H            30
#define GUI_CONTENT_H           (GUI_H - GUI_TABBAR_H)
#define GUI_PAD                 4
#define GUI_ROW_H               30      /* smallest comfortable touch row */

#define GUI_REFRESH_MS          400U

/*------------------------------------------------------------ Palette ---------------------------------------------------------------
 * Dark industrial: a near-black field, raised cards, one accent, and
 * three status colours that are never used decoratively.
 */
#define GUI_COL_BG              0x0E1116UL
#define GUI_COL_CARD            0x1A2029UL
#define GUI_COL_CARD_HI         0x232C38UL
#define GUI_COL_BAR_TRACK       0x2A3340UL
#define GUI_COL_ACCENT          0x2D7FF9UL
#define GUI_COL_TEXT            0xE6EAF0UL
#define GUI_COL_DIM             0x8B96A5UL
#define GUI_COL_OK              0x22C55EUL
#define GUI_COL_WARN            0xF59E0BUL
#define GUI_COL_BAD             0xEF4444UL
#define GUI_COL_OFF             0x4A5058UL

/* One colour per radio, used consistently on every screen so a channel
 * can be identified at a glance in the log and on the signal bars. */
extern const uint32_t u32RadioColour[DEF_MAX_LoRa_Module];

/*------------------------------------------------------------ Signal scale ----------------------------------------------------------
 * RSSI range the bars map over. -120 dBm is the practical noise floor
 * for SF9/BW250; -30 is a radio sitting next to the gateway.
 */
#define GUI_RSSI_MIN            (-120)
#define GUI_RSSI_MAX            (-30)

/* Colour thresholds, in dBm. */
#define GUI_RSSI_GOOD           (-85)
#define GUI_RSSI_FAIR           (-105)

uint32_t Fn_GUI_Rssi_Colour (int16_t i16Rssi);

/*------------------------------------------------------------ Shared helpers --------------------------------------------------------*/
/* A flat card with no border and no scrolling, the base of every row. */
lv_obj_t *Fn_GUI_Card (lv_obj_t *pObjParent, lv_coord_t x, lv_coord_t y,
                       lv_coord_t w, lv_coord_t h, uint32_t u32Colour);

/* A label in the project's type scale. */
lv_obj_t *Fn_GUI_Label (lv_obj_t *pObjParent, lv_coord_t x, lv_coord_t y,
                        uint32_t u32Colour, const lv_font_t *psFont, const char *chText);

/* Frequency in Hz as "868.100", without printf float support. */
void Fn_GUI_Format_MHz (uint32_t u32Hz, char *chBuf, uint16_t u16Size);

/*------------------------------------------------------------ Screen builders -------------------------------------------------------*/
void Fn_GUI_Radios_Build   (lv_obj_t *pObjParent);
void Fn_GUI_Radios_Refresh (void);

void Fn_GUI_Signal_Build   (lv_obj_t *pObjParent);
void Fn_GUI_Signal_Refresh (void);

void Fn_GUI_Log_Build      (lv_obj_t *pObjParent);
void Fn_GUI_Log_Refresh    (void);

#endif /* INC_GUI_COMMON_H_ */
