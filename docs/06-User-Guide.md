# 6. User Guide

How to operate the system day to day. No programming required.

---

## 6.1 Starting up

1. Power the gateway. The screen shows the Home dashboard after a few
   seconds — you should **not** see calibration crosshairs; the touch
   calibration is stored in EEPROM and loads automatically.
2. Power the nodes. Each blinks three times slowly, then once briefly
   every five seconds.
3. Plug the gateway's ST-Link USB cable into the PC.
4. Start the console:

```bash
cd PC_Tool
python lora_bridge.py
```

Or double-click `run_dashboard.bat`. To force a port:

```bash
python lora_bridge.py --port COM4
```

The browser opens at `http://127.0.0.1:8787/`. Within about ten seconds
you should see four radios healthy and four nodes listed.

**Stop it with Ctrl+C.** While it runs it holds the serial port, so
CubeIDE's terminal cannot open the same port at the same time.

---

## 6.2 The gateway touchscreen

Six tabs along the top.

| Tab | Shows |
|---|---|
| **Home** | one card per radio: frequency, modem settings, status dot, packet count |
| **Radio** | edit one radio's settings |
| **Signal** | RSSI and payload size bars per channel |
| **Log** | the last packets, newest first, colour-coded by channel |
| **Nodes** | one row per node |
| **Sys** | uptime, memory, radio health, watchdog |

### Reading the Log tab

```
R1   0s  14B  -97  +9 01E9
```

| Column | Meaning |
|---|---|
| `R1` | which radio heard it |
| `0s` | seconds since it arrived |
| `14B` | full on-air length |
| `-97` | RSSI in dBm |
| `+9` | SNR in dB |
| `01E9` | first two payload bytes: frame type, and the low byte of the node ID |

That last column is the quick one: `01` = data, `02` = status; and
1001→`E9`, 1002→`EA`, 1003→`EB`, 1004→`EC`. With the MIC flag set the
type byte reads `81` and `82` instead.

### Reading the Nodes tab

```
R1    1001    3s   24p  -97
```

Radio last heard on, node ID, seconds since last packet, total packets,
RSSI. A trailing `!` means the node has gone silent.

**Log is per packet; Nodes is per node.** Log answers "what just
arrived", Nodes answers "who is alive".

### Editing a radio from the screen

Radio tab → pick R1–R4 → adjust with the `−`/`+` steppers. The
frequency buttons repeat on long press. Then:

- **Apply** — takes effect now, lost on reboot
- **Save** — writes EEPROM, survives reboot

If two enabled radios end up closer than 2 MHz, Apply refuses and says
so. Nothing changes.

---

## 6.3 The PC console

### Dashboard

Top row: packets, rate, nodes, how many are authenticated, MIC failures,
gateway free heap.

Radio cards: a signal gauge, RSSI/SNR, accepted count, and
CRC/duplicate/blocked counts.

- **CRC errors** mean interference or a weak link on that channel.
- **Duplicates** mean two radios heard the same node — normal if a node
  is strong, a sign of duplicate node IDs if persistent.
- **Blocked** means the whitelist rejected it.

Chart: pick a metric and a time window, click legend chips to isolate a
node, hover for exact values.

### Configuration

Edit any radio, then:

| Button | Effect |
|---|---|
| **Apply** | immediate, lost on reboot |
| **Save to EEPROM** | persists |
| **Reload from gateway** | discards edits, re-reads actual settings |
| **Clear statistics** | zeroes counters (useful before a measurement) |
| **MIC report only / enforced** | see below |
| **Reboot gateway** | asks first |

### Turning on authentication

The system ships in **report only** mode: nodes without a MIC are still
accepted and counted.

To enforce:

1. Reflash **every** node with `DEF_NODE_MIC_ENABLE 1`.
2. On the console's Nodes page, confirm every node shows **signed**.
3. Configuration → click **MIC report only** to switch it to
   **enforced**.

From then on, a frame without a valid MIC is dropped.

> Enforcing while any node is still *legacy* will silence that node
> completely. The policy is not persisted — rebooting the gateway
> returns it to report-only, which is the escape hatch if you enforce
> too early.

---

## 6.4 Routine checks

**Is everything healthy?** Dashboard: four radios "healthy", nodes count
matches what you deployed, MIC failures zero, silent nodes zero.

**Is a node getting weaker?** Nodes page → click the row → RSSI history.
A steady downward trend over hours usually means a battery sagging or
something moving into the path.

**Is a channel noisy?** Compare CRC counts between radios. One channel
with CRC errors and the others clean points at interference on that
frequency, not at the gateway.

**Is the gateway healthy?** Diagnostics: stall mask should be `0x0`,
free heap stable, crypto self-test PASS, uplink dropped lines zero.

---

## 6.5 Data export

`PC_Tool/packets.csv` is appended continuously and contains every packet
since the bridge first ran. Open it in Excel directly.

The **Export CSV** button downloads what the browser currently holds —
useful for a quick slice, but the file on disk is the full record.

To start a fresh log, stop the bridge, rename or delete `packets.csv`,
and start it again.

---

## 6.6 Adding a node

1. `node_config.h`: set `DEF_NODE_ID` (unique) and `DEF_NODE_FREQ_HZ`
   (matching one gateway radio).
2. Build and flash that board — see
   [07-Build-and-Flash.md](07-Build-and-Flash.md).
3. Confirm on the console that it appears with the right ID and
   **signed** authentication.

Remember both the frequency and the ID. Two nodes sharing an ID merge
into one row and lose packets to the duplicate filter.

Next: [07-Build-and-Flash.md](07-Build-and-Flash.md)
