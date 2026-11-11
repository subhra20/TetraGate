/*
 * LoRa_Core_Oper.c
 *
 *  Created on: Oct 11, 2026
 *      Author: subhra
 *
 *  Conversion between sLoRaGtwConfParam (enums) and the LoRa driver
 *  objects (LoRa.c units: frequency in MHz, register values).
 */

#include "utils.h"

extern StructLoRaGtwConfParam sLoRaGtwConfParam;

/* EnumLoRaPowerGain -> RegPaConfig value */
static const uint8_t u8PowerTable[en_LoRa_POWER_Not_Valid] =
{
    POWER_11db,     /* en_LoRa_POWER_11db */
    POWER_14db,     /* en_LoRa_POWER_14db */
    POWER_17db,     /* en_LoRa_POWER_17db */
    POWER_20db,     /* en_LoRa_POWER_20db */
};

/*------------------------------------------------------------ sLoRaGtwConfParam -> driver --------------------------------------------*/
void Fn_Conf_To_Driver (const StructLoRaConf *psConf, LoRa *psLoRa)
{
    if ((psConf == NULL) || (psLoRa == NULL))
    {
        return;
    }

    /* LoRa.c only understands whole MHz. LoRa_init() uses this to get the
     * radio roughly on channel; Fn_Radio_Set_Frequency_Hz() then writes
     * the exact value, so fractional channels are not lost. */
    psLoRa->frequency             = (int)(psConf->u32FreqHz / 1000000UL);
    psLoRa->spredingFactor        = (uint8_t)psConf->enLoRaSF;                 /* = SF_x   */
    psLoRa->bandWidth             = (uint8_t)psConf->enLoRaBW;                 /* = BW_x   */
    psLoRa->crcRate               = (uint8_t)psConf->enLoRaCRC;                /* = CR_4_x */
    psLoRa->power                 = (psConf->enLoRaPowerGain < en_LoRa_POWER_Not_Valid) ?
                                    u8PowerTable[psConf->enLoRaPowerGain] : POWER_17db;
    psLoRa->overCurrentProtection = 130;                                       /* mA */
    psLoRa->preamble              = 10;
}

/*------------------------------------------------------------ Driver -> sLoRaGtwConfParam --------------------------------------------
 * Copies the settings the radio is really running with into the main
 * structure. Caller must hold mtx_configHandle.
 */
void Fn_Conf_Structure_Update (const LoRa *psLoRa, uint8_t u8LoRaSelector)
{
    StructLoRaConf *c;
    uint8_t i;

    if ((psLoRa == NULL) || (u8LoRaSelector >= DEF_MAX_LoRa_Module))
    {
        return;
    }

    c = &sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector];

    /* u32FreqHz is deliberately NOT written back from the driver: the
     * driver rounds to whole MHz, so copying it back would quietly
     * discard the fractional part of a channel like 868.1 MHz. The
     * configuration stays authoritative for frequency. */

    c->enLoRaSF = ((psLoRa->spredingFactor >= SF_7) && (psLoRa->spredingFactor <= SF_12)) ?
                  (EnumLoRaSF)psLoRa->spredingFactor : en_LoRa_SF_Not_Valid;

    c->enLoRaBW = (psLoRa->bandWidth <= BW_500KHz) ?
                  (EnumLoRaBW)psLoRa->bandWidth : en_LoRa_BW_Not_Valid;

    c->enLoRaCRC = ((psLoRa->crcRate >= CR_4_5) && (psLoRa->crcRate <= CR_4_8)) ?
                   (EnumLoRaCRC)psLoRa->crcRate : en_LoRa_CR_Not_Valid;

    c->enLoRaPowerGain = en_LoRa_POWER_Not_Valid;
    for (i = 0U; i < (uint8_t)en_LoRa_POWER_Not_Valid; i++)
    {
        if (u8PowerTable[i] == psLoRa->power)
        {
            c->enLoRaPowerGain = (EnumLoRaPowerGain)i;
            break;
        }
    }
}
