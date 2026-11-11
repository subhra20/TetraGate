/*
 * eeprom_operation.h
 *
 *  AT24C256 (32 KB, 64-byte pages) on I2C1.
 *
 *  EEPROM map
 *  0x0000 - 0x03FF  (16 pages, 1 KB)  LoRa configuration JSON
 *                   [0..3]  magic "LGCF"
 *                   [4..5]  version
 *                   [6..7]  JSON length
 *                   [8..11] CRC32 of the JSON text
 *                   [12.. ] JSON text
 *  0x0400 - 0x7FFF  free (node table, logs, touch calibration later)
 */

#ifndef INC_EEPROM_OPERATION_H_
#define INC_EEPROM_OPERATION_H_

#include "utils.h"

#define EEPROM_I2C_ADDR             0xA0U       /* A0..A2 = GND, 8-bit address */
#define EEPROM_SIZE                 32768U
#define EEPROM_PAGE_SIZE            64U
#define EEPROM_I2C_TIMEOUT_MS       100U
#define EEPROM_WRITE_CYCLE_MAX_MS   10U         /* datasheet tWR = 5 ms        */

/*  Configuration is held in two slots, A and B, so a power cut during a
 *  save can never leave the gateway without a valid configuration:
 *
 *    1. write the new configuration to the INACTIVE slot
 *    2. read it back and check the CRC
 *    3. only then switch the active slot in the header
 *
 *  At boot the active slot is tried first, then the other, then the
 *  factory defaults built into flash.
 */
#define EEPROM_HDR_ADDR             0x0000U
#define EEPROM_HDR_MAGIC            0x4C474844UL    /* "LGHD" */

#define EEPROM_SLOT_A_ADDR          0x0040U
#define EEPROM_SLOT_B_ADDR          0x1040U
#define EEPROM_SLOT_SIZE            4096U

#define EEPROM_JSON_HDR_SIZE        12U
#define EEPROM_JSON_MAX_LEN         (EEPROM_SLOT_SIZE - EEPROM_JSON_HDR_SIZE - 1U)

#define EEPROM_JSON_MAGIC           0x4C474346UL    /* "LGCF" */
#define EEPROM_JSON_VERSION         2U    /* 2 = frequency in Hz (key LFHZ) */

/* Reset reasons, read from the RCC at start-up. */
typedef enum
{
    en_Reset_Unknown = 0,
    en_Reset_PowerOn,
    en_Reset_Pin,
    en_Reset_Software,
    en_Reset_Iwdg,
    en_Reset_Wwdg,
    en_Reset_LowPower,
    en_Reset_BrownOut,
} EnumResetReason;

typedef struct
{
    uint8_t  u8ActiveSlot;      /* 0 = A, 1 = B      */
    uint8_t  u8ResetReason;     /* EnumResetReason   */
    uint16_t u16Reserved;
    uint32_t u32BootCount;
} StructEepromHdr;


typedef enum
{
    en_EEPROM_OK = 0,
    en_EEPROM_ERR_I2C,
    en_EEPROM_ERR_TIMEOUT,
    en_EEPROM_ERR_PARAM,
    en_EEPROM_ERR_EMPTY,        /* no magic / wrong version: nothing stored */
    en_EEPROM_ERR_CRC,          /* stored JSON damaged                      */
    en_EEPROM_ERR_TOO_LONG,
    en_EEPROM_ERR_VERIFY,       /* read-back CRC mismatch after write       */
} EnumEEPROMSts;

/* Low level */
void          Fn_EEPROM_Init  (void);
EnumEEPROMSts Fn_EEPROM_Read  (uint16_t u16Addr, uint8_t *pu8Data, uint16_t u16Len);
EnumEEPROMSts Fn_EEPROM_Write (uint16_t u16Addr, const uint8_t *pu8Data, uint16_t u16Len);

/*  Small fixed records elsewhere in the map. Layout:
 *    [0..3]  magic
 *    [4..5]  payload length
 *    [6..9]  CRC32 of the payload
 *    [10..]  payload
 *
 *  Addresses follow section 11 of the architecture document. */
#define EEPROM_RECORD_HDR_SIZE      10U
#define EEPROM_RECORD_MAX_LEN       256U

#define EEPROM_TOUCHCAL_ADDR        0x3040U
#define EEPROM_TOUCHCAL_MAGIC       0x4C544348UL    /* "LTCH" */

/* Node whitelist. Sits in the node-table region of the storage map; the
 * per-node keys for the MIC will share it later. */
#define EEPROM_WHITELIST_ADDR       0x2040U
#define EEPROM_WHITELIST_MAGIC      0x4C47574CUL    /* "LGWL" */

EnumEEPROMSts Fn_EEPROM_Record_Write (uint16_t u16Addr, uint32_t u32Magic, const void *pvData, uint16_t u16Len);
EnumEEPROMSts Fn_EEPROM_Record_Read  (uint16_t u16Addr, uint32_t u32Magic, void *pvData, uint16_t u16Len);

/* Header: active slot, boot counter, reset reason */
EnumEEPROMSts Fn_EEPROM_Hdr_Read  (StructEepromHdr *psHdr);
EnumEEPROMSts Fn_EEPROM_Hdr_Write (const StructEepromHdr *psHdr);

/* Which slot the last successful read came from: 0 = A, 1 = B. */
uint8_t Fn_EEPROM_Get_Active_Slot (void);

/* Cached from the last header read or write, so the UI can show these
 * without going near the I2C bus on every refresh. */
void Fn_EEPROM_Get_Boot_Info (uint32_t *pu32BootCount, uint8_t *pu8ResetReason);

/* JSON region */
EnumEEPROMSts Fn_EEPROM_JSON_Read  (char *chBuf, uint16_t u16BufSize);
EnumEEPROMSts Fn_EEPROM_JSON_Write (const char *chJson);
EnumEEPROMSts Fn_EEPROM_JSON_Erase (void);

#endif /* INC_EEPROM_OPERATION_H_ */
