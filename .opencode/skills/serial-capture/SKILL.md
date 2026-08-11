---
name: serial-capture
description: Use when capturing USART/UART debug output from a bare-metal STM32 (or any ST-Link-probed MCU) via the ST-Link virtual COM port and the output must include the very first bytes printed after reset, or when boot-time output keeps getting lost because the serial port opens after the board already started running.
---

# Serial Capture (Boot-Completeness)

## Overview

`st-flash reset` releases reset instantly; by the time your terminal/serial
tool opens the VCP and flushes, the firmware has already printed its startup
lines and they are gone. This skill holds the MCU in reset first, then opens
the serial port, then releases reset, so the capture is byte-complete from
boot.

Capture is **marker-driven**: bytes before a `--start-marker` line are
dropped (so stale bytes from a previous session never enter the log), and
recording stops on the `--stop-marker` line. `--duration` is only a fallback
timeout for firmware that never prints markers.

## When to Use

- A test firmware prints diagnostic output at startup and you need ALL of it.
- You keep seeing truncated serial logs (missing first lines) with
  `st-flash reset` + `cat`.
- You are iterating on boot diagnostics and want a reproducible capture.
- You want a clean log that begins on the firmware's entry marker and ends
  on a stop marker, immune to stale UART bytes.

**When NOT to use:** plain `st-flash reset` + `cat` is fine when losing the
first few lines does not matter.

## How It Works

1. `openocd` starts in the background and halts the CPU (`reset halt`).
   The firmware does not run; the UART stays silent.
2. The serial port is opened and its RX FIFO flushed: `flush_serial_input`
   sets a blocking `timeout=0.5`, then reads repeatedly until a read
   returns empty (timeout fired = pipe silent = truly empty), and restores
   the original timeout.
3. `reset run` is sent over openocd's telnet console; the firmware boots and
   prints from byte 0.
4. Output before the start marker is discarded; output is recorded from the
   start marker until the stop marker (or the fallback `--duration`
   timeout).

> `st-flash reset` has **no** hold/release mode (arguments are silently
> ignored), which is why openocd's `reset halt` / `reset run` is used.

## Usage

```bash
python3 .opencode/skills/serial-capture/scripts/serial_capture.py \
  --port /dev/ttyACM0 --baud 115200
```

- `--output <file>`: write capture to a file instead of stdout.
- `--cfg <path>`: custom openocd config (auto-generated ST-Link + stm32f1
  config by default).
- `--start-marker <str>` / `--stop-marker <str>`: override the markers.
  Defaults are `=== Unit Test Runner Begin ===` and `=== Unit Test Runner
  End ===` (printed by the test runner). Pass an empty string to disable a
  marker.
- `--duration <sec>`: fallback timeout only; default 180 s. Used if the
  stop marker never appears (e.g. a streaming test with no end marker).
- The script holds reset, opens+flushes the port, releases reset, and
  records from the start marker to the stop marker.

## Port / Baud Configuration

The script resolves port and baud in this priority order:

1. `--port` / `--baud` CLI flags
2. `SERIAL_CAPTURE_PORT` / `SERIAL_CAPTURE_BAUD` / `SERIAL_CAPTURE_DURATION`
   environment variables
3. Auto-detected serial port (first `/dev/serial/by-id/*` symlink, else the
   single `/dev/ttyACM*` or `/dev/ttyUSB*` device), baud falls back to
   `115200`, duration to `10.0`

Use env vars when the device path varies between runs:

```bash
export SERIAL_CAPTURE_PORT=/dev/ttyACM0
export SERIAL_CAPTURE_BAUD=230400
export SERIAL_CAPTURE_DURATION=15
python3 .opencode/skills/serial-capture/scripts/serial_capture.py
```

## Common Mistakes

- Relying on `--duration` for correctness: it is a fallback only. Prefer
  firmware that prints an end marker; with markers the log boundary is exact
  and the capture ends promptly.
- The script keeps an openocd daemon alive during the capture; it is
  terminated automatically in `finally`. Do not run `st-flash` against the
  same ST-Link concurrently.
- If `openocd` reports a very low target voltage, the SWD link may be
  unreliable; check the probe connection and board power.
- **Capture still starts with stale lines** (e.g. an orphan `R014:` row
  before `=== Unit Test Runner Begin ===`): the flush did not reach silence,
  meaning the MCU was still emitting bytes when it should have been halted.
  The start marker discards them regardless, so check the marker behaviour:
  re-run with a fresh ST-Link connection; if it persists, confirm openocd's
  `reset halt` actually holds the CPU (watch for the `WARNING: serial input
  never went quiet` message).
