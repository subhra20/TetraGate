/* USER CODE BEGIN Header */
/*
 * freertos.c
 *
 *  Task creation. TaskInit = device configuration (EEPROM JSON or defaults).
 */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include "utils.h"
#include "usart.h"
#include "eeprom_operation.h"
#include "DeviceConfigJSON.h"
#include "radio_manager.h"
#include "packet_log.h"
#include "node_table.h"
#include "monitor.h"
#include "lvgl_port.h"
#include "gui_manager.h"
#include "xpt2046.h"
#include "lvgl.h"
#include "uplink.h"
#include "gw_command.h"
/* USER CODE END Includes */

osThreadId TaskInitHandle;
osThreadId Task_LoRa_1Handle;
osThreadId Task_LoRa_2Handle;
osThreadId Task_LoRa_3Handle;
osThreadId Task_LoRa_4Handle;
osThreadId Task_LVGLHandle;
osThreadId Task_MonitorHandle;
osSemaphoreId sem_lora_uartHandle;

/* USER CODE BEGIN Variables */
osMutexId mtx_configHandle;     /* protects sLoRaGtwConfParam */

extern UART_HandleTypeDef huart3;
/* USER CODE END Variables */

void Fn_Task_Taskinit(void const * argument);
void Fn_Task_LoRa_First(void const * argument);
void Fn_Task_LoRa_Second(void const * argument);
void Fn_Task_LoRa_Third(void const * argument);
void Fn_Task_LoRa_Fourth(void const * argument);
void Fn_Task_Display(void const * argument);
void Fn_Task_Monitor(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* USER CODE BEGIN FunctionPrototypes */
static void _Fn_Uart_Print (const char *chMsg);
static uint8_t _Fn_Reset_Reason (void);
/* USER CODE END FunctionPrototypes */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN RTOS_MUTEX */
  osMutexDef(mtx_config);
  mtx_configHandle = osMutexCreate(osMutex(mtx_config));
  /* USER CODE END RTOS_MUTEX */

  osSemaphoreDef(sem_lora_uart);
  sem_lora_uartHandle = osSemaphoreCreate(osSemaphore(sem_lora_uart), 1);

  /* TaskInit stack 512 words: cJSON parse/print runs here (set the same in the .ioc) */
  osThreadDef(TaskInit, Fn_Task_Taskinit, osPriorityAboveNormal, 0, 512);
  TaskInitHandle = osThreadCreate(osThread(TaskInit), NULL);

  osThreadDef(Task_LoRa_1, Fn_Task_LoRa_First, osPriorityNormal, 0, 256);
  Task_LoRa_1Handle = osThreadCreate(osThread(Task_LoRa_1), NULL);

  osThreadDef(Task_LoRa_2, Fn_Task_LoRa_Second, osPriorityNormal, 0, 256);
  Task_LoRa_2Handle = osThreadCreate(osThread(Task_LoRa_2), NULL);

  osThreadDef(Task_LoRa_3, Fn_Task_LoRa_Third, osPriorityNormal, 0, 256);
  Task_LoRa_3Handle = osThreadCreate(osThread(Task_LoRa_3), NULL);

  osThreadDef(Task_LoRa_4, Fn_Task_LoRa_Fourth, osPriorityNormal, 0, 256);
  Task_LoRa_4Handle = osThreadCreate(osThread(Task_LoRa_4), NULL);

  /* LVGL rendering needs far more than the 256 word default (set the same in the .ioc) */
  osThreadDef(Task_LVGL, Fn_Task_Display, osPriorityNormal, 0, 2048);
  Task_LVGLHandle = osThreadCreate(osThread(Task_LVGL), NULL);

  /* Supervisor and watchdog. Low priority on purpose: it must only run
     when every other task has had its turn. */
  osThreadDef(Task_Monitor, Fn_Task_Monitor, osPriorityLow, 0, 256);
  Task_MonitorHandle = osThreadCreate(osThread(Task_Monitor), NULL);
}

/* USER CODE BEGIN Header_Fn_Task_Taskinit */
/**
  * Task 1: device configuration. Runs once, then deletes itself.
  *
  *  EEPROM init -> valid JSON in the JSON region?
  *    yes : parse JSON -> memcpy into sLoRaGtwConfParam -> configure the 4 LoRa modules from it
  *    no  : Fn_Def_LoRa_1st/2nd/3rd/4th_Init() (defaults) -> generate JSON -> store in EEPROM
  */
/* USER CODE END Header_Fn_Task_Taskinit */
void Fn_Task_Taskinit(void const * argument)
{
  /* USER CODE BEGIN Fn_Task_Taskinit */
  static char chJsonBuf[EEPROM_JSON_MAX_LEN + 1U];
  EnumEEPROMSts enEepSts;
  EnumEEPROMSts enSaveSts;
  enJSON_ProcessStatus enJsonSts = enum_ERROR_JSON_FORMAT;
  char *pchJson;
  char chLine[80];
  uint8_t i;

  memset(&sLoRaGtwConfParam, 0, sizeof(sLoRaGtwConfParam));

  /* 1. EEPROM, JSON library and the received packet ring */
  Fn_JSON_Init();       /* cJSON -> pvPortMalloc / vPortFree */
  Fn_EEPROM_Init();     /* EEPROM bus mutex                  */
  Fn_PktLog_Init();     /* packet ring mutex                 */
  Fn_NodeTable_Init();  /* node table and whitelist          */
  (void)Fn_NodeTable_Whitelist_Load();

  /* 1a. Boot counter and reset reason, kept in the EEPROM header. */
  {
    StructEepromHdr sHdr;

    if (Fn_EEPROM_Hdr_Read(&sHdr) != en_EEPROM_OK)
    {
      (void)memset(&sHdr, 0, sizeof(sHdr));
      sHdr.u8ActiveSlot = 1U;       /* first config write goes to slot A */
    }

    sHdr.u32BootCount++;
    sHdr.u8ResetReason = _Fn_Reset_Reason();
    (void)Fn_EEPROM_Hdr_Write(&sHdr);
  }

  /* 2. Valid JSON in the EEPROM region? */
  enEepSts = Fn_EEPROM_JSON_Read(chJsonBuf, sizeof(chJsonBuf));

  if (enEepSts == en_EEPROM_OK)
  {
    /* 3a. Parse -> validate -> copy into sLoRaGtwConfParam */
    enJsonSts = FnProcessLoRaConfig(chJsonBuf);
  }

  if ((enEepSts == en_EEPROM_OK) && (enJsonSts == enum_JSON_PROCESS_OK))
  {
    /* 4a. Configure the LoRa modules from sLoRaGtwConfParam
     *     (each call writes the running settings + status back into it) */
    for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
    {
      (void)Fn_LoRa_Init_From_Conf(i);
    }
    _Fn_Uart_Print("\r\nConfig: loaded from EEPROM\r\n");
  }
  else
  {
    /* 3b. No valid JSON: default configuration
     *     (each call writes the running settings + status into sLoRaGtwConfParam) */
    (void)Fn_Def_LoRa_1st_Init();
    (void)Fn_Def_LoRa_2nd_Init();
    (void)Fn_Def_LoRa_3rd_Init();
    (void)Fn_Def_LoRa_4th_Init();

    /* 4b. Generate JSON from sLoRaGtwConfParam and store it */
    pchJson = Fn_JSON_LoRa_Config_Generate();
    enSaveSts = (pchJson != NULL) ? Fn_EEPROM_JSON_Write(pchJson) : en_EEPROM_ERR_PARAM;
    if (pchJson != NULL)
    {
      cJSON_free(pchJson);
    }

    (void)snprintf(chLine, sizeof(chLine), "\r\nConfig: defaults (EEPROM read %d, JSON %d), saved to EEPROM: %s\r\n",
                   (int)enEepSts, (int)enJsonSts, (enSaveSts == en_EEPROM_OK) ? "OK" : "FAILED");
    _Fn_Uart_Print(chLine);
  }

  /* 5. Report: active configuration and radio status on UART3 */
  pchJson = Fn_JSON_LoRa_Config_Generate();
  if (pchJson != NULL)
  {
    _Fn_Uart_Print(pchJson);
    _Fn_Uart_Print("\r\n");
    cJSON_free(pchJson);
  }

  for (i = 0U; i < DEF_MAX_LoRa_Module; i++)
  {
    (void)snprintf(chLine, sizeof(chLine), "LoRa%u: %s\r\n", (unsigned)(i + 1U),
                   (sLoRaGtwConfParam.sLoRaConf[i].enLoRaSts == en_LoRa_init_Success) ? "OK"       :
                   (sLoRaGtwConfParam.sLoRaConf[i].enLoRaSts == en_LoRa_Disabled)     ? "disabled" :
                   (sLoRaGtwConfParam.sLoRaConf[i].enLoRaSts == en_LoRa_init_Fail)    ? "not found (0x42 != 0x12)" :
                                                                         "unavailable");
    _Fn_Uart_Print(chLine);
  }

  osThreadTerminate(NULL);
  /* USER CODE END Fn_Task_Taskinit */
}

/* The four radios sit on four independent SPI peripherals, so one task
 * each lets their FIFO reads run in parallel. They all run the same body
 * in radio_manager.c, so there is only one implementation to maintain. */
void Fn_Task_LoRa_First(void const * argument)
{
  /* USER CODE BEGIN Fn_Task_LoRa_First */
  Fn_Radio_Task_Service(DEF_LoRa_Module_1);
  /* USER CODE END Fn_Task_LoRa_First */
}

void Fn_Task_LoRa_Second(void const * argument)
{
  /* USER CODE BEGIN Fn_Task_LoRa_Second */
  Fn_Radio_Task_Service(DEF_LoRa_Module_2);
  /* USER CODE END Fn_Task_LoRa_Second */
}

void Fn_Task_LoRa_Third(void const * argument)
{
  /* USER CODE BEGIN Fn_Task_LoRa_Third */
  Fn_Radio_Task_Service(DEF_LoRa_Module_3);
  /* USER CODE END Fn_Task_LoRa_Third */
}

void Fn_Task_LoRa_Fourth(void const * argument)
{
  /* USER CODE BEGIN Fn_Task_LoRa_Fourth */
  Fn_Radio_Task_Service(DEF_LoRa_Module_4);
  /* USER CODE END Fn_Task_LoRa_Fourth */
}

void Fn_Task_Display(void const * argument)
{
  /* USER CODE BEGIN Fn_Task_Display */
  char chLine[112];
  StructTouchCal sCal;

  osDelay(800);                 /* let TaskInit finish its UART report */

  Fn_LVGL_Port_Init();          /* ILI9341 + XPT2046 + LVGL core */

  (void)snprintf(chLine, sizeof(chLine), "LVGL %u.%u.%u up, starting touch calibration\r\n",
                 (unsigned)LVGL_VERSION_MAJOR, (unsigned)LVGL_VERSION_MINOR,
                 (unsigned)LVGL_VERSION_PATCH);
  _Fn_Uart_Print(chLine);

  /* Stored calibration wins: only a blank or damaged record makes the
     user touch crosshairs again. */
  if (Fn_EEPROM_Record_Read(EEPROM_TOUCHCAL_ADDR, EEPROM_TOUCHCAL_MAGIC,
                            &sCal, (uint16_t)sizeof(sCal)) == en_EEPROM_OK)
  {
    Fn_XPT2046_Set_Calibration(&sCal);
    _Fn_Uart_Print("TOUCH cal: loaded from EEPROM\r\n");
  }
  else
  {
    bool bCalOk = false;
    uint8_t u8Try;

    /* A rejected calibration is worth asking again for: falling back to
       the defaults leaves touch unusable. */
    for (u8Try = 0U; (u8Try < 3U) && !bCalOk; u8Try++)
    {
      bCalOk = Fn_GUI_Calibrate();
      if (!bCalOk)
      {
        StructCalDebug sDbg;
        uint8_t k;

        Fn_GUI_Get_Cal_Debug(&sDbg);
        (void)snprintf(chLine, sizeof(chLine),
                       "TOUCH cal rejected: reason=%u pts=%u edges L%u R%u T%u B%u spread %u/%u swap=%u\r\n",
                       (unsigned)sDbg.enResult, (unsigned)sDbg.u8Points,
                       (unsigned)sDbg.u16Edge[0], (unsigned)sDbg.u16Edge[1],
                       (unsigned)sDbg.u16Edge[2], (unsigned)sDbg.u16Edge[3],
                       (unsigned)sDbg.u16SpreadA, (unsigned)sDbg.u16SpreadB,
                       (unsigned)sDbg.bSwap);
        _Fn_Uart_Print(chLine);

        for (k = 0U; k < sDbg.u8Points; k++)
        {
          (void)snprintf(chLine, sizeof(chLine), "  pt%u raw x=%4u y=%4u\r\n",
                         (unsigned)(k + 1U),
                         (unsigned)sDbg.u16RawX[k], (unsigned)sDbg.u16RawY[k]);
          _Fn_Uart_Print(chLine);
        }
      }
    }

    if (bCalOk)
    {
      EnumEEPROMSts enSave;

      Fn_XPT2046_Get_Calibration(&sCal);
      enSave = Fn_EEPROM_Record_Write(EEPROM_TOUCHCAL_ADDR, EEPROM_TOUCHCAL_MAGIC,
                                      &sCal, (uint16_t)sizeof(sCal));

      (void)snprintf(chLine, sizeof(chLine),
                     "TOUCH cal OK: x %u..%u y %u..%u swap=%u saved=%s\r\n",
                     (unsigned)sCal.u16RawXMin, (unsigned)sCal.u16RawXMax,
                     (unsigned)sCal.u16RawYMin, (unsigned)sCal.u16RawYMax,
                     (unsigned)sCal.bSwapXY,
                     (enSave == en_EEPROM_OK) ? "yes" : "FAILED");
    }
    else
    {
      (void)snprintf(chLine, sizeof(chLine), "TOUCH cal: gave up, using defaults\r\n");
    }
    _Fn_Uart_Print(chLine);
  }

  Fn_GUI_Init();                /* builds all six screens */

  /* All six screens are built up front, so this is the peak LVGL pool
     usage. Worth reporting: exhausting it asserts inside LVGL, which
     looks like a hang rather than an allocation failure. */
  {
    lv_mem_monitor_t sMon;

    lv_mem_monitor(&sMon);
    (void)snprintf(chLine, sizeof(chLine),
                   "GUI: 6 screens up, LVGL pool %u%% used, %lu B free\r\n",
                   (unsigned)sMon.used_pct, (unsigned long)sMon.free_size);
    _Fn_Uart_Print(chLine);
  }

  /* From here USART3 carries machine readable JSON lines as well as the
     boot log. A PC tool keys on the leading brace and ignores the rest. */
  Fn_Uplink_Hello();

  /* Arms interrupt driven reception so a PC can configure the gateway.
     Started after the hello so the feed is already announced. */
  Fn_Command_Init();

  for(;;)
  {
    Fn_Monitor_Checkin(en_Mon_LVGL);

    /* LVGL 8.3 is not thread safe: it is only ever touched from here. */
    (void)lv_timer_handler();
    osDelay(5);
  }
  /* USER CODE END Fn_Task_Display */
}

void Fn_Task_Monitor(void const * argument)
{
  /* USER CODE BEGIN Fn_Task_Monitor */
  Fn_Monitor_Task_Service();
  /* USER CODE END Fn_Task_Monitor */
}

/* USER CODE BEGIN Application */
/* Read once at start-up, then cleared so the next reset reports its own
 * cause. The watchdog and software flags are checked before the pin and
 * power-on flags, which are often set alongside them. */
static uint8_t _Fn_Reset_Reason (void)
{
  uint8_t u8Reason = (uint8_t)en_Reset_Unknown;

  if      (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) { u8Reason = (uint8_t)en_Reset_Iwdg;     }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) { u8Reason = (uint8_t)en_Reset_Wwdg;     }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))  { u8Reason = (uint8_t)en_Reset_Software; }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) { u8Reason = (uint8_t)en_Reset_LowPower; }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST))  { u8Reason = (uint8_t)en_Reset_BrownOut; }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))  { u8Reason = (uint8_t)en_Reset_PowerOn;  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))  { u8Reason = (uint8_t)en_Reset_Pin;      }

  __HAL_RCC_CLEAR_RESET_FLAGS();

  return u8Reason;
}

static void _Fn_Uart_Print (const char *chMsg)
{
  if (chMsg == NULL)
  {
    return;
  }

  osSemaphoreWait(sem_lora_uartHandle, osWaitForever);
  (void)HAL_UART_Transmit(&huart3, (uint8_t *)chMsg, (uint16_t)strlen(chMsg), 1000U);
  osSemaphoreRelease(sem_lora_uartHandle);
}
/* USER CODE END Application */
