/*
 * lvgl_port.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See lvgl_port.h.
 */

#include "lvgl_port.h"
#include "ili9341.h"
#include "xpt2046.h"
#include "lvgl.h"

/* Two partial buffers so LVGL can render one while the other is in flight.
 * These are DMA targets, so they must stay in normal SRAM: the F4's CCM
 * at 0x10000000 is not reachable by DMA. */
static lv_color_t sDrawBuf1[ILI9341_WIDTH * LVGL_DRAW_BUF_LINES];
static lv_color_t sDrawBuf2[ILI9341_WIDTH * LVGL_DRAW_BUF_LINES];

static lv_disp_draw_buf_t sDrawBufDsc;
static lv_disp_drv_t      sDispDrv;
static lv_indev_drv_t     sIndevDrv;

static lv_disp_drv_t *psActiveDrv;

/*------------------------------------------------------------ DMA complete ----------------------------------------------------------
 * Runs in the SPI5 DMA interrupt. lv_disp_flush_ready() is explicitly
 * documented as safe to call from an interrupt.
 */
static void _Fn_LvglPort_Flush_Done (void)
{
    if (psActiveDrv != NULL)
    {
        lv_disp_drv_t *p = psActiveDrv;
        psActiveDrv = NULL;
        lv_disp_flush_ready(p);
    }
}

/*------------------------------------------------------------ Display flush ---------------------------------------------------------
 * LV_COLOR_16_SWAP is 1 in lv_conf.h, so lv_color_t already holds the
 * big endian RGB565 the ILI9341 expects and the buffer can go straight
 * out as bytes.
 */
static void _Fn_LvglPort_Flush (lv_disp_drv_t *psDrv, const lv_area_t *psArea, lv_color_t *psColour)
{
    uint32_t u32Px;

    u32Px = (uint32_t)(psArea->x2 - psArea->x1 + 1) * (uint32_t)(psArea->y2 - psArea->y1 + 1);

    Fn_ILI9341_Set_Window((uint16_t)psArea->x1, (uint16_t)psArea->y1,
                          (uint16_t)psArea->x2, (uint16_t)psArea->y2);

    psActiveDrv = psDrv;

    if (!Fn_ILI9341_Flush_DMA((const uint8_t *)psColour, u32Px * 2U))
    {
        /* Could not start the transfer: do not strand LVGL waiting. */
        psActiveDrv = NULL;
        lv_disp_flush_ready(psDrv);
    }
}

/*------------------------------------------------------------ Touch input -----------------------------------------------------------*/
static lv_coord_t sLastX;
static lv_coord_t sLastY;
static volatile bool bLastPressed;

bool Fn_LVGL_Port_Get_Touch (int16_t *pi16X, int16_t *pi16Y)
{
    if (pi16X != NULL) { *pi16X = (int16_t)sLastX; }
    if (pi16Y != NULL) { *pi16Y = (int16_t)sLastY; }

    return bLastPressed;
}

static void _Fn_LvglPort_Touch_Read (lv_indev_drv_t *psDrv, lv_indev_data_t *psData)
{
    uint16_t u16X;
    uint16_t u16Y;

    LV_UNUSED(psDrv);

    if (Fn_XPT2046_Read(&u16X, &u16Y))
    {
        sLastX = (lv_coord_t)u16X;
        sLastY = (lv_coord_t)u16Y;
        bLastPressed  = true;
        psData->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        bLastPressed  = false;
        psData->state = LV_INDEV_STATE_RELEASED;
    }

    /* LVGL wants the last known point on release too. */
    psData->point.x = sLastX;
    psData->point.y = sLastY;
}

/*------------------------------------------------------------ Init ------------------------------------------------------------------*/
void Fn_LVGL_Port_Init (void)
{
    Fn_ILI9341_Init();
    Fn_XPT2046_Init();

    lv_init();

    lv_disp_draw_buf_init(&sDrawBufDsc, sDrawBuf1, sDrawBuf2,
                          ILI9341_WIDTH * LVGL_DRAW_BUF_LINES);

    lv_disp_drv_init(&sDispDrv);
    sDispDrv.hor_res  = ILI9341_WIDTH;
    sDispDrv.ver_res  = ILI9341_HEIGHT;
    sDispDrv.flush_cb = _Fn_LvglPort_Flush;
    sDispDrv.draw_buf = &sDrawBufDsc;
    (void)lv_disp_drv_register(&sDispDrv);

    Fn_ILI9341_Set_Flush_Done_Cb(_Fn_LvglPort_Flush_Done);

    lv_indev_drv_init(&sIndevDrv);
    sIndevDrv.type    = LV_INDEV_TYPE_POINTER;
    sIndevDrv.read_cb = _Fn_LvglPort_Touch_Read;
    (void)lv_indev_drv_register(&sIndevDrv);
}
