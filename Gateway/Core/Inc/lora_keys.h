/*
 * lora_keys.h
 *
 *  The one shared secret in a TetraGate network.
 *
 *  ====================================================================
 *   THE KEY BELOW IS A PUBLISHED EXAMPLE. IT IS NOT SECRET.
 *
 *   It is printed in a public repository, so every copy of this file in
 *   the world holds the same value. A network running it is not
 *   authenticated against anybody who has seen this project - it only
 *   rejects accidental traffic and corrupted frames.
 *
 *   Generate your own before you deploy anything. One line:
 *
 *     python -c "import secrets; print(', '.join('0x%02XU' % b for b in secrets.token_bytes(16)))"
 *
 *   Paste the output between the braces, and keep the result out of
 *   version control. See the Security section of the root README.
 *  ====================================================================
 *
 *  DUPLICATED between the gateway and every node project, and it must be
 *  byte for byte identical in all of them. Change it here and the whole
 *  network has to be reflashed together, because every per-node key is
 *  derived from it:
 *
 *      K_node = AES-128-CMAC(K_master, "LGW1" || node_id_le16)
 *
 *  Deliberately on one line: a multi-line macro needs backslash
 *  continuations, and those are easy to corrupt silently when this file
 *  is edited by a script. A broken key macro is a network that goes
 *  quiet for no visible reason.
 *
 *  LIMITATIONS, stated plainly:
 *
 *  Even your own key is compiled into the binary, so anyone who can read
 *  the flash can read it. Set RDP level 1 on deployed parts, or this is
 *  only a lock against people who do not have your firmware.
 *
 *  Moving the key to a dedicated EEPROM slot written at provisioning -
 *  outside the A/B config slots, so a factory reset cannot orphan
 *  deployed nodes - is the next step, and is not implemented here.
 */

#ifndef INC_LORA_KEYS_H_
#define INC_LORA_KEYS_H_

#include <stdint.h>

/* EXAMPLE KEY - PUBLIC - REPLACE BEFORE DEPLOYMENT (see above) */
#define DEF_LORA_MASTER_KEY_INIT { 0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU }

#endif /* INC_LORA_KEYS_H_ */
