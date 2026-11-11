/*
 * gui_manager.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Navigation, the Home dashboard, the Nodes list, the System screen and
 *  the touch calibration flow. The Radios, Signal and Log screens live in
 *  their own files and are built and refreshed through gui_common.h.
 *
 *  Only the visible tab is refreshed. Redrawing six screens at 400 ms
 *  would spend most of the UI task's time on pixels nobody is looking at.
 *
 *  The GUI reads gateway state and never touches a radio or a bus.
 */

#include "gui_common.h"
#include "gui_manager.h"
#include "radio_manager.h"
#include "packet_log.h"
#include "node_table.h"
#include "eeprom_operation.h"
#include "xpt2046.h"
#include "monitor.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"

/*------------------------------------------------------------ Tab indices ----------------------------------------------------------*/
#define TAB_HOME        0U
#define TAB_RADIOS      1U
#define TAB_SIGNAL      2U
#define TAB_LOG         3U
#define TAB_NODES       4U
#define TAB_SYSTEM      5U

#define HOME_CARD_W     154
#define HOME_CARD_H     92

static lv_obj_t      *pObjTabview;
static StructCalDebug sCalDbg;

/* Home */
typedef struct
{
    lv_obj_t *pObjName;
    lv_obj_t *pObjFreq;
    lv_obj_t *pObjModem;
    lv_obj_t *pObjStats;
    lv_obj_t *pObjDot;
} StructHomeCard;

static StructHomeCard sHome[DEF_MAX_LoRa_Module];
static lv_obj_t      *pObjBanner;

/* Nodes */
static lv_obj_t *pObjNodeHdr;
static lv_obj_t *pObjNodeLbl;

/* System */
static lv_obj_t *pObjSysDevice;
static lv_obj_t *pObjSysHeapBar;
static lv_obj_t *pObjSysHeapTxt;
static lv_obj_t *pObjSysStacks;
static lv_obj_t *pObjSysLvgl;
static lv_obj_t *pObjSysRadios;
static lv_obj_t *pObjSysWdt;
static lv_obj_t *pObjSysStore;

extern osThreadId Task_LoRa_1Handle;
extern osThreadId Task_LoRa_2Handle;
extern osThreadId Task_LoRa_3Handle;
extern osThreadId Task_LoRa_4Handle;
extern osThreadId Task_LVGLHandle;

static const uint16_t u16BwKhzTable[] = { 8U, 10U, 16U, 21U, 31U, 42U, 63U, 125U, 250U, 500U };

void Fn_GUI_Get_Cal_Debug (StructCalDebug *psDbg)
{
    if (psDbg != NULL)
    {
        *psDbg = sCalDbg;
    }
}

/*============================================================ Home ==================================================================*/
static void _Fn_Home_Build (lv_obj_t *pObjParent)
{
    uint8_t i;

    lv_obj_clear_flag(pObjParent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pObjParent, GUI_PAD, LV_PART_MAIN);

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        lv_coord_t x = (lv_coord_t)((i % 2U) * (HOME_CARD_W + 4));
        lv_coord_t y = (lv_coord_t)((i / 2U) * (HOME_CARD_H + 4));
        lv_obj_t *pObjCard = Fn_GUI_Card(pObjParent, x, y, HOME_CARD_W, HOME_CARD_H, GUI_COL_CARD);
        char chName[4];

        (void)Fn_GUI_Card(pObjCard, 0, 0, 4, HOME_CARD_H, u32RadioColour[i]);

        (void)snprintf(chName, sizeof(chName), "R%u", (unsigned)(i + 1U));
        sHome[i].pObjName = Fn_GUI_Label(pObjCard, 10, 6, GUI_COL_TEXT,
                                         &lv_font_montserrat_16, chName);

        sHome[i].pObjDot = Fn_GUI_Card(pObjCard, HOME_CARD_W - 18, 10, 10, 10, GUI_COL_OFF);
        lv_obj_set_style_radius(sHome[i].pObjDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);

        sHome[i].pObjFreq  = Fn_GUI_Label(pObjCard, 10, 28, GUI_COL_TEXT, NULL, "--");
        sHome[i].pObjModem = Fn_GUI_Label(pObjCard, 10, 48, GUI_COL_DIM,  NULL, "--");
        sHome[i].pObjStats = Fn_GUI_Label(pObjCard, 10, 68, GUI_COL_DIM,  NULL, "--");
    }

    pObjBanner = Fn_GUI_Label(pObjParent, 2, 194, GUI_COL_WARN, NULL, "");
    lv_obj_add_flag(pObjBanner, LV_OBJ_FLAG_HIDDEN);
}

static void _Fn_Home_Refresh (void)
{
    StructLoRaConf sConf;
    uint8_t  u8Faults = 0U;
    uint8_t  i;
    char     chFreq[20];

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        const StructRadioStats *s = Fn_Radio_Get_Stats(i);
        uint32_t u32Colour;

        osMutexWait(mtx_configHandle, osWaitForever);
        sConf = sLoRaGtwConfParam.sLoRaConf[i];
        osMutexRelease(mtx_configHandle);

        if (s == NULL)
        {
            continue;
        }

        Fn_GUI_Format_MHz(sConf.u32FreqHz, chFreq, sizeof(chFreq));
        lv_label_set_text_fmt(sHome[i].pObjFreq, "%s MHz", chFreq);

        lv_label_set_text_fmt(sHome[i].pObjModem, "SF%u  BW%u",
                              (unsigned)sConf.enLoRaSF,
                              (unsigned)((sConf.enLoRaBW < en_LoRa_BW_Not_Valid) ?
                                         u16BwKhzTable[sConf.enLoRaBW] : 0U));

        if (s->u32RxGood > 0U)
        {
            lv_label_set_text_fmt(sHome[i].pObjStats, "%lu pkt  %d dBm",
                                  (unsigned long)s->u32RxGood, (int)s->i16LastRssi);
        }
        else
        {
            lv_label_set_text(sHome[i].pObjStats, "no packets");
        }

        if (sConf.enLoRaSts == en_LoRa_Disabled)
        {
            u32Colour = GUI_COL_OFF;
        }
        else if ((sConf.enLoRaSts == en_LoRa_init_Success) && s->bHealthy)
        {
            u32Colour = GUI_COL_OK;
        }
        else
        {
            u32Colour = GUI_COL_BAD;
            u8Faults++;
        }

        lv_obj_set_style_bg_color(sHome[i].pObjDot, lv_color_hex(u32Colour), LV_PART_MAIN);
    }

    if (u8Faults > 0U)
    {
        lv_label_set_text_fmt(pObjBanner, "%u radio(s) not responding", (unsigned)u8Faults);
        lv_obj_clear_flag(pObjBanner, LV_OBJ_FLAG_HIDDEN);
    }
    else if (Fn_NodeTable_Silent_Count() > 0U)
    {
        lv_label_set_text_fmt(pObjBanner, "%u node(s) silent",
                              (unsigned)Fn_NodeTable_Silent_Count());
        lv_obj_clear_flag(pObjBanner, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(pObjBanner, LV_OBJ_FLAG_HIDDEN);
    }
}

/*============================================================ Nodes =================================================================*/
static void _Fn_Nodes_Build (lv_obj_t *pObjParent)
{
    lv_obj_clear_flag(pObjParent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pObjParent, GUI_PAD, LV_PART_MAIN);

    pObjNodeHdr = Fn_GUI_Label(pObjParent, 2, 0, GUI_COL_DIM, NULL, "no nodes heard yet");

    pObjNodeLbl = lv_label_create(pObjParent);
    lv_obj_set_pos(pObjNodeLbl, 2, 22);
    lv_label_set_recolor(pObjNodeLbl, true);
    lv_obj_set_style_text_color(pObjNodeLbl, lv_color_hex(GUI_COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_line_space(pObjNodeLbl, 2, LV_PART_MAIN);
    lv_label_set_text(pObjNodeLbl, "");
}

static void _Fn_Nodes_Refresh (void)
{
    static char chBuf[600];
    StructNode sN;
    uint32_t u32Now   = osKernelSysTick();
    uint16_t u16Count = Fn_NodeTable_Count();
    uint16_t u16Show  = (u16Count > 9U) ? 9U : u16Count;
    uint16_t u16Wl    = Fn_NodeTable_Whitelist_Count();
    uint16_t i;
    int iPos = 0;

    lv_label_set_text_fmt(pObjNodeHdr, "%u nodes   %u silent   whitelist %s",
                          (unsigned)u16Count, (unsigned)Fn_NodeTable_Silent_Count(),
                          (u16Wl == 0U) ? "off" : "on");

    if (u16Count == 0U)
    {
        lv_label_set_text(pObjNodeLbl, "no nodes heard yet");
        return;
    }

    chBuf[0] = '\0';

    for (i = 0U; i < u16Show; i++)
    {
        uint32_t u32AgeS;

        if (!Fn_NodeTable_Get(i, &sN))
        {
            break;
        }

        u32AgeS = (u32Now - sN.u32LastSeenMs) / 1000U;

        iPos += snprintf(&chBuf[iPos], sizeof(chBuf) - (size_t)iPos,
                         "#%06lX R%u# %5u %4lus %4lup %4d%s\n",
                         (unsigned long)u32RadioColour[sN.u8Radio],
                         (unsigned)(sN.u8Radio + 1U),
                         (unsigned)sN.u16NodeId,
                         (unsigned long)u32AgeS,
                         (unsigned long)sN.u32Packets,
                         (int)sN.i16LastRssi,
                         sN.bSilent ? " !" : "");

        if (iPos >= (int)(sizeof(chBuf) - 64U))
        {
            break;
        }
    }

    lv_label_set_text(pObjNodeLbl, chBuf);
}

/*============================================================ System ================================================================
 * Grouped into Device, Memory and Health, because the question an
 * operator brings to this screen is one of those three, never all of it.
 */
static void _Fn_System_Build (lv_obj_t *pObjParent)
{
    lv_obj_t *pObjCard;

    lv_obj_clear_flag(pObjParent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pObjParent, GUI_PAD, LV_PART_MAIN);

    (void)Fn_GUI_Label(pObjParent, 2, 0, GUI_COL_ACCENT, NULL, "DEVICE");
    pObjCard = Fn_GUI_Card(pObjParent, 0, 16, 312, 40, GUI_COL_CARD);
    pObjSysDevice = Fn_GUI_Label(pObjCard, 8, 3, GUI_COL_TEXT, NULL, "--");

    (void)Fn_GUI_Label(pObjParent, 2, 58, GUI_COL_ACCENT, NULL, "MEMORY");
    pObjCard = Fn_GUI_Card(pObjParent, 0, 74, 312, 56, GUI_COL_CARD);

    pObjSysHeapBar = lv_bar_create(pObjCard);
    lv_obj_set_pos(pObjSysHeapBar, 8, 6);
    lv_obj_set_size(pObjSysHeapBar, 150, 10);
    lv_obj_set_style_bg_color(pObjSysHeapBar, lv_color_hex(GUI_COL_BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pObjSysHeapBar, lv_color_hex(GUI_COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(pObjSysHeapBar, 3, LV_PART_MAIN);
    lv_bar_set_range(pObjSysHeapBar, 0, (int32_t)configTOTAL_HEAP_SIZE);

    pObjSysHeapTxt = Fn_GUI_Label(pObjCard, 166, 1, GUI_COL_DIM, NULL, "--");
    pObjSysStacks  = Fn_GUI_Label(pObjCard, 8, 20, GUI_COL_DIM, NULL, "--");
    pObjSysLvgl    = Fn_GUI_Label(pObjCard, 8, 37, GUI_COL_DIM, NULL, "--");

    (void)Fn_GUI_Label(pObjParent, 2, 132, GUI_COL_ACCENT, NULL, "HEALTH");
    pObjCard = Fn_GUI_Card(pObjParent, 0, 148, 312, 54, GUI_COL_CARD);
    pObjSysRadios = Fn_GUI_Label(pObjCard, 8, 3,  GUI_COL_TEXT, NULL, "--");
    pObjSysWdt    = Fn_GUI_Label(pObjCard, 8, 20, GUI_COL_DIM,  NULL, "--");
    pObjSysStore  = Fn_GUI_Label(pObjCard, 8, 36, GUI_COL_DIM,  NULL, "--");
    lv_label_set_recolor(pObjSysRadios, true);
}

static void _Fn_System_Refresh (void)
{
    static const char * const pchReset[] =
    {
        "unknown", "power-on", "pin", "software", "IWDG", "WWDG", "low-power", "brown-out"
    };
    char chRadios[112];
    uint32_t u32Up    = osKernelSysTick() / 1000U;
    uint32_t u32Boots = 0U;
    uint32_t u32Free  = (uint32_t)xPortGetFreeHeapSize();
    uint32_t u32Used  = (uint32_t)configTOTAL_HEAP_SIZE - u32Free;
    uint32_t u32Stall = Fn_Monitor_Get_Stall_Mask();
    uint8_t  u8Reason = 0U;
    uint8_t  i;
    int iPos = 0;

    Fn_EEPROM_Get_Boot_Info(&u32Boots, &u8Reason);
    if (u8Reason >= (uint8_t)(sizeof(pchReset) / sizeof(pchReset[0])))
    {
        u8Reason = 0U;
    }

    lv_label_set_text_fmt(pObjSysDevice, "Up %lu:%02lu:%02lu    Boot #%lu\nReset: %s    LVGL %u.%u.%u",
                          (unsigned long)(u32Up / 3600U),
                          (unsigned long)((u32Up / 60U) % 60U),
                          (unsigned long)(u32Up % 60U),
                          (unsigned long)u32Boots,
                          pchReset[u8Reason],
                          (unsigned)LVGL_VERSION_MAJOR, (unsigned)LVGL_VERSION_MINOR,
                          (unsigned)LVGL_VERSION_PATCH);

    lv_bar_set_value(pObjSysHeapBar, (int32_t)u32Used, LV_ANIM_OFF);
    lv_label_set_text_fmt(pObjSysHeapTxt, "%lu / %lu B",
                          (unsigned long)u32Used, (unsigned long)configTOTAL_HEAP_SIZE);

    lv_label_set_text_fmt(pObjSysStacks, "Stack free  LoRa %u %u %u %u   UI %u",
                          (unsigned)uxTaskGetStackHighWaterMark((TaskHandle_t)Task_LoRa_1Handle),
                          (unsigned)uxTaskGetStackHighWaterMark((TaskHandle_t)Task_LoRa_2Handle),
                          (unsigned)uxTaskGetStackHighWaterMark((TaskHandle_t)Task_LoRa_3Handle),
                          (unsigned)uxTaskGetStackHighWaterMark((TaskHandle_t)Task_LoRa_4Handle),
                          (unsigned)uxTaskGetStackHighWaterMark((TaskHandle_t)Task_LVGLHandle));

    /* LVGL's own pool is separate from the FreeRTOS heap and is what
     * runs out first when screens gain widgets: worth watching. */
    {
        lv_mem_monitor_t sMon;

        lv_mem_monitor(&sMon);
        lv_label_set_text_fmt(pObjSysLvgl, "LVGL pool  %u%% used   %u%% frag   free %lu B",
                              (unsigned)sMon.used_pct, (unsigned)sMon.frag_pct,
                              (unsigned long)sMon.free_size);
    }

    /* One coloured chip per radio: green healthy, grey off, red faulty. */
    chRadios[0] = '\0';
    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        const StructRadioStats *s = Fn_Radio_Get_Stats(i);
        EnumLoRaSts enSts;
        uint32_t u32Colour;

        osMutexWait(mtx_configHandle, osWaitForever);
        enSts = sLoRaGtwConfParam.sLoRaConf[i].enLoRaSts;
        osMutexRelease(mtx_configHandle);

        if (enSts == en_LoRa_Disabled)
        {
            u32Colour = GUI_COL_OFF;
        }
        else if ((enSts == en_LoRa_init_Success) && (s != NULL) && s->bHealthy)
        {
            u32Colour = GUI_COL_OK;
        }
        else
        {
            u32Colour = GUI_COL_BAD;
        }

        iPos += snprintf(&chRadios[iPos], sizeof(chRadios) - (size_t)iPos,
                         "#%06lX R%u# ", (unsigned long)u32Colour, (unsigned)(i + 1U));
    }

    lv_label_set_text(pObjSysRadios, chRadios);

    lv_label_set_text_fmt(pObjSysWdt, "Watchdog  %lu feeds   stall 0x%02X   %s",
                          (unsigned long)Fn_Monitor_Get_Feed_Count(),
                          (unsigned)u32Stall,
                          (u32Stall == 0U) ? "all tasks alive" : "TASK STALLED");

    lv_label_set_text_fmt(pObjSysStore, "Config slot %c   touch calibration stored",
                          (Fn_EEPROM_Get_Active_Slot() == 0U) ? 'A' : 'B');
}

/*============================================================ Touch calibration =====================================================*/
#define CAL_INSET               20
#define CAL_POINT_TIMEOUT_MS    30000U
#define CAL_AVG_SAMPLES         16U
#define CAL_EDGE_TOL            800U

typedef struct
{
    uint16_t u16X;
    uint16_t u16Y;
} StructRawPt;

static uint16_t _Fn_Cal_Diff (uint16_t a, uint16_t b)
{
    return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static void _Fn_Cal_Pump (void)
{
    Fn_Monitor_Checkin(en_Mon_LVGL);
    (void)lv_timer_handler();
    osDelay(10);
}

static void _Fn_Cal_Wait_Release (void)
{
    uint16_t u16X;
    uint16_t u16Y;
    uint32_t u32QuietMs = 0U;

    while (u32QuietMs < 250U)
    {
        if (Fn_XPT2046_Read_Raw(&u16X, &u16Y, NULL))
        {
            u32QuietMs = 0U;
        }
        else
        {
            u32QuietMs += 10U;
        }
        _Fn_Cal_Pump();
    }
}

static bool _Fn_Cal_Wait_Press (StructRawPt *psPt)
{
    uint16_t u16X;
    uint16_t u16Y;
    uint32_t u32SumX = 0U;
    uint32_t u32SumY = 0U;
    uint32_t u32Deadline = HAL_GetTick() + CAL_POINT_TIMEOUT_MS;
    uint8_t  u8N = 0U;

    while (!Fn_XPT2046_Read_Raw(&u16X, &u16Y, NULL))
    {
        if (HAL_GetTick() > u32Deadline)
        {
            return false;
        }
        _Fn_Cal_Pump();
    }

    osDelay(120);

    while ((u8N < CAL_AVG_SAMPLES) && (HAL_GetTick() <= u32Deadline))
    {
        if (Fn_XPT2046_Read_Raw(&u16X, &u16Y, NULL))
        {
            u32SumX += u16X;
            u32SumY += u16Y;
            u8N++;
        }
        osDelay(10);
    }

    if (u8N == 0U)
    {
        return false;
    }

    psPt->u16X = (uint16_t)(u32SumX / u8N);
    psPt->u16Y = (uint16_t)(u32SumY / u8N);

    return true;
}

static void _Fn_Cal_Extrapolate (uint16_t *pu16Min, uint16_t *pu16Max, uint16_t u16Span)
{
    uint32_t u32Range = (uint32_t)(*pu16Max - *pu16Min);
    uint32_t u32Inner = (uint32_t)(u16Span - 1U - (2U * CAL_INSET));
    uint32_t u32Ext;

    if (u32Inner == 0U)
    {
        return;
    }

    u32Ext = (u32Range * (uint32_t)CAL_INSET) / u32Inner;

    *pu16Min = (uint16_t)((*pu16Min > u32Ext) ? (*pu16Min - u32Ext) : 0U);
    *pu16Max = (uint16_t)(((uint32_t)*pu16Max + u32Ext > 4095U) ? 4095U : (*pu16Max + u32Ext));
}

bool Fn_GUI_Calibrate (void)
{
    static const lv_coord_t sCx[4] =
    {
        CAL_INSET, GUI_W - 1 - CAL_INSET, GUI_W - 1 - CAL_INSET, CAL_INSET
    };
    static const lv_coord_t sCy[4] =
    {
        CAL_INSET, CAL_INSET, GUI_H - 1 - CAL_INSET, GUI_H - 1 - CAL_INSET
    };
    static const char * const pchPrompt[4] =
    {
        "top left", "top right", "bottom right", "bottom left"
    };

    StructRawPt sPt[4];
    StructTouchCal sNewCal;
    lv_obj_t *pObjScr;
    lv_obj_t *pObjMsg;
    lv_obj_t *pObjVert;
    lv_obj_t *pObjHorz;
    uint16_t u16A[4];
    uint16_t u16B[4];
    uint16_t u16AMin;
    uint16_t u16AMax;
    uint16_t u16BMin;
    uint16_t u16BMax;
    uint16_t u16Tmp;
    bool bSwap;
    bool bInvX;
    bool bInvY;
    uint8_t i;

    (void)memset(&sCalDbg, 0, sizeof(sCalDbg));
    sCalDbg.enResult = en_Cal_Timeout;

    pObjScr = lv_scr_act();
    lv_obj_set_style_bg_color(pObjScr, lv_color_hex(GUI_COL_BG), LV_PART_MAIN);
    lv_obj_clear_flag(pObjScr, LV_OBJ_FLAG_SCROLLABLE);

    pObjMsg = lv_label_create(pObjScr);
    lv_obj_set_style_text_color(pObjMsg, lv_color_hex(GUI_COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(pObjMsg, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(pObjMsg, LV_ALIGN_CENTER, 0, 0);

    pObjVert = lv_obj_create(pObjScr);
    lv_obj_set_size(pObjVert, 2, 22);
    lv_obj_set_style_border_width(pObjVert, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pObjVert, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pObjVert, lv_color_hex(GUI_COL_WARN), LV_PART_MAIN);

    pObjHorz = lv_obj_create(pObjScr);
    lv_obj_set_size(pObjHorz, 22, 2);
    lv_obj_set_style_border_width(pObjHorz, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pObjHorz, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pObjHorz, lv_color_hex(GUI_COL_WARN), LV_PART_MAIN);

    for (i = 0U; i < 4U; i++)
    {
        lv_label_set_text_fmt(pObjMsg, "Touch the %s crosshair\n(%u of 4)",
                              pchPrompt[i], (unsigned)(i + 1U));
        lv_obj_set_pos(pObjVert, (lv_coord_t)(sCx[i] - 1), (lv_coord_t)(sCy[i] - 11));
        lv_obj_set_pos(pObjHorz, (lv_coord_t)(sCx[i] - 11), (lv_coord_t)(sCy[i] - 1));

        _Fn_Cal_Wait_Release();

        if (!_Fn_Cal_Wait_Press(&sPt[i]))
        {
            lv_obj_clean(pObjScr);
            return false;
        }

        sCalDbg.u16RawX[i] = sPt[i].u16X;
        sCalDbg.u16RawY[i] = sPt[i].u16Y;
        sCalDbg.u8Points   = (uint8_t)(i + 1U);

        lv_label_set_text(pObjMsg, "Got it - release");
    }

    _Fn_Cal_Wait_Release();
    lv_obj_clean(pObjScr);

    bSwap = (_Fn_Cal_Diff(sPt[1].u16Y, sPt[0].u16Y) > _Fn_Cal_Diff(sPt[1].u16X, sPt[0].u16X));

    for (i = 0U; i < 4U; i++)
    {
        u16A[i] = bSwap ? sPt[i].u16Y : sPt[i].u16X;
        u16B[i] = bSwap ? sPt[i].u16X : sPt[i].u16Y;
    }

    sCalDbg.bSwap      = bSwap;
    sCalDbg.u16Edge[0] = _Fn_Cal_Diff(u16A[0], u16A[3]);
    sCalDbg.u16Edge[1] = _Fn_Cal_Diff(u16A[1], u16A[2]);
    sCalDbg.u16Edge[2] = _Fn_Cal_Diff(u16B[0], u16B[1]);
    sCalDbg.u16Edge[3] = _Fn_Cal_Diff(u16B[2], u16B[3]);

    if ((sCalDbg.u16Edge[0] > CAL_EDGE_TOL) || (sCalDbg.u16Edge[1] > CAL_EDGE_TOL) ||
        (sCalDbg.u16Edge[2] > CAL_EDGE_TOL) || (sCalDbg.u16Edge[3] > CAL_EDGE_TOL))
    {
        sCalDbg.enResult = en_Cal_EdgeMismatch;
        return false;
    }

    u16AMin = (uint16_t)(((uint32_t)u16A[0] + u16A[3]) / 2U);
    u16AMax = (uint16_t)(((uint32_t)u16A[1] + u16A[2]) / 2U);
    u16BMin = (uint16_t)(((uint32_t)u16B[0] + u16B[1]) / 2U);
    u16BMax = (uint16_t)(((uint32_t)u16B[2] + u16B[3]) / 2U);

    bInvX = (u16AMin > u16AMax);
    if (bInvX) { u16Tmp = u16AMin; u16AMin = u16AMax; u16AMax = u16Tmp; }

    bInvY = (u16BMin > u16BMax);
    if (bInvY) { u16Tmp = u16BMin; u16BMin = u16BMax; u16BMax = u16Tmp; }

    sCalDbg.u16SpreadA = _Fn_Cal_Diff(u16AMax, u16AMin);
    sCalDbg.u16SpreadB = _Fn_Cal_Diff(u16BMax, u16BMin);

    if ((sCalDbg.u16SpreadA < 1500U) || (sCalDbg.u16SpreadB < 1500U))
    {
        sCalDbg.enResult = en_Cal_SpreadTooSmall;
        return false;
    }

    _Fn_Cal_Extrapolate(&u16AMin, &u16AMax, GUI_W);
    _Fn_Cal_Extrapolate(&u16BMin, &u16BMax, GUI_H);

    sNewCal.bSwapXY  = bSwap;
    sNewCal.bInvertX = bInvX;
    sNewCal.bInvertY = bInvY;

    if (!bSwap)
    {
        sNewCal.u16RawXMin = u16AMin;  sNewCal.u16RawXMax = u16AMax;
        sNewCal.u16RawYMin = u16BMin;  sNewCal.u16RawYMax = u16BMax;
    }
    else
    {
        sNewCal.u16RawYMin = u16AMin;  sNewCal.u16RawYMax = u16AMax;
        sNewCal.u16RawXMin = u16BMin;  sNewCal.u16RawXMax = u16BMax;
    }

    Fn_XPT2046_Set_Calibration(&sNewCal);
    sCalDbg.enResult = en_Cal_OK;

    return true;
}

/*============================================================ Navigation ============================================================*/
static void _Fn_Gui_Refresh (lv_timer_t *psTimer)
{
    uint16_t u16Tab;

    LV_UNUSED(psTimer);

    u16Tab = (pObjTabview != NULL) ? lv_tabview_get_tab_act(pObjTabview) : TAB_HOME;

    switch (u16Tab)
    {
        case TAB_RADIOS: Fn_GUI_Radios_Refresh(); break;
        case TAB_SIGNAL: Fn_GUI_Signal_Refresh(); break;
        case TAB_LOG:    Fn_GUI_Log_Refresh();    break;
        case TAB_NODES:  _Fn_Nodes_Refresh();     break;
        case TAB_SYSTEM: _Fn_System_Refresh();    break;
        case TAB_HOME:
        default:         _Fn_Home_Refresh();      break;
    }
}

void Fn_GUI_Init (void)
{
    lv_obj_t *pObjTabBar;

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(GUI_COL_BG), LV_PART_MAIN);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    pObjTabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, GUI_TABBAR_H);
    lv_obj_set_style_bg_color(pObjTabview, lv_color_hex(GUI_COL_BG), LV_PART_MAIN);

    pObjTabBar = lv_tabview_get_tab_btns(pObjTabview);
    lv_obj_set_style_bg_color(pObjTabBar, lv_color_hex(GUI_COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_text_color(pObjTabBar, lv_color_hex(GUI_COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_color(pObjTabBar, lv_color_hex(GUI_COL_TEXT),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(pObjTabBar, lv_color_hex(GUI_COL_CARD_HI),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(pObjTabBar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pObjTabBar, 0, LV_PART_MAIN);

    /* Six short names, because 320 px divided six ways is 53 px a tab. */
    _Fn_Home_Build   (lv_tabview_add_tab(pObjTabview, "Home"));
    Fn_GUI_Radios_Build(lv_tabview_add_tab(pObjTabview, "Radio"));
    Fn_GUI_Signal_Build(lv_tabview_add_tab(pObjTabview, "Signal"));
    Fn_GUI_Log_Build   (lv_tabview_add_tab(pObjTabview, "Log"));
    _Fn_Nodes_Build  (lv_tabview_add_tab(pObjTabview, "Nodes"));
    _Fn_System_Build (lv_tabview_add_tab(pObjTabview, "Sys"));

    (void)lv_timer_create(_Fn_Gui_Refresh, GUI_REFRESH_MS, NULL);
    _Fn_Gui_Refresh(NULL);
}
