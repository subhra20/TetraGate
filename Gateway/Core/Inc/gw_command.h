/*
 * gw_command.h
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  Southbound control: commands arriving from a PC on USART3, one JSON
 *  object per line, CRLF or LF terminated.
 *
 *  This is the other half of uplink.c. Together they let a PC tool both
 *  watch the gateway and configure it, over the single serial port the
 *  Nucleo already exposes as a virtual COM port.
 *
 *  Commands
 *  --------
 *    {"cmd":"ping"}
 *    {"cmd":"get_config"}
 *    {"cmd":"get_nodes"}
 *    {"cmd":"set_radio","n":1,"en":1,"freq":868100000,"sf":9,"bw":8,"cr":4,"pwr":2}
 *    {"cmd":"save"}
 *    {"cmd":"clear_stats"}
 *    {"cmd":"set_mic_policy","require":1}
 *    {"cmd":"reboot"}
 *
 *  Every command is answered with exactly one line:
 *    {"type":"ack","cmd":"save","ok":1}
 *    {"type":"ack","cmd":"set_radio","ok":0,"err":"spacing"}
 *
 *  set_radio applies immediately but is NOT persistent; save writes the
 *  running configuration to EEPROM. That is the same Apply/Save split
 *  the touchscreen uses, and for the same reason: an operator should be
 *  able to try a channel without committing to it.
 *
 *  Parsing is done with a small hand written scanner rather than cJSON.
 *  This runs in the monitor task, which has a 1 KB stack and is the
 *  watchdog feeder, so avoiding both heap allocation and recursive
 *  descent is worth the fifty lines.
 */

#ifndef INC_GW_COMMAND_H_
#define INC_GW_COMMAND_H_

#include "utils.h"

/* Longest command line accepted. Anything longer is discarded up to the
   next newline and answered with an error, rather than silently
   truncated into a different valid command. */
#define DEF_CMD_LINE_MAX        192U

/* Bytes buffered between service calls. The monitor runs every 200 ms;
   at 115200 that is up to ~2300 bytes in the worst case, but commands
   are typed by a human or sent one at a time by a tool, so this is
   generous. Overruns are counted, not hidden. */
#define DEF_CMD_RING_BYTES      512U

/* Arms interrupt driven reception. Call once, after the UART is up. */
void Fn_Command_Init (void);

/* Drains whatever arrived, dispatching every complete line. Call from
   the monitor task. */
void Fn_Command_Service (void);

/* Bytes lost because the ring was full when they arrived. */
uint32_t Fn_Command_Get_Overruns (void);

#endif /* INC_GW_COMMAND_H_ */
