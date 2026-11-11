/*
 * lora_crypto.c
 *
 *  Created on: Nov 11, 2026
 *      Author: subhr
 *
 *  See lora_crypto.h. DUPLICATED in the node project - keep in step.
 *
 *  Plain table-free AES-128, encrypt direction only. Neither part in
 *  this system has an AES accelerator: the STM32F429 has no CRYP (that
 *  is the F437 and F439), and the STM32G030 has no AES block either, so
 *  both ends run this. It costs about 1.4 KB of flash and a CMAC over a
 *  22 byte frame is two block encryptions - a few microseconds even on
 *  the 64 MHz node. Nothing here is on a hot path.
 *
 *  This is a straightforward reference implementation, not a hardened
 *  one: it makes no attempt at constant-time behaviour, because the
 *  threat here is a forged radio packet, not an attacker with a probe on
 *  the board measuring how long the MIC check took.
 */

#include "lora_crypto.h"
#include <string.h>

static const uint8_t au8Sbox[256] =
{
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/*------------------------------------------------------------ AES core --------------------------------------------------------------
 * State is 16 bytes in column major order: index = 4*column + row, the
 * same order the bytes arrive in.
 */
static uint8_t _Fn_XTime (uint8_t u8A)
{
    return (uint8_t)((uint8_t)(u8A << 1) ^ (((u8A & 0x80U) != 0U) ? 0x1BU : 0x00U));
}

static void _Fn_ShiftRows (uint8_t *pu8S)
{
    uint8_t u8T;

    /* row 1 left by 1 */
    u8T = pu8S[1];  pu8S[1] = pu8S[5];  pu8S[5] = pu8S[9];
    pu8S[9] = pu8S[13]; pu8S[13] = u8T;

    /* row 2 left by 2 */
    u8T = pu8S[2];  pu8S[2] = pu8S[10];  pu8S[10] = u8T;
    u8T = pu8S[6];  pu8S[6] = pu8S[14];  pu8S[14] = u8T;

    /* row 3 left by 3, which is right by 1 */
    u8T = pu8S[15]; pu8S[15] = pu8S[11]; pu8S[11] = pu8S[7];
    pu8S[7] = pu8S[3]; pu8S[3] = u8T;
}

static void _Fn_MixColumns (uint8_t *pu8S)
{
    uint8_t c;

    for (c = 0U; c < 16U; c += 4U)
    {
        uint8_t a0 = pu8S[c], a1 = pu8S[c+1], a2 = pu8S[c+2], a3 = pu8S[c+3];
        uint8_t t  = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);

        pu8S[c]   = (uint8_t)(a0 ^ t ^ _Fn_XTime((uint8_t)(a0 ^ a1)));
        pu8S[c+1] = (uint8_t)(a1 ^ t ^ _Fn_XTime((uint8_t)(a1 ^ a2)));
        pu8S[c+2] = (uint8_t)(a2 ^ t ^ _Fn_XTime((uint8_t)(a2 ^ a3)));
        pu8S[c+3] = (uint8_t)(a3 ^ t ^ _Fn_XTime((uint8_t)(a3 ^ a0)));
    }
}

void Fn_AES128_Init (StructAes128 *psCtx, const uint8_t au8Key[16])
{
    static const uint8_t au8Rcon[10] =
        { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36 };
    uint8_t i;

    (void)memcpy(psCtx->u8RoundKey, au8Key, 16);

    for (i = 1U; i <= 10U; i++)
    {
        const uint8_t *pu8Prev = &psCtx->u8RoundKey[(i - 1U) * 16U];
        uint8_t       *pu8Cur  = &psCtx->u8RoundKey[i * 16U];
        uint8_t t[4];
        uint8_t j;

        /* RotWord then SubWord of the previous last column, plus Rcon */
        t[0] = (uint8_t)(au8Sbox[pu8Prev[13]] ^ au8Rcon[i - 1U]);
        t[1] = au8Sbox[pu8Prev[14]];
        t[2] = au8Sbox[pu8Prev[15]];
        t[3] = au8Sbox[pu8Prev[12]];

        for (j = 0U; j < 4U; j++)
        {
            pu8Cur[j] = (uint8_t)(pu8Prev[j] ^ t[j]);
        }
        for (j = 4U; j < 16U; j++)
        {
            pu8Cur[j] = (uint8_t)(pu8Prev[j] ^ pu8Cur[j - 4U]);
        }
    }
}

void Fn_AES128_Encrypt (const StructAes128 *psCtx, const uint8_t au8In[16],
                        uint8_t au8Out[16])
{
    uint8_t s[16];
    uint8_t r, i;

    (void)memcpy(s, au8In, 16);

    for (i = 0U; i < 16U; i++)
    {
        s[i] ^= psCtx->u8RoundKey[i];
    }

    for (r = 1U; r < 10U; r++)
    {
        for (i = 0U; i < 16U; i++) { s[i] = au8Sbox[s[i]]; }
        _Fn_ShiftRows(s);
        _Fn_MixColumns(s);
        for (i = 0U; i < 16U; i++) { s[i] ^= psCtx->u8RoundKey[(r * 16U) + i]; }
    }

    for (i = 0U; i < 16U; i++) { s[i] = au8Sbox[s[i]]; }
    _Fn_ShiftRows(s);
    for (i = 0U; i < 16U; i++) { s[i] ^= psCtx->u8RoundKey[160U + i]; }

    (void)memcpy(au8Out, s, 16);
}

/*------------------------------------------------------------ CMAC ------------------------------------------------------------------*/
static void _Fn_Dbl (uint8_t *pu8B)
{
    uint8_t u8Carry = ((pu8B[0] & 0x80U) != 0U) ? 0x87U : 0x00U;
    uint8_t i;

    for (i = 0U; i < 15U; i++)
    {
        pu8B[i] = (uint8_t)((uint8_t)(pu8B[i] << 1) | (uint8_t)(pu8B[i + 1U] >> 7));
    }
    pu8B[15] = (uint8_t)((uint8_t)(pu8B[15] << 1) ^ u8Carry);
}

void Fn_CMAC (const uint8_t au8Key[16], const uint8_t *pu8Msg, uint16_t u16Len,
              uint8_t au8Mac[16])
{
    StructAes128 sCtx;
    uint8_t K1[16], K2[16], X[16], blk[16];
    uint16_t u16Blocks;
    uint16_t b;
    uint8_t  j;
    bool bComplete;

    Fn_AES128_Init(&sCtx, au8Key);

    (void)memset(K1, 0, 16);
    Fn_AES128_Encrypt(&sCtx, K1, K1);     /* L = AES(K, 0^128) */
    _Fn_Dbl(K1);                          /* K1 = dbl(L)       */
    (void)memcpy(K2, K1, 16);
    _Fn_Dbl(K2);                          /* K2 = dbl(K1)      */

    if (u16Len == 0U)
    {
        u16Blocks = 1U;
        bComplete = false;
    }
    else
    {
        u16Blocks = (uint16_t)((u16Len + 15U) / 16U);
        bComplete = ((u16Len % 16U) == 0U);
    }

    (void)memset(X, 0, 16);

    for (b = 0U; b + 1U < u16Blocks; b++)
    {
        for (j = 0U; j < 16U; j++)
        {
            X[j] ^= pu8Msg[(b * 16U) + j];
        }
        Fn_AES128_Encrypt(&sCtx, X, X);
    }

    if (bComplete)
    {
        for (j = 0U; j < 16U; j++)
        {
            blk[j] = (uint8_t)(pu8Msg[((u16Blocks - 1U) * 16U) + j] ^ K1[j]);
        }
    }
    else
    {
        uint16_t u16Rem = (uint16_t)(u16Len - ((u16Blocks - 1U) * 16U));

        (void)memset(blk, 0, 16);
        if (u16Rem > 0U)
        {
            (void)memcpy(blk, &pu8Msg[(u16Blocks - 1U) * 16U], u16Rem);
        }
        blk[u16Rem] = 0x80U;
        for (j = 0U; j < 16U; j++)
        {
            blk[j] ^= K2[j];
        }
    }

    for (j = 0U; j < 16U; j++)
    {
        X[j] ^= blk[j];
    }
    Fn_AES128_Encrypt(&sCtx, X, au8Mac);
}

/*------------------------------------------------------------ MIC -------------------------------------------------------------------*/
void Fn_Mic_Derive_Key (const uint8_t au8Master[16], uint16_t u16NodeId,
                        uint8_t au8NodeKey[16])
{
    uint8_t au8Info[6];

    au8Info[0] = (uint8_t)'L';
    au8Info[1] = (uint8_t)'G';
    au8Info[2] = (uint8_t)'W';
    au8Info[3] = (uint8_t)'1';
    au8Info[4] = (uint8_t)(u16NodeId & 0xFFU);
    au8Info[5] = (uint8_t)(u16NodeId >> 8);

    Fn_CMAC(au8Master, au8Info, (uint16_t)sizeof(au8Info), au8NodeKey);
}

void Fn_Mic_Compute (const uint8_t au8NodeKey[16], const uint8_t *pu8Frame,
                     uint16_t u16Len, uint8_t au8Mic[LORA_MIC_BYTES])
{
    uint8_t au8Mac[16];

    Fn_CMAC(au8NodeKey, pu8Frame, u16Len, au8Mac);

    /* First 4 bytes, the same convention LoRaWAN uses. Both ends must
       agree; this is the line to look at if MICs ever mismatch. */
    (void)memcpy(au8Mic, au8Mac, LORA_MIC_BYTES);
}

bool Fn_Mic_Verify (const uint8_t au8Master[16], const uint8_t *pu8Frame,
                    uint8_t u8Len)
{
    uint8_t au8NodeKey[16];
    uint8_t au8Want[LORA_MIC_BYTES];
    uint16_t u16Body;
    uint16_t u16NodeId;
    uint8_t  u8Diff = 0U;
    uint8_t  i;

    if ((pu8Frame == NULL) || (u8Len <= LORA_MIC_BYTES))
    {
        return false;
    }

    u16Body   = (uint16_t)(u8Len - LORA_MIC_BYTES);
    u16NodeId = (uint16_t)((uint16_t)pu8Frame[1] | ((uint16_t)pu8Frame[2] << 8));

    Fn_Mic_Derive_Key(au8Master, u16NodeId, au8NodeKey);
    Fn_Mic_Compute(au8NodeKey, pu8Frame, u16Body, au8Want);

    /* Compared without an early exit. Not strictly needed against a
       radio attacker, but it costs nothing to not leak the position of
       the first wrong byte. */
    for (i = 0U; i < LORA_MIC_BYTES; i++)
    {
        u8Diff |= (uint8_t)(au8Want[i] ^ pu8Frame[u16Body + i]);
    }

    return (u8Diff == 0U);
}

/*------------------------------------------------------------ Self test -------------------------------------------------------------
 * FIPS-197 C.1 for the block cipher, RFC 4493 for CMAC. The two CMAC
 * cases are deliberate: an empty message exercises the padded path with
 * K2, and a single full block exercises the complete path with K1.
 * Those two branches are where a CMAC implementation usually goes wrong.
 */
bool Fn_Crypto_Selftest (void)
{
    static const uint8_t au8FipsKey[16] =
        { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
          0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
    static const uint8_t au8FipsIn[16] =
        { 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
          0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    static const uint8_t au8FipsOut[16] =
        { 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
          0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };

    static const uint8_t au8CmacKey[16] =
        { 0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
          0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
    static const uint8_t au8CmacEmpty[16] =
        { 0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,
          0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46 };
    static const uint8_t au8CmacMsg16[16] =
        { 0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
          0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a };
    static const uint8_t au8CmacTag16[16] =
        { 0x07,0x0a,0x16,0xb4,0x6b,0x4d,0x41,0x44,
          0xf7,0x9b,0xdd,0x9d,0xd0,0x4a,0x28,0x7c };

    StructAes128 sCtx;
    uint8_t au8Out[16];

    Fn_AES128_Init(&sCtx, au8FipsKey);
    Fn_AES128_Encrypt(&sCtx, au8FipsIn, au8Out);
    if (memcmp(au8Out, au8FipsOut, 16) != 0)
    {
        return false;
    }

    Fn_CMAC(au8CmacKey, NULL, 0U, au8Out);
    if (memcmp(au8Out, au8CmacEmpty, 16) != 0)
    {
        return false;
    }

    Fn_CMAC(au8CmacKey, au8CmacMsg16, 16U, au8Out);
    if (memcmp(au8Out, au8CmacTag16, 16) != 0)
    {
        return false;
    }

    return true;
}
