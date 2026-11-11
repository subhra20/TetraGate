/*
 * uplink.c
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  See uplink.h.
 *
 *  Two implementation notes worth knowing before changing anything.
 *
 *  The line buffer is static, not automatic. The radio tasks and the
 *  monitor task each have a 1 KB stack with roughly 480 bytes spare, so
 *  a 512 byte frame buffer on their stack would overflow it. The buffer
 *  is safe to share because it is only touched while the UART semaphore
 *  is held.
 *
 *  No float is printed anywhere. The build has no printf floating point
 *  support, and enabling it to render a temperature would cost several
 *  kilobytes. Scaled integers go out exactly as the node sent them -
 *  temp_cc is centi-degrees, hum_cp is centi-percent - and the PC
 *  divides. The wire stays lossless either way.
 */

#include "uplink.h"
#include "radio_manager.h"
#include "node_table.h"
#include "monitor.h"
#include "lora_crypto.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"

/* Defined in freertos.c, which also uses it for the boot log. */
extern osSemaphoreId sem_lora_uartHandle;

/*------------------------------------------------------------ Node frame layout -----------------------------------------------------
 * Mirrors node_packet.h on the transmitter. Kept as plain offsets
 * rather than a packed struct: the frame is little endian on air and a
 * struct cast would depend on this compiler agreeing about padding.
 */
#define UP_OFF_TYPE         0U
#define UP_OFF_ID           1U
#define UP_OFF_SEQ          3U

#define UP_TYPE_DATA        0x01U
#define UP_TYPE_STATUS      0x02U

#define UP_DATA_LEN         14U
#define UP_STATUS_LEN       10U

/* Sized for the worst case status line, which is the long one: about
   160 bytes of header plus four radio objects that reach ~135 bytes
   each once the packet counters grow to ten digits, so ~700. At 512 the
   overflow guard below fired after the third radio and radio 4 was
   silently missing from the feed. */
#define UP_BUF_BYTES        1024

static char     chLine[UP_BUF_BYTES];
static uint32_t u32Dropped;
static uint32_t u32NextStatusTick;

/* Result of the on-target AES and CMAC vector check, reported in every
   status line. If this is ever 0 the gateway cannot verify anything and
   every MIC check is meaningless, so the PC must be able to see it. */
static bool     bCryptoOk;

/*------------------------------------------------------------ Little endian readers -------------------------------------------------*/
static uint16_t _Fn_U16 (const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t _Fn_U32 (const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*------------------------------------------------------------ Transmit --------------------------------------------------------------
 * Takes the semaphore with a timeout and gives up rather than wait. A
 * caller here is either a radio task, which should be back listening,
 * or the monitor task, which must get back to feeding the watchdog.
 */
static bool _Fn_Send (void)
{
#if (DEF_UPLINK_ENABLE == 1)
    uint16_t u16Len = (uint16_t)strlen(chLine);

    (void)HAL_UART_Transmit(&huart3, (uint8_t *)chLine, u16Len, 200U);

    return true;
#else
    return false;
#endif
}

static bool _Fn_Lock (void)
{
#if (DEF_UPLINK_ENABLE == 1)
    if (osSemaphoreWait(sem_lora_uartHandle, DEF_UPLINK_SEM_WAIT_MS) != osOK)
    {
        u32Dropped++;
        return false;
    }

    return true;
#else
    return false;
#endif
}

static void _Fn_Unlock (void)
{
#if (DEF_UPLINK_ENABLE == 1)
    (void)osSemaphoreRelease(sem_lora_uartHandle);
#endif
}

/*------------------------------------------------------------ Hello -----------------------------------------------------------------*/
void Fn_Uplink_Hello (void)
{
#if (DEF_UPLINK_ENABLE == 1)
    /* Proven on this part, not assumed from the source: there is no host
       compiler in this toolchain, so the FIPS-197 and RFC 4493 vectors
       are checked here, once, at start-up. */
    bCryptoOk = Fn_Crypto_Selftest();

    if (!_Fn_Lock())
    {
        return;
    }

    (void)snprintf(chLine, sizeof(chLine),
                   "{\"type\":\"hello\",\"schema\":%u,\"device\":\"lora-gateway\","
                   "\"radios\":%u,\"crypto_ok\":%u,\"mic_required\":%u,"
                   "\"t_ms\":%lu}\r\n",
                   (unsigned)DEF_UPLINK_SCHEMA,
                   (unsigned)DEF_MAX_LoRa_Module,
                   (unsigned)(bCryptoOk ? 1U : 0U),
                   (unsigned)DEF_MIC_POLICY_REQUIRE,
                   (unsigned long)osKernelSysTick());

    (void)_Fn_Send();
    _Fn_Unlock();
#endif
}

/*------------------------------------------------------------ Packet ----------------------------------------------------------------*/
void Fn_Uplink_Packet (uint8_t u8Radio, const uint8_t *pu8Data, uint8_t u8Len,
                       int16_t i16Rssi, int8_t i8Snr, EnumMicState enMic)
{
#if (DEF_UPLINK_ENABLE == 1)
    static const char * const pchMic[] = { "none", "ok", "fail" };
    int iPos;
    uint8_t u8Type;
    uint8_t u8Body;

    if ((pu8Data == NULL) || (u8Len < NODE_PKT_MIN_LEN) || (u8Radio >= DEF_MAX_LoRa_Module))
    {
        return;
    }

    if (!_Fn_Lock())
    {
        return;
    }

    /* The top bit marks a MIC-carrying frame; the payload type is the
       low 7 bits. Decoding without masking would make every
       authenticated frame look like an unknown type. */
    u8Type = (uint8_t)(pu8Data[UP_OFF_TYPE] & 0x7FU);
    u8Body = (enMic == en_Mic_Absent) ? u8Len : (uint8_t)(u8Len - LORA_MIC_BYTES);

    iPos = snprintf(chLine, sizeof(chLine),
                    "{\"type\":\"packet\",\"radio\":%u,\"node\":%u,\"seq\":%u,"
                    "\"len\":%u,\"rssi\":%d,\"snr\":%d,\"mic\":\"%s\"",
                    (unsigned)(u8Radio + 1U),
                    (unsigned)_Fn_U16(&pu8Data[UP_OFF_ID]),
                    (unsigned)pu8Data[UP_OFF_SEQ],
                    (unsigned)u8Len,
                    (int)i16Rssi,
                    (int)i8Snr,
                    pchMic[(enMic <= en_Mic_Fail) ? (unsigned)enMic : 0U]);

    if ((u8Type == UP_TYPE_DATA) && (u8Body >= UP_DATA_LEN))
    {
        iPos += snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos),
                         ",\"frame\":\"data\",\"temp_cc\":%d,\"hum_cp\":%u,"
                         "\"batt_mv\":%u,\"uptime_s\":%lu",
                         (int)(int16_t)_Fn_U16(&pu8Data[4]),
                         (unsigned)_Fn_U16(&pu8Data[6]),
                         (unsigned)_Fn_U16(&pu8Data[8]),
                         (unsigned long)_Fn_U32(&pu8Data[10]));
    }
    else if ((u8Type == UP_TYPE_STATUS) && (u8Body >= UP_STATUS_LEN))
    {
        iPos += snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos),
                         ",\"frame\":\"status\",\"batt_mv\":%u,\"uptime_s\":%lu",
                         (unsigned)_Fn_U16(&pu8Data[4]),
                         (unsigned long)_Fn_U32(&pu8Data[6]));
    }
    else
    {
        /* Unrecognised frame. Still reported, with a hex preview, because
           real traffic that cannot be decoded is exactly what someone
           debugging a new node type needs to see. */
        uint8_t u8Show = (u8Len < DEF_UPLINK_RAW_BYTES) ? u8Len : (uint8_t)DEF_UPLINK_RAW_BYTES;
        uint8_t i;

        iPos += snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos),
                         ",\"frame\":\"unknown\",\"raw\":\"");

        for (i = 0U; i < u8Show; i++)
        {
            iPos += snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos),
                             "%02X", pu8Data[i]);
        }

        iPos += snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos), "\"");
    }

    (void)snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos),
                   ",\"t_ms\":%lu}\r\n", (unsigned long)osKernelSysTick());

    (void)_Fn_Send();
    _Fn_Unlock();
#else
    (void)u8Radio;
    (void)pu8Data;
    (void)u8Len;
    (void)i16Rssi;
    (void)i8Snr;
    (void)enMic;
#endif
}

/*------------------------------------------------------------ Status ----------------------------------------------------------------*/
void Fn_Uplink_Status_Tick (void)
{
#if (DEF_UPLINK_ENABLE == 1)
    uint32_t u32Now = osKernelSysTick();
    int iPos;
    uint8_t i;

    if ((u32NextStatusTick != 0U) && ((int32_t)(u32Now - u32NextStatusTick) < 0))
    {
        return;
    }

    u32NextStatusTick = u32Now + DEF_UPLINK_STATUS_PERIOD_MS;

    if (!_Fn_Lock())
    {
        return;
    }

    /* The schema is repeated here, not only in the hello line. A PC tool
       started after the gateway booted never sees hello, so without this
       its version check would never run and would pass by default. */
    /* Authentication totals across all four radios. Aggregated rather
       than per radio to keep the line inside one buffer; the per packet
       "mic" field is what shows which node is which. */
    {
        uint32_t u32MicOk = 0U, u32MicFail = 0U, u32NoMic = 0U;

        for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
        {
            const StructRadioStats *sa = Fn_Radio_Get_Stats(i);

            if (sa != NULL)
            {
                u32MicOk   += sa->u32RxMicOk;
                u32MicFail += sa->u32RxMicFail;
                u32NoMic   += sa->u32RxNoMic;
            }
        }

        iPos = snprintf(chLine, sizeof(chLine),
                        "{\"type\":\"status\",\"schema\":%u,\"t_ms\":%lu,\"nodes\":%u,"
                        "\"silent\":%u,\"whitelist\":%u,\"heap\":%lu,\"stall\":%lu,"
                        "\"dropped\":%lu,\"mic_ok\":%lu,\"mic_fail\":%lu,"
                        "\"no_mic\":%lu,\"mic_required\":%u,\"crypto_ok\":%u,"
                        "\"radios\":[",
                        (unsigned)DEF_UPLINK_SCHEMA,
                        (unsigned long)u32Now,
                        (unsigned)Fn_NodeTable_Count(),
                        (unsigned)Fn_NodeTable_Silent_Count(),
                        (unsigned)Fn_NodeTable_Whitelist_Count(),
                        (unsigned long)xPortGetFreeHeapSize(),
                        (unsigned long)Fn_Monitor_Get_Stall_Mask(),
                        (unsigned long)u32Dropped,
                        (unsigned long)u32MicOk,
                        (unsigned long)u32MicFail,
                        (unsigned long)u32NoMic,
                        (unsigned)(Fn_Radio_Get_Mic_Required() ? 1U : 0U),
                        (unsigned)(bCryptoOk ? 1U : 0U));
    }

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        const StructRadioStats *s = Fn_Radio_Get_Stats(i);
        bool bEn;
        EnumLoRaSts enSts;

        osMutexWait(mtx_configHandle, osWaitForever);
        bEn   = sLoRaGtwConfParam.sLoRaConf[i].bLoRaEn;
        enSts = sLoRaGtwConfParam.sLoRaConf[i].enLoRaSts;
        osMutexRelease(mtx_configHandle);

        if (s == NULL)
        {
            continue;
        }

        iPos += snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos),
                         "%s{\"n\":%u,\"en\":%u,\"ok\":%u,\"freq\":%lu,\"good\":%lu,"
                         "\"crc\":%lu,\"dup\":%lu,\"blk\":%lu,\"reinit\":%lu,"
                         "\"rssi\":%d,\"snr\":%d}",
                         (i == 0U) ? "" : ",",
                         (unsigned)(i + 1U),
                         (unsigned)(bEn ? 1U : 0U),
                         (unsigned)(((enSts == en_LoRa_init_Success) && s->bHealthy) ? 1U : 0U),
                         (unsigned long)s->u32FreqHzActual,
                         (unsigned long)s->u32RxGood,
                         (unsigned long)s->u32RxCrcErr,
                         (unsigned long)s->u32RxDuplicate,
                         (unsigned long)s->u32RxBlocked,
                         (unsigned long)s->u32ReInit,
                         (int)s->i16LastRssi,
                         (int)s->i8LastSnr);

        /* Leaves room for one more radio object plus the terminator. */
        if (iPos >= (int)(sizeof(chLine) - 160))
        {
            break;
        }
    }

    (void)snprintf(&chLine[iPos], (size_t)(sizeof(chLine) - iPos), "]}\r\n");

    (void)_Fn_Send();
    _Fn_Unlock();
#endif
}

uint32_t Fn_Uplink_Get_Dropped (void)
{
    return u32Dropped;
}

void Fn_Uplink_Send_Line (const char *pchLine)
{
#if (DEF_UPLINK_ENABLE == 1)
    if (pchLine == NULL)
    {
        return;
    }

    if (!_Fn_Lock())
    {
        return;
    }

    (void)HAL_UART_Transmit(&huart3, (uint8_t *)pchLine,
                            (uint16_t)strlen(pchLine), 200U);
    _Fn_Unlock();
#else
    (void)pchLine;
#endif
}
