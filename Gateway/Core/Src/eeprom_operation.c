/*
 * eeprom_operation.c
 *
 *  Created on: Oct 2, 2026
 *      Author: subhr
 *
 *  AT24C256 driver (page-safe writes, ACK polling) and the fixed JSON
 *  configuration region. A stored JSON is "valid" only when magic,
 *  version, length and CRC32 all match.
 */

#include "eeprom_operation.h"
#include "i2c.h"

extern I2C_HandleTypeDef hi2c1;

static osMutexId mtx_eepromHandle = NULL;

/*------------------------------------------------------------ CRC32 (IEEE, reflected 0xEDB88320) -------------------------------------*/
static uint32_t _Fn_CRC32_Update (uint32_t u32Crc, const uint8_t *pu8Data, uint32_t u32Len)
{
    uint32_t i;
    uint8_t  b;

    u32Crc = ~u32Crc;
    for (i = 0U; i < u32Len; i++)
    {
        u32Crc ^= pu8Data[i];
        for (b = 0U; b < 8U; b++)
        {
            u32Crc = (u32Crc & 1U) ? ((u32Crc >> 1) ^ 0xEDB88320UL) : (u32Crc >> 1);
        }
    }
    return ~u32Crc;
}

/*------------------------------------------------------------ Little-endian helpers ---------------------------------------------------*/
static void     _Fn_Put32 (uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void     _Fn_Put16 (uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint32_t _Fn_Get32 (const uint8_t *p)       { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t _Fn_Get16 (const uint8_t *p)       { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

/*------------------------------------------------------------ Low level ---------------------------------------------------------------*/
void Fn_EEPROM_Init (void)
{
    if (mtx_eepromHandle == NULL)
    {
        osMutexDef(mtx_eeprom);
        mtx_eepromHandle = osMutexCreate(osMutex(mtx_eeprom));
    }
}

/* Wait until the internal write cycle ends (device ACKs its address again). */
static EnumEEPROMSts _Fn_EEPROM_WaitReady (void)
{
    uint32_t u32Start = HAL_GetTick();

    while (HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDR, 1U, 2U) != HAL_OK)
    {
        if ((HAL_GetTick() - u32Start) > EEPROM_WRITE_CYCLE_MAX_MS)
        {
            return en_EEPROM_ERR_TIMEOUT;
        }
        osDelay(1);
    }
    return en_EEPROM_OK;
}

EnumEEPROMSts Fn_EEPROM_Read (uint16_t u16Addr, uint8_t *pu8Data, uint16_t u16Len)
{
    HAL_StatusTypeDef enHal;

    if ((pu8Data == NULL) || (u16Len == 0U) || (((uint32_t)u16Addr + u16Len) > EEPROM_SIZE))
    {
        return en_EEPROM_ERR_PARAM;
    }

    osMutexWait(mtx_eepromHandle, osWaitForever);
    enHal = HAL_I2C_Mem_Read(&hi2c1, EEPROM_I2C_ADDR, u16Addr, I2C_MEMADD_SIZE_16BIT,
                             pu8Data, u16Len, EEPROM_I2C_TIMEOUT_MS + (u16Len / 4U));
    osMutexRelease(mtx_eepromHandle);

    return (enHal == HAL_OK) ? en_EEPROM_OK : en_EEPROM_ERR_I2C;
}

/* Splits the write so no chunk crosses a 64-byte page boundary. */
EnumEEPROMSts Fn_EEPROM_Write (uint16_t u16Addr, const uint8_t *pu8Data, uint16_t u16Len)
{
    EnumEEPROMSts enSts = en_EEPROM_OK;
    uint16_t u16Chunk;

    if ((pu8Data == NULL) || (u16Len == 0U) || (((uint32_t)u16Addr + u16Len) > EEPROM_SIZE))
    {
        return en_EEPROM_ERR_PARAM;
    }

    osMutexWait(mtx_eepromHandle, osWaitForever);

    while (u16Len > 0U)
    {
        u16Chunk = (uint16_t)(EEPROM_PAGE_SIZE - (u16Addr % EEPROM_PAGE_SIZE));
        if (u16Chunk > u16Len)
        {
            u16Chunk = u16Len;
        }

        if (HAL_I2C_Mem_Write(&hi2c1, EEPROM_I2C_ADDR, u16Addr, I2C_MEMADD_SIZE_16BIT,
                              (uint8_t *)pu8Data, u16Chunk, EEPROM_I2C_TIMEOUT_MS) != HAL_OK)
        {
            enSts = en_EEPROM_ERR_I2C;
            break;
        }

        enSts = _Fn_EEPROM_WaitReady();
        if (enSts != en_EEPROM_OK)
        {
            break;
        }

        u16Addr += u16Chunk;
        pu8Data += u16Chunk;
        u16Len  -= u16Chunk;
    }

    osMutexRelease(mtx_eepromHandle);
    return enSts;
}

/*------------------------------------------------------------ Small records ----------------------------------------------------------
 * Same safety rule as the JSON region: written, then read back and
 * compared before the write is reported as successful.
 */
EnumEEPROMSts Fn_EEPROM_Record_Write (uint16_t u16Addr, uint32_t u32Magic, const void *pvData, uint16_t u16Len)
{
    uint8_t u8Buf[EEPROM_RECORD_HDR_SIZE + EEPROM_RECORD_MAX_LEN];
    uint8_t u8Chk[EEPROM_RECORD_HDR_SIZE + EEPROM_RECORD_MAX_LEN];
    uint16_t u16Total;
    EnumEEPROMSts enSts;

    if ((pvData == NULL) || (u16Len == 0U) || (u16Len > EEPROM_RECORD_MAX_LEN))
    {
        return en_EEPROM_ERR_PARAM;
    }

    u16Total = (uint16_t)(EEPROM_RECORD_HDR_SIZE + u16Len);

    _Fn_Put32(&u8Buf[0], u32Magic);
    _Fn_Put16(&u8Buf[4], u16Len);
    _Fn_Put32(&u8Buf[6], _Fn_CRC32_Update(0U, (const uint8_t *)pvData, u16Len));
    (void)memcpy(&u8Buf[EEPROM_RECORD_HDR_SIZE], pvData, u16Len);

    enSts = Fn_EEPROM_Write(u16Addr, u8Buf, u16Total);
    if (enSts != en_EEPROM_OK)
    {
        return enSts;
    }

    enSts = Fn_EEPROM_Read(u16Addr, u8Chk, u16Total);
    if (enSts != en_EEPROM_OK)
    {
        return enSts;
    }

    return (memcmp(u8Buf, u8Chk, u16Total) == 0) ? en_EEPROM_OK : en_EEPROM_ERR_VERIFY;
}

EnumEEPROMSts Fn_EEPROM_Record_Read (uint16_t u16Addr, uint32_t u32Magic, void *pvData, uint16_t u16Len)
{
    uint8_t u8Buf[EEPROM_RECORD_HDR_SIZE + EEPROM_RECORD_MAX_LEN];
    EnumEEPROMSts enSts;

    if ((pvData == NULL) || (u16Len == 0U) || (u16Len > EEPROM_RECORD_MAX_LEN))
    {
        return en_EEPROM_ERR_PARAM;
    }

    enSts = Fn_EEPROM_Read(u16Addr, u8Buf, (uint16_t)(EEPROM_RECORD_HDR_SIZE + u16Len));
    if (enSts != en_EEPROM_OK)
    {
        return enSts;
    }

    if ((_Fn_Get32(&u8Buf[0]) != u32Magic) || (_Fn_Get16(&u8Buf[4]) != u16Len))
    {
        return en_EEPROM_ERR_EMPTY;
    }

    if (_Fn_CRC32_Update(0U, &u8Buf[EEPROM_RECORD_HDR_SIZE], u16Len) != _Fn_Get32(&u8Buf[6]))
    {
        return en_EEPROM_ERR_CRC;
    }

    (void)memcpy(pvData, &u8Buf[EEPROM_RECORD_HDR_SIZE], u16Len);

    return en_EEPROM_OK;
}

/*------------------------------------------------------------ JSON region: read -------------------------------------------------------
 * Returns en_EEPROM_OK only for a complete, CRC-correct JSON. chBuf is
 * null-terminated.
 */
static uint8_t u8ActiveSlot = 0U;

uint8_t Fn_EEPROM_Get_Active_Slot (void)
{
    return u8ActiveSlot;
}

static uint16_t _Fn_Slot_Addr (uint8_t u8Slot)
{
    return (u8Slot == 0U) ? EEPROM_SLOT_A_ADDR : EEPROM_SLOT_B_ADDR;
}

/*------------------------------------------------------------ Header ----------------------------------------------------------------*/
static StructEepromHdr sLastHdr;

EnumEEPROMSts Fn_EEPROM_Hdr_Read (StructEepromHdr *psHdr)
{
    EnumEEPROMSts enSts = Fn_EEPROM_Record_Read(EEPROM_HDR_ADDR, EEPROM_HDR_MAGIC,
                                                psHdr, (uint16_t)sizeof(*psHdr));
    if (enSts == en_EEPROM_OK)
    {
        sLastHdr = *psHdr;
    }

    return enSts;
}

EnumEEPROMSts Fn_EEPROM_Hdr_Write (const StructEepromHdr *psHdr)
{
    EnumEEPROMSts enSts = Fn_EEPROM_Record_Write(EEPROM_HDR_ADDR, EEPROM_HDR_MAGIC,
                                                 psHdr, (uint16_t)sizeof(*psHdr));
    if (enSts == en_EEPROM_OK)
    {
        sLastHdr = *psHdr;
    }

    return enSts;
}

void Fn_EEPROM_Get_Boot_Info (uint32_t *pu32BootCount, uint8_t *pu8ResetReason)
{
    if (pu32BootCount  != NULL) { *pu32BootCount  = sLastHdr.u32BootCount;  }
    if (pu8ResetReason != NULL) { *pu8ResetReason = sLastHdr.u8ResetReason; }
}

static EnumEEPROMSts _Fn_Slot_Read (uint16_t u16Addr, char *chBuf, uint16_t u16BufSize)
{
    uint8_t  u8Hdr[EEPROM_JSON_HDR_SIZE];
    uint16_t u16Len;
    EnumEEPROMSts enSts;

    if ((chBuf == NULL) || (u16BufSize < 2U))
    {
        return en_EEPROM_ERR_PARAM;
    }

    enSts = Fn_EEPROM_Read(u16Addr, u8Hdr, sizeof(u8Hdr));
    if (enSts != en_EEPROM_OK)
    {
        return enSts;
    }

    if ((_Fn_Get32(&u8Hdr[0]) != EEPROM_JSON_MAGIC) || (_Fn_Get16(&u8Hdr[4]) != EEPROM_JSON_VERSION))
    {
        return en_EEPROM_ERR_EMPTY;
    }

    u16Len = _Fn_Get16(&u8Hdr[6]);
    if ((u16Len == 0U) || (u16Len > EEPROM_JSON_MAX_LEN) || (u16Len >= u16BufSize))
    {
        return en_EEPROM_ERR_TOO_LONG;
    }

    enSts = Fn_EEPROM_Read((uint16_t)(u16Addr + EEPROM_JSON_HDR_SIZE), (uint8_t *)chBuf, u16Len);
    if (enSts != en_EEPROM_OK)
    {
        return enSts;
    }
    chBuf[u16Len] = '\0';

    if (_Fn_CRC32_Update(0U, (const uint8_t *)chBuf, u16Len) != _Fn_Get32(&u8Hdr[8]))
    {
        return en_EEPROM_ERR_CRC;
    }

    return en_EEPROM_OK;
}

/*------------------------------------------------------------ JSON region: write ------------------------------------------------------
 * JSON text first, header last, then read back and compare the CRC.
 * If power fails during the write, the CRC no longer matches and the next
 * boot falls back to the default configuration.
 */
static EnumEEPROMSts _Fn_Slot_Write (uint16_t u16Addr, const char *chJson)
{
    uint8_t  u8Hdr[EEPROM_JSON_HDR_SIZE];
    uint8_t  u8Tmp[EEPROM_PAGE_SIZE];
    uint16_t u16Len, u16Pos, u16Part;
    uint32_t u32Crc, u32ReadCrc = 0U;
    EnumEEPROMSts enSts;

    if (chJson == NULL)
    {
        return en_EEPROM_ERR_PARAM;
    }

    u16Len = (uint16_t)strnlen(chJson, EEPROM_JSON_MAX_LEN + 1U);
    if ((u16Len == 0U) || (u16Len > EEPROM_JSON_MAX_LEN))
    {
        return en_EEPROM_ERR_TOO_LONG;
    }

    u32Crc = _Fn_CRC32_Update(0U, (const uint8_t *)chJson, u16Len);

    /* 1. JSON text */
    enSts = Fn_EEPROM_Write((uint16_t)(u16Addr + EEPROM_JSON_HDR_SIZE), (const uint8_t *)chJson, u16Len);
    if (enSts != en_EEPROM_OK)
    {
        return enSts;
    }

    /* 2. header */
    _Fn_Put32(&u8Hdr[0], EEPROM_JSON_MAGIC);
    _Fn_Put16(&u8Hdr[4], EEPROM_JSON_VERSION);
    _Fn_Put16(&u8Hdr[6], u16Len);
    _Fn_Put32(&u8Hdr[8], u32Crc);

    enSts = Fn_EEPROM_Write(u16Addr, u8Hdr, sizeof(u8Hdr));
    if (enSts != en_EEPROM_OK)
    {
        return enSts;
    }

    /* 3. read back in 64-byte pieces and verify */
    for (u16Pos = 0U; u16Pos < u16Len; u16Pos += u16Part)
    {
        u16Part = ((u16Len - u16Pos) > sizeof(u8Tmp)) ? (uint16_t)sizeof(u8Tmp) : (uint16_t)(u16Len - u16Pos);

        enSts = Fn_EEPROM_Read((uint16_t)(u16Addr + EEPROM_JSON_HDR_SIZE + u16Pos), u8Tmp, u16Part);
        if (enSts != en_EEPROM_OK)
        {
            return enSts;
        }
        u32ReadCrc = _Fn_CRC32_Update(u32ReadCrc, u8Tmp, u16Part);
    }

    return (u32ReadCrc == u32Crc) ? en_EEPROM_OK : en_EEPROM_ERR_VERIFY;
}

/*------------------------------------------------------------ JSON region: erase ------------------------------------------------------
 * Clears the header only; next boot finds no valid JSON and loads defaults.
 */
/*------------------------------------------------------------ Config: A/B slots ------------------------------------------------------
 * Read: active slot, then the other, then let the caller fall back to
 * the built-in defaults.
 */
EnumEEPROMSts Fn_EEPROM_JSON_Read (char *chBuf, uint16_t u16BufSize)
{
    StructEepromHdr sHdr;
    EnumEEPROMSts enSts;
    uint8_t u8First;

    /* No readable header just means "start with slot A". */
    u8First = (Fn_EEPROM_Hdr_Read(&sHdr) == en_EEPROM_OK) ? (uint8_t)(sHdr.u8ActiveSlot & 1U) : 0U;

    enSts = _Fn_Slot_Read(_Fn_Slot_Addr(u8First), chBuf, u16BufSize);
    if (enSts == en_EEPROM_OK)
    {
        u8ActiveSlot = u8First;
        return en_EEPROM_OK;
    }

    enSts = _Fn_Slot_Read(_Fn_Slot_Addr((uint8_t)(u8First ^ 1U)), chBuf, u16BufSize);
    if (enSts == en_EEPROM_OK)
    {
        u8ActiveSlot = (uint8_t)(u8First ^ 1U);
    }

    return enSts;
}

/* Write: into the inactive slot, verified, and only then is the header
 * switched. An interrupted save leaves the previous slot untouched and
 * still active. */
EnumEEPROMSts Fn_EEPROM_JSON_Write (const char *chJson)
{
    StructEepromHdr sHdr;
    EnumEEPROMSts enSts;
    uint8_t u8Target;

    if (Fn_EEPROM_Hdr_Read(&sHdr) != en_EEPROM_OK)
    {
        (void)memset(&sHdr, 0, sizeof(sHdr));
        sHdr.u8ActiveSlot = 1U;     /* so the first ever write lands in A */
    }

    u8Target = (uint8_t)((sHdr.u8ActiveSlot & 1U) ^ 1U);

    enSts = _Fn_Slot_Write(_Fn_Slot_Addr(u8Target), chJson);
    if (enSts != en_EEPROM_OK)
    {
        return enSts;               /* the active slot is still intact */
    }

    sHdr.u8ActiveSlot = u8Target;

    enSts = Fn_EEPROM_Hdr_Write(&sHdr);
    if (enSts == en_EEPROM_OK)
    {
        u8ActiveSlot = u8Target;
    }

    return enSts;
}

EnumEEPROMSts Fn_EEPROM_JSON_Erase (void)
{
    uint8_t u8Blank[EEPROM_JSON_HDR_SIZE];

    memset(u8Blank, 0xFF, sizeof(u8Blank));
    return Fn_EEPROM_Write(EEPROM_HDR_ADDR, u8Blank, sizeof(u8Blank));
}
