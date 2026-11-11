/*
 * node_table.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  One row per end node heard on any radio, as described in section 7
 *  of the architecture document (features 7 to 10).
 *
 *  The table is live state and is NOT stored: it rebuilds itself from
 *  traffic within one reporting interval. The whitelist IS stored,
 *  because it is configuration rather than observation.
 *
 *  Packet header (architecture document, section 10):
 *     [0]    type        0x01 data, 0x02 status, 0x10 command, 0x20 ACK
 *     [1..2] node ID     little endian
 *     [3]    sequence    matches ACKs, and detects duplicates
 *
 *  Written by the four radio tasks and read by the UI, so one mutex
 *  guards the whole table.
 */

#ifndef INC_NODE_TABLE_H_
#define INC_NODE_TABLE_H_

#include "utils.h"

#define NODE_TABLE_MAX          100U
#define NODE_WHITELIST_MAX      100U

/* Shortest header that carries a node ID and a sequence number. */
#define NODE_PKT_MIN_LEN        4U

/* A repeat of the same node and sequence inside this window is the same
 * transmission heard again, usually on another radio. */
#define NODE_DUPLICATE_WINDOW_MS    4000U

/* A node is silent once it has missed this multiple of its own learned
 * interval. Nodes need at least two packets before an interval exists. */
#define NODE_SILENT_FACTOR      3U
#define NODE_SILENT_FLOOR_MS    60000U

typedef struct
{
    uint16_t u16NodeId;
    uint8_t  u8Radio;           /* radio it was last heard on       */
    uint8_t  u8LastSeq;
    uint32_t u32FirstSeenMs;
    uint32_t u32LastSeenMs;
    uint32_t u32Packets;
    uint32_t u32Duplicates;
    uint32_t u32IntervalMs;     /* learned, smoothed                */
    int16_t  i16LastRssi;
    int8_t   i8LastSnr;
    bool     bSilent;
} StructNode;

typedef enum
{
    en_Node_Accepted = 0,
    en_Node_Duplicate,          /* same ID and sequence, within the window */
    en_Node_NotWhitelisted,
    en_Node_BadLength,          /* too short to carry a node ID            */
} EnumNodeResult;

void Fn_NodeTable_Init (void);

/* Classify and record a received frame. Call from a radio task. */
EnumNodeResult Fn_NodeTable_Update (uint8_t u8Radio, const uint8_t *pu8Data, uint8_t u8Len,
                                    int16_t i16Rssi, int8_t i8Snr);

uint16_t Fn_NodeTable_Count (void);
bool     Fn_NodeTable_Get (uint16_t u16Index, StructNode *psOut);
uint16_t Fn_NodeTable_Silent_Count (void);

/* Re-evaluates the silent flags. Call periodically from the monitor. */
void Fn_NodeTable_Check_Silent (void);

/*------------------------------------------------------------ Whitelist -------------------------------------------------------------
 * Empty means "accept every node", which is the useful default while
 * commissioning. Once any ID is listed, only listed nodes are accepted.
 */
uint16_t Fn_NodeTable_Whitelist_Count (void);
bool     Fn_NodeTable_Whitelist_Contains (uint16_t u16NodeId);
bool     Fn_NodeTable_Whitelist_Add (uint16_t u16NodeId);
bool     Fn_NodeTable_Whitelist_Remove (uint16_t u16NodeId);
bool     Fn_NodeTable_Whitelist_Get (uint16_t u16Index, uint16_t *pu16NodeId);

/* EEPROM, via the generic record API. */
bool Fn_NodeTable_Whitelist_Load (void);
bool Fn_NodeTable_Whitelist_Save (void);

#endif /* INC_NODE_TABLE_H_ */
