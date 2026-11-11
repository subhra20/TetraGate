# 8. Troubleshooting

Every entry here is a fault that actually occurred, or one the design
makes likely. Start from the symptom.

---

## 8.1 A node is not appearing at all

A LoRa mismatch produces **silence, not an error**. Check in this order:

| Check | Where |
|---|---|
| Frequency matches the gateway radio | `DEF_NODE_FREQ_HZ` vs Configuration page |
| SF, BW, CR all match | `node_config.h` vs Configuration page |
| Sync word matches | `DEF_NODE_SYNC_WORD` vs `DEF_RADIO_SYNC_WORD_DEFAULT` (both `0x12`) |
| The node is actually transmitting | LED should flash briefly every 5 s |
| `u32TxOk` is climbing | read `sNodeStats` over SWD |
| The right radio is enabled | Configuration page |

**Does the radio see *anything*?** On the Dashboard, look at that
radio's CRC count:

- **CRC errors rising** — something is transmitting and being received
  badly. Frequency is roughly right; modem settings or signal quality
  are wrong.
- **Everything zero, RSSI 0, "no signal"** — no RF energy at all. The
  node is off, out of range, or on a completely different frequency.

---

## 8.2 A radio shows zero packets while others work

This is what a dead channel looks like. Two common causes:

**The node lost power or drifted.** Check the node's LED.

**The node was reflashed with the wrong identity.** This happened during
development: a board was flashed with `node_config.h` still set to
another node's ID and frequency. The result was 900 MHz silent, and two
physical boards both transmitting as node 1004 on 880 MHz.

How to recognise it: on the Packets page, filter to the busy radio and
look at the sequence numbers and node uptime. Two interleaved
sequence streams with different uptimes means two boards:

```
node=1004 seq=123 mic=none len=14 rssi=-52 up=11154   ← board A, 3.1 h
node=1004 seq=204 mic=ok   len=18 rssi=-32 up=1052    ← board B, 17 min
node=1004 seq=124 mic=none len=14 rssi=-52 up=11159
node=1004 seq=205 mic=ok   len=18 rssi=-31 up=1057
```

Two independent sequence counters, two uptimes, two RSSI levels. Fix by
reflashing the board on the probe with its correct identity.

---

## 8.3 `mic_fail` is non-zero

A frame claimed a MIC and failed verification. These frames are
**always dropped**, under either policy.

| Cause | How to tell |
|---|---|
| `lora_keys.h` differs between gateway and node | diff the two files — they must be byte-identical |
| `lora_crypto.c` differs | same |
| Genuine forgery attempt | rare; the count would climb steadily with no node change |

After changing the master key, **every** device must be reflashed. A
partial rollout means the un-updated nodes all fail.

Check `crypto_ok` on the Diagnostics page first. If it reads **FAIL**,
the gateway's own AES/CMAC is broken and no verification result means
anything.

---

## 8.4 A node went "legacy" after being flashed with MIC

Confirm `DEF_NODE_MIC_ENABLE` is `1` in the build that was actually
flashed, and that the on-air length grew:

- data frame: 14 → **18** bytes
- status frame: 10 → **14** bytes

The Packets page shows the length. If it is still 14 for a data frame,
MIC is not compiled in.

---

## 8.5 Nothing on the PC console

| Symptom | Cause |
|---|---|
| "bridge unreachable" | the Python bridge is not running |
| "disconnected" in the sidebar | wrong COM port, or another program holds it |
| Connected but no packets | gateway not transmitting on USART3 — check the Log tab on the screen |

CubeIDE's serial terminal and the bridge cannot share the port. Close
one.

To find the port:

```bash
python -c "import serial.tools.list_ports as l; [print(p.device, p.description) for p in l.comports()]"
```

The gateway appears as **ST-Link Virtual COM Port**, because USART3 is
wired to it on a Nucleo board.

---

## 8.6 Partial lines in the feed

The Diagnostics page counts them, and shows them as `[partial]`.

A handful right after a gateway reset is **normal** — the reset cuts a
line in half mid-transmission. A continuously rising count means
something else: a baud mismatch, or a cable problem.

---

## 8.7 The gateway reboots in a loop

The watchdog is doing its job: some task stopped checking in.

If it started right after a GUI change, suspect **LVGL memory
exhaustion**. The symptom is distinctive: the boot line
`GUI: N screens up, LVGL pool …` never appears, because
`Fn_GUI_Init()` never returns. `LV_USE_ASSERT_MALLOC` turns a failed
allocation into an infinite loop, not an error message.

Fix: raise `LV_MEM_SIZE` in `Core/Inc/lv_conf.h`. Watch the boot line —
past about 80 % usage, raise it before adding widgets.

Otherwise, read `u32StallMask` over SWD or look at the Diagnostics page.
Each bit is one task: LoRa1–4, then LVGL.

---

## 8.8 Build fails on `lvgl.h: No such file or directory`

The include path has been lost from `.cproject` — most likely because
CubeMX regenerated code from the `.ioc`.

Add back, in **both** Debug and Release:

```
../Middlewares/Third_Party/lvgl
```

See [07-Build-and-Flash.md](07-Build-and-Flash.md) §7.2. Do **not** fix
it by editing `Debug/**/subdir.mk` — that file is regenerated and your
change will vanish at the next IDE build, which is exactly how this
fault was introduced.

---

## 8.9 Touch is inaccurate or crosshairs appear every boot

Calibration lives in EEPROM at `0x3040`. Crosshairs on every boot mean
the stored record is failing its validity check — a bad EEPROM write, or
the record was never written.

Recalibrate once and confirm the boot log says:

```
TOUCH cal: loaded from EEPROM
```

---

## 8.10 Duplicates climbing on one radio

`dup` counts a node and sequence number seen twice within 4 seconds.

- **A few** — normal when a node is strong enough to be heard on a
  second radio.
- **Roughly half of all traffic** — suspect two nodes sharing an ID
  (§8.2), or a node whose sequence number is not advancing.

A node that does not increment its sequence will have every report after
the first discarded. Confirm with `sNodeStats.u8LastSeq` over SWD.

---

## 8.11 Node IDs you have never heard of

Nodes appear in the list with impossible IDs, one packet each, nonsense
readings, and RSSI around -110 to -118 dBm. They are almost always on the
radio tuned closest to 868 MHz.

**These are not your nodes, and nothing is broken.** They are other
people's LoRa equipment. Two things give it away:

| Evidence | Meaning |
|---|---|
| Frame length 150-180 bytes | yours are 14 or 18 |
| SNR -15 to -18 dB | below the noise floor, so it IS real LoRa - random noise cannot demodulate at all |
| One packet each, hours apart | a distant device at the edge of range |
| Only on one radio | the frequency is the thing they have in common |

868.100 MHz is EU868 channel 0, the default first channel in essentially
every LoRaWAN stack, and sync word `0x12` is the default in nearly every
Arduino and RadioHead LoRa library. Anything nearby using both lands in
your receiver.

A foreign frame that happens to be 14 bytes gets decoded as a data frame,
which is where readings like -305 C and 183 % humidity come from.

The same traffic is what drives up the CRC error count on that radio: most
of it is too weak to decode, a little of it gets through.

**Three ways to deal with it**, cheapest first:

1. **Move that radio off 868.100 MHz.** Clears the phantom nodes and the
   CRC errors together, because they are one phenomenon. Remember to
   reflash the node on that channel to match.
2. **Enforce the MIC** once every node is signed. A foreign frame has no
   MIC and is dropped before it ever reaches the node table.
3. **Use the whitelist** (`en_Node_NotWhitelisted` in `node_table.c`) to
   pin the gateway to your own node IDs.

---

## 8.12 `mic_fail` counts one or two with no node change

Expected, and not an attack. A corrupted frame whose first byte happens to
have bit 7 set *claims* a MIC it cannot satisfy, so it is counted as a
failure and dropped - which is the correct outcome.

Suspect something real only when the count climbs steadily. Then read
section 8.3.

---

## 8.13 Quick reference: healthy system

| Reading | Expected |
|---|---|
| Radios | four "healthy" |
| Stall mask | `0x0` |
| Crypto self-test | PASS |
| `mic_fail` | 0 |
| Uplink dropped | 0 |
| Partial lines | 0, or a few after a reset |
| Duplicates | 0 to a few |
| `mic_ok + no_mic` | equals `good + duplicates` |
