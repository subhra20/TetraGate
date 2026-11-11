/*
 * node_config.h
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  Everything about this node a commissioning engineer might change:
 *  its identity, the channel it speaks on, and how often it reports.
 *
 *  The radio settings below are NOT free choices. They have to match
 *  one of the gateway's four receivers exactly - frequency, spreading
 *  factor, bandwidth, coding rate AND sync word - or the gateway never
 *  hears this node and nothing shows up on its screens. They currently
 *  mirror the gateway's radio 1.
 *
 *  Gateway reference: LoRa_Gateway_TFT_EEPROM, radio 1 is configured
 *  868.100 MHz / SF9 / BW 250 kHz / CR 4/8 / 17 dBm, and its
 *  radio_manager writes sync word 0x12.
 */

#ifndef INC_NODE_CONFIG_H_
#define INC_NODE_CONFIG_H_

/*------------------------------------------------------------ Identity --------------------------------------------------------------
 * 16 bit, little endian on air. The gateway keys its node table on
 * this, so two nodes must never share one. It appears on the gateway's
 * Nodes screen exactly as written here.
 */
#define DEF_NODE_ID                 1002U			//1003U			//1004U				//1001U

/*------------------------------------------------------------ Channel ---------------------------------------------------------------
 * Must equal the gateway radio this node reports to.
 *
 * The shared LoRa driver only takes whole MHz, which cannot express
 * 868.100. node_radio tunes the carrier registers directly afterwards,
 * the same way the gateway's radio_manager does, so this is exact.
 */
#define DEF_NODE_FREQ_HZ            900000000UL		//915000000UL		//880000000UL		//868100000UL
#define DEF_NODE_FREQ_MIN_HZ        868000000UL
#define DEF_NODE_FREQ_MAX_HZ        915000000UL

#define DEF_NODE_SF                 SF_9
#define DEF_NODE_BW                 BW_250KHz
#define DEF_NODE_CR                 CR_4_8
#define DEF_NODE_POWER              POWER_17db
#define DEF_NODE_PREAMBLE           10U
#define DEF_NODE_OCP_MA             130U

/* 0x12 is the SX127x reset value, so the node would match the gateway
 * even without writing it. It is written explicitly anyway: if the
 * gateway ever moves to a private sync word, this is the one line that
 * has to follow, and a silent mismatch is invisible from either end. */
#define DEF_NODE_SYNC_WORD          0x12U

/*------------------------------------------------------------ Reporting -------------------------------------------------------------
 * Airtime, from the SX1276 formula at SF9 / BW250 / CR 4/8, explicit
 * header, CRC on, 10 symbol preamble (Tsym = 2.048 ms). These are the
 * figures WITH the MIC, which adds four bytes to every frame:
 *      18 byte data frame    48 payload symbols   about 127 ms
 *      14 byte status frame  40 payload symbols   about 111 ms
 * Without the MIC they are 14 and 10 bytes, about 111 and  95 ms.
 *
 * At one report every 5 s the data frame is roughly 2.5% duty cycle,
 * which is OVER the 1% that ETSI allows in the EU 868.0 - 868.6
 * sub-band. 5 s is kept deliberately as a BENCH TEST rate: it makes the
 * gateway screens move while the link is being brought up.
 *
 * Before this node is deployed, decide the reporting rate against the
 * band rules that actually apply where it runs. 1 s is the floor for
 * 1% with the signed frame; 20 s gives about 0.64% with margin.
 *
 * The gateway treats the same node and sequence number seen twice
 * within 4 s as one transmission heard on two radios, so the sequence
 * number MUST advance every frame or every report after the first is
 * discarded as a duplicate.
 */
#define DEF_NODE_TX_PERIOD_MS       5000U
#define DEF_NODE_TX_TIMEOUT_MS      500U

/* Every Nth report is a short status frame instead of a data frame.
 * Two frame sizes keep the gateway's payload bar and type handling
 * honest; one fixed size would test neither. */
#define DEF_NODE_STATUS_EVERY       10U

/*------------------------------------------------------------ Frame types -----------------------------------------------------------
 * From the gateway architecture document, section 10. Only the two
 * uplink types are produced here; 0x10 and 0x20 are downlink and the
 * gateway does not transmit yet.
 */
#define DEF_NODE_TYPE_DATA          0x01U
#define DEF_NODE_TYPE_STATUS        0x02U

/*------------------------------------------------------------ Authentication -------------------------------------------------------
 * Appends a 4 byte AES-128-CMAC to every frame so the gateway can tell
 * a real node from anything else transmitting on the channel. See
 * lora_crypto.h for the scheme and for what it does not protect.
 *
 * The top bit of the type byte marks a frame as carrying a MIC, so the
 * gateway can tell an authenticated frame from a legacy one directly
 * rather than inferring it from the length - 14 bytes is an
 * unauthenticated data frame AND an authenticated status frame, so
 * length alone is ambiguous.
 */
#define DEF_NODE_MIC_ENABLE         1
#define DEF_NODE_TYPE_MIC_FLAG      0x80U

/* The gateway discards anything shorter than this: too small to carry
 * an identity. */
#define DEF_NODE_PKT_MIN_LEN        4U
#define DEF_NODE_PKT_MAX_LEN        32U

/*------------------------------------------------------------ Watchdog --------------------------------------------------------------
 * A node that wedges goes quiet, and a quiet node is indistinguishable
 * from one out of range. Resetting is strictly better than hanging.
 * Frozen while halted on the debugger so single stepping still works.
 */
#define DEF_NODE_WATCHDOG_ENABLE    1

#endif /* INC_NODE_CONFIG_H_ */
