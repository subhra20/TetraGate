/*
 * node_packet.c
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  See node_packet.h for the frame layout.
 *
 *  The readings are a bounded random walk rather than a fixed string or
 *  a counter. A constant payload would make the gateway's payload bar
 *  and RSSI history look identical every report, which tests the link
 *  but tests none of the display; a walk drifts the way a real sensor
 *  does and exercises the full range of those widgets. It is integer
 *  only - this part has no FPU and 32 KB of flash, so pulling in
 *  software floating point for cosmetic noise would be a poor trade.
 *
 *  The generator is seeded from the MCU's unique ID, so two nodes
 *  flashed with the same firmware do not report identical values and
 *  can be told apart on the gateway by more than their node ID.
 */

#include "node_config.h"
#include "node_packet.h"
#include "lora_crypto.h"
#include "lora_keys.h"
#include "LoRa.h"

static StructNodeReading sReading;
static uint32_t          u32Lcg;
static uint8_t           u8BattTick;

#if (DEF_NODE_MIC_ENABLE == 1)
/* Derived once at start-up. The node ID never changes, so deriving per
   frame would repeat the same two AES blocks forever. */
static uint8_t au8NodeKey[16];
#endif

/*------------------------------------------------------------ Generator -------------------------------------------------------------*/
static uint32_t _Fn_Rand (void)
{
    /* Numerical Recipes constants. The low bits of an LCG are poor, so
       the result is taken from the top half. */
    u32Lcg = (u32Lcg * 1664525UL) + 1013904223UL;

    return (u32Lcg >> 16);
}

/* One step of a random walk, clamped to a plausible range. */
static int32_t _Fn_Walk (int32_t i32Val, int32_t i32Step, int32_t i32Min, int32_t i32Max)
{
    i32Val += (int32_t)(_Fn_Rand() % (uint32_t)((2 * i32Step) + 1)) - i32Step;

    if (i32Val < i32Min)
    {
        i32Val = i32Min;
    }
    if (i32Val > i32Max)
    {
        i32Val = i32Max;
    }

    return i32Val;
}

void Fn_Node_Packet_Init (void)
{
#if (DEF_NODE_MIC_ENABLE == 1)
    static const uint8_t au8Master[16] = DEF_LORA_MASTER_KEY_INIT;

    Fn_Mic_Derive_Key(au8Master, (uint16_t)DEF_NODE_ID, au8NodeKey);
#endif

    u32Lcg = HAL_GetUIDw0() ^ ((uint32_t)DEF_NODE_ID << 16);

    /* A plausible indoor starting point. */
    sReading.i16TempCc     = 2700;      /* 27.00 C  */
    sReading.u16HumidityCp = 6500;      /* 65.00 %  */
    sReading.u16BatteryMv  = 3900U;     /* fresh cell */
    sReading.u32UptimeS    = 0U;
    u8BattTick             = 0U;
}

static void _Fn_Advance_Reading (void)
{
    sReading.i16TempCc =
        (int16_t)_Fn_Walk(sReading.i16TempCc, 25, 1800, 4200);       /* +-0.25 C a step */

    sReading.u16HumidityCp =
        (uint16_t)_Fn_Walk((int32_t)sReading.u16HumidityCp, 40, 3000, 9500);

    /* Battery only ever falls, a millivolt at a time, so the trend on
       the gateway means something. Floor is a flat cell, not zero. */
    u8BattTick++;
    if ((u8BattTick >= 4U) && (sReading.u16BatteryMv > 3300U))
    {
        sReading.u16BatteryMv--;
        u8BattTick = 0U;
    }

    sReading.u32UptimeS = HAL_GetTick() / 1000UL;
}

/*------------------------------------------------------------ Framing ---------------------------------------------------------------*/
static void _Fn_Put_U16 (uint8_t *pu8Buf, uint16_t u16Val)
{
    pu8Buf[0] = (uint8_t)(u16Val & 0xFFU);
    pu8Buf[1] = (uint8_t)(u16Val >> 8);
}

static void _Fn_Put_U32 (uint8_t *pu8Buf, uint32_t u32Val)
{
    pu8Buf[0] = (uint8_t)(u32Val & 0xFFU);
    pu8Buf[1] = (uint8_t)((u32Val >> 8) & 0xFFU);
    pu8Buf[2] = (uint8_t)((u32Val >> 16) & 0xFFU);
    pu8Buf[3] = (uint8_t)((u32Val >> 24) & 0xFFU);
}

uint8_t Fn_Node_Packet_Build (uint8_t *pu8Buf, uint8_t u8BufSize,
                              uint8_t u8Seq, bool bStatusFrame)
{
    uint8_t u8Body = bStatusFrame ? DEF_NODE_BODY_STATUS_LEN : DEF_NODE_BODY_DATA_LEN;
    uint8_t u8Len  = bStatusFrame ? DEF_NODE_FRAME_STATUS_LEN : DEF_NODE_FRAME_DATA_LEN;

    if ((pu8Buf == NULL) || (u8BufSize < u8Len))
    {
        return 0U;
    }

    _Fn_Advance_Reading();

    pu8Buf[0] = (uint8_t)((bStatusFrame ? DEF_NODE_TYPE_STATUS : DEF_NODE_TYPE_DATA)
#if (DEF_NODE_MIC_ENABLE == 1)
                          | DEF_NODE_TYPE_MIC_FLAG
#endif
                         );
    _Fn_Put_U16(&pu8Buf[1], (uint16_t)DEF_NODE_ID);
    pu8Buf[3] = u8Seq;

    if (bStatusFrame)
    {
        _Fn_Put_U16(&pu8Buf[4], sReading.u16BatteryMv);
        _Fn_Put_U32(&pu8Buf[6], sReading.u32UptimeS);
    }
    else
    {
        _Fn_Put_U16(&pu8Buf[4], (uint16_t)sReading.i16TempCc);
        _Fn_Put_U16(&pu8Buf[6], sReading.u16HumidityCp);
        _Fn_Put_U16(&pu8Buf[8], sReading.u16BatteryMv);
        _Fn_Put_U32(&pu8Buf[10], sReading.u32UptimeS);
    }

#if (DEF_NODE_MIC_ENABLE == 1)
    /* Covers everything already written, so the type, node ID and
       sequence number are all authenticated: a forger cannot change the
       claimed node ID without invalidating the MIC. */
    Fn_Mic_Compute(au8NodeKey, pu8Buf, (uint16_t)u8Body, &pu8Buf[u8Body]);
#else
    (void)u8Body;
#endif

    return u8Len;
}

const StructNodeReading *Fn_Node_Packet_Get_Reading (void)
{
    return &sReading;
}
