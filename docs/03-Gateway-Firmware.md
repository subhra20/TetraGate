# 3. Gateway Firmware

STM32F429ZI, FreeRTOS, LVGL 8.3. Roughly **434 KB flash (21 %)** and
**139 KB RAM (73 %)**.

---

## 3.1 Module map

| File | Responsibility |
|---|---|
| `freertos.c` | task creation, boot sequence, the UI task body |
| `radio_manager.c` | per-radio service, statistics, health, MIC check, exact tuning |
| `node_table.c` | one row per node, duplicates, whitelist, silence detection |
| `packet_log.c` | ring of the last 32 packets for the Packets screen |
| `monitor.c` | watchdog, task check-ins, periodic housekeeping |
| `uplink.c` | northbound JSON feed on USART3 |
| `gw_command.c` | southbound JSON commands from the PC |
| `lora_crypto.c` | AES-128, CMAC, MIC verify, self-test |
| `eeprom_operation.c` | A/B config slots, touch calibration storage |
| `DeviceConfigJSON.c` | configuration ⇄ JSON for the EEPROM |
| `gui_*.c` | the six touchscreen screens |
| `LoRa.c` | the shared SX1276 driver — **extend, never rewrite** |

---

## 3.2 Boot sequence

```
HAL + clocks
  ↓
TaskInit
  ├─ EEPROM: load configuration JSON   → or defaults if absent/corrupt
  ├─ start radios 1..4                 → "LoRa1: OK" etc on USART3
  └─ exit
Task_LVGL
  ├─ LVGL init, touch calibration load
  ├─ build all six screens
  ├─ crypto self-test                  → reported in the hello line
  ├─ Fn_Uplink_Hello()
  └─ Fn_Command_Init()                 → arms UART reception
Task_Monitor
  ├─ start IWDG
  └─ loop every 200 ms
```

The watchdog is started by the monitor task, not by `main()`. That means
it can never be left running with nothing to feed it:

```c
/* Starting the IWDG here means it can never be left running with no
 * feeder. Once started it cannot be stopped except by a reset. */
MX_IWDG_Init();
```

---

## 3.3 `radio_manager.c` — the receive path

This is the most important function in the gateway. It runs in the
radio's own task, woken by DIO0.

### Step 1 — read the flags first

```c
u8Irq = LoRa_read(p, RegIrqFlags);

if ((u8Irq & RADIO_IRQ_PAYLOAD_CRC_ERR) != 0U)
{
    s->u32RxCrcErr++;
    LoRa_write(p, RegIrqFlags, RADIO_IRQ_CLEAR_ALL);
    LoRa_startReceiving(p);
    return;
}
```

`LoRa_receive()` clears the entire IRQ register, so the CRC bit must be
read before the payload or it is destroyed. A damaged packet is counted
per radio and dropped — those counters are what tell you a channel has
interference rather than no traffic.

### Step 2 — signal quality

```c
/* RegPktSnrValue is a signed quarter-dB value. */
s->i8LastSnr    = (int8_t)((int8_t)LoRa_read(p, RegPktSnrValue) / 4);
s->i16LastRssi  = (int16_t)LoRa_getRSSI(p);
```

The SNR register is signed and in quarter-dB units — a common source of
nonsense readings if treated as a plain byte.

### Step 3 — authenticity, before anything believes the frame

```c
if ((u8RxBuf[u8LoRaSelector][0] & DEF_MIC_TYPE_FLAG) == 0U)
{
    s->u32RxNoMic++;
    enMic = en_Mic_Absent;

    if (bMicRequired)
    {
        return;
    }
}
else if (Fn_Mic_Verify(au8MasterKey, u8RxBuf[u8LoRaSelector], u8Len))
{
    s->u32RxMicOk++;
    enMic = en_Mic_Ok;
}
else
{
    /* Claimed a MIC and got it wrong. Always dropped, whatever the
     * policy: this is a forgery or a key mismatch. */
    s->u32RxMicFail++;
    return;
}
```

Three outcomes, three counters. Note the asymmetry: a *missing* MIC is
tolerated under the report policy, a *wrong* MIC never is.

### Step 4 — identity, duplicates, whitelist

```c
enNode = Fn_NodeTable_Update(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len,
                             s->i16LastRssi, s->i8LastSnr);

if (enNode == en_Node_Duplicate)
{
    s->u32RxDuplicate++;
    return;
}
```

### Step 5 — store and publish

```c
s->u32LastRxTick = osKernelSysTick();
s->u32RxGood++;

Fn_LoRa_Rx_Update(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len, (int)s->i16LastRssi);
Fn_PktLog_Push(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len, s->i16LastRssi, s->i8LastSnr);

Fn_Uplink_Packet(u8LoRaSelector, u8RxBuf[u8LoRaSelector], u8Len,
                 s->i16LastRssi, s->i8LastSnr, enMic);
```

`u32RxGood` therefore means **accepted**, not "came off the air" —
CRC errors, duplicates, blocked and MIC failures all have their own
counters. The counters reconcile exactly:

```
mic_ok + no_mic  ==  good + duplicates
```

(MIC is checked *before* de-duplication, so duplicates are counted on
both sides of that equation.)

### Health and recovery

```c
bool Fn_Radio_Health_Check (uint8_t u8LoRaSelector)
{
    return (LoRa_read(pRadio[u8LoRaSelector], RegVersion) == 0x12U);
}
```

If a radio hears nothing for `DEF_RADIO_HEALTH_PERIOD_MS` the task reads
the chip version register. After `DEF_RADIO_HEALTH_FAIL_LIMIT`
consecutive failures it re-initialises the radio and counts a
`u32ReInit`. A rising re-init count means a flaky SPI link or a loose
module.

---

## 3.4 `node_table.c` — who is out there

```c
u16Id = (uint16_t)((uint16_t)pu8Data[1] | ((uint16_t)pu8Data[2] << 8));
u8Seq = pu8Data[3];

/* An empty whitelist means commissioning: accept everything. */
if ((u16WhitelistCount > 0U) && !_Fn_Whitelist_Find(u16Id, NULL))
{
    osMutexRelease(mtx_nodeHandle);
    return en_Node_NotWhitelisted;
}
```

An empty whitelist accepting everything is a deliberate commissioning
mode: you cannot add a node to a whitelist until you have seen its ID.

### Silence detection

```c
#define NODE_SILENT_FACTOR      3U
#define NODE_SILENT_FLOOR_MS    60000U
```

A node's reporting interval is **learned** from the gap between its
packets, smoothed. It is marked silent after
`max(3 × interval, 60 s)`. Two packets are needed before an interval
exists, so a node that transmits once and dies is never marked silent —
it simply has no interval to miss.

---

## 3.5 `uplink.c` — the northbound feed

One JSON object per line on USART3 at 115200.

### It must never block its callers

```c
static bool _Fn_Lock (void)
{
    if (osSemaphoreWait(sem_lora_uartHandle, DEF_UPLINK_SEM_WAIT_MS) != osOK)
    {
        u32Dropped++;
        return false;
    }
    return true;
}
```

The callers are the four radio tasks and the monitor task. The monitor
task is the watchdog feeder — if the uplink held it up on a busy port,
a slow UART would reset the board. So the semaphore is taken with a
**50 ms timeout** and the line is dropped instead. Dropped lines are
counted and reported, so a quiet feed can be told from a broken one.

### The buffer is static

```c
#define UP_BUF_BYTES        1024
static char chLine[UP_BUF_BYTES];
```

The radio and monitor tasks have 1 KB stacks with roughly 480 bytes
spare. A kilobyte of frame buffer on their stack would overflow it. The
buffer is safe to share because it is only touched while the semaphore
is held.

> **A real bug caught here.** The buffer was originally 512 bytes. The
> status line builds four radio objects in a loop with an overflow
> guard, and at 512 the guard tripped after the *third* radio — radio 4
> silently vanished from the feed. It was found by decoding an actual
> status line rather than trusting the format string.

### No floating point

```c
",\"frame\":\"data\",\"temp_cc\":%d,\"hum_cp\":%u,"
```

The build has no printf float support, and enabling it to render a
temperature would cost kilobytes. Scaled integers go out exactly as the
node sent them — `temp_cc` is centi-degrees — and the PC divides. The
wire stays lossless either way.

---

## 3.6 `gw_command.c` — the southbound path

### Interrupt-driven reception

```c
void HAL_UART_RxCpltCallback (UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        uint16_t u16Next = (uint16_t)((u16Head + 1U) % DEF_CMD_RING_BYTES);

        if (u16Next != u16Tail)
        {
            au8Ring[u16Head] = u8RxByte;
            u16Head = u16Next;
        }
        else
        {
            u32Overruns++;
        }

        (void)HAL_UART_Receive_IT(huart, &u8RxByte, 1U);
    }
}
```

Polling was not an option. The peripheral holds one byte and the monitor
task runs every 200 ms; at 115200 that would lose all but roughly one
byte in two thousand. USART3's interrupt was already enabled by CubeMX,
so no `.ioc` change was needed.

### A hand-written scanner, not cJSON

```c
static bool _Fn_Get_Int (const char *pchJson, const char *pchKey, int32_t *pi32Out)
{
    const char *p = _Fn_Find_Key(pchJson, pchKey);

    if (p == NULL) { return false; }
    if ((*p != '-') && ((*p < '0') || (*p > '9'))) { return false; }

    *pi32Out = (int32_t)strtol(p, NULL, 10);
    return true;
}
```

cJSON is already linked into this firmware for the EEPROM config, but it
allocates and recurses. Neither belongs in the watchdog feeder's 1 KB
stack, and the command objects are flat and small.

### Validate the whole configuration, commit nothing on failure

```c
if (!Fn_LoRa_Config_Validate(&sTrial))
{
    _Fn_Ack("set_radio", false, "invalid or under 2 MHz spacing");
    return;
}
```

A trial copy is built, every supplied field applied, then the *entire*
configuration validated. A rejected command changes nothing. Each field
is optional, so a command may change only the frequency and leave the
modem settings alone.

The trial copy is `static` for the same reason as the uplink buffer:
`StructLoRaGtwConfParam` carries four 255-byte receive buffers.

### Command set

| Command | Effect |
|---|---|
| `{"cmd":"ping"}` | liveness check |
| `{"cmd":"get_config"}` | emits a `config` line |
| `{"cmd":"get_nodes"}` | emits one `node` line per node |
| `{"cmd":"set_radio","n":1,...}` | apply to the running radio |
| `{"cmd":"save"}` | write configuration to EEPROM |
| `{"cmd":"clear_stats"}` | zero the counters |
| `{"cmd":"set_mic_policy","require":1}` | switch report ⇄ enforce |
| `{"cmd":"reboot"}` | reset the gateway |

Every command is answered with exactly one line:

```json
{"type":"ack","cmd":"set_radio","ok":0,"err":"invalid or under 2 MHz spacing"}
```

---

## 3.7 `lora_crypto.c` — proving it works

There is no host C compiler in this toolchain, so correctness is
demonstrated **on the target**:

```c
Fn_AES128_Init(&sCtx, au8FipsKey);
Fn_AES128_Encrypt(&sCtx, au8FipsIn, au8Out);
if (memcmp(au8Out, au8FipsOut, 16) != 0) { return false; }

Fn_CMAC(au8CmacKey, NULL, 0U, au8Out);
if (memcmp(au8Out, au8CmacEmpty, 16) != 0) { return false; }

Fn_CMAC(au8CmacKey, au8CmacMsg16, 16U, au8Out);
if (memcmp(au8Out, au8CmacTag16, 16) != 0) { return false; }
```

- FIPS-197 C.1 for the block cipher
- RFC 4493 for CMAC, with **two** cases chosen on purpose: an empty
  message exercises the padded path with K2, a single full block
  exercises the complete path with K1. Those two branches are where a
  CMAC implementation usually goes wrong.

The result appears as `crypto_ok` in the hello line and in every status
line. Both parts report **PASS** on real silicon.

---

## 3.8 The touchscreen

Six tabs: Home, Radio, Signal, Log, Nodes, Sys. Only the **visible** tab
is refreshed, every 400 ms:

```c
u16Tab = (pObjTabview != NULL) ? lv_tabview_get_tab_act(pObjTabview) : TAB_HOME;

switch (u16Tab)
{
    case TAB_RADIOS: Fn_GUI_Radios_Refresh(); break;
    ...
}
```

Redrawing six screens would spend most of the UI task's time on pixels
nobody is looking at.

### LVGL memory

```c
#define LV_MEM_SIZE (56U * 1024U)
```

The six screens use about **36 KB (64 %)** of that pool at start-up,
reported on the boot line. It was 32 KB originally, and building the
screens exhausted it — which, because `LV_USE_ASSERT_MALLOC` is on,
asserts into an infinite loop. The symptom was not an error message but
a hang, and the watchdog resetting the board every five seconds. Past
about 80 % usage, raise it before adding widgets.

Next: [04-Node-Firmware.md](04-Node-Firmware.md)
