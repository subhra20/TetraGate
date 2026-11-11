/*
 * radio_manager.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Interrupt driven receive path for the four RFM95W radios.
 *
 *  This module sits on top of the proven LoRa.c driver and only calls its
 *  public functions. It adds the gateway side plumbing that the driver
 *  deliberately does not provide:
 *
 *    - DIO0 (RxDone) interrupt -> task notification, so nothing polls
 *    - per radio statistics: good packets, CRC errors, dropped packets
 *    - last RSSI and SNR of the received packet
 *    - sync word, so two networks on one frequency stay apart
 *    - RegVersion health check with automatic re-init of a dead radio
 *
 *  One task per radio. The four radios sit on four independent SPI
 *  peripherals (SPI1..SPI4), so their FIFO reads really do run in
 *  parallel. All four tasks run the same function body,
 *  Fn_Radio_Task_Service(), so there is still only one implementation.
 */

#ifndef INC_RADIO_MANAGER_H_
#define INC_RADIO_MANAGER_H_

#include "utils.h"

/* Default network ID. 0x34 is reserved for LoRaWAN, so stay off it. */
#define DEF_RADIO_SYNC_WORD_DEFAULT     0x12U

/*------------------------------------------------------------ Authentication -------------------------------------------------------
 * Nodes mark an authenticated frame by setting the top bit of the type
 * byte and appending a 4 byte AES-128-CMAC. See lora_crypto.h.
 *
 * Rollout matters here. Turning verification on while nodes are still
 * unauthenticated would silence the whole network, and nodes are flashed
 * one at a time. So the default policy REPORTS rather than enforces:
 *
 *   0 - a frame with no MIC is accepted and counted in u32RxNoMic. Use
 *       this while flashing nodes, and watch the PC dashboard until
 *       every node shows as authenticated.
 *   1 - a frame with no MIC is dropped. Switch to this once it does.
 *
 * A frame that CLAIMS a MIC and fails the check is always dropped, under
 * either policy: that is not a legacy node, it is a forgery or a key
 * mismatch, and neither should ever be believed.
 */
#define DEF_MIC_POLICY_REQUIRE          0
#define DEF_MIC_TYPE_FLAG               0x80U

/* How often a radio that hears nothing re-checks RegVersion */
#define DEF_RADIO_HEALTH_PERIOD_MS      1000U

/* Consecutive failed health checks before the radio is re-initialised */
#define DEF_RADIO_HEALTH_FAIL_LIMIT     3U

/*------------------------------------------------------------ Per radio live statistics ---------------------------------------------
 * Written only by that radio's own task, read by the UI and the PC link.
 * Every field is single word, so a reader never sees a torn value.
 */
typedef struct
{
    uint32_t u32RxGood;         /* packets accepted                        */
    uint32_t u32RxCrcErr;       /* PayloadCrcError set in RegIrqFlags      */
    uint32_t u32RxEmpty;        /* RxDone but zero bytes readable          */
    uint32_t u32RxDuplicate;    /* same node and sequence, heard twice     */
    uint32_t u32RxBlocked;      /* node not on the whitelist               */
    uint32_t u32RxMicOk;        /* MIC present and correct                 */
    uint32_t u32RxMicFail;      /* MIC present and WRONG - forged or wrong key */
    uint32_t u32RxNoMic;        /* no MIC at all - legacy node             */
    uint32_t u32HealthFail;     /* RegVersion reads that were not 0x12     */
    uint32_t u32ReInit;         /* automatic recoveries performed          */
    uint32_t u32LastRxTick;     /* osKernelSysTick() of the last good packet */
    uint32_t u32FreqHzActual;   /* read back from RegFr*, not what we asked for */
    int16_t  i16LastRssi;       /* dBm                                     */
    int8_t   i8LastSnr;         /* dB                                      */
    bool     bHealthy;          /* last RegVersion check passed            */
} StructRadioStats;

/*------------------------------------------------------------ API -------------------------------------------------------------------*/

/* Body of each of the four LoRa tasks. Never returns. */
void Fn_Radio_Task_Service (uint8_t u8LoRaSelector);

/* RegVersion == 0x12 ? Safe to call from any task. */
bool Fn_Radio_Health_Check (uint8_t u8LoRaSelector);

/* Network ID (RegSyncWord). Apply after the radio is initialised. */
void Fn_Radio_Set_Sync_Word (uint8_t u8LoRaSelector, uint8_t u8SyncWord);

/* Zeroes every per-radio counter. The frequency readback and health
   flag are left alone: those describe the radio, not the traffic. */
void Fn_Radio_Clear_Stats (void);

/* DEF_MIC_POLICY_REQUIRE is only the power-on default. The policy is
   switchable at runtime so a PC tool can move the gateway from
   reporting to enforcing once every node shows as authenticated,
   without a reflash. Not persisted: a reboot returns to the compiled
   default, deliberately, so a bad enforcement decision cannot lock the
   network out permanently. */
void Fn_Radio_Set_Mic_Required (bool bRequire);
bool Fn_Radio_Get_Mic_Required (void);

/* Exact carrier frequency in Hz.
 *
 * LoRa.c's LoRa_setFrequency() takes whole MHz, which cannot express a
 * channel like 868.1 MHz. Rather than change that proven function, this
 * writes RegFrMsb/Mid/Lsb through the driver's public register API, so
 * the register sequences in LoRa.c stay exactly as they are. */
void Fn_Radio_Set_Frequency_Hz (uint8_t u8LoRaSelector, uint32_t u32Hz);

/* What the radio's carrier registers actually hold, in Hz. */
uint32_t Fn_Radio_Get_Frequency_Hz (uint8_t u8LoRaSelector);

/* Read only view of the statistics, NULL if the index is out of range. */
const StructRadioStats *Fn_Radio_Get_Stats (uint8_t u8LoRaSelector);

/* Ask a radio to re-apply sLoRaGtwConfParam. The work is done by that
 * radio's own task, so the caller never touches its SPI bus - which is
 * what lets the UI change settings safely. */
void Fn_Radio_Request_Reconfig (uint8_t u8LoRaSelector);

#endif /* INC_RADIO_MANAGER_H_ */
