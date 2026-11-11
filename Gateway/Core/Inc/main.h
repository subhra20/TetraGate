/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CS_Pin GPIO_PIN_4
#define CS_GPIO_Port GPIOE
#define DC_Pin GPIO_PIN_5
#define DC_GPIO_Port GPIOE
#define NSS_3_Pin GPIO_PIN_13
#define NSS_3_GPIO_Port GPIOC
#define RESET_Pin GPIO_PIN_2
#define RESET_GPIO_Port GPIOF
#define LED_1_Pin GPIO_PIN_0
#define LED_1_GPIO_Port GPIOB
#define NSS_4_Pin GPIO_PIN_10
#define NSS_4_GPIO_Port GPIOE
#define LED_3_Pin GPIO_PIN_14
#define LED_3_GPIO_Port GPIOB
#define RST_4_Pin GPIO_PIN_11
#define RST_4_GPIO_Port GPIOD
#define DIO_4_Pin GPIO_PIN_12
#define DIO_4_GPIO_Port GPIOD
#define RST_1_Pin GPIO_PIN_7
#define RST_1_GPIO_Port GPIOC
#define DIO_1_Pin GPIO_PIN_9
#define DIO_1_GPIO_Port GPIOA
#define DIO_3_Pin GPIO_PIN_15
#define DIO_3_GPIO_Port GPIOA
#define NSS_2_Pin GPIO_PIN_4
#define NSS_2_GPIO_Port GPIOD
#define RST_2_Pin GPIO_PIN_5
#define RST_2_GPIO_Port GPIOD
#define DIO_2_Pin GPIO_PIN_6
#define DIO_2_GPIO_Port GPIOD
#define T_CS_Pin GPIO_PIN_11
#define T_CS_GPIO_Port GPIOG
#define NSS_1_Pin GPIO_PIN_6
#define NSS_1_GPIO_Port GPIOB
#define RST_3_Pin GPIO_PIN_7
#define RST_3_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define CCMRAM_ALIGN __attribute__((section(".ccmram")))
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
