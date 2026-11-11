/*
 * packet_log.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See packet_log.h.
 */

#include "packet_log.h"

static StructPktEntry sEntry[PKT_LOG_DEPTH];
static uint16_t       u16Head;          /* next slot to write        */
static uint16_t       u16Count;         /* valid entries, <= depth   */
static uint32_t       u32Total;         /* everything ever pushed    */
static osMutexId      mtx_pktlogHandle = NULL;

void Fn_PktLog_Init (void)
{
    if (mtx_pktlogHandle == NULL)
    {
        osMutexDef(mtx_pktlog);
        mtx_pktlogHandle = osMutexCreate(osMutex(mtx_pktlog));
    }

    u16Head  = 0U;
    u16Count = 0U;
    u32Total = 0U;
}

void Fn_PktLog_Push (uint8_t u8Radio, const uint8_t *pu8Data, uint8_t u8Len,
                     int16_t i16Rssi, int8_t i8Snr)
{
    StructPktEntry *p;
    uint8_t u8Copy;

    if ((pu8Data == NULL) || (mtx_pktlogHandle == NULL))
    {
        return;
    }

    u8Copy = (u8Len > PKT_LOG_PREVIEW_BYTES) ? PKT_LOG_PREVIEW_BYTES : u8Len;

    osMutexWait(mtx_pktlogHandle, osWaitForever);

    p = &sEntry[u16Head];
    p->u32Tick = osKernelSysTick();
    p->u8Radio = u8Radio;
    p->u8Len   = u8Len;
    p->i16Rssi = i16Rssi;
    p->i8Snr   = i8Snr;
    (void)memset(p->u8Data, 0, sizeof(p->u8Data));
    (void)memcpy(p->u8Data, pu8Data, u8Copy);

    u16Head = (uint16_t)((u16Head + 1U) % PKT_LOG_DEPTH);
    if (u16Count < PKT_LOG_DEPTH)
    {
        u16Count++;
    }
    u32Total++;

    osMutexRelease(mtx_pktlogHandle);
}

uint16_t Fn_PktLog_Count (void)
{
    uint16_t u16N;

    if (mtx_pktlogHandle == NULL)
    {
        return 0U;
    }

    osMutexWait(mtx_pktlogHandle, osWaitForever);
    u16N = u16Count;
    osMutexRelease(mtx_pktlogHandle);

    return u16N;
}

uint32_t Fn_PktLog_Total (void)
{
    return u32Total;
}

bool Fn_PktLog_Get (uint16_t u16Index, StructPktEntry *psOut)
{
    bool bOk = false;
    uint16_t u16Slot;

    if ((psOut == NULL) || (mtx_pktlogHandle == NULL))
    {
        return false;
    }

    osMutexWait(mtx_pktlogHandle, osWaitForever);

    if (u16Index < u16Count)
    {
        /* Walk back from the newest entry, wrapping at zero. */
        u16Slot = (uint16_t)((u16Head + PKT_LOG_DEPTH - 1U - u16Index) % PKT_LOG_DEPTH);
        *psOut  = sEntry[u16Slot];
        bOk     = true;
    }

    osMutexRelease(mtx_pktlogHandle);

    return bOk;
}

void Fn_PktLog_Clear (void)
{
    if (mtx_pktlogHandle == NULL)
    {
        return;
    }

    osMutexWait(mtx_pktlogHandle, osWaitForever);
    u16Head  = 0U;
    u16Count = 0U;
    osMutexRelease(mtx_pktlogHandle);
}
