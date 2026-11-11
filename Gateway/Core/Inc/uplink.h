/*
 * uplink.h
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  Northbound data feed: one line of JSON per event on USART3 at
 *  115200, for a PC to read and visualise.
 *
 *  This is the counterpart to the binary frame the nodes send. The air
 *  link is 14 packed bytes because airtime is expensive; once the data
 *  is on a wire, bandwidth is free and a self describing text format is
 *  worth far more than the bytes it costs. One JSON object per line,
 *  terminated CRLF, so a reader can work line by line with no framing
 *  of its own.
 *
 *  Line types:
 *
 *    hello    once at start-up, carries the schema version so a PC tool
 *             can refuse a firmware it does not understand
 *    packet   one per accepted frame, with the node payload decoded
 *    status   every DEF_UPLINK_STATUS_PERIOD_MS, gateway and radio health
 *
 *  Nothing here ever blocks a caller for long. The UART is shared with
 *  the boot log through sem_lora_uartHandle, and the callers are the
 *  radio tasks and the monitor task - and the monitor task is the
 *  watchdog feeder, so a lost line is always preferable to a wait. The
 *  semaphore is therefore taken with a timeout and the line is dropped
 *  if the port is busy.
 */

#ifndef INC_UPLINK_H_
#define INC_UPLINK_H_

#include "utils.h"

/* One switch to silence the feed entirely, for a gateway running
   standalone with nothing attached to USART3. */
#define DEF_UPLINK_ENABLE               1

#define DEF_UPLINK_SCHEMA               1U

/* Dropped rather than waited for. See the note above. */
#define DEF_UPLINK_SEM_WAIT_MS          50U

#define DEF_UPLINK_STATUS_PERIOD_MS     2000U

/* Bytes of payload echoed as hex when a frame cannot be decoded. */
#define DEF_UPLINK_RAW_BYTES            8U

/* How a frame fared against its message integrity code. Reported per
   packet so the PC can show which nodes are authenticated during a
   rollout, rather than only a pass/fail total. */
typedef enum
{
    en_Mic_Absent = 0,      /* no MIC on the frame: node not yet reflashed */
    en_Mic_Ok,              /* MIC present and correct                     */
    en_Mic_Fail,            /* MIC present and wrong - never delivered     */
} EnumMicState;

/* Emits the hello line. Call once, after the radios are up. */
void Fn_Uplink_Hello (void);

/* One accepted frame. Called from the radio task with the FULL receive
   buffer, not the truncated copy the packet log keeps. */
void Fn_Uplink_Packet (uint8_t u8Radio, const uint8_t *pu8Data, uint8_t u8Len,
                       int16_t i16Rssi, int8_t i8Snr, EnumMicState enMic);

/* Call every monitor iteration; emits only when the period is due, so
   the caller needs no timer of its own. */
void Fn_Uplink_Status_Tick (void);

/* Lines the UART was too busy to take. Visible on request so a quiet
   feed can be told from a dropped one. */
uint32_t Fn_Uplink_Get_Dropped (void);

/* Sends one pre-built line through the same serialised port the feed
   uses, so command replies cannot interleave with a status line and
   produce two corrupt JSON objects. The caller supplies the trailing
   CRLF. Drops the line rather than wait, like everything else here. */
void Fn_Uplink_Send_Line (const char *pchLine);

#endif /* INC_UPLINK_H_ */
