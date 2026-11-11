/*
 * radio_manager.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See radio_manager.h. Nothing here writes a LoRa register directly:
 *  every access goes through the public LoRa.c functions, so the proven
 *  register sequences stay untouched.
 */

#include "radio_manager.h"
#include "packet_log.h"
#include "node_table.h"
#include "monitor.h"
#include "uplink.h"
#include "lora_crypto.h"
#include "lora_keys.h"
#include "gw_command.h"
#include "FreeRTOS.h"
#include "task.h"

/* Power-on default from radio_manager.h; switchable at runtime by a PC
   command so enforcement can be turned on after the last node is
   flashed, without rebuilding the gateway. */
static volatile bool bMicRequired = (DEF_MIC_POLICY_REQUIRE == 1);

/*------------------------------------------------------------ RegIrqFlags bits (SX1276 datasheet 4.1.2.6) ---------------------------*/
#define RADIO_IRQ_RX_DONE           0x40U
#define RADIO_IRQ_PAYLOAD_CRC_ERR   0x20U
#define RADIO_IRQ_CLEAR_ALL         0xFFU

/* The four driver objects, in LoRa 1..4 order. Defined in utils.c. */
static LoRa * const pRadio[DEF_MAX_LoRa_Module] =
{
    &myLoRa,        /* LoRa 1, SPI1 */
    &myLoRa_i,      /* LoRa 2, SPI2 */
    &myLoRa_ii,     /* LoRa 3, SPI3 */
    &myLoRa_iii,    /* LoRa 4, SPI4 */
};

static StructRadioStats sRadioStats[DEF_MAX_LoRa_Module];

/* Task handles, filled in by each task itself so there is no start-up
 * ordering problem between MX_FREERTOS_Init() and the first DIO0 edge. */
static TaskHandle_t xRadioTask[DEF_MAX_LoRa_Module];

/* One receive buffer per radio. Kept out of the task stacks: a 255 byte
 * local would be a quarter of the 1 KB stack each LoRa task gets. */
static uint8_t u8RxBuf[DEF_MAX_LoRa_Module][DEF_LoRa_MAX_Data_Receive_Len];

/* Set by any task, cleared by the owning radio task. */
static volatile bool bReconfigReq[DEF_MAX_LoRa_Module];

void Fn_Radio_Request_Reconfig (uint8_t u8LoRaSelector)
{
    if (u8LoRaSelector < DEF_MAX_LoRa_Module)
    {
        bReconfigReq[u8LoRaSelector] = true;

        /* Wake the task so the change is applied now rather than at its
         * next health-check timeout. */
        if (xRadioTask[u8LoRaSelector] != NULL)
        {
            xTaskNotifyGive(xRadioTask[u8LoRaSelector]);
        }
    }
}

/*------------------------------------------------------------ DIO0 interrupt ---------------------------------------------------------
 * RxDone is mapped to DIO0 by LoRa_init(). The ISR identifies the radio
 * and wakes its task: no SPI traffic happens here.
 *
 * DIO_1 PA9, DIO_2 PD6, DIO_3 PA15, DIO_4 PD12 - four distinct pin masks,
 * so the pin alone identifies the radio.
 */
void HAL_GPIO_EXTI_Callback (uint16_t GPIO_Pin)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t u8Idx;

    switch (GPIO_Pin)
    {
        case DIO_1_Pin: u8Idx = DEF_LoRa_Module_1; break;
        case DIO_2_Pin: u8Idx = DEF_LoRa_Module_2; break;
        case DIO_3_Pin: u8Idx = DEF_LoRa_Module_3; break;
        case DIO_4_Pin: u8Idx = DEF_LoRa_Module_4; break;
        default:        return;     /* not ours */
    }

    if (xRadioTask[u8Idx] != NULL)
    {
        vTaskNotifyGiveFromISR(xRadioTask[u8Idx], &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*------------------------------------------------------------ Is this radio supposed to be running? ---------------------------------*/
static bool _Fn_Radio_Is_Active (uint8_t u8LoRaSelector)
{
    EnumLoRaSts enSts;

    osMutexWait(mtx_configHandle, osWaitForever);
    enSts = sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector].enLoRaSts;
    osMutexRelease(mtx_configHandle);

    return (enSts == en_LoRa_init_Success);
}

/*------------------------------------------------------------ Drain one packet ------------------------------------------------------
 * Called from the radio's own task after DIO0 fired.
 *
 * RegIrqFlags has to be read BEFORE LoRa_receive(), because that function
 * clears the whole register with 0xFF and the CRC error bit would be lost.
 */
static void _Fn_Radio_Handle_Rx (uint8_t u8LoRaSelector)
{
    static const uint8_t au8MasterKey[16] = DEF_LORA_MASTER_KEY_INIT;

    LoRa *p = pRadio[u8LoRaSelector];
    StructRadioStats *s = &sRadioStats[u8LoRaSelector];
    EnumNodeResult enNode;
    EnumMicState enMic = en_Mic_Absent;
    uint8_t u8Irq;
    uint8_t u8Len;

    u8Irq = LoRa_read(p, RegIrqFlags);

    if ((u8Irq & RADIO_IRQ_PAYLOAD_CRC_ERR) != 0U)
    {
        /* Damaged packet: count it, drop it, go back to listening.
         * The architecture doc wants these visible per radio. */
        s->u32RxCrcErr++;
        LoRa_write(p, RegIrqFlags, RADIO_IRQ_CLEAR_ALL);
        LoRa_startReceiving(p);
        return;
    }

    if ((u8Irq & RADIO_IRQ_RX_DONE) == 0U)
    {
        /* Spurious edge, nothing to read. */
        return;
    }

    u8Len = LoRa_receive(p, u8RxBuf[u8LoRaSelector], DEF_LoRa_MAX_Data_Receive_Len);

    if (u8Len == 0U)
    {
        s->u32RxEmpty++;
        return;
    }

    /* RegPktSnrValue is a signed quarter-dB value. */
    s->i8LastSnr    = (int8_t)((int8_t)LoRa_read(p, RegPktSnrValue) / 4);
    s->i16LastRssi  = (int16_t)LoRa_getRSSI(p);

    /* Authenticity first: nothing below should act on a frame we have
     * not decided to believe. */
    if ((u8RxBuf[u8LoRaSelector][0] & DEF_MIC_TYPE_FLAG) == 0U)
    {
        /* No MIC at all - a node not yet reflashed. */
        s->u32RxNoMic++;
        enMic = en_Mic_Absent;

        if (bMicRequired)
        {
            return;
        }
    }
    else if (Fn_Mic_Verify(au8MasterKey, u8RxBuf[u8LoRaSelector], u8Len))
    {
        s->u32RxMicOk++;
        enMic = en_Mic_Ok;
    }
    else
    {
        /* Claimed a MIC and got it wrong. Always dropped, whatever the
         * policy: this is a forgery or a key mismatch, and believing
         * either would defeat the point of having a MIC at all. */
        s->u32RxMicFail++;
        return;
    }

    /* Identify the node, filter duplicates heard on another radio, and
     * apply the whitelist. A frame too short to carry a node ID is still
     * counted and logged: it is real traffic, just not from a node we
     * can identify. */
    enNode = Fn_NodeTable_Update(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len,
                                 s->i16LastRssi, s->i8LastSnr);

    if (enNode == en_Node_Duplicate)
    {
        s->u32RxDuplicate++;
        return;
    }

    if (enNode == en_Node_NotWhitelisted)
    {
        s->u32RxBlocked++;
        return;
    }

    /* Counted only once the frame has survived the duplicate and
     * whitelist checks, so u32RxGood means "accepted" rather than
     * "came off the air" - the duplicates have their own counter. */
    s->u32LastRxTick = osKernelSysTick();
    s->u32RxGood++;

    /* Hand the payload to the shared gateway structure, and keep a copy
     * in the ring buffer the Packets screen reads. */
    Fn_LoRa_Rx_Update(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len, (int)s->i16LastRssi);
    Fn_PktLog_Push(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len, s->i16LastRssi, s->i8LastSnr);

    /* Northbound feed for a PC tool. Given the FULL buffer, not the
     * truncated copy the packet log keeps, because the decoder needs
     * bytes past the preview. Drops its line rather than block us. */
    Fn_Uplink_Packet(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len,
                     s->i16LastRssi, s->i8LastSnr, enMic);
}

/*------------------------------------------------------------ Health check ----------------------------------------------------------*/
bool Fn_Radio_Health_Check (uint8_t u8LoRaSelector)
{
    if (u8LoRaSelector >= DEF_MAX_LoRa_Module)
    {
        return false;
    }

    return (LoRa_read(pRadio[u8LoRaSelector], RegVersion) == 0x12U);
}

/*------------------------------------------------------------ Sync word -------------------------------------------------------------*/
/*------------------------------------------------------------ MIC policy ------------------------------------------------------------*/
void Fn_Radio_Set_Mic_Required (bool bRequire)
{
    bMicRequired = bRequire;
}

bool Fn_Radio_Get_Mic_Required (void)
{
    return bMicRequired;
}

/*------------------------------------------------------------ Counters --------------------------------------------------------------*/
void Fn_Radio_Clear_Stats (void)
{
    uint8_t i;

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        StructRadioStats *s = &sRadioStats[i];

        s->u32RxGood      = 0U;
        s->u32RxCrcErr    = 0U;
        s->u32RxEmpty     = 0U;
        s->u32RxDuplicate = 0U;
        s->u32RxBlocked   = 0U;
        s->u32RxMicOk     = 0U;
        s->u32RxMicFail   = 0U;
        s->u32RxNoMic     = 0U;
        s->u32HealthFail  = 0U;
        s->u32ReInit      = 0U;
    }
}

void Fn_Radio_Set_Sync_Word (uint8_t u8LoRaSelector, uint8_t u8SyncWord)
{
    if (u8LoRaSelector < DEF_MAX_LoRa_Module)
    {
        LoRa_write(pRadio[u8LoRaSelector], RegSyncWord, u8SyncWord);
    }
}

/*------------------------------------------------------------ Exact frequency ------------------------------------------------------
 * FRF = f / FSTEP, with FSTEP = 32 MHz / 2^19 = 61.035 Hz, so
 * FRF = (f_Hz << 19) / 32000000. The shift overflows 32 bits at these
 * frequencies (915e6 << 19 is about 4.8e14), hence the 64-bit
 * intermediate; this runs only when a radio is configured.
 */
void Fn_Radio_Set_Frequency_Hz (uint8_t u8LoRaSelector, uint32_t u32Hz)
{
    LoRa *p;
    uint32_t u32Frf;
    int iMode;

    if ((u8LoRaSelector >= DEF_MAX_LoRa_Module) ||
        (u32Hz < DEF_LoRa_FREQ_MIN_HZ) || (u32Hz > DEF_LoRa_FREQ_MAX_HZ))
    {
        return;
    }

    p = pRadio[u8LoRaSelector];
    u32Frf = (uint32_t)(((uint64_t)u32Hz << 19) / 32000000ULL);

    /* The carrier registers must not be written while transmitting. */
    iMode = p->current_mode;
    LoRa_gotoMode(p, STNBY_MODE);

    LoRa_write(p, RegFrMsb, (uint8_t)(u32Frf >> 16));
    LoRa_write(p, RegFrMid, (uint8_t)(u32Frf >> 8));
    LoRa_write(p, RegFrLsb, (uint8_t)(u32Frf));

    LoRa_gotoMode(p, iMode);
}

/* Inverse of the above: FRF back to Hz. */
uint32_t Fn_Radio_Get_Frequency_Hz (uint8_t u8LoRaSelector)
{
    LoRa *p;
    uint32_t u32Frf;

    if (u8LoRaSelector >= DEF_MAX_LoRa_Module)
    {
        return 0U;
    }

    p = pRadio[u8LoRaSelector];

    u32Frf  = ((uint32_t)LoRa_read(p, RegFrMsb) << 16);
    u32Frf |= ((uint32_t)LoRa_read(p, RegFrMid) << 8);
    u32Frf |=  (uint32_t)LoRa_read(p, RegFrLsb);

    return (uint32_t)(((uint64_t)u32Frf * 32000000ULL) >> 19);
}

/*------------------------------------------------------------ Statistics ------------------------------------------------------------*/
const StructRadioStats *Fn_Radio_Get_Stats (uint8_t u8LoRaSelector)
{
    return (u8LoRaSelector < DEF_MAX_LoRa_Module) ? &sRadioStats[u8LoRaSelector] : NULL;
}

/*------------------------------------------------------------ Settings LoRa_init() does not cover ----------------------------------
 * LoRa_init() leaves the radio on a whole-MHz carrier and never writes a
 * sync word, so both are applied here after every (re)configuration.
 */
static void _Fn_Radio_Apply_Extras (uint8_t u8LoRaSelector)
{
    uint32_t u32Hz;

    osMutexWait(mtx_configHandle, osWaitForever);
    u32Hz = sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector].u32FreqHz;
    osMutexRelease(mtx_configHandle);

    Fn_Radio_Set_Frequency_Hz(u8LoRaSelector, u32Hz);
    Fn_Radio_Set_Sync_Word(u8LoRaSelector, DEF_RADIO_SYNC_WORD_DEFAULT);

    /* Read the carrier registers back, so the statistics show the
     * frequency the radio is really on rather than the one requested.
     * A write that silently failed shows up here as a mismatch. */
    sRadioStats[u8LoRaSelector].u32FreqHzActual =
        Fn_Radio_Get_Frequency_Hz(u8LoRaSelector);
}

/*------------------------------------------------------------ Task body -------------------------------------------------------------
 * Blocks on a DIO0 notification. When nothing arrives for
 * DEF_RADIO_HEALTH_PERIOD_MS the radio is checked and, after
 * DEF_RADIO_HEALTH_FAIL_LIMIT consecutive failures, re-initialised.
 */
void Fn_Radio_Task_Service (uint8_t u8LoRaSelector)
{
    StructRadioStats *s;
    uint8_t u8FailRun = 0U;

    if (u8LoRaSelector >= DEF_MAX_LoRa_Module)
    {
        for (;;)
        {
            osDelay(1000);
        }
    }

    s = &sRadioStats[u8LoRaSelector];

    /* Register before the first edge can matter. */
    xRadioTask[u8LoRaSelector] = xTaskGetCurrentTaskHandle();

    /* TaskInit configures the radios; wait until this one is up. A radio
     * that is disabled, or that failed its version check at boot, waits
     * here - but still honours a reconfigure request, which is how the
     * UI switches one back on. */
    while (!_Fn_Radio_Is_Active(u8LoRaSelector))
    {
        if (bReconfigReq[u8LoRaSelector])
        {
            bReconfigReq[u8LoRaSelector] = false;
            (void)Fn_LoRa_Init_From_Conf(u8LoRaSelector);
        }

        /* A disabled radio parks here. It is still a healthy task, so it
         * must keep checking in or it would stall the watchdog. */
        Fn_Monitor_Checkin((EnumMonTask)(en_Mon_LoRa1 + u8LoRaSelector));
        osDelay(50);
    }

    _Fn_Radio_Apply_Extras(u8LoRaSelector);
    LoRa_startReceiving(pRadio[u8LoRaSelector]);
    s->bHealthy = true;

    for (;;)
    {
        Fn_Monitor_Checkin((EnumMonTask)(en_Mon_LoRa1 + u8LoRaSelector));

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DEF_RADIO_HEALTH_PERIOD_MS)) > 0U)
        {
            if (bReconfigReq[u8LoRaSelector])
            {
                /* Settings changed elsewhere: re-apply them here, on the
                 * only task that owns this radio's SPI bus. */
                bReconfigReq[u8LoRaSelector] = false;

                if (Fn_LoRa_Init_From_Conf(u8LoRaSelector))
                {
                    _Fn_Radio_Apply_Extras(u8LoRaSelector);
                    LoRa_startReceiving(pRadio[u8LoRaSelector]);
                    s->bHealthy = true;
                }
                u8FailRun = 0U;
                continue;
            }

            _Fn_Radio_Handle_Rx(u8LoRaSelector);
            u8FailRun = 0U;
            continue;
        }

        /* Quiet for a second: is the radio still there? */
        if (Fn_Radio_Health_Check(u8LoRaSelector))
        {
            s->bHealthy = true;
            u8FailRun   = 0U;
            continue;
        }

        s->bHealthy = false;
        s->u32HealthFail++;
        u8FailRun++;

        if (u8FailRun >= DEF_RADIO_HEALTH_FAIL_LIMIT)
        {
            /* Reset and reconfigure from the stored configuration. The
             * other three radios keep running, as the doc requires. */
            u8FailRun = 0U;
            s->u32ReInit++;

            if (Fn_LoRa_Init_From_Conf(u8LoRaSelector))
            {
                _Fn_Radio_Apply_Extras(u8LoRaSelector);
                LoRa_startReceiving(pRadio[u8LoRaSelector]);
                s->bHealthy = true;
            }
        }
    }
}
