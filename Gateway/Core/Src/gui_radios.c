/*
 * gui_radios.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Per-radio configuration from the panel: enable, frequency, spreading
 *  factor, bandwidth, coding rate and TX power, with the live status.
 *
 *  Edits are STAGED in a local copy and only reach the gateway on Apply,
 *  so several parameters can be set before the radio is disturbed - one
 *  re-initialisation per Apply instead of one per tap. Apply is live
 *  only; Save also writes the configuration to the EEPROM. That is the
 *  Apply/Save split the architecture document asks for.
 *
 *  The UI never touches a radio. Apply writes the shared structure under
 *  its mutex and raises a reconfigure request; the radio's own task does
 *  the SPI work.
 */

#include "gui_common.h"
#include "radio_manager.h"
#include "eeprom_operation.h"
#include "DeviceConfigJSON.h"
#include "cJSON.h"

/*------------------------------------------------------------ Layout ----------------------------------------------------------------*/
#define RAD_SEL_Y           0
#define RAD_SEL_H           26
#define RAD_SEL_W           74
#define RAD_ROW1_Y          30      /* enable + status   */
#define RAD_ROW2_Y          62      /* frequency         */
#define RAD_ROW3_Y          94      /* SF  | BW          */
#define RAD_ROW4_Y          126     /* CR  | power       */
#define RAD_ROW5_Y          156     /* apply | save      */
#define RAD_HALF_W          152
#define RAD_FULL_W          312
#define RAD_BTN_W           26

#define RAD_FREQ_STEP_HZ    100000UL     /* 100 kHz a tap */

typedef enum
{
    en_Par_Freq = 0,
    en_Par_SF,
    en_Par_BW,
    en_Par_CR,
    en_Par_Pwr,
} EnumParam;

static const uint16_t u16BwKhz[] = { 8U, 10U, 16U, 21U, 31U, 42U, 63U, 125U, 250U, 500U };
static const uint8_t  u8PwrDbm[] = { 11U, 14U, 17U, 20U };

static StructLoRaConf sEdit;            /* staged copy of the selected radio */
static uint8_t        u8Sel;            /* which radio is being edited       */
static bool           bDirty;

static lv_obj_t *pObjSelBtn[DEF_MAX_LoRa_Module];
static lv_obj_t *pObjEnSw;
static lv_obj_t *pObjStatus;
static lv_obj_t *pObjValFreq;
static lv_obj_t *pObjValSF;
static lv_obj_t *pObjValBW;
static lv_obj_t *pObjValCR;
static lv_obj_t *pObjValPwr;
static lv_obj_t *pObjHint;

/*------------------------------------------------------------ Helpers ---------------------------------------------------------------*/
static const char *_Fn_Status_Text (EnumLoRaSts enSts)
{
    switch (enSts)
    {
        case en_LoRa_init_Success: return "running";
        case en_LoRa_Disabled:     return "off";
        case en_LoRa_init_Fail:    return "not found";
        default:                   return "unavailable";
    }
}

static void _Fn_Load_From_Core (uint8_t u8Idx)
{
    osMutexWait(mtx_configHandle, osWaitForever);
    sEdit = sLoRaGtwConfParam.sLoRaConf[u8Idx];
    osMutexRelease(mtx_configHandle);

    bDirty = false;
}

/* Refresh every value label from the staged copy. */
static void _Fn_Show_Staged (void)
{
    char chBuf[20];

    Fn_GUI_Format_MHz(sEdit.u32FreqHz, chBuf, sizeof(chBuf));
    lv_label_set_text_fmt(pObjValFreq, "%s MHz", chBuf);

    lv_label_set_text_fmt(pObjValSF, "SF%u", (unsigned)sEdit.enLoRaSF);
    lv_label_set_text_fmt(pObjValBW, "%u k",
                          (unsigned)((sEdit.enLoRaBW < en_LoRa_BW_Not_Valid) ?
                                     u16BwKhz[sEdit.enLoRaBW] : 0U));
    lv_label_set_text_fmt(pObjValCR, "4/%u", (unsigned)(sEdit.enLoRaCRC + 4U));
    lv_label_set_text_fmt(pObjValPwr, "%u dB",
                          (unsigned)((sEdit.enLoRaPowerGain < en_LoRa_POWER_Not_Valid) ?
                                     u8PwrDbm[sEdit.enLoRaPowerGain] : 0U));

    if (sEdit.bLoRaEn)
    {
        lv_obj_add_state(pObjEnSw, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(pObjEnSw, LV_STATE_CHECKED);
    }

    lv_label_set_text(pObjHint, bDirty ? "edited - Apply or Save" : "");
    lv_obj_set_style_text_color(pObjHint, lv_color_hex(bDirty ? GUI_COL_WARN : GUI_COL_DIM),
                                LV_PART_MAIN);
}

/*------------------------------------------------------------ Stepper ---------------------------------------------------------------*/
static void _Fn_Step (EnumParam enPar, int8_t i8Dir)
{
    switch (enPar)
    {
        case en_Par_Freq:
        {
            uint32_t u32Hz = sEdit.u32FreqHz;

            if (i8Dir > 0)
            {
                u32Hz = ((u32Hz + RAD_FREQ_STEP_HZ) > DEF_LoRa_FREQ_MAX_HZ) ?
                        DEF_LoRa_FREQ_MAX_HZ : (u32Hz + RAD_FREQ_STEP_HZ);
            }
            else
            {
                u32Hz = ((u32Hz < (DEF_LoRa_FREQ_MIN_HZ + RAD_FREQ_STEP_HZ))) ?
                        DEF_LoRa_FREQ_MIN_HZ : (u32Hz - RAD_FREQ_STEP_HZ);
            }

            sEdit.u32FreqHz = u32Hz;
        }
        break;

        case en_Par_SF:
            if ((i8Dir > 0) && (sEdit.enLoRaSF < en_LoRa_SF_12)) { sEdit.enLoRaSF++; }
            if ((i8Dir < 0) && (sEdit.enLoRaSF > en_LoRa_SF_7))  { sEdit.enLoRaSF--; }
        break;

        case en_Par_BW:
            if ((i8Dir > 0) && (sEdit.enLoRaBW < en_LoRa_BW_500KHz)) { sEdit.enLoRaBW++; }
            if ((i8Dir < 0) && (sEdit.enLoRaBW > en_LoRa_BW_7_8KHz)) { sEdit.enLoRaBW--; }
        break;

        case en_Par_CR:
            if ((i8Dir > 0) && (sEdit.enLoRaCRC < en_LoRa_CR_4_8)) { sEdit.enLoRaCRC++; }
            if ((i8Dir < 0) && (sEdit.enLoRaCRC > en_LoRa_CR_4_5)) { sEdit.enLoRaCRC--; }
        break;

        case en_Par_Pwr:
        default:
            if ((i8Dir > 0) && (sEdit.enLoRaPowerGain < en_LoRa_POWER_20db)) { sEdit.enLoRaPowerGain++; }
            if ((i8Dir < 0) && (sEdit.enLoRaPowerGain > en_LoRa_POWER_11db)) { sEdit.enLoRaPowerGain--; }
        break;
    }

    bDirty = true;
    _Fn_Show_Staged();
}

static void _Fn_Step_Cb (lv_event_t *psEvent)
{
    uint32_t u32Code = (uint32_t)(uintptr_t)lv_event_get_user_data(psEvent);

    _Fn_Step((EnumParam)(u32Code >> 1), ((u32Code & 1U) != 0U) ? 1 : -1);
}

static void _Fn_Enable_Cb (lv_event_t *psEvent)
{
    sEdit.bLoRaEn = lv_obj_has_state(lv_event_get_target(psEvent), LV_STATE_CHECKED);
    bDirty = true;
    _Fn_Show_Staged();
}

/*------------------------------------------------------------ Apply and Save --------------------------------------------------------
 * The whole configuration is validated with the edit in place, so the
 * 868-915 MHz limit and the 2 MHz spacing rule are enforced here rather
 * than discovered by a radio that will not start.
 */
static bool _Fn_Commit (void)
{
    /* Static: the structure carries four 255-byte receive buffers, so a
     * local copy would put over 1 KB on the UI task's stack underneath
     * whatever LVGL is already using. */
    static StructLoRaGtwConfParam sTrial;

    osMutexWait(mtx_configHandle, osWaitForever);
    sTrial = sLoRaGtwConfParam;
    osMutexRelease(mtx_configHandle);

    sTrial.sLoRaConf[u8Sel].bLoRaEn         = sEdit.bLoRaEn;
    sTrial.sLoRaConf[u8Sel].u32FreqHz       = sEdit.u32FreqHz;
    sTrial.sLoRaConf[u8Sel].enLoRaSF        = sEdit.enLoRaSF;
    sTrial.sLoRaConf[u8Sel].enLoRaBW        = sEdit.enLoRaBW;
    sTrial.sLoRaConf[u8Sel].enLoRaCRC       = sEdit.enLoRaCRC;
    sTrial.sLoRaConf[u8Sel].enLoRaPowerGain = sEdit.enLoRaPowerGain;

    if (!Fn_LoRa_Config_Validate(&sTrial))
    {
        lv_label_set_text(pObjHint, "rejected: check spacing, min 2 MHz apart");
        lv_obj_set_style_text_color(pObjHint, lv_color_hex(GUI_COL_BAD), LV_PART_MAIN);
        return false;
    }

    osMutexWait(mtx_configHandle, osWaitForever);
    sLoRaGtwConfParam.sLoRaConf[u8Sel].bLoRaEn         = sEdit.bLoRaEn;
    sLoRaGtwConfParam.sLoRaConf[u8Sel].u32FreqHz       = sEdit.u32FreqHz;
    sLoRaGtwConfParam.sLoRaConf[u8Sel].enLoRaSF        = sEdit.enLoRaSF;
    sLoRaGtwConfParam.sLoRaConf[u8Sel].enLoRaBW        = sEdit.enLoRaBW;
    sLoRaGtwConfParam.sLoRaConf[u8Sel].enLoRaCRC       = sEdit.enLoRaCRC;
    sLoRaGtwConfParam.sLoRaConf[u8Sel].enLoRaPowerGain = sEdit.enLoRaPowerGain;
    osMutexRelease(mtx_configHandle);

    Fn_Radio_Request_Reconfig(u8Sel);

    bDirty = false;

    return true;
}

static void _Fn_Apply_Cb (lv_event_t *psEvent)
{
    LV_UNUSED(psEvent);

    if (_Fn_Commit())
    {
        lv_label_set_text(pObjHint, "applied - lost at reboot unless saved");
        lv_obj_set_style_text_color(pObjHint, lv_color_hex(GUI_COL_DIM), LV_PART_MAIN);
    }
}

static void _Fn_Save_Cb (lv_event_t *psEvent)
{
    char *pchJson;
    EnumEEPROMSts enSts = en_EEPROM_ERR_PARAM;

    LV_UNUSED(psEvent);

    if (!_Fn_Commit())
    {
        return;
    }

    pchJson = Fn_JSON_LoRa_Config_Generate();
    if (pchJson != NULL)
    {
        enSts = Fn_EEPROM_JSON_Write(pchJson);
        cJSON_free(pchJson);
    }

    lv_label_set_text(pObjHint, (enSts == en_EEPROM_OK) ? "saved to EEPROM" : "SAVE FAILED");
    lv_obj_set_style_text_color(pObjHint,
                                lv_color_hex((enSts == en_EEPROM_OK) ? GUI_COL_OK : GUI_COL_BAD),
                                LV_PART_MAIN);
}

/*------------------------------------------------------------ Radio selector --------------------------------------------------------*/
static void _Fn_Paint_Selector (void)
{
    uint8_t i;

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        bool bOn = (i == u8Sel);

        lv_obj_set_style_bg_color(pObjSelBtn[i],
                                  lv_color_hex(bOn ? u32RadioColour[i] : GUI_COL_CARD),
                                  LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(pObjSelBtn[i], 0),
                                    lv_color_hex(bOn ? 0x0E1116UL : GUI_COL_TEXT),
                                    LV_PART_MAIN);
    }
}

static void _Fn_Select_Cb (lv_event_t *psEvent)
{
    u8Sel = (uint8_t)(uintptr_t)lv_event_get_user_data(psEvent);

    _Fn_Paint_Selector();
    _Fn_Load_From_Core(u8Sel);
    _Fn_Show_Staged();
}

/*------------------------------------------------------------ Builders --------------------------------------------------------------*/
static lv_obj_t *_Fn_Make_Btn (lv_obj_t *pObjParent, lv_coord_t x, lv_coord_t y,
                               lv_coord_t w, lv_coord_t h, const char *chText,
                               uint32_t u32Bg, lv_event_cb_t pfnCb, void *pvUser)
{
    lv_obj_t *pObjBtn = lv_btn_create(pObjParent);
    lv_obj_t *pObjLbl;

    lv_obj_set_pos(pObjBtn, x, y);
    lv_obj_set_size(pObjBtn, w, h);
    lv_obj_set_style_bg_color(pObjBtn, lv_color_hex(u32Bg), LV_PART_MAIN);
    lv_obj_set_style_radius(pObjBtn, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(pObjBtn, 0, LV_PART_MAIN);

    pObjLbl = lv_label_create(pObjBtn);
    lv_label_set_text(pObjLbl, chText);
    lv_obj_center(pObjLbl);
    lv_obj_set_style_text_color(pObjLbl, lv_color_hex(GUI_COL_TEXT), LV_PART_MAIN);

    if (pfnCb != NULL)
    {
        lv_obj_add_event_cb(pObjBtn, pfnCb, LV_EVENT_CLICKED, pvUser);
    }

    return pObjBtn;
}

/* One stepper: name, minus, value, plus. Long press repeats, so a big
 * frequency move does not need forty taps. */
static lv_obj_t *_Fn_Make_Stepper (lv_obj_t *pObjParent, lv_coord_t x, lv_coord_t y,
                                   lv_coord_t w, const char *chName, EnumParam enPar)
{
    lv_obj_t *pObjRow = Fn_GUI_Card(pObjParent, x, y, w, 28, GUI_COL_CARD);
    lv_obj_t *pObjVal;
    lv_obj_t *pObjBtn;
    lv_coord_t xMinus = (w > 200) ? 52 : 36;
    lv_coord_t xPlus  = (lv_coord_t)(w - RAD_BTN_W - 6);
    lv_coord_t xVal   = (lv_coord_t)(xMinus + RAD_BTN_W + 2);
    lv_coord_t wVal   = (lv_coord_t)(xPlus - xVal - 2);

    (void)Fn_GUI_Label(pObjRow, 6, 7, GUI_COL_DIM, NULL, chName);

    pObjBtn = _Fn_Make_Btn(pObjRow, xMinus, 2, RAD_BTN_W, 24, "-", GUI_COL_CARD_HI,
                           _Fn_Step_Cb, (void *)(uintptr_t)(((uint32_t)enPar << 1) | 0U));
    lv_obj_add_event_cb(pObjBtn, _Fn_Step_Cb, LV_EVENT_LONG_PRESSED_REPEAT,
                        (void *)(uintptr_t)(((uint32_t)enPar << 1) | 0U));

    pObjVal = lv_label_create(pObjRow);
    lv_obj_set_pos(pObjVal, xVal, 6);
    lv_obj_set_width(pObjVal, wVal);
    lv_obj_set_style_text_align(pObjVal, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(pObjVal, lv_color_hex(GUI_COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(pObjVal, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(pObjVal, "--");

    pObjBtn = _Fn_Make_Btn(pObjRow, xPlus, 2, RAD_BTN_W, 24, "+", GUI_COL_CARD_HI,
                           _Fn_Step_Cb, (void *)(uintptr_t)(((uint32_t)enPar << 1) | 1U));
    lv_obj_add_event_cb(pObjBtn, _Fn_Step_Cb, LV_EVENT_LONG_PRESSED_REPEAT,
                        (void *)(uintptr_t)(((uint32_t)enPar << 1) | 1U));

    return pObjVal;
}

void Fn_GUI_Radios_Build (lv_obj_t *pObjParent)
{
    lv_obj_t *pObjRow;
    uint8_t i;
    char chName[4];

    lv_obj_clear_flag(pObjParent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pObjParent, GUI_PAD, LV_PART_MAIN);

    /* Radio selector */
    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        (void)snprintf(chName, sizeof(chName), "R%u", (unsigned)(i + 1U));
        pObjSelBtn[i] = _Fn_Make_Btn(pObjParent, (lv_coord_t)(i * (RAD_SEL_W + 4)), RAD_SEL_Y,
                                     RAD_SEL_W, RAD_SEL_H, chName, GUI_COL_CARD,
                                     _Fn_Select_Cb, (void *)(uintptr_t)i);
    }

    /* Enable + live status */
    pObjRow = Fn_GUI_Card(pObjParent, 0, RAD_ROW1_Y, RAD_FULL_W, 28, GUI_COL_CARD);
    (void)Fn_GUI_Label(pObjRow, 6, 7, GUI_COL_DIM, NULL, "ENABLE");

    pObjEnSw = lv_switch_create(pObjRow);
    lv_obj_set_size(pObjEnSw, 48, 24);
    lv_obj_set_pos(pObjEnSw, 66, 2);
    lv_obj_set_style_bg_color(pObjEnSw, lv_color_hex(GUI_COL_OK), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(pObjEnSw, _Fn_Enable_Cb, LV_EVENT_VALUE_CHANGED, NULL);

    pObjStatus = Fn_GUI_Label(pObjRow, 130, 7, GUI_COL_DIM, NULL, "--");

    /* Parameters */
    pObjValFreq = _Fn_Make_Stepper(pObjParent, 0, RAD_ROW2_Y, RAD_FULL_W, "FREQ", en_Par_Freq);

    pObjValSF  = _Fn_Make_Stepper(pObjParent, 0,   RAD_ROW3_Y, RAD_HALF_W, "SF",  en_Par_SF);
    pObjValBW  = _Fn_Make_Stepper(pObjParent, 160, RAD_ROW3_Y, RAD_HALF_W, "BW",  en_Par_BW);
    pObjValCR  = _Fn_Make_Stepper(pObjParent, 0,   RAD_ROW4_Y, RAD_HALF_W, "CR",  en_Par_CR);
    pObjValPwr = _Fn_Make_Stepper(pObjParent, 160, RAD_ROW4_Y, RAD_HALF_W, "PWR", en_Par_Pwr);

    /* Apply and Save */
    (void)_Fn_Make_Btn(pObjParent, 0,   RAD_ROW5_Y, RAD_HALF_W, 30, "Apply",
                       GUI_COL_CARD_HI, _Fn_Apply_Cb, NULL);
    (void)_Fn_Make_Btn(pObjParent, 160, RAD_ROW5_Y, RAD_HALF_W, 30, "Save",
                       GUI_COL_ACCENT, _Fn_Save_Cb, NULL);

    pObjHint = Fn_GUI_Label(pObjParent, 2, RAD_ROW5_Y + 32, GUI_COL_DIM, NULL, "");

    u8Sel = 0U;
    _Fn_Paint_Selector();
    _Fn_Load_From_Core(u8Sel);
    _Fn_Show_Staged();
}

/* Only the live status is polled; the staged values belong to the
 * operator and must not be overwritten underneath them. */
void Fn_GUI_Radios_Refresh (void)
{
    EnumLoRaSts enSts;
    const StructRadioStats *psStats = Fn_Radio_Get_Stats(u8Sel);

    osMutexWait(mtx_configHandle, osWaitForever);
    enSts = sLoRaGtwConfParam.sLoRaConf[u8Sel].enLoRaSts;
    osMutexRelease(mtx_configHandle);

    if ((psStats != NULL) && (psStats->u32FreqHzActual > 0U))
    {
        char chBuf[20];

        Fn_GUI_Format_MHz(psStats->u32FreqHzActual, chBuf, sizeof(chBuf));
        lv_label_set_text_fmt(pObjStatus, "%s  on %s", _Fn_Status_Text(enSts), chBuf);
    }
    else
    {
        lv_label_set_text(pObjStatus, _Fn_Status_Text(enSts));
    }

    lv_obj_set_style_text_color(pObjStatus,
                                lv_color_hex((enSts == en_LoRa_init_Success) ? GUI_COL_OK :
                                             (enSts == en_LoRa_Disabled)     ? GUI_COL_DIM :
                                                                               GUI_COL_BAD),
                                LV_PART_MAIN);
}
