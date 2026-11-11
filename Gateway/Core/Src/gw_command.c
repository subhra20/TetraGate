/*
 * gw_command.c
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  See gw_command.h.
 *
 *  Reception is interrupt driven one byte at a time into a ring. USART3
 *  already had its interrupt enabled by CubeMX, so nothing in the .ioc
 *  changed. Polling was not an option: the peripheral holds one byte, and
 *  the monitor task runs every 200 ms, which at 115200 would lose all but
 *  roughly one byte in two thousand.
 *
 *  The reply buffer is static. This runs in the monitor task, which has a
 *  1 KB stack and feeds the watchdog, so a 640 byte local would be a poor
 *  idea. Only one command is processed at a time, so sharing it is safe.
 */

#include "gw_command.h"
#include "radio_manager.h"
#include "node_table.h"
#include "uplink.h"
#include "eeprom_operation.h"
#include "DeviceConfigJSON.h"
#include "cJSON.h"
#include "usart.h"
#include <stdlib.h>

#define CMD_REPLY_BYTES     640

static volatile uint8_t  au8Ring[DEF_CMD_RING_BYTES];
static volatile uint16_t u16Head;       /* written by the ISR  */
static volatile uint16_t u16Tail;       /* read by the task    */
static volatile uint32_t u32Overruns;

static uint8_t  u8RxByte;               /* the one byte HAL receives into */
static char     chLine[DEF_CMD_LINE_MAX];
static uint16_t u16LineLen;
static bool     bLineOverflow;

static char     chReply[CMD_REPLY_BYTES];

/*------------------------------------------------------------ Reception -------------------------------------------------------------*/
void HAL_UART_RxCpltCallback (UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        uint16_t u16Next = (uint16_t)((u16Head + 1U) % DEF_CMD_RING_BYTES);

        if (u16Next != u16Tail)
        {
            au8Ring[u16Head] = u8RxByte;
            u16Head = u16Next;
        }
        else
        {
            /* Full. Counted rather than silently dropped, so a PC tool
               sending faster than the gateway drains is diagnosable. */
            u32Overruns++;
        }

        (void)HAL_UART_Receive_IT(huart, &u8RxByte, 1U);
    }
}

void Fn_Command_Init (void)
{
    u16Head = 0U;
    u16Tail = 0U;
    u16LineLen = 0U;
    bLineOverflow = false;

    (void)HAL_UART_Receive_IT(&huart3, &u8RxByte, 1U);
}

uint32_t Fn_Command_Get_Overruns (void)
{
    return u32Overruns;
}

/*------------------------------------------------------------ Tiny JSON scanner -----------------------------------------------------
 * Enough for our own flat command objects and nothing more. cJSON is
 * linked into this firmware already, but it allocates and recurses, and
 * neither belongs in the watchdog feeder's 1 KB stack.
 */
static const char *_Fn_Find_Key (const char *pchJson, const char *pchKey)
{
    char chPat[24];
    const char *p;

    (void)snprintf(chPat, sizeof(chPat), "\"%s\"", pchKey);

    p = strstr(pchJson, chPat);
    if (p == NULL)
    {
        return NULL;
    }

    p += strlen(chPat);
    while ((*p == ' ') || (*p == ':'))
    {
        p++;
    }

    return p;
}

static bool _Fn_Get_Int (const char *pchJson, const char *pchKey, int32_t *pi32Out)
{
    const char *p = _Fn_Find_Key(pchJson, pchKey);

    if (p == NULL)
    {
        return false;
    }
    if ((*p != '-') && ((*p < '0') || (*p > '9')))
    {
        return false;
    }

    *pi32Out = (int32_t)strtol(p, NULL, 10);

    return true;
}

static bool _Fn_Get_Str (const char *pchJson, const char *pchKey,
                         char *pchOut, uint8_t u8Max)
{
    const char *p = _Fn_Find_Key(pchJson, pchKey);
    uint8_t i = 0U;

    if ((p == NULL) || (*p != '"'))
    {
        return false;
    }

    p++;
    while ((*p != '"') && (*p != '\0') && (i < (uint8_t)(u8Max - 1U)))
    {
        pchOut[i] = *p;
        i++;
        p++;
    }
    pchOut[i] = '\0';

    return (i > 0U);
}

/*------------------------------------------------------------ Replies ---------------------------------------------------------------*/
static void _Fn_Ack (const char *pchCmd, bool bOk, const char *pchErr)
{
    char chAck[112];

    if (bOk)
    {
        (void)snprintf(chAck, sizeof(chAck),
                       "{\"type\":\"ack\",\"cmd\":\"%s\",\"ok\":1}\r\n", pchCmd);
    }
    else
    {
        (void)snprintf(chAck, sizeof(chAck),
                       "{\"type\":\"ack\",\"cmd\":\"%s\",\"ok\":0,\"err\":\"%s\"}\r\n",
                       pchCmd, (pchErr != NULL) ? pchErr : "bad");
    }

    Fn_Uplink_Send_Line(chAck);
}

static void _Fn_Send_Config (void)
{
    int iPos;
    uint8_t i;

    iPos = snprintf(chReply, sizeof(chReply), "{\"type\":\"config\",\"radios\":[");

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        /* Scalars, not a struct copy. StructLoRaConf carries a 255 byte
           receive buffer, so copying one would put nearly 300 bytes on
           the monitor task's 1 KB stack - and that task feeds the
           watchdog. */
        uint32_t u32Freq;
        uint8_t  u8En, u8Sf, u8Bw, u8Cr, u8Pwr, u8Dl, u8Sts;

        osMutexWait(mtx_configHandle, osWaitForever);
        u8En    = sLoRaGtwConfParam.sLoRaConf[i].bLoRaEn ? 1U : 0U;
        u32Freq = sLoRaGtwConfParam.sLoRaConf[i].u32FreqHz;
        u8Sf    = (uint8_t)sLoRaGtwConfParam.sLoRaConf[i].enLoRaSF;
        u8Bw    = (uint8_t)sLoRaGtwConfParam.sLoRaConf[i].enLoRaBW;
        u8Cr    = (uint8_t)sLoRaGtwConfParam.sLoRaConf[i].enLoRaCRC;
        u8Pwr   = (uint8_t)sLoRaGtwConfParam.sLoRaConf[i].enLoRaPowerGain;
        u8Dl    = sLoRaGtwConfParam.sLoRaConf[i].bDatalogEn ? 1U : 0U;
        u8Sts   = (uint8_t)sLoRaGtwConfParam.sLoRaConf[i].enLoRaSts;
        osMutexRelease(mtx_configHandle);

        iPos += snprintf(&chReply[iPos], (size_t)(sizeof(chReply) - iPos),
                         "%s{\"n\":%u,\"en\":%u,\"freq\":%lu,\"sf\":%u,\"bw\":%u,"
                         "\"cr\":%u,\"pwr\":%u,\"dl\":%u,\"sts\":%u}",
                         (i == 0U) ? "" : ",",
                         (unsigned)(i + 1U),
                         (unsigned)u8En,
                         (unsigned long)u32Freq,
                         (unsigned)u8Sf,
                         (unsigned)u8Bw,
                         (unsigned)u8Cr,
                         (unsigned)u8Pwr,
                         (unsigned)u8Dl,
                         (unsigned)u8Sts);

        if (iPos >= (int)(sizeof(chReply) - 140))
        {
            break;
        }
    }

    (void)snprintf(&chReply[iPos], (size_t)(sizeof(chReply) - iPos),
                   "],\"mic_required\":%u}\r\n",
                   (unsigned)(Fn_Radio_Get_Mic_Required() ? 1U : 0U));

    Fn_Uplink_Send_Line(chReply);
}

/* One line per node rather than one big array: the table holds up to
   100 entries and a single line could not be bounded sensibly. */
static void _Fn_Send_Nodes (void)
{
    StructNode sN;
    uint16_t u16Count = Fn_NodeTable_Count();
    uint16_t i;

    for (i = 0U; i < u16Count; i++)
    {
        if (!Fn_NodeTable_Get(i, &sN))
        {
            break;
        }

        (void)snprintf(chReply, sizeof(chReply),
                       "{\"type\":\"node\",\"node\":%u,\"radio\":%u,\"seq\":%u,"
                       "\"packets\":%lu,\"dups\":%lu,\"rssi\":%d,\"snr\":%d,"
                       "\"silent\":%u,\"interval_ms\":%lu,\"last_ms\":%lu}\r\n",
                       (unsigned)sN.u16NodeId,
                       (unsigned)(sN.u8Radio + 1U),
                       (unsigned)sN.u8LastSeq,
                       (unsigned long)sN.u32Packets,
                       (unsigned long)sN.u32Duplicates,
                       (int)sN.i16LastRssi,
                       (int)sN.i8LastSnr,
                       (unsigned)(sN.bSilent ? 1U : 0U),
                       (unsigned long)sN.u32IntervalMs,
                       (unsigned long)sN.u32LastSeenMs);

        Fn_Uplink_Send_Line(chReply);
    }

    _Fn_Ack("get_nodes", true, NULL);
}

/*------------------------------------------------------------ set_radio -------------------------------------------------------------
 * Validated as a whole configuration before anything is committed, so
 * the 868-915 MHz limit and the 2 MHz spacing rule are enforced here
 * exactly as they are for the touchscreen. A rejected command changes
 * nothing.
 */
static void _Fn_Do_Set_Radio (const char *pchJson)
{
    static StructLoRaGtwConfParam sTrial;   /* static: 1 KB stack */
    int32_t i32N;
    int32_t i32Val;
    uint8_t u8Idx;

    if (!_Fn_Get_Int(pchJson, "n", &i32N) || (i32N < 1) ||
        (i32N > (int32_t)DEF_MAX_LoRa_Module))
    {
        _Fn_Ack("set_radio", false, "radio");
        return;
    }

    u8Idx = (uint8_t)(i32N - 1);

    osMutexWait(mtx_configHandle, osWaitForever);
    sTrial = sLoRaGtwConfParam;
    osMutexRelease(mtx_configHandle);

    /* Every field is optional: a command may change only the frequency
       and leave the modem settings alone. */
    if (_Fn_Get_Int(pchJson, "en", &i32Val))
    {
        sTrial.sLoRaConf[u8Idx].bLoRaEn = (i32Val != 0);
    }
    if (_Fn_Get_Int(pchJson, "freq", &i32Val))
    {
        sTrial.sLoRaConf[u8Idx].u32FreqHz = (uint32_t)i32Val;
    }
    if (_Fn_Get_Int(pchJson, "sf", &i32Val))
    {
        sTrial.sLoRaConf[u8Idx].enLoRaSF = (EnumLoRaSF)i32Val;
    }
    if (_Fn_Get_Int(pchJson, "bw", &i32Val))
    {
        sTrial.sLoRaConf[u8Idx].enLoRaBW = (EnumLoRaBW)i32Val;
    }
    if (_Fn_Get_Int(pchJson, "cr", &i32Val))
    {
        sTrial.sLoRaConf[u8Idx].enLoRaCRC = (EnumLoRaCRC)i32Val;
    }
    if (_Fn_Get_Int(pchJson, "pwr", &i32Val))
    {
        sTrial.sLoRaConf[u8Idx].enLoRaPowerGain = (EnumLoRaPowerGain)i32Val;
    }
    if (_Fn_Get_Int(pchJson, "dl", &i32Val))
    {
        sTrial.sLoRaConf[u8Idx].bDatalogEn = (i32Val != 0);
    }

    if (!Fn_LoRa_Config_Validate(&sTrial))
    {
        _Fn_Ack("set_radio", false, "invalid or under 2 MHz spacing");
        return;
    }

    osMutexWait(mtx_configHandle, osWaitForever);
    sLoRaGtwConfParam.sLoRaConf[u8Idx].bLoRaEn         = sTrial.sLoRaConf[u8Idx].bLoRaEn;
    sLoRaGtwConfParam.sLoRaConf[u8Idx].u32FreqHz       = sTrial.sLoRaConf[u8Idx].u32FreqHz;
    sLoRaGtwConfParam.sLoRaConf[u8Idx].enLoRaSF        = sTrial.sLoRaConf[u8Idx].enLoRaSF;
    sLoRaGtwConfParam.sLoRaConf[u8Idx].enLoRaBW        = sTrial.sLoRaConf[u8Idx].enLoRaBW;
    sLoRaGtwConfParam.sLoRaConf[u8Idx].enLoRaCRC       = sTrial.sLoRaConf[u8Idx].enLoRaCRC;
    sLoRaGtwConfParam.sLoRaConf[u8Idx].enLoRaPowerGain = sTrial.sLoRaConf[u8Idx].enLoRaPowerGain;
    sLoRaGtwConfParam.sLoRaConf[u8Idx].bDatalogEn      = sTrial.sLoRaConf[u8Idx].bDatalogEn;
    osMutexRelease(mtx_configHandle);

    Fn_Radio_Request_Reconfig(u8Idx);

    _Fn_Ack("set_radio", true, NULL);
}

static void _Fn_Do_Save (void)
{
    char *pchJson = Fn_JSON_LoRa_Config_Generate();
    EnumEEPROMSts enSts = en_EEPROM_ERR_PARAM;

    if (pchJson != NULL)
    {
        enSts = Fn_EEPROM_JSON_Write(pchJson);
        cJSON_free(pchJson);
    }

    _Fn_Ack("save", (enSts == en_EEPROM_OK), "eeprom write failed");
}

/*------------------------------------------------------------ Dispatch --------------------------------------------------------------*/
static void _Fn_Dispatch (const char *pchJson)
{
    char chCmd[24];
    int32_t i32Val;

    if (!_Fn_Get_Str(pchJson, "cmd", chCmd, (uint8_t)sizeof(chCmd)))
    {
        _Fn_Ack("?", false, "no cmd field");
        return;
    }

    if (strcmp(chCmd, "ping") == 0)
    {
        _Fn_Ack("ping", true, NULL);
    }
    else if (strcmp(chCmd, "get_config") == 0)
    {
        _Fn_Send_Config();
    }
    else if (strcmp(chCmd, "get_nodes") == 0)
    {
        _Fn_Send_Nodes();
    }
    else if (strcmp(chCmd, "set_radio") == 0)
    {
        _Fn_Do_Set_Radio(pchJson);
    }
    else if (strcmp(chCmd, "save") == 0)
    {
        _Fn_Do_Save();
    }
    else if (strcmp(chCmd, "clear_stats") == 0)
    {
        Fn_Radio_Clear_Stats();
        _Fn_Ack("clear_stats", true, NULL);
    }
    else if (strcmp(chCmd, "set_mic_policy") == 0)
    {
        if (_Fn_Get_Int(pchJson, "require", &i32Val))
        {
            Fn_Radio_Set_Mic_Required(i32Val != 0);
            _Fn_Ack("set_mic_policy", true, NULL);
        }
        else
        {
            _Fn_Ack("set_mic_policy", false, "require missing");
        }
    }
    else if (strcmp(chCmd, "reboot") == 0)
    {
        _Fn_Ack("reboot", true, NULL);
        HAL_Delay(60);                  /* let the ack reach the wire */
        NVIC_SystemReset();
    }
    else
    {
        _Fn_Ack(chCmd, false, "unknown command");
    }
}

void Fn_Command_Service (void)
{
    while (u16Tail != u16Head)
    {
        char ch = (char)au8Ring[u16Tail];

        u16Tail = (uint16_t)((u16Tail + 1U) % DEF_CMD_RING_BYTES);

        if ((ch == '\n') || (ch == '\r'))
        {
            if (bLineOverflow)
            {
                /* The line was too long. Answered rather than silently
                   truncated, because a truncated JSON object can still
                   parse as a different, valid command. */
                _Fn_Ack("?", false, "line too long");
                bLineOverflow = false;
            }
            else if (u16LineLen > 0U)
            {
                chLine[u16LineLen] = '\0';
                _Fn_Dispatch(chLine);
            }
            else
            {
                /* bare CR or LF: a CRLF pair, nothing to do */
            }

            u16LineLen = 0U;
        }
        else if (u16LineLen < (DEF_CMD_LINE_MAX - 1U))
        {
            chLine[u16LineLen] = ch;
            u16LineLen++;
        }
        else
        {
            bLineOverflow = true;
            u16LineLen = 0U;
        }
    }
}
