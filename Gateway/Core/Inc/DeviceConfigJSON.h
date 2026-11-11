/*
 * DeviceConfigJSON.h
 *
 *  LoRa gateway configuration <-> JSON.
 *
 *  JSON format (array of 1..4 objects, one per LoRa module):
 *
 *  [
 *    {"L_Sel":1,"LR":1,"LFHZ":868100000,"LSF":9,"LB":8,"LCRC":4,"LPG":2,"LDL":1},
 *    {"L_Sel":2,"LR":1,"LFHZ":900000000,"LSF":9,"LB":8,"LCRC":4,"LPG":2,"LDL":1},
 *    {"L_Sel":3,"LR":1,"LFHZ":915000000,"LSF":9,"LB":8,"LCRC":4,"LPG":2,"LDL":1},
 *    {"L_Sel":4,"LR":1,"LFHZ":880000000,"LSF":9,"LB":8,"LCRC":4,"LPG":2,"LDL":1}
 *  ]
 *
 *  Key    Meaning               Range
 *  L_Sel  LoRa module           1 .. 4 (required in every object)
 *  LR     Enable                0 / 1
 *  LFHZ   Frequency in Hz       868000000 .. 915000000 (e.g. 868100000)
 *                               5 = 890, 6 = 895, 7 = 900, 8 = 905, 9 = 910, 10 = 915 MHz)
 *  LSF    Spreading factor      7 .. 12
 *  LB     Bandwidth code        0 .. 9 (7 = 125 kHz, 8 = 250 kHz, 9 = 500 kHz)
 *  LCRC   Coding rate           1 .. 4 (1 = 4/5 ... 4 = 4/8)
 *  LPG    Power gain index      0 .. 3 (0 = 11 dB, 1 = 14 dB, 2 = 17 dB, 3 = 20 dB)
 *  LDL    Data log enable       0 / 1
 *
 *  A message may contain fewer than 4 objects and fewer keys per object:
 *  missing values keep their current setting.
 */

#ifndef INC_DEVICECONFIGJSON_H_
#define INC_DEVICECONFIGJSON_H_

#include "utils.h"
#include "cJSON.h"

#define JSON_KEY_SIZE   16U

typedef enum
{
    enum_JSON_PROCESS_OK = 0,
    enum_ERROR_JSON_FORMAT,             /* not valid JSON, or wrong structure      */
    enum_JSON_PROCESS_INVALID_KEY,      /* unknown key                             */
    enum_JSON_PROCESS_INVALID_VALUE,    /* not an integer number                   */
    enum_JSON_VALUE_LENGTH_ERROR,       /* value out of range                      */
    enum_JSON_MISSING_SELECT,           /* object without L_Sel                    */
    enum_JSON_DUPLICATE_MODULE,         /* same L_Sel twice in one message         */
    enum_JSON_CONFIG_REJECTED,          /* whole config invalid (e.g. spacing)     */
    enum_JSON_NO_MEMORY,                /* cJSON allocation failed                 */
} enJSON_ProcessStatus;

typedef enum
{
    enum_LoRa_Select = 0,
    enum_LoRa_En,
    enum_LoRa_Fz_Select,
    enum_LoRa_Spread_Factor,
    enum_LoRa_Bandwidth,
    enum_LoRa_CRC,
    enum_LoRa_Power_Gain,
    enum_LoRa_Datalog_En,
} enKeyWordType;

typedef enum
{
    enum_u8 = 0,
    enum_u32,
    enum_bool,
    enum_list,
} enValueType;

typedef struct
{
    const char    *chKeyword;
    enValueType    enValueType;
    enKeyWordType  enKeyWordType;
    uint32_t       u32ValLimLow;    /* inclusive */
    uint32_t       u32ValLimHigh;   /* inclusive */
} structJSONConfigParam;

/* Parse a JSON message into sLoRaGtwConfParam (all-or-nothing). */
enJSON_ProcessStatus FnProcessLoRaConfig (const char *chjson_msg);

/* Build JSON from sLoRaGtwConfParam. Free the result with cJSON_free(). */
char *Fn_JSON_LoRa_Config_Generate (void);

/* Route cJSON allocations to the FreeRTOS heap. Call once before any cJSON use. */
void Fn_JSON_Init (void);

#endif /* INC_DEVICECONFIGJSON_H_ */
