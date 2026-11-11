# 2. The Air Protocol

Everything in this document describes bytes that actually travel over
the radio. **There is no JSON on air.** JSON appears only on the USB
cable between the gateway and the PC, and inside the gateway's EEPROM.

---

## 2.1 Channel plan

| Radio | Frequency | Node |
|---|---|---|
| R1 | 868.100 MHz | 1001 |
| R2 | 900.000 MHz | 1002 |
| R3 | 915.000 MHz | 1003 |
| R4 | 880.000 MHz | 1004 |

Shared by every link:

```
Spreading factor   SF9
Bandwidth          250 kHz
Coding rate        4/8
TX power           17 dBm
Preamble           10 symbols
Sync word          0x12
CRC                enabled
Header             explicit
```

The gateway enforces two rules on any configuration, in
`Fn_LoRa_Config_Validate`:

```c
if ((c->u32FreqHz < DEF_LoRa_FREQ_MIN_HZ) || (c->u32FreqHz > DEF_LoRa_FREQ_MAX_HZ))
{
    return false;
}
...
if (u32Diff < DEF_LoRa_MIN_SPACING_HZ)
{
    return false;
}
```

- every frequency must be **868–915 MHz**
- any two *enabled* radios must be at least **2 MHz** apart

Two MHz is generous for a 250 kHz channel. It is set that way because
four receivers sit in one enclosure, and a strong signal on one channel
desensitises its neighbours.

> **Regulatory note.** These four channels span two regions: 868.1 MHz
> is the EU ETSI band, 915 MHz sits in the US 902–928 ISM band, and
> India's usual LoRa allocation is 865–867 MHz. One deployment spanning
> EU and US channels will not be legal in a single jurisdiction. Decide
> the band plan deliberately before deploying.

### Why the driver alone cannot do this

The shared `LoRa.c` sets frequency in whole MHz:

```c
void LoRa_setFrequency(LoRa* _LoRa, int freq){
	uint32_t F;
	F = (freq * 524288)>>5;
```

868.100 MHz is not expressible that way. Both the gateway and the node
therefore write the carrier registers directly afterwards, using the
same arithmetic on both ends:

```c
u32Frf = (uint32_t)(((uint64_t)u32Hz << 19) / 32000000ULL);

LoRa_write(p, RegFrMsb, (uint8_t)(u32Frf >> 16));
LoRa_write(p, RegFrMid, (uint8_t)(u32Frf >> 8));
LoRa_write(p, RegFrLsb, (uint8_t)(u32Frf));
```

`FRF = f / FSTEP` where `FSTEP = 32 MHz / 2^19 = 61.035 Hz`. The 64-bit
intermediate is required: `868e6 << 19` is about 4.6 × 10^14 and
overflows 32 bits.

The register is a whole number of 61.035 Hz steps, so 868.100 MHz
actually lands on **868,099,975 Hz** — 25 Hz low. Both ends compute the
identical value from the identical formula, so they land on exactly the
same carrier. The gateway reads the registers back and reports the real
figure rather than the requested one.

---

## 2.2 Frame format

```
 byte   0        1       2       3       4 .. n-5        n-4 .. n-1
      ┌──────┬───────────────┬────────┬──────────────┬──────────────┐
      │ type │   node ID     │  seq   │   payload    │     MIC      │
      │      │ little endian │        │              │   4 bytes    │
      └──────┴───────────────┴────────┴──────────────┴──────────────┘
```

| Field | Size | Notes |
|---|---|---|
| type | 1 | `0x01` data, `0x02` status; top bit `0x80` set = MIC present |
| node ID | 2 | little endian, unique per node |
| sequence | 1 | increments every frame, wraps at 255 |
| payload | n | depends on type |
| MIC | 4 | present only if the type's top bit is set |

### Data frame (type `0x01`, 18 bytes with MIC)

| Offset | Field | Encoding |
|---|---|---|
| 4..5 | temperature | int16 LE, centi-°C (2157 = 21.57 °C) |
| 6..7 | humidity | uint16 LE, centi-% |
| 8..9 | battery | uint16 LE, millivolts |
| 10..13 | node uptime | uint32 LE, seconds |

### Status frame (type `0x02`, 14 bytes with MIC)

| Offset | Field |
|---|---|
| 4..5 | battery, uint16 LE mV |
| 6..9 | node uptime, uint32 LE s |

Every tenth report is a status frame. Two frame sizes are deliberate:
one fixed size would never exercise the gateway's length handling or
the console's payload display.

### A real frame, decoded

```
81 EA 03 17 8C 0A 64 19 3C 0F 77 00 00 00  A3 1F 4E 2B

[0]       81            type 0x01 (data) with MIC flag 0x80
[1..2]    EA 03         node 0x03EA = 1002
[3]       17            sequence 23
[4..5]    8C 0A         0x0A8C = 2700  → 27.00 °C
[6..7]    64 19         0x1964 = 6500  → 65.00 %
[8..9]    3C 0F         0x0F3C = 3900  → 3.900 V
[10..13]  77 00 00 00   119 s uptime
[14..17]  A3 1F 4E 2B   MIC
```

### Why the type byte carries the MIC flag

Length alone cannot tell you whether a frame is authenticated:

| | without MIC | with MIC |
|---|---|---|
| data frame | 14 | 18 |
| status frame | 10 | 14 |

**14 bytes is both an unauthenticated data frame and an authenticated
status frame.** The flag removes the ambiguity:

```c
pu8Buf[0] = (uint8_t)((bStatusFrame ? DEF_NODE_TYPE_STATUS : DEF_NODE_TYPE_DATA)
#if (DEF_NODE_MIC_ENABLE == 1)
                      | DEF_NODE_TYPE_MIC_FLAG
#endif
                     );
```

Decoders must mask it off before comparing the type:

```c
u8Type = (uint8_t)(pu8Data[UP_OFF_TYPE] & 0x7FU);
```

---

## 2.3 The sequence number

One byte, incremented on every transmission including failed ones.

It matters more than it looks. The gateway treats **the same node ID and
the same sequence inside 4 seconds** as one transmission heard twice —
which is what happens when two radios both pick up a strong node:

```c
#define NODE_DUPLICATE_WINDOW_MS    4000U
```

A node that sent a constant sequence number would have every report
after the first silently counted as a duplicate and dropped. The very
first version of the node firmware sent a fixed string every 500 ms and
would have failed exactly this way.

---

## 2.4 Message authentication (MIC)

### The problem

Without a MIC, anyone within range on the right channel can transmit
`01 EA 03 17 ...` and both the gateway screen and the PC console will
present it as a genuine reading from node 1002.

### The scheme

```
K_node = AES-128-CMAC(K_master, "LGW1" || node_id_le16)
MIC    = first 4 bytes of AES-128-CMAC(K_node, frame_without_mic)
```

The gateway stores **one** master key. Every per-node key is derived on
demand, so adding a node needs no gateway-side key entry and the node
table needs no key column:

```c
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
```

The MIC covers the **whole frame before it** — type, node ID and
sequence included. A forger cannot change the claimed node ID without
invalidating the MIC, because the ID is an input to both the key
derivation *and* the MAC.

Verification reads the ID out of the frame itself, so a wrong ID simply
derives the wrong key and fails:

```c
u16NodeId = (uint16_t)((uint16_t)pu8Frame[1] | ((uint16_t)pu8Frame[2] << 8));

Fn_Mic_Derive_Key(au8Master, u16NodeId, au8NodeKey);
Fn_Mic_Compute(au8NodeKey, pu8Frame, u16Body, au8Want);

for (i = 0U; i < LORA_MIC_BYTES; i++)
{
    u8Diff |= (uint8_t)(au8Want[i] ^ pu8Frame[u16Body + i]);
}

return (u8Diff == 0U);
```

The comparison accumulates differences instead of returning early. That
is not strictly necessary against a radio attacker, but it costs nothing
to avoid leaking which byte was wrong.

### Why 4 bytes

A full CMAC is 16 bytes. Truncating to 4 leaves a 1-in-4-billion chance
per forgery attempt. At this link's data rate an attacker gets a few
attempts a second, so a blind forgery would take decades — while saving
12 bytes of airtime on every single frame, forever.

### Why software AES

Neither part has an AES accelerator. The **STM32F429 has no CRYP block**
(that is the F437 and F439), and the STM32G030 has none either. Both
ends run the encrypt-only AES in `lora_crypto.c`: about 1.4 KB of code,
and a CMAC over an 18-byte frame is two block encryptions — microseconds
even on the 64 MHz node.

### Report before enforce

```c
#define DEF_MIC_POLICY_REQUIRE          0
```

| Policy | Frame with no MIC | Frame with a **wrong** MIC |
|---|---|---|
| 0 — report | accepted, counted in `no_mic` | **dropped** |
| 1 — enforce | dropped | **dropped** |

A wrong MIC is *always* dropped. That is not a legacy node, it is a
forgery or a key mismatch, and believing either would defeat the point.

The policy can be switched from the PC console without reflashing, and
is deliberately **not persisted** — a reboot returns to the compiled
default, so a bad enforcement decision cannot lock the network out
permanently.

### What the MIC does not give you

| | Provided? |
|---|---|
| Authenticity — this really came from node 1002 | **yes** |
| Integrity — nobody altered it | **yes** |
| Confidentiality — nobody can read it | **no**, payload is plaintext |
| Freshness — this is not a recorded replay | **partly** |

Replay: the 4-second duplicate window catches immediate replays, and the
sequence number is covered by the MIC. But the sequence is one byte and
wraps every 256 frames — about 21 minutes at 5 s — so a frame replayed
after a wrap still verifies. Closing that needs a per-node sequence
window tolerant of node reboots.

Key secrecy: the master key is compiled into both binaries. Anyone who
can read the flash can read it. Set **RDP level 1** on deployed parts.

---

## 2.5 Airtime and duty cycle

At SF9 / BW 250 kHz / CR 4/8, explicit header, CRC on, 10-symbol
preamble, `Tsym = 2.048 ms`:

| Frame | Payload symbols | Airtime |
|---|---|---|
| 18-byte data | 48 | ~127 ms |
| 14-byte status | 40 | ~111 ms |

At one report per 5 s that is roughly **2.5 % duty cycle**, which is
**over the 1 %** that ETSI allows in the EU 868.0–868.6 sub-band.

5 s is a deliberate bench-test rate: it makes the screens move while the
link is being brought up. For deployment, about 13 s is the floor for
1 % with an 18-byte frame; 15 s gives margin. The constant is one line:

```c
#define DEF_NODE_TX_PERIOD_MS       5000U
```

Next: [03-Gateway-Firmware.md](03-Gateway-Firmware.md)
