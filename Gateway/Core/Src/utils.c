/*
 * utils.c
 *
 *  Created on: Sep 20, 2026
 *      Author: subhr
 *
 *  LoRa module configuration.
 *   - Fn_Def_LoRa_xxx_Init()  : default values -> LoRa object -> radio -> sLoRaGtwConfParam
 *   - Fn_LoRa_Init_From_Conf(): sLoRaGtwConfParam -> LoRa object -> radio -> sLoRaGtwConfParam
 *
 *  Rule: every path that touches a radio ends by writing the settings the
 *  radio really runs with, and its status, back into sLoRaGtwConfParam.
 */

#include "utils.h"

LoRa myLoRa      CCMRAM_ALIGN;     /* LoRa 1 */
LoRa myLoRa_i    CCMRAM_ALIGN;     /* LoRa 2 */
LoRa myLoRa_ii   CCMRAM_ALIGN;     /* LoRa 3 */
LoRa myLoRa_iii  CCMRAM_ALIGN;     /* LoRa 4 */

StructLoRaGtwConfParam sLoRaGtwConfParam CCMRAM_ALIGN;

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;
extern SPI_HandleTypeDef hspi4;

static LoRa * const pLoRaObj[DEF_MAX_LoRa_Module] = { &myLoRa, &myLoRa_i, &myLoRa_ii, &myLoRa_iii };

/*------------------------------------------------------------ Hardware binding (pins from the .ioc) ----------------------------------*/
static void _Fn_LoRa_Hw_Bind (uint8_t u8LoRaSelector)
{
    LoRa *p = pLoRaObj[u8LoRaSelector];

    switch (u8LoRaSelector)
    {
        case DEF_LoRa_Module_1:
            p->CS_port = NSS_1_GPIO_Port;
            p->CS_pin = NSS_1_Pin;
            p->reset_port = RST_1_GPIO_Port;
            p->reset_pin = RST_1_Pin;
            p->DIO0_port = DIO_1_GPIO_Port;
            p->DIO0_pin = DIO_1_Pin;
            p->hSPIx = &hspi1;
        break;

        case DEF_LoRa_Module_2:
            p->CS_port = NSS_2_GPIO_Port;
            p->CS_pin = NSS_2_Pin;
            p->reset_port = RST_2_GPIO_Port;
            p->reset_pin = RST_2_Pin;
            p->DIO0_port = DIO_2_GPIO_Port;
            p->DIO0_pin = DIO_2_Pin;
            p->hSPIx = &hspi2;
        break;

        case DEF_LoRa_Module_3:
            p->CS_port = NSS_3_GPIO_Port;
            p->CS_pin = NSS_3_Pin;
            p->reset_port = RST_3_GPIO_Port;
            p->reset_pin = RST_3_Pin;
            p->DIO0_port = DIO_3_GPIO_Port;
            p->DIO0_pin = DIO_3_Pin;
            p->hSPIx = &hspi3;
        break;

        case DEF_LoRa_Module_4:
        default:
            p->CS_port = NSS_4_GPIO_Port;
            p->CS_pin = NSS_4_Pin;
            p->reset_port = RST_4_GPIO_Port;
            p->reset_pin = RST_4_Pin;
            p->DIO0_port = DIO_4_GPIO_Port;
            p->DIO0_pin = DIO_4_Pin;
            p->hSPIx = &hspi4;
        break;
    }
}

/*------------------------------------------------------------ Write radio state into sLoRaGtwConfParam -------------------------------
 * Single place where a radio's settings and status are written back.
 */
static void _Fn_LoRa_Sync_To_Struct (uint8_t u8LoRaSelector, bool bEnable, EnumLoRaSts enSts)
{
    osMutexWait(mtx_configHandle, osWaitForever);

    Fn_Conf_Structure_Update(pLoRaObj[u8LoRaSelector], u8LoRaSelector);
    sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector].bLoRaEn   = bEnable;
    sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector].enLoRaSts = enSts;

    osMutexRelease(mtx_configHandle);
}

/*------------------------------------------------------------ Start one radio ---------------------------------------------------------
 * Reset, LoRa_init() called ONCE, then RX continuous.
 */
static EnumLoRaSts _Fn_LoRa_Start (uint8_t u8LoRaSelector)
{
    LoRa *p = pLoRaObj[u8LoRaSelector];
    uint16_t u16Ret;

    LoRa_reset(p);
    u16Ret = LoRa_init(p);

    if (u16Ret == LORA_OK)
    {
        LoRa_startReceiving(p);
        return en_LoRa_init_Success;
    }

    return (u16Ret == LORA_NOT_FOUND) ? en_LoRa_init_Fail : en_LoRa_Unavailable;
}

/*------------------------------------------------------------ Default: common end -----------------------------------------------------*/
static bool _Fn_Def_LoRa_Finish (uint8_t u8LoRaSelector, uint32_t u32Hz)
{
    EnumLoRaSts enSts;

    /* LoRa.c only takes whole MHz; the radio task writes the exact Hz. */
    pLoRaObj[u8LoRaSelector]->frequency = (int)(u32Hz / 1000000UL);

    enSts = _Fn_LoRa_Start(u8LoRaSelector);

    _Fn_LoRa_Sync_To_Struct(u8LoRaSelector, true, enSts);

    osMutexWait(mtx_configHandle, osWaitForever);
    sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector].u32FreqHz  = u32Hz;
    sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector].bDatalogEn = true;
    osMutexRelease(mtx_configHandle);

    return (enSts == en_LoRa_init_Success);
}

/*------------------------------------------------------------ Default Configuration of LoRa Modules ----------------------------------
 * Frequency in Hz, anywhere in the 868 .. 915 MHz band.
 */
bool Fn_Def_LoRa_1st_Init (void)
{
    myLoRa = newLoRa();
    _Fn_LoRa_Hw_Bind(DEF_LoRa_Module_1);

    myLoRa.frequency             = 868;             /* MHz, refined to Hz later */
    myLoRa.spredingFactor        = SF_9;
    myLoRa.bandWidth             = BW_250KHz;
    myLoRa.crcRate               = CR_4_8;
    myLoRa.power                 = POWER_17db;
    myLoRa.overCurrentProtection = 130;             /* mA  */
    myLoRa.preamble              = 10;

    return _Fn_Def_LoRa_Finish(DEF_LoRa_Module_1, 868100000UL);
}

bool Fn_Def_LoRa_2nd_Init (void)
{
    myLoRa_i = newLoRa();
    _Fn_LoRa_Hw_Bind(DEF_LoRa_Module_2);

    myLoRa_i.frequency             = 900;           /* MHz */
    myLoRa_i.spredingFactor        = SF_9;
    myLoRa_i.bandWidth             = BW_250KHz;
    myLoRa_i.crcRate               = CR_4_8;
    myLoRa_i.power                 = POWER_17db;
    myLoRa_i.overCurrentProtection = 130;
    myLoRa_i.preamble              = 10;

    return _Fn_Def_LoRa_Finish(DEF_LoRa_Module_2, 900000000UL);
}

bool Fn_Def_LoRa_3rd_Init (void)
{
    myLoRa_ii = newLoRa();
    _Fn_LoRa_Hw_Bind(DEF_LoRa_Module_3);

    myLoRa_ii.frequency             = 915;          /* MHz */
    myLoRa_ii.spredingFactor        = SF_9;
    myLoRa_ii.bandWidth             = BW_250KHz;
    myLoRa_ii.crcRate               = CR_4_8;
    myLoRa_ii.power                 = POWER_17db;
    myLoRa_ii.overCurrentProtection = 130;
    myLoRa_ii.preamble              = 10;

    return _Fn_Def_LoRa_Finish(DEF_LoRa_Module_3, 915000000UL);
}

bool Fn_Def_LoRa_4th_Init (void)
{
    myLoRa_iii = newLoRa();
    _Fn_LoRa_Hw_Bind(DEF_LoRa_Module_4);

    myLoRa_iii.frequency             = 880;         /* MHz (was 433: not usable on RFM95W) */
    myLoRa_iii.spredingFactor        = SF_9;
    myLoRa_iii.bandWidth             = BW_250KHz;
    myLoRa_iii.crcRate               = CR_4_8;
    myLoRa_iii.power                 = POWER_17db;
    myLoRa_iii.overCurrentProtection = 130;
    myLoRa_iii.preamble              = 10;

    return _Fn_Def_LoRa_Finish(DEF_LoRa_Module_4, 880000000UL);
}

/*------------------------------------------------------------ Configuration from sLoRaGtwConfParam ----------------------------------
 * Used after a valid JSON was parsed, and after any later change
 * (touch panel, PC). Ends with a sync back into sLoRaGtwConfParam.
 */
bool Fn_LoRa_Init_From_Conf (uint8_t u8LoRaSelector)
{
    StructLoRaConf sConf;
    EnumLoRaSts enSts;
    LoRa *p;

    if (u8LoRaSelector >= DEF_MAX_LoRa_Module)
    {
        return false;
    }

    p  = pLoRaObj[u8LoRaSelector];
    *p = newLoRa();
    _Fn_LoRa_Hw_Bind(u8LoRaSelector);

    osMutexWait(mtx_configHandle, osWaitForever);
    sConf = sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector];
    osMutexRelease(mtx_configHandle);

    Fn_Conf_To_Driver(&sConf, p);

    if (!sConf.bLoRaEn)
    {
        LoRa_reset(p);
        LoRa_gotoMode(p, SLEEP_MODE);
        enSts = en_LoRa_Disabled;
    }
    else
    {
        enSts = _Fn_LoRa_Start(u8LoRaSelector);
    }

    _Fn_LoRa_Sync_To_Struct(u8LoRaSelector, sConf.bLoRaEn, enSts);

    return (enSts == en_LoRa_init_Success) || (enSts == en_LoRa_Disabled);
}

/*------------------------------------------------------------ Received packet -> sLoRaGtwConfParam ----------------------------------*/
void Fn_LoRa_Rx_Update (uint8_t u8LoRaSelector, const uint8_t *pu8Data, uint8_t u8Len, int iRssi)
{
    StructLoRaConf *c;

    if ((u8LoRaSelector >= DEF_MAX_LoRa_Module) || (pu8Data == NULL))
    {
        return;
    }

    osMutexWait(mtx_configHandle, osWaitForever);

    c = &sLoRaGtwConfParam.sLoRaConf[u8LoRaSelector];
    c->u8ReceivePacketSize = u8Len;
    memcpy(c->u8RxData, pu8Data, u8Len);
    c->iRssi_val = iRssi;
    c->u32RxPacketCount++;

    osMutexRelease(mtx_configHandle);
}

/*------------------------------------------------------------ Validation --------------------------------------------------------------
 * Every field in range and enabled radios >= DEF_LoRa_MIN_SPACING_HZ apart.
 */
bool Fn_LoRa_Config_Validate (const StructLoRaGtwConfParam *psConf)
{
    uint8_t i, j;
    uint32_t u32Fi, u32Fj, u32Diff;

    if (psConf == NULL)
    {
        return false;
    }

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        const StructLoRaConf *c = &psConf->sLoRaConf[i];

        if ((c->u32FreqHz < DEF_LoRa_FREQ_MIN_HZ) || (c->u32FreqHz > DEF_LoRa_FREQ_MAX_HZ))
        {
        	return false;
        }

        if ((c->enLoRaSF < en_LoRa_SF_7) || (c->enLoRaSF > en_LoRa_SF_12))
        {
        	return false;
        }

        if (c->enLoRaBW > en_LoRa_BW_500KHz)
        {
        	return false;
        }

        if ((c->enLoRaCRC < en_LoRa_CR_4_5) || (c->enLoRaCRC > en_LoRa_CR_4_8))
        {
        	return false;
        }

        if (c->enLoRaPowerGain > en_LoRa_POWER_20db)
        {
        	return false;
        }
    }

    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
        if (!psConf->sLoRaConf[i].bLoRaEn)
        {
            continue;
        }

        for (j = (uint8_t)(i + 1U); j < DEF_MAX_LoRa_Module; j++)
        {
            if (!psConf->sLoRaConf[j].bLoRaEn)
            {
                continue;
            }

            u32Fi   = psConf->sLoRaConf[i].u32FreqHz;
            u32Fj   = psConf->sLoRaConf[j].u32FreqHz;
            u32Diff = (u32Fi > u32Fj) ? (u32Fi - u32Fj) : (u32Fj - u32Fi);

            if (u32Diff < DEF_LoRa_MIN_SPACING_HZ)
            {
                return false;
            }
        }
    }

    return true;
}
