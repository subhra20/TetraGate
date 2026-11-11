/*
 * node_radio.h
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  Brings the RFM95W up on the exact channel the gateway listens on and
 *  sends frames on it. Wraps the shared LoRa driver rather than changing
 *  it: that driver is hardware proven on both ends of this link and is
 *  extended additively, never edited.
 */

#ifndef INC_NODE_RADIO_H_
#define INC_NODE_RADIO_H_

#include "main.h"
#include <stdbool.h>

/* Brings up the radio on the configured channel. False means the
   RFM95W did not answer - wiring, power, or a dead module. */
bool Fn_Node_Radio_Init (SPI_HandleTypeDef *phSpi);

/* Sends one frame. False means the transmit-done flag never arrived
   inside the timeout. */
bool Fn_Node_Radio_Transmit (const uint8_t *pu8Data, uint8_t u8Len);

/* Reads the chip version register: cheap proof the SPI link is alive. */
bool Fn_Node_Radio_Healthy (void);

/* The carrier the radio is really on, read back from its registers
   rather than assumed. Should equal DEF_NODE_FREQ_HZ. */
uint32_t Fn_Node_Radio_Get_Frequency_Hz (void);

#endif /* INC_NODE_RADIO_H_ */
