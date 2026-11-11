/*
 * node_packet.h
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  Builds the frames this node puts on air, and simulates the readings
 *  they carry. No sensor is fitted yet, so the values are generated -
 *  see node_packet.c for why they are generated the way they are.
 *
 *  Frame layout, fixed by the gateway's architecture document
 *  (section 10) and parsed by its node_table:
 *
 *      [0]      type        0x01 data, 0x02 status
 *      [1..2]   node ID     little endian
 *      [3]      sequence
 *      [4...]   payload
 *
 *  Data frame, 14 bytes:
 *      [4..5]   temperature   int16  LE, centi-degrees C
 *      [6..7]   humidity      uint16 LE, centi-percent
 *      [8..9]   battery       uint16 LE, millivolts
 *      [10..13] uptime        uint32 LE, seconds
 *
 *  Status frame, 10 bytes:
 *      [4..5]   battery       uint16 LE, millivolts
 *      [6..9]   uptime        uint32 LE, seconds
 */

#ifndef INC_NODE_PACKET_H_
#define INC_NODE_PACKET_H_

#include "main.h"
#include "node_config.h"
#include "lora_crypto.h"
#include <stdbool.h>

/* Body is the part the MIC is computed over; the frame is the body plus
   the MIC, and is what goes on air. */
#define DEF_NODE_BODY_DATA_LEN      14U
#define DEF_NODE_BODY_STATUS_LEN    10U

#if (DEF_NODE_MIC_ENABLE == 1)
#define DEF_NODE_MIC_LEN            LORA_MIC_BYTES
#else
#define DEF_NODE_MIC_LEN            0U
#endif

#define DEF_NODE_FRAME_DATA_LEN     (DEF_NODE_BODY_DATA_LEN + DEF_NODE_MIC_LEN)
#define DEF_NODE_FRAME_STATUS_LEN   (DEF_NODE_BODY_STATUS_LEN + DEF_NODE_MIC_LEN)

/* The simulated environment, kept so it can be read over SWD: this
   board has no UART, so a debugger watch is the only way to see it. */
typedef struct
{
    int16_t  i16TempCc;         /* centi-degrees C   */
    uint16_t u16HumidityCp;     /* centi-percent     */
    uint16_t u16BatteryMv;
    uint32_t u32UptimeS;
} StructNodeReading;

void Fn_Node_Packet_Init (void);

/* Advances the simulated readings one step and frames them.
   Returns the frame length, or 0 if the buffer is too small. */
uint8_t Fn_Node_Packet_Build (uint8_t *pu8Buf, uint8_t u8BufSize,
                              uint8_t u8Seq, bool bStatusFrame);

const StructNodeReading *Fn_Node_Packet_Get_Reading (void);

#endif /* INC_NODE_PACKET_H_ */
