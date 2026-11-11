/*
 * gui_common.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See gui_common.h.
 */

#include "gui_common.h"

/* Blue, amber, violet, teal. Distinct at a glance and distinguishable
 * for the common colour vision deficiencies, which matters because the
 * log identifies a channel by colour as well as by name. */
const uint32_t u32RadioColour[DEF_MAX_LoRa_Module] =
{
    0x3B9EFFUL,
    0xF2A33CUL,
    0xA78BFAUL,
    0x2DD4BFUL,
};

uint32_t Fn_GUI_Rssi_Colour (int16_t i16Rssi)
{
    if (i16Rssi >= GUI_RSSI_GOOD)
    {
        return GUI_COL_OK;
    }

    return (i16Rssi >= GUI_RSSI_FAIR) ? GUI_COL_WARN : GUI_COL_BAD;
}

lv_obj_t *Fn_GUI_Card (lv_obj_t *pObjParent, lv_coord_t x, lv_coord_t y,
                       lv_coord_t w, lv_coord_t h, uint32_t u32Colour)
{
    lv_obj_t *pObj = lv_obj_create(pObjParent);

    lv_obj_set_pos(pObj, x, y);
    lv_obj_set_size(pObj, w, h);
    lv_obj_set_style_bg_color(pObj, lv_color_hex(u32Colour), LV_PART_MAIN);
    lv_obj_set_style_border_width(pObj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pObj, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pObj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(pObj, LV_OBJ_FLAG_SCROLLABLE);

    return pObj;
}

lv_obj_t *Fn_GUI_Label (lv_obj_t *pObjParent, lv_coord_t x, lv_coord_t y,
                        uint32_t u32Colour, const lv_font_t *psFont, const char *chText)
{
    lv_obj_t *pObj = lv_label_create(pObjParent);

    lv_obj_set_pos(pObj, x, y);
    lv_obj_set_style_text_color(pObj, lv_color_hex(u32Colour), LV_PART_MAIN);

    if (psFont != NULL)
    {
        lv_obj_set_style_text_font(pObj, psFont, LV_PART_MAIN);
    }

    lv_label_set_text(pObj, (chText != NULL) ? chText : "");

    return pObj;
}

/* Hz to "868.100". Integer only: the build has no printf float support
 * and enabling it for one label would be a poor trade. */
void Fn_GUI_Format_MHz (uint32_t u32Hz, char *chBuf, uint16_t u16Size)
{
    if ((chBuf == NULL) || (u16Size == 0U))
    {
        return;
    }

    (void)snprintf(chBuf, u16Size, "%u.%03u",
                   (unsigned)(u32Hz / 1000000UL),
                   (unsigned)((u32Hz % 1000000UL) / 1000UL));
}
