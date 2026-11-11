/*
 * packet_log.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Ring buffer of recently received packets, newest first.
 *
 *  Written by the four radio tasks and read by the UI task, so every
 *  access is behind one mutex. Only the first few payload bytes are
 *  kept: the Packets screen shows a preview, not the whole frame, and
 *  storing 32 x 255 bytes would cost 8 KB for no benefit.
 */

#ifndef INC_PACKET_LOG_H_
#define INC_PACKET_LOG_H_

#include "utils.h"

#define PKT_LOG_DEPTH           32U
#define PKT_LOG_PREVIEW_BYTES   8U

typedef struct
{
    uint32_t u32Tick;                           /* osKernelSysTick() at receipt */
    uint8_t  u8Radio;                           /* 0 .. DEF_MAX_LoRa_Module - 1 */
    uint8_t  u8Len;                             /* full length on air           */
    int16_t  i16Rssi;
    int8_t   i8Snr;
    uint8_t  u8Data[PKT_LOG_PREVIEW_BYTES];
} StructPktEntry;

void     Fn_PktLog_Init  (void);
void     Fn_PktLog_Push  (uint8_t u8Radio, const uint8_t *pu8Data, uint8_t u8Len,
                          int16_t i16Rssi, int8_t i8Snr);

/* Entries currently held, at most PKT_LOG_DEPTH. */
uint16_t Fn_PktLog_Count (void);

/* Everything ever received, which keeps counting past the ring depth. */
uint32_t Fn_PktLog_Total (void);

/* Index 0 is the newest. False when the index is past the end. */
bool     Fn_PktLog_Get   (uint16_t u16Index, StructPktEntry *psOut);

void     Fn_PktLog_Clear (void);

#endif /* INC_PACKET_LOG_H_ */
