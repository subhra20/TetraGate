/*
 * node_radio.c
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  See node_radio.h.
 *
 *  The one piece of real work here is tuning the carrier. The shared
 *  driver's LoRa_setFrequency takes whole MHz, which cannot express the
 *  gateway's 868.100 MHz, so the carrier registers are written directly
 *  afterwards. That code is the gateway's own Fn_Radio_Set_Frequency_Hz,
 *  ported unchanged apart from dropping the radio index: both ends of
 *  the link deriving the same carrier from the same arithmetic is the
 *  point, and a second, subtly different implementation would be a
 *  liability.
 */

#include "node_config.h"
#include "node_radio.h"
#include "LoRa.h"

/* The shared node copy of LoRa.h does not declare this register; the
   gateway's does. Defined here rather than edited into the proven
   driver header. */
#ifndef RegSyncWord
#define RegSyncWord             0x39
#endif

static LoRa     sLoRa;
static bool     bReady;
static uint32_t u32FreqHzActual;

/*------------------------------------------------------------ Exact carrier ---------------------------------------------------------
 * FRF = f / FSTEP, with FSTEP = 32 MHz / 2^19 = 61.035 Hz, so
 * FRF = (f_Hz << 19) / 32000000. The shift overflows 32 bits at these
 * frequencies (868e6 << 19 is about 4.6e14), hence the 64-bit
 * intermediate. This runs once at start-up, so its cost is irrelevant.
 */
static void _Fn_Set_Frequency_Hz (uint32_t u32Hz)
{
    uint32_t u32Frf;
    int iMode;

    if ((u32Hz < DEF_NODE_FREQ_MIN_HZ) || (u32Hz > DEF_NODE_FREQ_MAX_HZ))
    {
        return;
    }

    u32Frf = (uint32_t)(((uint64_t)u32Hz << 19) / 32000000ULL);

    /* The carrier registers must not be written while transmitting. */
    iMode = sLoRa.current_mode;
    LoRa_gotoMode(&sLoRa, STNBY_MODE);

    LoRa_write(&sLoRa, RegFrMsb, (uint8_t)(u32Frf >> 16));
    LoRa_write(&sLoRa, RegFrMid, (uint8_t)(u32Frf >> 8));
    LoRa_write(&sLoRa, RegFrLsb, (uint8_t)(u32Frf));

    LoRa_gotoMode(&sLoRa, iMode);
}

/* Inverse of the above: FRF back to Hz. */
uint32_t Fn_Node_Radio_Get_Frequency_Hz (void)
{
    uint32_t u32Frf;

    if (!bReady)
    {
        return 0U;
    }

    u32Frf  = ((uint32_t)LoRa_read(&sLoRa, RegFrMsb) << 16);
    u32Frf |= ((uint32_t)LoRa_read(&sLoRa, RegFrMid) << 8);
    u32Frf |=  (uint32_t)LoRa_read(&sLoRa, RegFrLsb);

    return (uint32_t)(((uint64_t)u32Frf * 32000000ULL) >> 19);
}

/*------------------------------------------------------------ Bring-up --------------------------------------------------------------*/
bool Fn_Node_Radio_Init (SPI_HandleTypeDef *phSpi)
{
    bReady = false;

    sLoRa = newLoRa();

    sLoRa.CS_port    = NSS_GPIO_Port;
    sLoRa.CS_pin     = NSS_Pin;
    sLoRa.reset_port = RST_GPIO_Port;
    sLoRa.reset_pin  = RST_Pin;
    sLoRa.DIO0_port  = DIO0_GPIO_Port;
    sLoRa.DIO0_pin   = DIO0_Pin;
    sLoRa.hSPIx      = phSpi;

    /* Whole MHz here; the exact carrier is set below. The rest must
       match the gateway radio named in node_config.h. */
    sLoRa.frequency             = (int)(DEF_NODE_FREQ_HZ / 1000000UL);
    sLoRa.spredingFactor        = DEF_NODE_SF;
    sLoRa.bandWidth             = DEF_NODE_BW;
    sLoRa.crcRate               = DEF_NODE_CR;
    sLoRa.power                 = DEF_NODE_POWER;
    sLoRa.overCurrentProtection = DEF_NODE_OCP_MA;
    sLoRa.preamble              = DEF_NODE_PREAMBLE;

    if (LoRa_init(&sLoRa) != LORA_OK)
    {
        return false;
    }

    /* Order mirrors the gateway's reconfigure path: identity of the
       channel first, then the exact carrier, then read it back. */
    LoRa_write(&sLoRa, RegSyncWord, DEF_NODE_SYNC_WORD);

    _Fn_Set_Frequency_Hz(DEF_NODE_FREQ_HZ);

    /* Idle in standby, not in receive. This node only transmits, and
       LoRa_transmit returns the radio to whatever mode it found. */
    LoRa_gotoMode(&sLoRa, STNBY_MODE);

    bReady = true;

    /* Read the carrier back so a write that silently failed shows up as
       a mismatch against DEF_NODE_FREQ_HZ rather than as a gateway that
       simply never hears anything. */
    u32FreqHzActual = Fn_Node_Radio_Get_Frequency_Hz();

    return true;
}

/*------------------------------------------------------------ Transmit --------------------------------------------------------------*/
bool Fn_Node_Radio_Transmit (const uint8_t *pu8Data, uint8_t u8Len)
{
    if (!bReady || (pu8Data == NULL) || (u8Len == 0U))
    {
        return false;
    }

    /* The driver takes a non-const pointer but only reads from it. */
    return (LoRa_transmit(&sLoRa, (uint8_t *)pu8Data, u8Len,
                          DEF_NODE_TX_TIMEOUT_MS) == 1U);
}

bool Fn_Node_Radio_Healthy (void)
{
    return (bReady && (LoRa_read(&sLoRa, RegVersion) == 0x12U));
}
