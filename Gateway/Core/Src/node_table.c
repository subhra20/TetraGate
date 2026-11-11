/*
 * node_table.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See node_table.h.
 */

#include "node_table.h"
#include "eeprom_operation.h"

static StructNode sNode[NODE_TABLE_MAX];
static uint16_t   u16NodeCount;

static uint16_t   u16Whitelist[NODE_WHITELIST_MAX];
static uint16_t   u16WhitelistCount;

static osMutexId  mtx_nodeHandle = NULL;

/* Stored form of the whitelist: count followed by the IDs. */
typedef struct
{
    uint16_t u16Count;
    uint16_t u16Id[NODE_WHITELIST_MAX];
} StructWhitelistRecord;

void Fn_NodeTable_Init (void)
{
    if (mtx_nodeHandle == NULL)
    {
        osMutexDef(mtx_node);
        mtx_nodeHandle = osMutexCreate(osMutex(mtx_node));
    }

    u16NodeCount      = 0U;
    u16WhitelistCount = 0U;
}

/*------------------------------------------------------------ Whitelist -------------------------------------------------------------*/
static bool _Fn_Whitelist_Find (uint16_t u16NodeId, uint16_t *pu16Index)
{
    uint16_t i;

    for (i = 0U; i < u16WhitelistCount; i++)
    {
        if (u16Whitelist[i] == u16NodeId)
        {
            if (pu16Index != NULL)
            {
                *pu16Index = i;
            }
            return true;
        }
    }

    return false;
}

uint16_t Fn_NodeTable_Whitelist_Count (void)
{
    return u16WhitelistCount;
}

bool Fn_NodeTable_Whitelist_Contains (uint16_t u16NodeId)
{
    bool bFound;

    osMutexWait(mtx_nodeHandle, osWaitForever);
    bFound = _Fn_Whitelist_Find(u16NodeId, NULL);
    osMutexRelease(mtx_nodeHandle);

    return bFound;
}

bool Fn_NodeTable_Whitelist_Add (uint16_t u16NodeId)
{
    bool bOk = false;

    osMutexWait(mtx_nodeHandle, osWaitForever);

    if (_Fn_Whitelist_Find(u16NodeId, NULL))
    {
        bOk = true;                     /* already listed */
    }
    else if (u16WhitelistCount < NODE_WHITELIST_MAX)
    {
        u16Whitelist[u16WhitelistCount] = u16NodeId;
        u16WhitelistCount++;
        bOk = true;
    }

    osMutexRelease(mtx_nodeHandle);

    return bOk;
}

bool Fn_NodeTable_Whitelist_Remove (uint16_t u16NodeId)
{
    uint16_t u16Idx;
    bool bOk = false;

    osMutexWait(mtx_nodeHandle, osWaitForever);

    if (_Fn_Whitelist_Find(u16NodeId, &u16Idx))
    {
        /* Order does not matter, so close the gap with the last entry. */
        u16WhitelistCount--;
        u16Whitelist[u16Idx] = u16Whitelist[u16WhitelistCount];
        bOk = true;
    }

    osMutexRelease(mtx_nodeHandle);

    return bOk;
}

bool Fn_NodeTable_Whitelist_Get (uint16_t u16Index, uint16_t *pu16NodeId)
{
    bool bOk = false;

    osMutexWait(mtx_nodeHandle, osWaitForever);

    if ((u16Index < u16WhitelistCount) && (pu16NodeId != NULL))
    {
        *pu16NodeId = u16Whitelist[u16Index];
        bOk = true;
    }

    osMutexRelease(mtx_nodeHandle);

    return bOk;
}

bool Fn_NodeTable_Whitelist_Load (void)
{
    static StructWhitelistRecord sRec;

    if (Fn_EEPROM_Record_Read(EEPROM_WHITELIST_ADDR, EEPROM_WHITELIST_MAGIC,
                              &sRec, (uint16_t)sizeof(sRec)) != en_EEPROM_OK)
    {
        return false;
    }

    if (sRec.u16Count > NODE_WHITELIST_MAX)
    {
        return false;
    }

    osMutexWait(mtx_nodeHandle, osWaitForever);
    u16WhitelistCount = sRec.u16Count;
    (void)memcpy(u16Whitelist, sRec.u16Id, (size_t)sRec.u16Count * sizeof(uint16_t));
    osMutexRelease(mtx_nodeHandle);

    return true;
}

bool Fn_NodeTable_Whitelist_Save (void)
{
    static StructWhitelistRecord sRec;

    osMutexWait(mtx_nodeHandle, osWaitForever);
    sRec.u16Count = u16WhitelistCount;
    (void)memcpy(sRec.u16Id, u16Whitelist, (size_t)u16WhitelistCount * sizeof(uint16_t));
    osMutexRelease(mtx_nodeHandle);

    return (Fn_EEPROM_Record_Write(EEPROM_WHITELIST_ADDR, EEPROM_WHITELIST_MAGIC,
                                   &sRec, (uint16_t)sizeof(sRec)) == en_EEPROM_OK);
}

/*------------------------------------------------------------ Table -----------------------------------------------------------------*/
static StructNode *_Fn_Node_Find (uint16_t u16NodeId)
{
    uint16_t i;

    for (i = 0U; i < u16NodeCount; i++)
    {
        if (sNode[i].u16NodeId == u16NodeId)
        {
            return &sNode[i];
        }
    }

    return NULL;
}

EnumNodeResult Fn_NodeTable_Update (uint8_t u8Radio, const uint8_t *pu8Data, uint8_t u8Len,
                                    int16_t i16Rssi, int8_t i8Snr)
{
    StructNode *p;
    uint32_t u32Now;
    uint32_t u32Gap;
    uint16_t u16Id;
    uint8_t  u8Seq;
    EnumNodeResult enResult = en_Node_Accepted;

    if ((pu8Data == NULL) || (u8Len < NODE_PKT_MIN_LEN))
    {
        return en_Node_BadLength;
    }

    u16Id = (uint16_t)((uint16_t)pu8Data[1] | ((uint16_t)pu8Data[2] << 8));
    u8Seq = pu8Data[3];

    osMutexWait(mtx_nodeHandle, osWaitForever);

    /* An empty whitelist means commissioning: accept everything. */
    if ((u16WhitelistCount > 0U) && !_Fn_Whitelist_Find(u16Id, NULL))
    {
        osMutexRelease(mtx_nodeHandle);
        return en_Node_NotWhitelisted;
    }

    u32Now = osKernelSysTick();
    p      = _Fn_Node_Find(u16Id);

    if (p == NULL)
    {
        if (u16NodeCount >= NODE_TABLE_MAX)
        {
            osMutexRelease(mtx_nodeHandle);
            return en_Node_BadLength;       /* table full: nothing sensible to do */
        }

        p = &sNode[u16NodeCount];
        u16NodeCount++;

        (void)memset(p, 0, sizeof(*p));
        p->u16NodeId      = u16Id;
        p->u32FirstSeenMs = u32Now;
        p->u32LastSeenMs  = u32Now;
        p->u8LastSeq      = u8Seq;
        p->u32Packets     = 1U;
        p->u8Radio        = u8Radio;
        p->i16LastRssi    = i16Rssi;
        p->i8LastSnr      = i8Snr;

        osMutexRelease(mtx_nodeHandle);
        return en_Node_Accepted;
    }

    u32Gap = u32Now - p->u32LastSeenMs;

    /* The same node and sequence again, soon after: one transmission
     * heard twice, typically by two radios. Keep the stronger reading. */
    if ((p->u8LastSeq == u8Seq) && (u32Gap < NODE_DUPLICATE_WINDOW_MS))
    {
        p->u32Duplicates++;

        if (i16Rssi > p->i16LastRssi)
        {
            p->i16LastRssi = i16Rssi;
            p->i8LastSnr   = i8Snr;
            p->u8Radio     = u8Radio;
        }

        osMutexRelease(mtx_nodeHandle);
        return en_Node_Duplicate;
    }

    /* Learn the reporting interval with a simple smoothing filter, so a
     * single late packet does not swing the silent threshold. */
    if (u32Gap > 0U)
    {
        p->u32IntervalMs = (p->u32IntervalMs == 0U) ?
                           u32Gap : ((p->u32IntervalMs * 3U) + u32Gap) / 4U;
    }

    p->u32LastSeenMs = u32Now;
    p->u8LastSeq     = u8Seq;
    p->u32Packets++;
    p->u8Radio       = u8Radio;
    p->i16LastRssi   = i16Rssi;
    p->i8LastSnr     = i8Snr;
    p->bSilent       = false;

    osMutexRelease(mtx_nodeHandle);

    return enResult;
}

uint16_t Fn_NodeTable_Count (void)
{
    return u16NodeCount;
}

bool Fn_NodeTable_Get (uint16_t u16Index, StructNode *psOut)
{
    bool bOk = false;

    if ((psOut == NULL) || (mtx_nodeHandle == NULL))
    {
        return false;
    }

    osMutexWait(mtx_nodeHandle, osWaitForever);

    if (u16Index < u16NodeCount)
    {
        *psOut = sNode[u16Index];
        bOk    = true;
    }

    osMutexRelease(mtx_nodeHandle);

    return bOk;
}

void Fn_NodeTable_Check_Silent (void)
{
    uint32_t u32Now;
    uint32_t u32Limit;
    uint16_t i;

    if (mtx_nodeHandle == NULL)
    {
        return;
    }

    u32Now = osKernelSysTick();

    osMutexWait(mtx_nodeHandle, osWaitForever);

    for (i = 0U; i < u16NodeCount; i++)
    {
        /* A node that has only ever sent once has no interval to judge
         * it by, so it is never called silent. */
        if (sNode[i].u32IntervalMs == 0U)
        {
            continue;
        }

        u32Limit = sNode[i].u32IntervalMs * NODE_SILENT_FACTOR;
        if (u32Limit < NODE_SILENT_FLOOR_MS)
        {
            u32Limit = NODE_SILENT_FLOOR_MS;
        }

        sNode[i].bSilent = ((u32Now - sNode[i].u32LastSeenMs) > u32Limit);
    }

    osMutexRelease(mtx_nodeHandle);
}

uint16_t Fn_NodeTable_Silent_Count (void)
{
    uint16_t u16N = 0U;
    uint16_t i;

    if (mtx_nodeHandle == NULL)
    {
        return 0U;
    }

    osMutexWait(mtx_nodeHandle, osWaitForever);
    for (i = 0U; i < u16NodeCount; i++)
    {
        if (sNode[i].bSilent)
        {
            u16N++;
        }
    }
    osMutexRelease(mtx_nodeHandle);

    return u16N;
}
