/*
 * monitor.h
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  Supervisor task and independent watchdog.
 *
 *  The point of a watchdog is to catch a task that has stopped doing its
 *  job. A monitor that simply refreshes the IWDG on a timer proves only
 *  that the monitor itself is running, so every supervised task checks
 *  in once per loop and the IWDG is refreshed only while all of them are
 *  within their deadline. A task that hangs stops being seen, the
 *  refresh stops, and the MCU resets about two seconds later.
 *
 *  The IWDG is started by this task rather than by main(), so the
 *  watchdog cannot be running before anything exists to feed it.
 */

#ifndef INC_MONITOR_H_
#define INC_MONITOR_H_

#include "utils.h"

#define MON_FEED_PERIOD_MS      200U

/* Start-up is fed unconditionally: radio bring-up and, on a first boot,
 * touch calibration both take far longer than the watchdog period. */
#define MON_BOOT_GRACE_MS       15000U

typedef enum
{
    en_Mon_LoRa1 = 0,
    en_Mon_LoRa2,
    en_Mon_LoRa3,
    en_Mon_LoRa4,
    en_Mon_LVGL,
    en_Mon_Count
} EnumMonTask;

/* Called by each supervised task, once per loop. Cheap on purpose. */
void Fn_Monitor_Checkin (EnumMonTask enTask);

/* Body of Task_Monitor. Never returns. */
void Fn_Monitor_Task_Service (void);

/* One bit per EnumMonTask: set means that task is past its deadline. */
uint32_t Fn_Monitor_Get_Stall_Mask (void);

/* Refreshes performed, so the System screen can show the watchdog is
 * genuinely being fed rather than merely configured. */
uint32_t Fn_Monitor_Get_Feed_Count (void);

#endif /* INC_MONITOR_H_ */
