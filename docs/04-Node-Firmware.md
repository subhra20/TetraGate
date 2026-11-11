# 4. Node Firmware

STM32G030F6P6: **32 KB flash, 8 KB RAM, 64 MHz, no FPU, no AES block**,
and no UART brought out to a connector. Bare metal — no RTOS.

Current build: **17.4 KB flash (53 %)**, **1.8 KB RAM (22 %)**.

Every constraint above shapes a design decision somewhere below.

---

## 4.1 Module map

| File | Responsibility |
|---|---|
| `main.c` | init, the report loop, LED, watchdog |
| `node_config.h` | identity, channel, timing — **everything you would change** |
| `node_packet.c` | the simulated readings and the frame builder |
| `node_radio.c` | radio bring-up, exact tuning, transmit |
| `lora_crypto.c` | AES-128, CMAC, MIC — duplicated from the gateway |
| `lora_keys.h` | the master key — duplicated from the gateway |
| `LoRa.c` | the shared SX1276 driver |

---

## 4.2 `node_config.h` — the one file to edit

```c
#define DEF_NODE_ID                 1002U
#define DEF_NODE_FREQ_HZ            900000000UL
#define DEF_NODE_SF                 SF_9
#define DEF_NODE_BW                 BW_250KHz
#define DEF_NODE_CR                 CR_4_8
#define DEF_NODE_POWER              POWER_17db
#define DEF_NODE_TX_PERIOD_MS       5000U
#define DEF_NODE_STATUS_EVERY       10U
#define DEF_NODE_MIC_ENABLE         1
```

**Each node needs its own build.** You cannot flash one `.elf` to all
four — the ID and the frequency are compile-time constants.

> **Changing the ID matters as much as the frequency.** The gateway keys
> its node table on the ID, so two nodes sharing one collapse into a
> single row, and the 4-second duplicate filter silently discards
> frames. This happened during development: a board was reflashed with
> the wrong identity, producing two node 1004s on 880 MHz and a dead
> 900 MHz channel. The symptom in the console was "3 nodes" and a radio
> with zero packets — see [08-Troubleshooting.md](08-Troubleshooting.md).

The settings must match the gateway radio exactly: frequency, spreading
factor, bandwidth, coding rate **and sync word**. A mismatch produces
silence, not an error.

---

## 4.3 `node_radio.c` — bring-up

```c
sLoRa.frequency             = (int)(DEF_NODE_FREQ_HZ / 1000000UL);
sLoRa.spredingFactor        = DEF_NODE_SF;
...
if (LoRa_init(&sLoRa) != LORA_OK)
{
    return false;
}

LoRa_write(&sLoRa, RegSyncWord, DEF_NODE_SYNC_WORD);
_Fn_Set_Frequency_Hz(DEF_NODE_FREQ_HZ);
LoRa_gotoMode(&sLoRa, STNBY_MODE);
```

Whole MHz goes in through the driver, then the exact carrier is written
directly — the same two-step the gateway uses, with the same arithmetic.

The sync word is written explicitly even though `0x12` is the chip's
reset value and the node would match anyway. If the gateway ever moves
to a private sync word, this is the one line that has to follow, and a
silent mismatch is invisible from both ends.

The radio idles in **standby, not receive**. This node only transmits,
and `LoRa_transmit()` returns the radio to whatever mode it found.

### Reading back, not assuming

```c
u32FreqHzActual = Fn_Node_Radio_Get_Frequency_Hz();
```

A carrier write that silently failed shows up here as a mismatch against
`DEF_NODE_FREQ_HZ`, rather than as a gateway that simply never hears
anything.

---

## 4.4 `node_packet.c` — simulated readings

No sensor is fitted, so the values are generated. A fixed string would
test the link but none of the display, so the readings are a bounded
random walk:

```c
static int32_t _Fn_Walk (int32_t i32Val, int32_t i32Step, int32_t i32Min, int32_t i32Max)
{
    i32Val += (int32_t)(_Fn_Rand() % (uint32_t)((2 * i32Step) + 1)) - i32Step;

    if (i32Val < i32Min) { i32Val = i32Min; }
    if (i32Val > i32Max) { i32Val = i32Max; }

    return i32Val;
}
```

- **Integer only.** This part has no FPU and 32 KB of flash; pulling in
  software floating point for cosmetic noise would be a poor trade.
- **Temperature** drifts ±0.25 °C a step within 18–42 °C.
- **Battery only ever falls**, a millivolt at a time, so the trend means
  something.

### Seeded from the chip's unique ID

```c
u32Lcg = HAL_GetUIDw0() ^ ((uint32_t)DEF_NODE_ID << 16);
```

Two nodes flashed with the same firmware would otherwise report
identical values. With this, each board's walk diverges — visible in the
console as four clearly separated temperature traces.

### Building the frame

```c
pu8Buf[0] = (uint8_t)((bStatusFrame ? DEF_NODE_TYPE_STATUS : DEF_NODE_TYPE_DATA)
#if (DEF_NODE_MIC_ENABLE == 1)
                      | DEF_NODE_TYPE_MIC_FLAG
#endif
                     );
_Fn_Put_U16(&pu8Buf[1], (uint16_t)DEF_NODE_ID);
pu8Buf[3] = u8Seq;
...
#if (DEF_NODE_MIC_ENABLE == 1)
    Fn_Mic_Compute(au8NodeKey, pu8Buf, (uint16_t)u8Body, &pu8Buf[u8Body]);
#endif
```

The node key is derived **once** at start-up, not per frame:

```c
static const uint8_t au8Master[16] = DEF_LORA_MASTER_KEY_INIT;

Fn_Mic_Derive_Key(au8Master, (uint16_t)DEF_NODE_ID, au8NodeKey);
```

The node ID never changes, so deriving per frame would repeat the same
two AES blocks forever.

---

## 4.5 `main.c` — the loop

```c
_Fn_Watchdog_Refresh();

bStatusFrame = ((DEF_NODE_STATUS_EVERY > 0U) &&
                (((u32Reports + 1U) % DEF_NODE_STATUS_EVERY) == 0U));

u8Len = Fn_Node_Packet_Build(u8Frame, (uint8_t)sizeof(u8Frame), u8Seq, bStatusFrame);

if (u8Len >= DEF_NODE_PKT_MIN_LEN)
{
    if (Fn_Node_Radio_Transmit(u8Frame, u8Len))
    {
        sNodeStats.u32TxOk++;
        _Fn_Led_Pulse(25U);
    }
    else
    {
        sNodeStats.u32TxFail++;
        _Fn_Led_Pulse(400U);
    }
    u8Seq++;
    u32Reports++;
}
```

The sequence advances on **every** frame including failed ones — see
[02-Air-Protocol.md](02-Air-Protocol.md) §2.3 for why that matters.

### Diagnosing a board with no serial port

This is the design consequence of the hardware. Two channels:

**The LED (PA7):**

| Pattern | Meaning |
|---|---|
| one brief flash per report | normal |
| one long 400 ms flash | that transmission failed |
| fast double blink, repeating | radio not answering — hunting for it |
| five fast blinks at boot | crypto self-test **failed** |
| three slow blinks at boot | radio found, carrier set, starting |

**`sNodeStats` over SWD** — deliberately global and `volatile` so a
debugger can watch it live:

```c
volatile StructNodeStats sNodeStats;
```

| Field | Meaning |
|---|---|
| `u32TxOk` / `u32TxFail` | transmissions attempted |
| `u32FreqHzActual` | carrier read back from the radio |
| `u16NodeId` | confirms which identity is flashed |
| `u8LastSeq`, `u8LastLen` | last frame sent |
| `u8RadioOk`, `u8CryptoOk` | health flags |

Read it with:

```bash
STM32_Programmer_CLI -c port=SWD sn=<node probe> mode=HOTPLUG -r32 0x2000008C 0x18
```

(The address comes from `arm-none-eabi-nm` — see
[07-Build-and-Flash.md](07-Build-and-Flash.md).)

### The watchdog, without the HAL

```c
IWDG->KR = 0x0000CCCCU;     /* start the counter, also starts LSI */
IWDG->KR = 0x00005555U;     /* unlock PR and RLR                 */
IWDG->PR = 6U;              /* /256                              */
IWDG->RLR = 999U;
```

This project has `HAL_IWDG_MODULE_ENABLED` commented out and does not
carry `stm32g0xx_hal_iwdg.c`. Using the HAL would mean copying a driver
into the tree and adding it to the build — more moving parts than a
watchdog is worth when the peripheral is four registers.

LSI is about 32 kHz, so `/256` with reload 999 gives
`(999 + 1) × 256 / 32000 = 8.0 s`. The loop comes round every 5 s plus
the transmit, leaving margin without letting a wedged node stay quiet
for long.

```c
#ifdef __HAL_DBGMCU_FREEZE_IWDG
    __HAL_DBGMCU_FREEZE_IWDG();
#endif
```

Frozen while the core is halted, so single stepping does not reset the
part.

---

## 4.6 Provisioning a new node

1. Edit `node_config.h`: set `DEF_NODE_ID` and `DEF_NODE_FREQ_HZ`.
2. Build.
3. **Confirm which physical board is on the probe** before flashing. If
   several boards are powered, the console's Nodes page will tell you
   which ID is currently strongest — that is usually the one on your
   bench.
4. Flash, selecting the probe by serial number.
5. Watch the console: the node should appear within a few seconds, and
   its Auth column should read **signed**.

Next: [05-PC-Console.md](05-PC-Console.md)
