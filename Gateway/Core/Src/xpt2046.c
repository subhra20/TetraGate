/*
 * xpt2046.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See xpt2046.h.
 */

#include "xpt2046.h"
#include "ili9341.h"

extern SPI_HandleTypeDef hspi6;

/*------------------------------------------------------------ Control bytes --------------------------------------------------------
 * [S][A2 A1 A0][MODE][SER/DFR][PD1][PD0]
 * S = 1, MODE = 0 (12 bit), SER/DFR = 0 (differential), PD = 00.
 */
#define XPT_CMD_READ_X          0xD0U       /* A = 101 */
#define XPT_CMD_READ_Y          0x90U       /* A = 001 */
#define XPT_CMD_READ_Z1         0xB0U       /* A = 011 */
#define XPT_CMD_READ_Z2         0xC0U       /* A = 100 */

#define XPT_SPI_TIMEOUT_MS      100U
#define XPT_SAMPLES             7U

/* A real touch never sits at either rail. The first conversion after
 * switching channel - and the whole sequence on the first touch after
 * power-up - can come back pegged at 0 or 4095, which is what wrecked
 * the first calibration attempt. Those readings are thrown away. */
#define XPT_RAW_MIN_VALID       16U
#define XPT_RAW_MAX_VALID       4079U
#define XPT_MIN_GOOD_SAMPLES    3U
#define XPT_RAW_INVALID         0xFFFFU

static StructTouchCal sCal =
{
    .u16RawXMin = 300U,
    .u16RawXMax = 3800U,
    .u16RawYMin = 300U,
    .u16RawYMax = 3800U,
    .bSwapXY    = true,     /* display is landscape (MADCTL MV = 1) */
    .bInvertX   = false,
    .bInvertY   = true,
};

/*------------------------------------------------------------ One 12 bit conversion ------------------------------------------------*/
static uint16_t _Fn_Xpt_Read_Channel (uint8_t u8Cmd)
{
    uint8_t u8Tx[3];
    uint8_t u8Rx[3];

    u8Tx[0] = u8Cmd;
    u8Tx[1] = 0x00U;
    u8Tx[2] = 0x00U;

    (void)HAL_SPI_TransmitReceive(&hspi6, u8Tx, u8Rx, 3U, XPT_SPI_TIMEOUT_MS);

    /* The conversion arrives in the two bytes after the command, left
     * aligned: 12 significant bits of a 16 bit field. */
    return (uint16_t)((((uint16_t)u8Rx[1] << 8) | (uint16_t)u8Rx[2]) >> 3);
}

static inline void _Fn_Xpt_Cs (bool bActive)
{
    HAL_GPIO_WritePin(T_CS_GPIO_Port, T_CS_Pin, bActive ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/*------------------------------------------------------------ Median of the valid samples ------------------------------------------
 * Returns XPT_RAW_INVALID when too few samples were usable.
 */
static uint16_t _Fn_Xpt_Median (uint8_t u8Cmd)
{
    uint16_t u16S[XPT_SAMPLES];
    uint16_t u16Val;
    uint16_t u16Tmp;
    uint8_t u8N = 0U;
    uint8_t i;
    uint8_t j;

    /* Throwaway conversion: the first read after changing channel can
     * still carry the previous measurement. */
    (void)_Fn_Xpt_Read_Channel(u8Cmd);

    for (i = 0U; i < XPT_SAMPLES; i++)
    {
        u16Val = _Fn_Xpt_Read_Channel(u8Cmd);

        if ((u16Val >= XPT_RAW_MIN_VALID) && (u16Val <= XPT_RAW_MAX_VALID))
        {
            u16S[u8N] = u16Val;
            u8N++;
        }
    }

    if (u8N < XPT_MIN_GOOD_SAMPLES)
    {
        return XPT_RAW_INVALID;
    }

    /* Insertion sort: a handful of elements, cheaper than anything clever. */
    for (i = 1U; i < u8N; i++)
    {
        u16Tmp = u16S[i];
        j = i;
        while ((j > 0U) && (u16S[j - 1U] > u16Tmp))
        {
            u16S[j] = u16S[j - 1U];
            j--;
        }
        u16S[j] = u16Tmp;
    }

    return u16S[u8N / 2U];
}

/*------------------------------------------------------------ Init ------------------------------------------------------------------*/
void Fn_XPT2046_Init (void)
{
    _Fn_Xpt_Cs(false);

    /* One throwaway conversion leaves the controller in a known state. */
    _Fn_Xpt_Cs(true);
    (void)_Fn_Xpt_Read_Channel(XPT_CMD_READ_Z1);
    _Fn_Xpt_Cs(false);
}

/*------------------------------------------------------------ Raw read --------------------------------------------------------------*/
bool Fn_XPT2046_Read_Raw (uint16_t *pu16X, uint16_t *pu16Y, uint16_t *pu16Z)
{
    uint16_t u16Z1;
    uint16_t u16Z2;
    uint16_t u16Z;
    bool bTouched;

    if ((pu16X == NULL) || (pu16Y == NULL))
    {
        return false;
    }

    _Fn_Xpt_Cs(true);

    u16Z1 = _Fn_Xpt_Read_Channel(XPT_CMD_READ_Z1);
    u16Z2 = _Fn_Xpt_Read_Channel(XPT_CMD_READ_Z2);

    /* Pressure rises as Z1 rises and Z2 falls. */
    u16Z = (uint16_t)(u16Z1 + (4095U - u16Z2));
    bTouched = (u16Z1 > 0U) && (u16Z > XPT2046_Z_THRESHOLD);

    if (bTouched)
    {
        uint16_t u16X = _Fn_Xpt_Median(XPT_CMD_READ_X);
        uint16_t u16Y = _Fn_Xpt_Median(XPT_CMD_READ_Y);

        if ((u16X == XPT_RAW_INVALID) || (u16Y == XPT_RAW_INVALID))
        {
            /* Pressure said yes but the position did not resolve. Report
             * no touch rather than hand back a railed coordinate. */
            bTouched = false;
        }
        else
        {
            *pu16X = u16X;
            *pu16Y = u16Y;
        }
    }

    _Fn_Xpt_Cs(false);

    if (pu16Z != NULL)
    {
        *pu16Z = u16Z;
    }

    return bTouched;
}

/*------------------------------------------------------------ Diagnostic -----------------------------------------------------------*/
void Fn_XPT2046_Read_Diag (uint16_t *pu16Z1, uint16_t *pu16Z2, uint16_t *pu16X, uint16_t *pu16Y)
{
    uint16_t u16Z1;
    uint16_t u16Z2;
    uint16_t u16X;
    uint16_t u16Y;

    _Fn_Xpt_Cs(true);
    u16Z1 = _Fn_Xpt_Read_Channel(XPT_CMD_READ_Z1);
    u16Z2 = _Fn_Xpt_Read_Channel(XPT_CMD_READ_Z2);
    u16X  = _Fn_Xpt_Read_Channel(XPT_CMD_READ_X);
    u16Y  = _Fn_Xpt_Read_Channel(XPT_CMD_READ_Y);
    _Fn_Xpt_Cs(false);

    if (pu16Z1 != NULL) { *pu16Z1 = u16Z1; }
    if (pu16Z2 != NULL) { *pu16Z2 = u16Z2; }
    if (pu16X  != NULL) { *pu16X  = u16X;  }
    if (pu16Y  != NULL) { *pu16Y  = u16Y;  }
}

/*------------------------------------------------------------ Raw -> pixels ---------------------------------------------------------*/
static uint16_t _Fn_Xpt_Scale (uint16_t u16Raw, uint16_t u16Min, uint16_t u16Max, uint16_t u16Span, bool bInvert)
{
    uint32_t u32Pos;

    if (u16Max <= u16Min)
    {
        return 0U;
    }

    if (u16Raw <= u16Min) { u32Pos = 0U; }
    else if (u16Raw >= u16Max) { u32Pos = (uint32_t)u16Span - 1U; }
    else
    {
        u32Pos = ((uint32_t)(u16Raw - u16Min) * (uint32_t)u16Span) / (uint32_t)(u16Max - u16Min);
        if (u32Pos >= u16Span)
        {
            u32Pos = (uint32_t)u16Span - 1U;
        }
    }

    return (uint16_t)(bInvert ? (((uint32_t)u16Span - 1U) - u32Pos) : u32Pos);
}

bool Fn_XPT2046_Read (uint16_t *pu16X, uint16_t *pu16Y)
{
    return Fn_XPT2046_Read_Both(NULL, NULL, pu16X, pu16Y);
}

bool Fn_XPT2046_Read_Both (uint16_t *pu16RawX, uint16_t *pu16RawY,
                           uint16_t *pu16PixX, uint16_t *pu16PixY)
{
    uint16_t u16RawX;
    uint16_t u16RawY;
    uint16_t u16PixX;
    uint16_t u16PixY;

    if (!Fn_XPT2046_Read_Raw(&u16RawX, &u16RawY, NULL))
    {
        return false;
    }

    if (pu16RawX != NULL) { *pu16RawX = u16RawX; }
    if (pu16RawY != NULL) { *pu16RawY = u16RawY; }

    if (sCal.bSwapXY)
    {
        /* Panel X runs along the display's vertical axis in landscape. */
        u16PixX = _Fn_Xpt_Scale(u16RawY, sCal.u16RawYMin, sCal.u16RawYMax, ILI9341_WIDTH,  sCal.bInvertX);
        u16PixY = _Fn_Xpt_Scale(u16RawX, sCal.u16RawXMin, sCal.u16RawXMax, ILI9341_HEIGHT, sCal.bInvertY);
    }
    else
    {
        u16PixX = _Fn_Xpt_Scale(u16RawX, sCal.u16RawXMin, sCal.u16RawXMax, ILI9341_WIDTH,  sCal.bInvertX);
        u16PixY = _Fn_Xpt_Scale(u16RawY, sCal.u16RawYMin, sCal.u16RawYMax, ILI9341_HEIGHT, sCal.bInvertY);
    }

    if (pu16PixX != NULL) { *pu16PixX = u16PixX; }
    if (pu16PixY != NULL) { *pu16PixY = u16PixY; }

    return true;
}

/*------------------------------------------------------------ Calibration -----------------------------------------------------------*/
void Fn_XPT2046_Set_Calibration (const StructTouchCal *psCal)
{
    if (psCal != NULL)
    {
        sCal = *psCal;
    }
}

void Fn_XPT2046_Get_Calibration (StructTouchCal *psCal)
{
    if (psCal != NULL)
    {
        *psCal = sCal;
    }
}
