# 1. Architecture

## 1.1 What LoRa is, briefly

LoRa is a radio modulation designed to trade **data rate for range**. It
spreads a small amount of data over a wide bandwidth for a long time,
which lets a receiver dig the signal out from below the noise floor. A
LoRa link can work at −120 dBm, where ordinary FSK would hear nothing.

Three settings control that trade:

| Setting | Meaning | Effect |
|---|---|---|
| **Spreading factor (SF7–SF12)** | how many chips per symbol | higher = more range, slower, more airtime |
| **Bandwidth (BW)** | how wide the channel is | wider = faster, less sensitive |
| **Coding rate (CR 4/5–4/8)** | forward error correction | more redundancy = survives interference, slower |

This project uses **SF9 / BW 250 kHz / CR 4/8** on every link: a middle
setting that is robust indoors without spending too long on air.

Two radios can only hear each other if **all three match**, plus the
carrier frequency and the sync word. A mismatch in any one of them
produces silence, not an error — which is why
[08-Troubleshooting.md](08-Troubleshooting.md) starts there.

---

## 1.2 Hardware

### Gateway — STM32F429ZI (Nucleo-144)

| Part | Connection | Purpose |
|---|---|---|
| 4 × RFM95W | SPI1, SPI2, SPI3, SPI4 | one receiver per channel |
| ILI9341 TFT 320×240 | SPI5 + DMA | user interface |
| XPT2046 touch | SPI6 | touch input |
| AT24C256 EEPROM | I2C | configuration, touch calibration |
| USART3 | PD8 / PD9 → ST-Link VCP | JSON feed and commands |

USART3 is wired to the ST-Link's virtual COM port on a Nucleo board,
which is why one USB cable gives you both debugging and the data feed.

### Node — STM32G030F6P6 (7Semi Anchor board)

| Part | Connection |
|---|---|
| RFM95W | SPI1: PA1 SCK, PA2 MOSI, PA6 MISO, PA5 NSS, PA4 RST, PB2 DIO0 |
| Status LED | PA7 |
| SWD | PA13 / PA14 |

**32 KB flash, 8 KB RAM, 64 MHz.** No UART is brought out, so the node
has no serial log — diagnosis is by LED and by reading variables over
SWD. This constraint shapes a lot of the node's design.

---

## 1.3 The gateway's seven tasks

The gateway runs FreeRTOS. Work is split into tasks so that slow things
never block fast things.

| Task | Priority | Stack | Job |
|---|---|---|---|
| `TaskInit` | normal | — | loads config, starts the radios, then exits |
| `Task_LoRa_1..4` | normal | 1 KB each | one per radio: sleep until DIO0, read packet, file it |
| `Task_LVGL` | normal | 8 KB | draw the screen, read touch |
| `Task_Monitor` | low | 1 KB | watchdog, housekeeping, PC commands |

### Why one task per radio

Each RFM95W has its own DIO0 interrupt line that goes high when a packet
has been received. A dedicated task can block on *its* radio and wake
only for *its* packets. Four packets arriving at the same instant are
handled in parallel instead of queueing behind each other.

The alternative — one task polling four radios in a loop — would add
latency and risk missing a packet while servicing another.

### Why the monitor task matters

`Task_Monitor` is the only task that feeds the hardware watchdog
(IWDG), and it refuses to feed it if any other task has stopped
checking in:

```c
for (i = 0U; i < (uint8_t)en_Mon_Count; i++)
{
    if ((u32Now - u32LastSeen[i]) > u32DeadlineMs[i])
    {
        u32Mask |= (1UL << i);
    }
}
u32StallMask = u32Mask;
```

Each task calls `Fn_Monitor_Checkin()` in its loop. If a task hangs, its
deadline expires, the monitor stops feeding, and the hardware resets the
board. **This caught a real bug during development**: a GUI change
exhausted LVGL's memory pool, which asserts into an infinite loop, and
the watchdog turned a silent hang into a visible five-second reset loop.

Because the monitor is the watchdog feeder, anything that runs inside it
must be quick. That is why the PC command handler drops a reply rather
than wait for a busy serial port.

---

## 1.4 The journey of one packet

```
  node                                   gateway
  ────                                   ───────
  build 18-byte frame
  append 4-byte MIC
  LoRa_transmit()  ─────── air ────────▶ RFM95W decodes
                                         DIO0 goes high
                                         ↓
                                         Task_LoRa_n wakes
                                         ↓
                                     1.  read RegIrqFlags   ← BEFORE the payload
                                     2.  CRC bad?  count, drop
                                     3.  read bytes, RSSI, SNR
                                     4.  verify MIC         ← authenticity
                                     5.  node table: ID, sequence, duplicate?
                                     6.  whitelist check
                                     7.  store + packet ring
                                     8.  emit JSON on USART3
                                         ↓
                                     Task_LVGL redraws (≤400 ms)
                                         ↓
                                     PC console updates (≤1 s)
```

Step 1 is not an accident of ordering. `LoRa_receive()` clears the whole
interrupt register with `0xFF`, so the CRC error bit must be read first
or it is lost forever:

```c
/* RegIrqFlags has to be read BEFORE LoRa_receive(), because that function
 * clears the whole register with 0xFF and the CRC error bit would be lost. */
u8Irq = LoRa_read(p, RegIrqFlags);
```

Step 4 comes before step 5 deliberately: nothing should update the node
table, the statistics or the screen until the frame has been shown to be
genuine.

---

## 1.5 Where data lives

| Store | Survives reboot? | Why |
|---|---|---|
| Radio configuration | **Yes** — EEPROM | it is configuration |
| Whitelist | **Yes** — EEPROM | configuration, not observation |
| Touch calibration | **Yes** — EEPROM `0x3040` | you should calibrate once, ever |
| Node table | No | an *observation*; rebuilds from traffic in one interval |
| Packet ring (32) | No | recent history for the Packets screen |
| Radio statistics | No | counters since boot |

### The A/B EEPROM slots

```
0x0040   slot A   (4096 bytes)
0x1040   slot B   (4096 bytes)
0x3040   touch calibration
```

A save writes the **inactive** slot, verifies it, then switches which
slot is active. If power fails mid-write, the old slot is untouched and
still valid. This is what makes the Save button safe to press.

---

## 1.6 Concurrency rules

Two mutexes guard everything shared:

| Mutex | Guards |
|---|---|
| `mtx_configHandle` | `sLoRaGtwConfParam` — the live radio configuration |
| `mtx_nodeHandle` | the node table and whitelist |

Plus one semaphore, `sem_lora_uartHandle`, which serialises USART3 so a
status line and a packet line cannot interleave into one corrupt object.

Three rules the code follows everywhere:

1. **The GUI never touches a radio.** It writes the shared config and
   raises a reconfigure request; the radio's own task does the SPI work.
2. **Only `Task_LVGL` calls `lv_*` functions.** LVGL 8.3 is not thread
   safe, and this project keeps the GUI behind the gateway core rather
   than locking the whole library.
3. **Nothing that holds a mutex does slow I/O.** Configuration is copied
   out under the mutex, then used after releasing it.

---

## 1.7 Design decisions worth knowing

**Binary on air, JSON on the wire.** A 14-byte frame costs 111 ms of
airtime; the same data as JSON would cost about 440 ms. Once the data
reaches a USB cable, bandwidth is free and a self-describing format is
worth far more than the bytes. This split is also what LoRaWAN does.

**Apply and Save are separate.** Apply changes the running radio
immediately; Save writes EEPROM. An operator should be able to try a
channel without committing to it.

**Report before enforce.** MIC verification defaults to *counting*
unauthenticated frames rather than dropping them, because nodes are
reflashed one at a time and enforcing early would silence the network.

**Static buffers in small-stack tasks.** The radio and monitor tasks
have 1 KB stacks. Anything larger than a couple of hundred bytes is
`static`, protected by the mutex or semaphore that already serialises
the caller.

Next: [02-Air-Protocol.md](02-Air-Protocol.md)
