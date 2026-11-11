/*
 * ili9341.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See ili9341.h.
 */

#include "ili9341.h"

extern SPI_HandleTypeDef hspi5;

/*------------------------------------------------------------ Commands --------------------------------------------------------------*/
#define ILI_CMD_SLPOUT          0x11U
#define ILI_CMD_DISPON          0x29U
#define ILI_CMD_CASET           0x2AU
#define ILI_CMD_PASET           0x2BU
#define ILI_CMD_RAMWR           0x2CU
#define ILI_CMD_MADCTL          0x36U
#define ILI_CMD_PIXFMT          0x3AU
#define ILI_CMD_RDDID4          0xD3U

/* MV = 1 (row/column exchange) gives 320 x 240 landscape, BGR panel order.
 * MY | MX added to rotate the image 180 degrees, which is the way this
 * panel is physically mounted. */
#define ILI_MADCTL_LANDSCAPE    0xE8U

#define ILI_SPI_TIMEOUT_MS      1000U

/* Largest run of identical pixels pushed per blocking chunk when filling. */
#define ILI_FILL_CHUNK_PX       256U

static volatile bool bDmaBusy;
static void (*pfnFlushDone)(void);

void Fn_ILI9341_Set_Flush_Done_Cb (void (*pfnDone)(void))
{
    pfnFlushDone = pfnDone;
}

/*------------------------------------------------------------ Pin helpers -----------------------------------------------------------*/
static inline void _Fn_ILI_Cs (bool bActive)
{
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, bActive ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static inline void _Fn_ILI_Dc (bool bData)
{
    HAL_GPIO_WritePin(DC_GPIO_Port, DC_Pin, bData ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/*------------------------------------------------------------ Byte level ------------------------------------------------------------*/
static void _Fn_ILI_Write_Cmd (uint8_t u8Cmd)
{
    _Fn_ILI_Dc(false);
    _Fn_ILI_Cs(true);
    (void)HAL_SPI_Transmit(&hspi5, &u8Cmd, 1U, ILI_SPI_TIMEOUT_MS);
    _Fn_ILI_Cs(false);
}

static void _Fn_ILI_Write_Data (const uint8_t *pu8Data, uint16_t u16Len)
{
    _Fn_ILI_Dc(true);
    _Fn_ILI_Cs(true);
    (void)HAL_SPI_Transmit(&hspi5, (uint8_t *)pu8Data, u16Len, ILI_SPI_TIMEOUT_MS);
    _Fn_ILI_Cs(false);
}

static void _Fn_ILI_Write_Cmd_Params (uint8_t u8Cmd, const uint8_t *pu8Params, uint16_t u16Len)
{
    _Fn_ILI_Write_Cmd(u8Cmd);
    if (u16Len > 0U)
    {
        _Fn_ILI_Write_Data(pu8Params, u16Len);
    }
}

/*------------------------------------------------------------ Reset -----------------------------------------------------------------*/
static void _Fn_ILI_Hw_Reset (void)
{
    HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_SET);
    osDelay(5);
    HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_RESET);
    osDelay(20);
    HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_SET);
    osDelay(150);
}

/*------------------------------------------------------------ Panel ID --------------------------------------------------------------
 * Register 0xD3 returns a dummy byte then 0x00 0x93 0x41.
 *
 * ILI9341 reads are specified at about 6.6 MHz maximum, well below the
 * 21 Mbit/s used for writing, so the bus is slowed down for this one
 * transfer and put back afterwards.
 */
uint32_t Fn_ILI9341_Read_Id (void)
{
    uint8_t u8Cmd = ILI_CMD_RDDID4;
    uint8_t u8Rx[4] = { 0U, 0U, 0U, 0U };
    uint32_t u32OldPrescaler = hspi5.Init.BaudRatePrescaler;
    uint32_t u32Id;

    hspi5.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;     /* 2.6 Mbit/s */
    (void)HAL_SPI_Init(&hspi5);

    _Fn_ILI_Dc(false);
    _Fn_ILI_Cs(true);
    (void)HAL_SPI_Transmit(&hspi5, &u8Cmd, 1U, ILI_SPI_TIMEOUT_MS);
    _Fn_ILI_Dc(true);
    (void)HAL_SPI_Receive(&hspi5, u8Rx, 4U, ILI_SPI_TIMEOUT_MS);
    _Fn_ILI_Cs(false);

    hspi5.Init.BaudRatePrescaler = u32OldPrescaler;
    (void)HAL_SPI_Init(&hspi5);

    /* u8Rx[0] is the dummy byte. */
    u32Id = ((uint32_t)u8Rx[1] << 16) | ((uint32_t)u8Rx[2] << 8) | (uint32_t)u8Rx[3];

    return u32Id;
}

/*------------------------------------------------------------ Init ------------------------------------------------------------------*/
void Fn_ILI9341_Init (void)
{
    static const uint8_t u8PwCtrA[]   = { 0x39U, 0x2CU, 0x00U, 0x34U, 0x02U };
    static const uint8_t u8PwCtrB[]   = { 0x00U, 0xC1U, 0x30U };
    static const uint8_t u8DrvTimA[]  = { 0x85U, 0x00U, 0x78U };
    static const uint8_t u8DrvTimB[]  = { 0x00U, 0x00U };
    static const uint8_t u8PwOnSeq[]  = { 0x64U, 0x03U, 0x12U, 0x81U };
    static const uint8_t u8PumpRatio[]= { 0x20U };
    static const uint8_t u8PwCtr1[]   = { 0x23U };
    static const uint8_t u8PwCtr2[]   = { 0x10U };
    static const uint8_t u8VmCtr1[]   = { 0x3EU, 0x28U };
    static const uint8_t u8VmCtr2[]   = { 0x86U };
    static const uint8_t u8Madctl[]   = { ILI_MADCTL_LANDSCAPE };
    static const uint8_t u8PixFmt[]   = { 0x55U };                  /* 16 bit */
    static const uint8_t u8FrmCtr1[]  = { 0x00U, 0x18U };
    static const uint8_t u8DisCtrl[]  = { 0x08U, 0x82U, 0x27U };
    static const uint8_t u8Gamma3En[] = { 0x00U };
    static const uint8_t u8GammaSet[] = { 0x01U };
    static const uint8_t u8GammaP[]   = { 0x0FU, 0x31U, 0x2BU, 0x0CU, 0x0EU, 0x08U, 0x4EU,
                                          0xF1U, 0x37U, 0x07U, 0x10U, 0x03U, 0x0EU, 0x09U, 0x00U };
    static const uint8_t u8GammaN[]   = { 0x00U, 0x0EU, 0x14U, 0x03U, 0x11U, 0x07U, 0x31U,
                                          0xC1U, 0x48U, 0x08U, 0x0FU, 0x0CU, 0x31U, 0x36U, 0x0FU };

    bDmaBusy = false;

    _Fn_ILI_Cs(false);
    _Fn_ILI_Hw_Reset();

    _Fn_ILI_Write_Cmd_Params(0xCBU, u8PwCtrA,    sizeof(u8PwCtrA));
    _Fn_ILI_Write_Cmd_Params(0xCFU, u8PwCtrB,    sizeof(u8PwCtrB));
    _Fn_ILI_Write_Cmd_Params(0xE8U, u8DrvTimA,   sizeof(u8DrvTimA));
    _Fn_ILI_Write_Cmd_Params(0xEAU, u8DrvTimB,   sizeof(u8DrvTimB));
    _Fn_ILI_Write_Cmd_Params(0xEDU, u8PwOnSeq,   sizeof(u8PwOnSeq));
    _Fn_ILI_Write_Cmd_Params(0xF7U, u8PumpRatio, sizeof(u8PumpRatio));
    _Fn_ILI_Write_Cmd_Params(0xC0U, u8PwCtr1,    sizeof(u8PwCtr1));
    _Fn_ILI_Write_Cmd_Params(0xC1U, u8PwCtr2,    sizeof(u8PwCtr2));
    _Fn_ILI_Write_Cmd_Params(0xC5U, u8VmCtr1,    sizeof(u8VmCtr1));
    _Fn_ILI_Write_Cmd_Params(0xC7U, u8VmCtr2,    sizeof(u8VmCtr2));
    _Fn_ILI_Write_Cmd_Params(ILI_CMD_MADCTL, u8Madctl, sizeof(u8Madctl));
    _Fn_ILI_Write_Cmd_Params(ILI_CMD_PIXFMT, u8PixFmt, sizeof(u8PixFmt));
    _Fn_ILI_Write_Cmd_Params(0xB1U, u8FrmCtr1,  sizeof(u8FrmCtr1));
    _Fn_ILI_Write_Cmd_Params(0xB6U, u8DisCtrl,  sizeof(u8DisCtrl));
    _Fn_ILI_Write_Cmd_Params(0xF2U, u8Gamma3En, sizeof(u8Gamma3En));
    _Fn_ILI_Write_Cmd_Params(0x26U, u8GammaSet, sizeof(u8GammaSet));
    _Fn_ILI_Write_Cmd_Params(0xE0U, u8GammaP,   sizeof(u8GammaP));
    _Fn_ILI_Write_Cmd_Params(0xE1U, u8GammaN,   sizeof(u8GammaN));

    _Fn_ILI_Write_Cmd(ILI_CMD_SLPOUT);
    osDelay(120);
    _Fn_ILI_Write_Cmd(ILI_CMD_DISPON);
    osDelay(20);
}

/*------------------------------------------------------------ Drawing window --------------------------------------------------------*/
void Fn_ILI9341_Set_Window (uint16_t u16X1, uint16_t u16Y1, uint16_t u16X2, uint16_t u16Y2)
{
    uint8_t u8Buf[4];

    u8Buf[0] = (uint8_t)(u16X1 >> 8);
    u8Buf[1] = (uint8_t)(u16X1 & 0xFFU);
    u8Buf[2] = (uint8_t)(u16X2 >> 8);
    u8Buf[3] = (uint8_t)(u16X2 & 0xFFU);
    _Fn_ILI_Write_Cmd_Params(ILI_CMD_CASET, u8Buf, 4U);

    u8Buf[0] = (uint8_t)(u16Y1 >> 8);
    u8Buf[1] = (uint8_t)(u16Y1 & 0xFFU);
    u8Buf[2] = (uint8_t)(u16Y2 >> 8);
    u8Buf[3] = (uint8_t)(u16Y2 & 0xFFU);
    _Fn_ILI_Write_Cmd_Params(ILI_CMD_PASET, u8Buf, 4U);

    _Fn_ILI_Write_Cmd(ILI_CMD_RAMWR);
}

/*------------------------------------------------------------ Solid fill ------------------------------------------------------------*/
void Fn_ILI9341_Fill_Rect (uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H, uint16_t u16Colour)
{
    static uint8_t u8Chunk[ILI_FILL_CHUNK_PX * 2U];
    uint32_t u32Remaining;
    uint32_t u32Px;
    uint16_t i;

    if ((u16W == 0U) || (u16H == 0U) ||
        (u16X >= ILI9341_WIDTH) || (u16Y >= ILI9341_HEIGHT))
    {
        return;
    }

    if ((uint32_t)u16X + u16W > ILI9341_WIDTH)  { u16W = (uint16_t)(ILI9341_WIDTH  - u16X); }
    if ((uint32_t)u16Y + u16H > ILI9341_HEIGHT) { u16H = (uint16_t)(ILI9341_HEIGHT - u16Y); }

    /* Big endian on the wire. */
    for (i = 0U; i < ILI_FILL_CHUNK_PX; i++)
    {
        u8Chunk[(i * 2U)]      = (uint8_t)(u16Colour >> 8);
        u8Chunk[(i * 2U) + 1U] = (uint8_t)(u16Colour & 0xFFU);
    }

    Fn_ILI9341_Set_Window(u16X, u16Y, (uint16_t)(u16X + u16W - 1U), (uint16_t)(u16Y + u16H - 1U));

    u32Remaining = (uint32_t)u16W * (uint32_t)u16H;

    _Fn_ILI_Dc(true);
    _Fn_ILI_Cs(true);
    while (u32Remaining > 0U)
    {
        u32Px = (u32Remaining > ILI_FILL_CHUNK_PX) ? ILI_FILL_CHUNK_PX : u32Remaining;
        (void)HAL_SPI_Transmit(&hspi5, u8Chunk, (uint16_t)(u32Px * 2U), ILI_SPI_TIMEOUT_MS);
        u32Remaining -= u32Px;
    }
    _Fn_ILI_Cs(false);
}

void Fn_ILI9341_Fill_Screen (uint16_t u16Colour)
{
    Fn_ILI9341_Fill_Rect(0U, 0U, ILI9341_WIDTH, ILI9341_HEIGHT, u16Colour);
}

/*------------------------------------------------------------ DMA flush -------------------------------------------------------------
 * The window must already be set by the caller. CS is released in the
 * transfer complete callback below.
 */
bool Fn_ILI9341_Flush_DMA (const uint8_t *pu8Data, uint32_t u32Len)
{
    if (bDmaBusy || (pu8Data == NULL) || (u32Len == 0U) || (u32Len > 0xFFFFU))
    {
        return false;
    }

    bDmaBusy = true;

    _Fn_ILI_Dc(true);
    _Fn_ILI_Cs(true);

    if (HAL_SPI_Transmit_DMA(&hspi5, (uint8_t *)pu8Data, (uint16_t)u32Len) != HAL_OK)
    {
        _Fn_ILI_Cs(false);
        bDmaBusy = false;
        return false;
    }

    return true;
}

bool Fn_ILI9341_Is_Busy (void)
{
    return bDmaBusy;
}

/*------------------------------------------------------------ DMA completion --------------------------------------------------------*/
void HAL_SPI_TxCpltCallback (SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI5)
    {
        _Fn_ILI_Cs(false);
        bDmaBusy = false;

        if (pfnFlushDone != NULL)
        {
            pfnFlushDone();
        }
    }
}
