/*
 * gui_signal.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Signal quality per channel, in the style of a field tester: the
 *  number you act on is large, and the bar beside it says at a glance
 *  whether the link is healthy without reading the number at all.
 *
 *  Two bars per channel. The upper one is RSSI over -120 to -30 dBm,
 *  coloured green, amber or red by link quality. The lower one is the
 *  last payload size over 0 to 255 bytes, which is what tells an
 *  operator whether a node is sending a full report or a truncated one.
 *
 *  Every value is read from radio_manager's statistics. This screen
 *  never touches a radio.
 */

#include "gui_common.h"
#include "radio_manager.h"

#define SIG_HDR_H           18
#define SIG_ROW_H           48
#define SIG_BAR_X           34
#define SIG_BAR_W           150
#define SIG_TXT_X           192

#define SIG_SIZE_MAX        255

typedef struct
{
    lv_obj_t *pObjRssiBar;
    lv_obj_t *pObjSizeBar;
    lv_obj_t *pObjRssiTxt;
    lv_obj_t *pObjSubTxt;
    lv_obj_t *pObjBadge;
} StructSigRow;

static StructSigRow sRow[DEF_MAX_LoRa_Module];
static lv_obj_t    *pObjHdr;

static lv_obj_t *_Fn_Make_Bar (lv_obj_t *pObjParent, lv_coord_t y, lv_coord_t h,
                               int32_t i32Min, int32_t i32Max)
{
    lv_obj_t *pObjBar = lv_bar_create(pObjParent);

    lv_obj_set_pos(pObjBar, SIG_BAR_X, y);
    lv_obj_set_size(pObjBar, SIG_BAR_W, h);
    lv_obj_set_style_bg_color(pObjBar, lv_color_hex(GUI_COL_BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(pObjBar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(pObjBar, 3, LV_PART_INDICATOR);
    lv_bar_set_range(pObjBar, (int32_t)i32Min, (int32_t)i32Max);
    lv_bar_set_value(pObjBar, (int32_t)i32Min, LV_ANIM_OFF);

    return pObjBar;
}

void Fn_GUI_Signal_Build (lv_obj_t *pObjParent)
{
    uint8_t i;

    lv_obj_clear_flag(pObjParent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pObjParent, GUI_PAD, LV_PART_MAIN);

    pObjHdr = Fn_GUI_Label(pObjParent, 2, 0, GUI_COL_DIM, NULL, "waiting for traffic");

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        lv_coord_t y = (lv_coord_t)(SIG_HDR_H + (i * SIG_ROW_H));
        lv_obj_t *pObjLbl;
        char chName[4];

        /* Channel badge, in that channel's colour on every screen. */
        sRow[i].pObjBadge = Fn_GUI_Card(pObjParent, 0, (lv_coord_t)(y + 10), 26, 26,
                                        u32RadioColour[i]);
        (void)snprintf(chName, sizeof(chName), "%u", (unsigned)(i + 1U));
        pObjLbl = lv_label_create(sRow[i].pObjBadge);
        lv_label_set_text(pObjLbl, chName);
        lv_obj_center(pObjLbl);
        lv_obj_set_style_text_color(pObjLbl, lv_color_hex(GUI_COL_BG), LV_PART_MAIN);

        sRow[i].pObjRssiBar = _Fn_Make_Bar(pObjParent, (lv_coord_t)(y + 8), 14,
                                           GUI_RSSI_MIN, GUI_RSSI_MAX);
        sRow[i].pObjSizeBar = _Fn_Make_Bar(pObjParent, (lv_coord_t)(y + 28), 8,
                                           0, SIG_SIZE_MAX);

        sRow[i].pObjRssiTxt = Fn_GUI_Label(pObjParent, SIG_TXT_X, (lv_coord_t)(y + 6),
                                           GUI_COL_DIM, &lv_font_montserrat_16, "--");
        sRow[i].pObjSubTxt  = Fn_GUI_Label(pObjParent, SIG_TXT_X, (lv_coord_t)(y + 27),
                                           GUI_COL_DIM, NULL, "no packets");
    }
}

void Fn_GUI_Signal_Refresh (void)
{
    uint32_t u32Total = 0U;
    uint32_t u32Err   = 0U;
    uint8_t  i;

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        const StructRadioStats *s = Fn_Radio_Get_Stats(i);
        uint32_t u32Colour;
        uint8_t  u8Len;

        if (s == NULL)
        {
            continue;
        }

        u32Total += s->u32RxGood;
        u32Err   += s->u32RxCrcErr;

        if (s->u32RxGood == 0U)
        {
            lv_bar_set_value(sRow[i].pObjRssiBar, GUI_RSSI_MIN, LV_ANIM_OFF);
            lv_bar_set_value(sRow[i].pObjSizeBar, 0, LV_ANIM_OFF);
            lv_label_set_text(sRow[i].pObjRssiTxt, "--");
            lv_obj_set_style_text_color(sRow[i].pObjRssiTxt, lv_color_hex(GUI_COL_DIM), LV_PART_MAIN);
            lv_label_set_text(sRow[i].pObjSubTxt, "no packets");
            continue;
        }

        u32Colour = Fn_GUI_Rssi_Colour(s->i16LastRssi);

        lv_bar_set_value(sRow[i].pObjRssiBar, (int32_t)s->i16LastRssi, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(sRow[i].pObjRssiBar, lv_color_hex(u32Colour), LV_PART_INDICATOR);

        /* u8ReceivePacketSize is the last payload length the core kept. */
        osMutexWait(mtx_configHandle, osWaitForever);
        u8Len = sLoRaGtwConfParam.sLoRaConf[i].u8ReceivePacketSize;
        osMutexRelease(mtx_configHandle);

        lv_bar_set_value(sRow[i].pObjSizeBar, (int32_t)u8Len, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(sRow[i].pObjSizeBar, lv_color_hex(u32RadioColour[i]),
                                  LV_PART_INDICATOR);

        lv_label_set_text_fmt(sRow[i].pObjRssiTxt, "%d dBm", (int)s->i16LastRssi);
        lv_obj_set_style_text_color(sRow[i].pObjRssiTxt, lv_color_hex(u32Colour), LV_PART_MAIN);

        lv_label_set_text_fmt(sRow[i].pObjSubTxt, "%u B  SNR %d  %lu pkt",
                              (unsigned)u8Len, (int)s->i8LastSnr,
                              (unsigned long)s->u32RxGood);
    }

    lv_label_set_text_fmt(pObjHdr, "%lu packets   %lu CRC errors",
                          (unsigned long)u32Total, (unsigned long)u32Err);
}
