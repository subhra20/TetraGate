/*
 * utils.h
 *
 *  Created on: Sep 20, 2026
 *      Author: subhr
 *
 *  sLoRaGtwConfParam is the main structure of the gateway. It always
 *  reflects the real state of the 4 LoRa modules: it is updated after
 *  every radio (re)configuration and every received packet.
 */

#ifndef INC_UTILS_H_
#define INC_UTILS_H_

#include "main.h"
#include "stdio.h"
#include "string.h"
#include "stdbool.h"
#include "spi.h"
#include "LoRa.h"
#include "cmsis_os.h"

#define DEF_OS_RTOS_ENABLE              1

/*------------------------------------------------------------ LoRa modules ------------------------------------------------------------*/
#define DEF_MAX_LoRa_Module             4

#define DEF_LoRa_Module_1               0
#define DEF_LoRa_Module_2               1
#define DEF_LoRa_Module_3               2
#define DEF_LoRa_Module_4               3

/* RFM95W FIFO holds max 255 bytes payload */
#define DEF_LoRa_MAX_Data_Receive_Len   255

/* RFM95W usable band, and the minimum spacing between two ENABLED
 * radios. Hertz throughout: the channel-index enum this replaced could
 * not express 868.1 MHz. */
#define DEF_LoRa_FREQ_MIN_HZ            868000000UL
#define DEF_LoRa_FREQ_MAX_HZ            915000000UL
#define DEF_LoRa_MIN_SPACING_HZ         2000000UL

/*------------------------------------------------------------ Enumerations ------------------------------------------------------------*/
/* Values equal the LoRa.h SF_x defines (7..12) */
typedef enum
{
    en_LoRa_SF_7 = 7,
    en_LoRa_SF_8 = 8,
    en_LoRa_SF_9 = 9,
    en_LoRa_SF_10 = 10,
    en_LoRa_SF_11 = 11,
    en_LoRa_SF_12 = 12,
    en_LoRa_SF_Not_Valid,
} EnumLoRaSF;

/* Values equal the LoRa.h BW_x defines (0..9) */
typedef enum
{
    en_LoRa_BW_7_8KHz = 0,
    en_LoRa_BW_10_4KHz,
    en_LoRa_BW_15_6KHz,
    en_LoRa_BW_20_8KHz,
    en_LoRa_BW_31_25KHz,
    en_LoRa_BW_41_7KHz,
    en_LoRa_BW_62_5KHz,
    en_LoRa_BW_125KHz,
    en_LoRa_BW_250KHz,
    en_LoRa_BW_500KHz,
    en_LoRa_BW_Not_Valid,
} EnumLoRaBW;

/* Coding rate. Values equal the LoRa.h CR_4_x defines (1..4) */
typedef enum
{
    en_LoRa_CR_4_5 = 1,
    en_LoRa_CR_4_6 = 2,
    en_LoRa_CR_4_7 = 3,
    en_LoRa_CR_4_8 = 4,
    en_LoRa_CR_Not_Valid,
} EnumLoRaCRC;

/* Index into the power table (LoRa.h POWER_xxdb register values) */
typedef enum
{
    en_LoRa_POWER_11db = 0,
    en_LoRa_POWER_14db,
    en_LoRa_POWER_17db,
    en_LoRa_POWER_20db,
    en_LoRa_POWER_Not_Valid,
} EnumLoRaPowerGain;

typedef enum
{
    en_LoRa_init_Fail = 0,      /* 404 : RegVersion != 0x12          */
    en_LoRa_init_Success,       /* 200                                */
    en_LoRa_Unavailable,        /* 503 : handler/pins not set         */
    en_LoRa_Disabled,           /* radio switched off by config       */
} EnumLoRaSts;

/*------------------------------------------------------------ Main structure -----------------------------------------------------------
 * Fields marked JSON are stored in the EEPROM. The others are live state.
 */
typedef struct
{
    bool              bLoRaEn;                                  /* JSON "LR"   */
    uint32_t          u32FreqHz;                                /* JSON "LFHZ" */
    EnumLoRaSF        enLoRaSF;                                 /* JSON "LSF"  */
    EnumLoRaBW        enLoRaBW;                                 /* JSON "LB"   */
    EnumLoRaCRC       enLoRaCRC;                                /* JSON "LCRC" */
    EnumLoRaPowerGain enLoRaPowerGain;                          /* JSON "LPG"  */
    EnumLoRaSts       enLoRaSts;                                /* live        */
    uint8_t           u8ReceivePacketSize;                      /* live        */
    uint8_t           u8RxData[DEF_LoRa_MAX_Data_Receive_Len];  /* live        */
    int               iRssi_val;                                /* live        */
    uint32_t          u32RxPacketCount;                         /* live        */
    bool              bDatalogEn;                               /* JSON "LDL"  */
} StructLoRaConf;

typedef struct
{
    StructLoRaConf sLoRaConf[DEF_MAX_LoRa_Module];
} StructLoRaGtwConfParam;

/*------------------------------------------------------------ Globals -----------------------------------------------------------------*/
/* LoRa driver objects (LoRa.c keeps its own settings in these) */
extern LoRa myLoRa;         /* LoRa 1 */
extern LoRa myLoRa_i;       /* LoRa 2 */
extern LoRa myLoRa_ii;      /* LoRa 3 */
extern LoRa myLoRa_iii;     /* LoRa 4 */

extern StructLoRaGtwConfParam sLoRaGtwConfParam;
extern osMutexId              mtx_configHandle;     /* protects sLoRaGtwConfParam */

/*------------------------------------------------------------ Functions ---------------------------------------------------------------*/
void Fn_Delay (uint32_t Delay);

/* utils.c : default configuration (used when EEPROM has no valid JSON) */
bool Fn_Def_LoRa_1st_Init (void);
bool Fn_Def_LoRa_2nd_Init (void);
bool Fn_Def_LoRa_3rd_Init (void);
bool Fn_Def_LoRa_4th_Init (void);

/* utils.c : configuration from sLoRaGtwConfParam (after valid JSON / any change) */
bool Fn_LoRa_Init_From_Conf (uint8_t u8LoRaSelector);
bool Fn_LoRa_Config_Validate (const StructLoRaGtwConfParam *psConf);

/* utils.c : store a received packet in sLoRaGtwConfParam (call from the LoRa tasks) */
void Fn_LoRa_Rx_Update (uint8_t u8LoRaSelector, const uint8_t *pu8Data, uint8_t u8Len, int iRssi);

/* LoRa_Core_Oper.c */
void     Fn_Conf_To_Driver (const StructLoRaConf *psConf, LoRa *psLoRa);
void     Fn_Conf_Structure_Update (const LoRa *psLoRa, uint8_t u8LoRaSelector);

#endif /* INC_UTILS_H_ */
