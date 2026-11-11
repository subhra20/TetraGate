/* USER CODE BEGIN Header */
/*
 * main.c
 *
 *  LoRa node: RFM95W on a 7Semi STM32G030F6P6 Anchor board.
 *
 *  Reports a simulated sensor reading to the gateway every few seconds
 *  on the gateway radio 1 channel. No sensor is fitted yet, so the
 *  readings are generated - see node_packet.c.
 *
 *  Everything worth changing lives in node_config.h. The radio settings
 *  there have to match the gateway receiver exactly or the gateway
 *  never hears this node.
 *
 *  This board brings out no UART, so there is no serial log. The two
 *  ways to see what the node is doing locally are the status LED and
 *  the sNodeStats structure below, which is deliberately global and
 *  volatile so a debugger can watch it over SWD. End to end, the real
 *  evidence is the gateway Signal and Log screens.
 */
/* USER CODE END Header */

#include "main.h"
#include "LoRa.h"
#include "node_config.h"
#include "node_packet.h"
#include "node_radio.h"
#include "lora_crypto.h"

SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */

/* Readable over SWD, because this board has no serial port. Volatile so
   the stores are not optimised away and a debugger sees them live. */
typedef struct
{
    uint32_t u32TxOk;
    uint32_t u32TxFail;
    uint32_t u32FreqHzActual;   /* read back from the radio, not assumed */
    uint32_t u32UptimeS;
    uint16_t u16NodeId;
    uint8_t  u8LastSeq;
    uint8_t  u8LastLen;
    uint8_t  u8RadioOk;
    uint8_t  u8CryptoOk;        /* AES and CMAC vectors passed on this part */
} StructNodeStats;

volatile StructNodeStats sNodeStats;

/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);

/* USER CODE BEGIN 0 */

/*------------------------------------------------------------ Watchdog --------------------------------------------------------------
 * Driven straight from the IWDG registers rather than through the HAL.
 * This project has HAL_IWDG_MODULE_ENABLED commented out and does not
 * carry stm32g0xx_hal_iwdg.c, so using the HAL would mean copying a
 * driver into the tree and adding it to the build - more moving parts
 * than a watchdog is worth when the peripheral is four registers.
 *
 * LSI is about 32 kHz. Prescaler /256 with reload 999 gives
 * (999 + 1) * 256 / 32000 = 8.0 s. The loop comes round every
 * DEF_NODE_TX_PERIOD_MS plus the transmit, so 8 s leaves margin without
 * letting a wedged node stay quiet for long.
 */
static void _Fn_Watchdog_Start (void)
{
#if (DEF_NODE_WATCHDOG_ENABLE == 1)
    uint32_t u32Guard;

    /* Frozen while the core is halted, so single stepping does not reset
       the part. Harmless with no debugger attached. */
#ifdef __HAL_DBGMCU_FREEZE_IWDG
    __HAL_DBGMCU_FREEZE_IWDG();
#endif

    IWDG->KR = 0x0000CCCCU;     /* start the counter, also starts LSI */
    IWDG->KR = 0x00005555U;     /* unlock PR and RLR                 */
    IWDG->PR = 6U;              /* /256                              */
    IWDG->RLR = 999U;

    /* Wait for the new values to be taken. Bounded: if LSI never comes
       up, carrying on unprotected beats hanging here forever. */
    u32Guard = 100000U;
    while ((IWDG->SR != 0U) && (u32Guard > 0U))
    {
        u32Guard--;
    }

    IWDG->KR = 0x0000AAAAU;     /* first refresh */
#endif
}

static void _Fn_Watchdog_Refresh (void)
{
#if (DEF_NODE_WATCHDOG_ENABLE == 1)
    IWDG->KR = 0x0000AAAAU;
#endif
}

/*------------------------------------------------------------ Status LED ------------------------------------------------------------*/
static void _Fn_Led_Pulse (uint32_t u32Ms)
{
    HAL_GPIO_WritePin(Status_LED_GPIO_Port, Status_LED_Pin, GPIO_PIN_SET);
    HAL_Delay(u32Ms);
    HAL_GPIO_WritePin(Status_LED_GPIO_Port, Status_LED_Pin, GPIO_PIN_RESET);
}

/* A node whose radio does not answer is useless, but it should say so
   rather than sit dark: fast double blink, retry, forever. From the
   gateway, a dead module and a node out of range look identical, so the
   distinction has to be visible here. */
static void _Fn_Wait_For_Radio (void)
{
    while (!Fn_Node_Radio_Init(&hspi1))
    {
        _Fn_Watchdog_Refresh();
        _Fn_Led_Pulse(60U);
        HAL_Delay(120U);
        _Fn_Led_Pulse(60U);
        HAL_Delay(760U);
    }
}

/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  uint8_t  u8Frame[DEF_NODE_PKT_MAX_LEN];
  uint8_t  u8Seq      = 0U;
  uint32_t u32Reports = 0U;
  /* USER CODE END 1 */

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */
  _Fn_Watchdog_Start();

  sNodeStats.u16NodeId = (uint16_t)DEF_NODE_ID;

  /* Prove the crypto on the part that will run it. There is no host
     compiler in this toolchain, so the FIPS-197 and RFC 4493 vectors are
     checked here instead. A failure means every MIC this node sends is
     wrong and the gateway will reject all of it, so make it visible:
     five fast blinks, and the flag is readable over SWD. */
  sNodeStats.u8CryptoOk = Fn_Crypto_Selftest() ? 1U : 0U;

  if (sNodeStats.u8CryptoOk == 0U)
  {
      uint8_t i;

      for (i = 0U; i < 5U; i++)
      {
          _Fn_Led_Pulse(70U);
          HAL_Delay(70U);
      }
  }

  Fn_Node_Packet_Init();

  _Fn_Wait_For_Radio();

  sNodeStats.u8RadioOk       = 1U;
  sNodeStats.u32FreqHzActual = Fn_Node_Radio_Get_Frequency_Hz();

  /* Three slow blinks: the radio answered and the carrier is set. */
  _Fn_Led_Pulse(150U);
  HAL_Delay(150U);
  _Fn_Led_Pulse(150U);
  HAL_Delay(150U);
  _Fn_Led_Pulse(150U);
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    bool    bStatusFrame;
    uint8_t u8Len;

    _Fn_Watchdog_Refresh();

    /* Every Nth report is the short status frame, so the gateway sees
       two frame types and two payload sizes rather than one of each. */
    bStatusFrame = ((DEF_NODE_STATUS_EVERY > 0U) &&
                    (((u32Reports + 1U) % DEF_NODE_STATUS_EVERY) == 0U));

    u8Len = Fn_Node_Packet_Build(u8Frame, (uint8_t)sizeof(u8Frame),
                                 u8Seq, bStatusFrame);

    if (u8Len >= DEF_NODE_PKT_MIN_LEN)
    {
        if (Fn_Node_Radio_Transmit(u8Frame, u8Len))
        {
            sNodeStats.u32TxOk++;
            _Fn_Led_Pulse(25U);        /* brief: one flash a report */
        }
        else
        {
            sNodeStats.u32TxFail++;
            _Fn_Led_Pulse(400U);       /* long: that one did not go */
        }

        sNodeStats.u8LastSeq = u8Seq;
        sNodeStats.u8LastLen = u8Len;

        /* Advances on every frame, including failed ones. The gateway
           treats a repeated sequence number inside 4 s as the same
           transmission heard twice, so reusing one would get this node
           reports silently counted as duplicates. */
        u8Seq++;
        u32Reports++;
    }

    /* If the SPI link has dropped, stop pretending and go back to
       bringing the radio up. */
    if (!Fn_Node_Radio_Healthy())
    {
        sNodeStats.u8RadioOk = 0U;
        _Fn_Wait_For_Radio();
        sNodeStats.u8RadioOk       = 1U;
        sNodeStats.u32FreqHzActual = Fn_Node_Radio_Get_Frequency_Hz();
    }

    sNodeStats.u32UptimeS = HAL_GetTick() / 1000UL;

    HAL_Delay(DEF_NODE_TX_PERIOD_MS);
    /* USER CODE END 3 */
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, RST_Pin|NSS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(Status_LED_GPIO_Port, Status_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : RST_Pin NSS_Pin Status_LED_Pin */
  GPIO_InitStruct.Pin = RST_Pin|NSS_Pin|Status_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : DIO0_Pin */
  GPIO_InitStruct.Pin = DIO0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DIO0_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
