# 7. Building and Flashing

---

## 7.1 Toolchain

STM32CubeIDE 1.19.0, with:

| Tool | Path (this machine) |
|---|---|
| arm-none-eabi-gcc 13.3 | `…/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin` |
| STM32_Programmer_CLI | `…/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.200.202503041107/tools/bin` |

There is **no host C compiler** (no gcc/clang for Windows). That is why
the crypto is verified by an on-target self-test rather than host unit
tests.

---

## 7.2 Building in the IDE

Open the project and press Build. Nothing special.

### New source files need no setup

`.cproject` declares `Core`, `Drivers` and `Middlewares` as source
folders, so any `.c` you drop into `Core/Src` is discovered
automatically. You do **not** need to add it to a makefile.

### New *settings* do need setup — in `.cproject`

> **This caused a real outage.** The LVGL include path once existed only
> in the generated `Debug/**/subdir.mk` files. Command-line `make`
> worked for weeks. Then the first IDE build **regenerated those
> makefiles from `.cproject`**, the path vanished, and every file
> including `lvgl.h` failed with "No such file or directory".

**Never edit `Debug/**/subdir.mk`, `Debug/sources.mk` or
`Debug/objects.list`.** CubeIDE overwrites them at the start of every
build. Settings belong in `.cproject`:

Project Properties → C/C++ Build → Settings → Tool Settings → MCU GCC
Compiler → Include paths.

The gateway needs exactly one non-default entry:

```
../Middlewares/Third_Party/lvgl
```

in **both** the Debug and Release configurations.

`.cproject` is CRLF. If you patch it with a script, `sed -i` in Git Bash
strips every carriage return and rewrites the whole file — use a binary-
mode edit and check with `diff` that only the intended lines changed.

---

## 7.3 Building from the command line

A headless build regenerates the makefiles from `.cproject` exactly as
the IDE does, which makes it a genuine test of the project settings:

```bash
stm32cubeidec.exe --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "<a throwaway workspace>" \
  -import "D:\STM32 CubeIDE Workspace\LoRa_STM32_Directory\LoRa_Gateway_TFT_EEPROM" \
  -build "LoRa_Gateway_TFT_EEPROM/Debug"
```

Use `-cleanBuild` instead of `-build` for a full rebuild.

**Use a throwaway workspace,** not the real one: a running IDE holds a
lock on its workspace and the headless build will fail.

---

## 7.4 Flashing — pick the right probe

With both boards connected there are **two** ST-Link probes. Flashing
the wrong one is an easy and annoying mistake.

List them:

```bash
STM32_Programmer_CLI -l
```

Identify which is which by device ID:

```bash
STM32_Programmer_CLI -c port=SWD sn=<serial> mode=HOTPLUG
```

| Device ID | Part | Board |
|---|---|---|
| `0x419` | STM32F42xxx | **gateway** |
| `0x466` | STM32G03x | **node** |

Then flash with the serial number given explicitly:

```bash
# gateway
STM32_Programmer_CLI -c port=SWD sn=066DFF565087534867135717 mode=UR \
  -w Debug/LoRa_Gateway_TFT_EEPROM.elf -v -rst

# node
STM32_Programmer_CLI -c port=SWD sn=2109021F5318303030303032 mode=UR \
  -w Debug/LoRa_Transmeter_STM32G030F6P6.elf -v -rst
```

(Those serials are the ones on this bench; re-run `-l` if hardware
changes.)

> **Before flashing a node, confirm which physical board is attached.**
> `node_config.h` holds whatever identity was last edited, and flashing
> it onto a different board silently gives you two nodes with the same
> ID and a dead channel. This happened once during development: 900 MHz
> went quiet and two boards both claimed to be node 1004. The console's
> Nodes page shows which ID is strongest, which is usually the board in
> front of you.

---

## 7.5 Reading variables over SWD

The node has no serial port, so this is how you inspect it.

Find the address:

```bash
arm-none-eabi-nm Debug/LoRa_Transmeter_STM32G030F6P6.elf | grep sNodeStats
```

Read it (24 bytes):

```bash
STM32_Programmer_CLI -c port=SWD sn=<node probe> mode=HOTPLUG -r32 0x2000008C 0x18
```

Layout:

| Offset | Field |
|---|---|
| +0 | `u32TxOk` |
| +4 | `u32TxFail` |
| +8 | `u32FreqHzActual` |
| +12 | `u32UptimeS` |
| +16 | `u16NodeId` (low half-word), `u8LastSeq`, `u8LastLen` |
| +20 | `u8RadioOk`, `u8CryptoOk` |

The same technique works on the gateway for `u32StallMask`,
`u32FeedCount` and friends.

`mode=HOTPLUG` attaches without resetting, so the board keeps running
while you read.

---

## 7.6 Memory budgets

| | Flash | RAM |
|---|---|---|
| Gateway | ~434 KB of 2048 KB (21 %) | ~139 KB of 192 KB (**73 %**) |
| Node | ~17.4 KB of 32 KB (53 %) | ~1.8 KB of 8 KB (22 %) |

RAM on the gateway is the figure to watch. The two consumers that grow
are:

- **LVGL's pool**, `LV_MEM_SIZE` in `lv_conf.h`, currently 56 KB with
  the six screens using about 64 % of it. Exhausting it asserts into a
  hang, not an error message.
- **Draw buffers** (~30 KB), which must stay in DMA-reachable SRAM —
  never CCM.

The boot line reports LVGL pool usage every time:

```
GUI: 6 screens up, LVGL pool 64% used, 21144 B free
```

---

## 7.7 A caution about CubeMX regeneration

If you regenerate code from the `.ioc`, CubeMX may rewrite `.cproject`
and drop hand-added include paths — specifically the LVGL one. If
builds suddenly fail on `lvgl.h`, that is the first thing to check.

Next: [08-Troubleshooting.md](08-Troubleshooting.md)
