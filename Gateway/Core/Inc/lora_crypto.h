/*
 * lora_crypto.h
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  Message authentication for the LoRa link: AES-128-CMAC truncated to
 *  4 bytes, with per-node keys derived from one gateway master key.
 *
 *  Without this, anything within range on the right channel can transmit
 *  a frame claiming to be any node, and both the gateway screens and the
 *  PC dashboard will present it as fact. The MIC makes a frame
 *  unforgeable without the key.
 *
 *  THIS FILE IS DUPLICATED in the node project, exactly as LoRa.c is.
 *  Both ends must compute the identical MIC, so if you change anything
 *  here - the derivation string, the truncation, the covered bytes -
 *  change it in both or the link goes silent.
 *
 *  Scheme
 *  ------
 *    K_node = AES-128-CMAC(K_master, "LGW1" || node_id_le16)
 *    MIC    = first 4 bytes of AES-128-CMAC(K_node, frame[0 .. len-5])
 *
 *  The MIC occupies the last 4 bytes of the frame and covers everything
 *  before it, so the type, node ID and sequence number are all
 *  authenticated - a forger cannot change the claimed node ID without
 *  invalidating the MIC.
 *
 *  Truncating to 4 bytes leaves a 1-in-4-billion chance per forgery
 *  attempt. At this link's data rate an attacker gets a few attempts per
 *  second, so a blind forgery would take decades.
 *
 *  What this does NOT give you
 *  ---------------------------
 *  Confidentiality: the payload is still plaintext on air. Anyone can
 *  READ your temperatures; they just cannot forge them.
 *
 *  Freshness: a captured frame replayed later still has a valid MIC. The
 *  gateway's 4 second duplicate window catches immediate replays, and
 *  the sequence number is covered by the MIC, but the sequence is one
 *  byte and wraps every 256 frames, so a replay after a wrap would be
 *  accepted. Closing that needs a per-node sequence window with
 *  tolerance for node reboots, which is a separate change.
 *
 *  Key secrecy: the master key is compiled into both binaries. Anyone
 *  who can read the flash can read the key, so set RDP level 1 on
 *  deployed parts. Moving the master key into its own EEPROM slot,
 *  outside the A/B config slots, is the provisioning step for later.
 */

#ifndef INC_LORA_CRYPTO_H_
#define INC_LORA_CRYPTO_H_

#include <stdint.h>
#include <stdbool.h>

#define LORA_MIC_BYTES          4U
#define LORA_AES_BLOCK          16U

/* Expanded key schedule. Encrypt direction only: CMAC never decrypts,
   and leaving out the inverse cipher halves the code size. */
typedef struct
{
    uint8_t u8RoundKey[176];
} StructAes128;

void Fn_AES128_Init    (StructAes128 *psCtx, const uint8_t au8Key[16]);
void Fn_AES128_Encrypt (const StructAes128 *psCtx, const uint8_t au8In[16],
                        uint8_t au8Out[16]);

/* RFC 4493 AES-CMAC. Handles a zero length message. */
void Fn_CMAC (const uint8_t au8Key[16], const uint8_t *pu8Msg, uint16_t u16Len,
              uint8_t au8Mac[16]);

/* K_node = CMAC(K_master, "LGW1" || node_id_le16) */
void Fn_Mic_Derive_Key (const uint8_t au8Master[16], uint16_t u16NodeId,
                        uint8_t au8NodeKey[16]);

/* MIC over pu8Frame[0 .. u16Len-1], written to au8Mic. The caller
   passes the length WITHOUT the MIC field. */
void Fn_Mic_Compute (const uint8_t au8NodeKey[16], const uint8_t *pu8Frame,
                     uint16_t u16Len, uint8_t au8Mic[LORA_MIC_BYTES]);

/* Checks the trailing 4 bytes of a complete received frame. u8Len is the
   full on-air length including the MIC. The node ID is read from the
   frame itself, so a forger changing it simply fails the check. */
bool Fn_Mic_Verify (const uint8_t au8Master[16], const uint8_t *pu8Frame,
                    uint8_t u8Len);

/* FIPS-197 and RFC 4493 vectors, run on the target. There is no host
   compiler in this toolchain, so this is how the implementation is
   proven on the part that will actually run it. */
bool Fn_Crypto_Selftest (void);

#endif /* INC_LORA_CRYPTO_H_ */
