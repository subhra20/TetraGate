/*
 * gui_log.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Communication log for all four channels on one page.
 *
 *  Traffic arrives interleaved across four radios, so the log is one
 *  chronological list rather than four separate ones - that is the only
 *  way to see that two channels heard the same node, or that one has
 *  gone quiet while the others carry on. Each line is tagged with its
 *  channel in that channel's colour, the same colour the Signal screen
 *  uses, so a channel can be picked out without reading the tag.
 *
 *  The counters along the top give the per-channel totals the single
 *  list cannot show at a glance.
 */

#include "gui_common.h"
#include "radio_manager.h"
#include "packet_log.h"

#define LOG_CHIP_Y          0
#define LOG_CHIP_H          24
#define LOG_CHIP_W          74
#define LOG_LIST_Y          28
#define LOG_LINES           10
#define LOG_BUF_BYTES       768

static lv_obj_t *pObjChip[DEF_MAX_LoRa_Module];
static lv_obj_t *pObjList;
static lv_obj_t *pObjFoot;

void Fn_GUI_Log_Build (lv_obj_t *pObjParent)
{
    uint8_t i;

    lv_obj_clear_flag(pObjParent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pObjParent, GUI_PAD, LV_PART_MAIN);

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        lv_obj_t *pObjCard = Fn_GUI_Card(pObjParent, (lv_coord_t)(i * (LOG_CHIP_W + 4)),
                                         LOG_CHIP_Y, LOG_CHIP_W, LOG_CHIP_H, GUI_COL_CARD);

        /* A stripe in the channel's colour, so the chip is identifiable
         * even where the text is too small to read. */
        (void)Fn_GUI_Card(pObjCard, 0, 0, 4, LOG_CHIP_H, u32RadioColour[i]);

        pObjChip[i] = Fn_GUI_Label(pObjCard, 9, 4, u32RadioColour[i], NULL, "R0 0");
    }

    pObjList = lv_label_create(pObjParent);
    lv_obj_set_pos(pObjList, 2, LOG_LIST_Y);
    lv_obj_set_width(pObjList, 308);
    lv_label_set_recolor(pObjList, true);
    lv_obj_set_style_text_color(pObjList, lv_color_hex(GUI_COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_line_space(pObjList, 2, LV_PART_MAIN);
    lv_label_set_text(pObjList, "no traffic yet");

    pObjFoot = Fn_GUI_Label(pObjParent, 2, 192, GUI_COL_DIM, NULL, "");
}

void Fn_GUI_Log_Refresh (void)
{
    static char chBuf[LOG_BUF_BYTES];
    StructPktEntry sE;
    uint32_t u32Now = osKernelSysTick();
    uint32_t u32Dup = 0U;
    uint32_t u32Blk = 0U;
    uint16_t u16Have = Fn_PktLog_Count();
    uint16_t u16Show = (u16Have > LOG_LINES) ? LOG_LINES : u16Have;
    uint16_t i;
    int iPos = 0;

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        const StructRadioStats *s = Fn_Radio_Get_Stats(i);

        if (s == NULL)
        {
            continue;
        }

        u32Dup += s->u32RxDuplicate;
        u32Blk += s->u32RxBlocked;

        lv_label_set_text_fmt(pObjChip[i], "R%u %lu",
                              (unsigned)(i + 1U), (unsigned long)s->u32RxGood);
    }

    if (u16Show == 0U)
    {
        lv_label_set_text(pObjList, "no traffic yet");
    }
    else
    {
        chBuf[0] = '\0';

        for (i = 0U; i < u16Show; i++)
        {
            uint32_t u32AgeS;

            if (!Fn_PktLog_Get(i, &sE))
            {
                break;
            }

            u32AgeS = (u32Now - sE.u32Tick) / 1000U;
            if (u32AgeS > 999U)
            {
                u32AgeS = 999U;
            }

            /* The colour marker tags the channel; the rest stays in the
             * default text colour so the tag is what stands out. */
            iPos += snprintf(&chBuf[iPos], (size_t)(LOG_BUF_BYTES - iPos),
                             "#%06lX R%u# %3lus %3uB %4d %+3d %02X%02X\n",
                             (unsigned long)u32RadioColour[sE.u8Radio],
                             (unsigned)(sE.u8Radio + 1U),
                             (unsigned long)u32AgeS,
                             (unsigned)sE.u8Len,
                             (int)sE.i16Rssi,
                             (int)sE.i8Snr,
                             sE.u8Data[0], sE.u8Data[1]);

            if (iPos >= (int)(LOG_BUF_BYTES - 64))
            {
                break;
            }
        }

        lv_label_set_text(pObjList, chBuf);
    }

    lv_label_set_text_fmt(pObjFoot, "%lu total   %lu duplicate   %lu blocked",
                          (unsigned long)Fn_PktLog_Total(),
                          (unsigned long)u32Dup, (unsigned long)u32Blk);
}
