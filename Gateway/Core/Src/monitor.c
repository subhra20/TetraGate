/*
 * monitor.c
 *
 *  Created on: Nov 1, 2026
 *      Author: subhr
 *
 *  See monitor.h.
 */

#include "monitor.h"
#include "iwdg.h"
#include "radio_manager.h"
#include "node_table.h"
#include "uplink.h"
#include "gw_command.h"

/* How long a task may go unseen before it counts as stalled.
 *
 * A radio task blocks on its DIO0 notification with a 1 s health-check
 * timeout, so it is seen at least once a second: 3 s is three missed
 * cycles. The UI task loops every 5 ms, but a full LVGL redraw plus a
 * DMA flush can hold it for a while, so it gets more room. */
static const uint32_t u32DeadlineMs[en_Mon_Count] =
{
    3000U,      /* LoRa 1 */
    3000U,      /* LoRa 2 */
    3000U,      /* LoRa 3 */
    3000U,      /* LoRa 4 */
    5000U,      /* LVGL   */
};

static volatile uint32_t u32LastSeen[en_Mon_Count];
static volatile uint32_t u32StallMask;
static volatile uint32_t u32FeedCount;

void Fn_Monitor_Checkin (EnumMonTask enTask)
{
    if (enTask < en_Mon_Count)
    {
        u32LastSeen[enTask] = HAL_GetTick();
    }
}

uint32_t Fn_Monitor_Get_Stall_Mask (void)
{
    return u32StallMask;
}

uint32_t Fn_Monitor_Get_Feed_Count (void)
{
    return u32FeedCount;
}

void Fn_Monitor_Task_Service (void)
{
    uint32_t u32Start = HAL_GetTick();
    uint32_t u32Now;
    uint32_t u32Mask;
    uint8_t  i;

    /* Treat every task as just-seen, so the first sweep cannot trip on
     * tasks that have not reached their loop yet. */
    for (i = 0U; i < (uint8_t)en_Mon_Count; i++)
    {
        u32LastSeen[i] = u32Start;
    }

    /* Starting the IWDG here means it can never be left running with no
     * feeder. Once started it cannot be stopped except by a reset. */
    MX_IWDG_Init();

    for (;;)
    {
        osDelay(MON_FEED_PERIOD_MS);

        u32Now  = HAL_GetTick();
        u32Mask = 0U;

        for (i = 0U; i < (uint8_t)en_Mon_Count; i++)
        {
            if ((u32Now - u32LastSeen[i]) > u32DeadlineMs[i])
            {
                u32Mask |= (1UL << i);
            }
        }

        u32StallMask = u32Mask;

        /* Cheap, and the only place that needs to run periodically. */
        Fn_NodeTable_Check_Silent();

        /* Gateway health line for the PC tool. Emits only when its own
         * period is due, and gives up on a busy UART instead of waiting
         * - this task is the watchdog feeder and must not be held up. */
        Fn_Uplink_Status_Tick();

        /* Commands arriving from a PC. Handled here rather than in a
         * task of its own: they are rare, and this task already wakes
         * every 200 ms. A save writes the EEPROM and takes a few hundred
         * milliseconds, which is well inside the watchdog period. */
        Fn_Command_Service();

        /* Refuse to refresh while anything is overdue: that is the whole
         * point. During the start-up grace period, refresh regardless. */
        if ((u32Mask == 0U) || ((u32Now - u32Start) < MON_BOOT_GRACE_MS))
        {
            HAL_IWDG_Refresh(&hiwdg);
            u32FeedCount++;
        }
    }
}
