# LoRa Sensor Network — Documentation

A four-channel LoRa gateway with a touchscreen, four battery-style sensor
nodes, and a PC console for monitoring and configuration.

This documentation is written to be read in order by someone who has not
seen the project before. It assumes you can read C, but it does not
assume you know LoRa, FreeRTOS or AES.

---

## The system in one paragraph

Four **nodes** (STM32G030F6P6 + RFM95W) each transmit a small binary
frame every five seconds on its own frequency. One **gateway**
(STM32F429ZI + four RFM95W + a 320×240 touchscreen) listens on all four
frequencies at once, checks each frame is authentic, and shows what it
hears on its screen. The gateway also streams every packet as JSON over
its USB serial port, where a **PC console** draws live charts and lets
you reconfigure the radios.

```
  4 NODES                   AIR                    GATEWAY                PC
  G030 + RFM95W                                    F429 + 4x RFM95W

  1001  868.100 MHz  ──────────────▶  radio 1 ──┐
  1002  900.000 MHz  ──────────────▶  radio 2 ──┤                 USB
  1003  915.000 MHz  ──────────────▶  radio 3 ──┼──▶ TFT screen    │
  1004  880.000 MHz  ──────────────▶  radio 4 ──┘                 ▼
        18 bytes, every 5 s                       USART3 ──────▶ browser
                                                   JSON          console
```

---

## Read in this order

| # | Document | What it covers |
|---|---|---|
| 1 | [01-Architecture.md](01-Architecture.md) | Hardware, tasks, where data lives, why it is built this way |
| 2 | [02-Air-Protocol.md](02-Air-Protocol.md) | The bytes on air, the MIC, channels and airtime |
| 3 | [03-Gateway-Firmware.md](03-Gateway-Firmware.md) | Gateway code, module by module, with excerpts |
| 4 | [04-Node-Firmware.md](04-Node-Firmware.md) | Node code, module by module, with excerpts |
| 5 | [05-PC-Console.md](05-PC-Console.md) | The Python bridge and the browser console |
| 6 | [06-User-Guide.md](06-User-Guide.md) | How to operate the gateway and the console |
| 7 | [07-Build-and-Flash.md](07-Build-and-Flash.md) | Toolchain, building, flashing, picking the right probe |
| 8 | [08-Troubleshooting.md](08-Troubleshooting.md) | Symptom → cause → fix |

---

## Quick start

1. Power the gateway and the nodes.
2. Plug the gateway's ST-Link USB into the PC.
3. Run:

```bash
cd PC_Tool
python lora_bridge.py
```

The browser opens at `http://127.0.0.1:8787/`. Within about ten seconds
you should see four radios reporting and four nodes appearing.

Only `pyserial` is required, and it is already installed on this
machine. Full details in [06-User-Guide.md](06-User-Guide.md).

---

## Repository layout

```
LoRa_Gateway_TFT_EEPROM/          gateway firmware (STM32F429ZI)
├── Core/Inc, Core/Src            application code
├── Middlewares/Third_Party/lvgl  graphics library for the TFT
├── PC_Tool/                      the PC console
│   ├── lora_bridge.py            serial ⇄ HTTP bridge
│   ├── dashboard.html            the browser console
│   └── run_dashboard.bat         Windows launcher
└── Docs/                         this documentation

LoRa_Transmeter_STM32G030F6P6/    node firmware (STM32G030F6P6)
└── Core/Inc, Core/Src
```

### Files duplicated on purpose

Three files exist in **both** firmware projects and must stay
byte-identical. They are duplicated rather than shared because the two
are separate STM32CubeIDE projects with no common source folder.

| File | Why it must match |
|---|---|
| `LoRa.c` / `LoRa.h` | the proven SX1276 driver, used by both ends |
| `lora_crypto.c` / `.h` | both ends must compute the identical MIC |
| `lora_keys.h` | the shared master key |

If you change one, change the other, or the link goes quiet with no
error message anywhere.
