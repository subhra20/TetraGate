/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.c
  * @brief   This file provides code for the configuration
  *          of the IWDG instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "iwdg.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

IWDG_HandleTypeDef hiwdg;

/* IWDG init function */
void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Reload = 999;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/* USER CODE BEGIN 1 */
/*
 * Timeout: LSI (32 kHz nominal) / prescaler 64 = 500 Hz, reload 999
 *          -> (999 + 1) / 500 = 2.0 s.
 *
 * The LSI is an RC oscillator with a wide tolerance, so the real timeout is
 * roughly 1.4 .. 3.1 s over the full range. The monitor task must therefore
 * refresh well inside that: once per second is the intended rate.
 *
 * HAL_IWDG_Init() starts the counter and the IWDG cannot be stopped again
 * except by a reset, which is why MX_IWDG_Init() is only called once a task
 * exists that refreshes it with HAL_IWDG_Refresh(&hiwdg).
 */
/* USER CODE END 1 */
