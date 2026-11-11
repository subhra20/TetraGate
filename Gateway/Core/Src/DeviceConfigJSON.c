/*
 * DeviceConfigJSON.c
 *
 *  LoRa gateway configuration <-> JSON (format: see DeviceConfigJSON.h).
 *
 *  Parsing is all-or-nothing: values are written into a staging copy and,
 *  when the whole message and the resulting configuration are valid, the
 *  configuration fields (only those) are copied into sLoRaGtwConfParam.
 *  Live fields (status, RX data, RSSI) are never overwritten here.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "cJSON.h"
#include "utils.h"
#include "DeviceConfigJSON.h"

extern StructLoRaGtwConfParam sLoRaGtwConfParam;

static StructLoRaGtwConfParam sJSONLoRaGtwConfParam CCMRAM_ALIGN;   /* staging copy */

/*------------------------------------------------------------ Keyword table -----------------------------------------------------------*/
static const structJSONConfigParam sJSONLoRaVal[] =
{
    { "L_Sel", enum_u8,   enum_LoRa_Select,        1U,                    DEF_MAX_LoRa_Module   },
    { "LR",    enum_bool, enum_LoRa_En,            0U,                    1U                    },
    { "LFHZ",  enum_u32,  enum_LoRa_Fz_Select,     DEF_LoRa_FREQ_MIN_HZ,  DEF_LoRa_FREQ_MAX_HZ  },
    { "LSF",   enum_list, enum_LoRa_Spread_Factor, en_LoRa_SF_7,          en_LoRa_SF_12         },
    { "LB",    enum_list, enum_LoRa_Bandwidth,     en_LoRa_BW_7_8KHz,     en_LoRa_BW_500KHz     },
    { "LCRC",  enum_list, enum_LoRa_CRC,           en_LoRa_CR_4_5,        en_LoRa_CR_4_8        },
    { "LPG",   enum_list, enum_LoRa_Power_Gain,    en_LoRa_POWER_11db,    en_LoRa_POWER_20db    },
    { "LDL",   enum_bool, enum_LoRa_Datalog_En,    0U,                    1U                    },
};

#define JSON_LORA_KEYWORD_NO   ((uint8_t)(sizeof(sJSONLoRaVal) / sizeof(sJSONLoRaVal[0])))

/*------------------------------------------------------------ cJSON heap hooks --------------------------------------------------------*/
void Fn_JSON_Init (void)
{
    cJSON_Hooks sHooks;

    sHooks.malloc_fn = pvPortMalloc;
    sHooks.free_fn   = vPortFree;
    cJSON_InitHooks(&sHooks);
}

/*------------------------------------------------------------ Helpers -----------------------------------------------------------------*/
static const structJSONConfigParam *_Fn_FindParamByKey (const char *chKey)
{
    uint8_t i;

    if (chKey == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < JSON_LORA_KEYWORD_NO; i++)
    {
        if (strcmp(sJSONLoRaVal[i].chKeyword, chKey) == 0)
        {
            return &sJSONLoRaVal[i];
        }
    }

    return NULL;
}

/* Reads an integer and checks it against the table limits (inclusive). */
static enJSON_ProcessStatus _Fn_GetCheckedValue (const cJSON *jItem, const structJSONConfigParam *psParam, uint32_t *pu32Value)
{
    double dValue;

    if ((jItem == NULL) || (psParam == NULL) || (pu32Value == NULL) || !cJSON_IsNumber(jItem))
    {
        return enum_JSON_PROCESS_INVALID_VALUE;
    }

    dValue = jItem->valuedouble;

    /* range check BEFORE any cast, so 256 or -1 can never wrap into range */
    if ((dValue < (double)psParam->u32ValLimLow) || (dValue > (double)psParam->u32ValLimHigh))
    {
        return enum_JSON_VALUE_LENGTH_ERROR;
    }

    /* must be a whole number (rejects 9.5) */
    if ((double)(uint32_t)dValue != dValue)
    {
        return enum_JSON_PROCESS_INVALID_VALUE;
    }

    *pu32Value = (uint32_t)dValue;
    return enum_JSON_PROCESS_OK;
}

static void _Fn_StoreValue (StructLoRaConf *psConf, enKeyWordType enKey, uint32_t u32Value)
{
    switch (enKey)
    {
        case enum_LoRa_En:
        	psConf->bLoRaEn         = (u32Value != 0U);
        break;

        case enum_LoRa_Fz_Select:
        	psConf->u32FreqHz       = u32Value;
        break;

        case enum_LoRa_Spread_Factor:
        	psConf->enLoRaSF        = (EnumLoRaSF)u32Value;
        break;

        case enum_LoRa_Bandwidth:
        	psConf->enLoRaBW        = (EnumLoRaBW)u32Value;
        break;

        case enum_LoRa_CRC:
        	psConf->enLoRaCRC       = (EnumLoRaCRC)u32Value;
        break;

        case enum_LoRa_Power_Gain:
        	psConf->enLoRaPowerGain = (EnumLoRaPowerGain)u32Value;
        break;

        case enum_LoRa_Datalog_En:
        	psConf->bDatalogEn      = (u32Value != 0U);
        break;

        default:
        break;
    }
}

/* Copies only the JSON (configuration) fields of one module. */
static void _Fn_CopyConfFields (StructLoRaConf *psDst, const StructLoRaConf *psSrc)
{
    psDst->bLoRaEn         = psSrc->bLoRaEn;
    psDst->u32FreqHz       = psSrc->u32FreqHz;
    psDst->enLoRaSF        = psSrc->enLoRaSF;
    psDst->enLoRaBW        = psSrc->enLoRaBW;
    psDst->enLoRaCRC       = psSrc->enLoRaCRC;
    psDst->enLoRaPowerGain = psSrc->enLoRaPowerGain;
    psDst->bDatalogEn      = psSrc->bDatalogEn;
}

/*------------------------------------------------------------ One module object -------------------------------------------------------
 * L_Sel is read first with cJSON_GetObjectItemCaseSensitive, so the key
 * order inside the object does not matter.
 */
static enJSON_ProcessStatus _Fn_ParseModuleObject (const cJSON *jObject, uint8_t *pu8SeenMask)
{
    const cJSON *jSel;
    const cJSON *jItem;
    const structJSONConfigParam *psParam;
    enJSON_ProcessStatus enStatus;
    uint32_t u32Value;
    uint8_t u8Index;

    if (!cJSON_IsObject(jObject))
    {
        return enum_ERROR_JSON_FORMAT;
    }

    jSel = cJSON_GetObjectItemCaseSensitive(jObject, "L_Sel");
    if (jSel == NULL)
    {
        return enum_JSON_MISSING_SELECT;
    }

    enStatus = _Fn_GetCheckedValue(jSel, &sJSONLoRaVal[0], &u32Value);
    if (enStatus != enum_JSON_PROCESS_OK)
    {
        return enStatus;
    }

    u8Index = (uint8_t)(u32Value - 1U);

    if ((*pu8SeenMask & (uint8_t)(1U << u8Index)) != 0U)
    {
        return enum_JSON_DUPLICATE_MODULE;
    }
    *pu8SeenMask |= (uint8_t)(1U << u8Index);

    for (jItem = jObject->child; jItem != NULL; jItem = jItem->next)
    {
        psParam = _Fn_FindParamByKey(jItem->string);

        if (psParam == NULL)
        {
            return enum_JSON_PROCESS_INVALID_KEY;
        }

        if (psParam->enKeyWordType == enum_LoRa_Select)
        {
            continue;   /* already handled */
        }

        enStatus = _Fn_GetCheckedValue(jItem, psParam, &u32Value);
        if (enStatus != enum_JSON_PROCESS_OK)
        {
            return enStatus;
        }

        _Fn_StoreValue(&sJSONLoRaGtwConfParam.sLoRaConf[u8Index], psParam->enKeyWordType, u32Value);
    }

    return enum_JSON_PROCESS_OK;
}

/*------------------------------------------------------------ Public: parse -----------------------------------------------------------*/
enJSON_ProcessStatus FnProcessLoRaConfig (const char *chjson_msg)
{
    cJSON *jRoot;
    const cJSON *jItem;
    enJSON_ProcessStatus enStatus = enum_JSON_PROCESS_OK;
    uint8_t u8SeenMask = 0U;
    uint8_t u8Count = 0U;
    uint8_t i;

    if (chjson_msg == NULL)
    {
        return enum_ERROR_JSON_FORMAT;
    }

    jRoot = cJSON_Parse(chjson_msg);
    if (jRoot == NULL)
    {
        return enum_ERROR_JSON_FORMAT;
    }

    if (!cJSON_IsArray(jRoot) || (cJSON_GetArraySize(jRoot) < 1) || (cJSON_GetArraySize(jRoot) > DEF_MAX_LoRa_Module))
    {
        cJSON_Delete(jRoot);
        return enum_ERROR_JSON_FORMAT;
    }

    /* staging copy starts from the current config: missing keys keep their value */
    osMutexWait(mtx_configHandle, osWaitForever);
    memcpy(&sJSONLoRaGtwConfParam, &sLoRaGtwConfParam, sizeof(sJSONLoRaGtwConfParam));
    osMutexRelease(mtx_configHandle);

    for (jItem = jRoot->child; jItem != NULL; jItem = jItem->next)
    {
        enStatus = _Fn_ParseModuleObject(jItem, &u8SeenMask);
        if (enStatus != enum_JSON_PROCESS_OK)
        {
            break;
        }
        u8Count++;
    }

    cJSON_Delete(jRoot);

    if (enStatus != enum_JSON_PROCESS_OK)
    {
        return enStatus;
    }

    if ((u8Count == 0U) || !Fn_LoRa_Config_Validate(&sJSONLoRaGtwConfParam))
    {
        return enum_JSON_CONFIG_REJECTED;
    }

    osMutexWait(mtx_configHandle, osWaitForever);
    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        _Fn_CopyConfFields(&sLoRaGtwConfParam.sLoRaConf[i], &sJSONLoRaGtwConfParam.sLoRaConf[i]);
    }
    osMutexRelease(mtx_configHandle);

    return enum_JSON_PROCESS_OK;
}

/*------------------------------------------------------------ Public: generate --------------------------------------------------------*/
static bool _Fn_AddModuleObject (cJSON *jArray, const StructLoRaConf *c, uint8_t u8LoRaSel)
{
    cJSON *jObj = cJSON_CreateObject();

    if (jObj == NULL)
    {
        return false;
    }

    if ((cJSON_AddNumberToObject(jObj, "L_Sel", u8LoRaSel)                    == NULL) ||
        (cJSON_AddNumberToObject(jObj, "LR",    c->bLoRaEn ? 1 : 0)           == NULL) ||
        (cJSON_AddNumberToObject(jObj, "LFHZ",  (double)c->u32FreqHz)         == NULL) ||
        (cJSON_AddNumberToObject(jObj, "LSF",   c->enLoRaSF)                  == NULL) ||
        (cJSON_AddNumberToObject(jObj, "LB",    c->enLoRaBW)                  == NULL) ||
        (cJSON_AddNumberToObject(jObj, "LCRC",  c->enLoRaCRC)                 == NULL) ||
        (cJSON_AddNumberToObject(jObj, "LPG",   c->enLoRaPowerGain)           == NULL) ||
        (cJSON_AddNumberToObject(jObj, "LDL",   c->bDatalogEn ? 1 : 0)        == NULL))
    {
        cJSON_Delete(jObj);
        return false;
    }

    if (!cJSON_AddItemToArray(jArray, jObj))
    {
        cJSON_Delete(jObj);
        return false;
    }

    return true;
}

char *Fn_JSON_LoRa_Config_Generate (void)
{
    cJSON *jArray;
    char *pchJsonMsg = NULL;
    bool bOk = true;
    uint8_t i;

    jArray = cJSON_CreateArray();
    if (jArray == NULL)
    {
        return NULL;
    }

    /* read sLoRaGtwConfParam under the mutex, so the 4 modules are one consistent snapshot */
    osMutexWait(mtx_configHandle, osWaitForever);
    for (i = 0U; (i < DEF_MAX_LoRa_Module) && bOk; i++)
    {
        bOk = _Fn_AddModuleObject(jArray, &sLoRaGtwConfParam.sLoRaConf[i], (uint8_t)(i + 1U));
    }
    osMutexRelease(mtx_configHandle);

    if (bOk)
    {
        pchJsonMsg = cJSON_PrintUnformatted(jArray);   /* caller frees with cJSON_free() */
    }
    cJSON_Delete(jArray);

    return pchJsonMsg;
}
