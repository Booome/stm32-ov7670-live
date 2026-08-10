#!/usr/bin/env python3
"""Capture USART output from the moment the MCU boots.

Holds the MCU in reset with openocd `reset halt`, opens and flushes the
serial port while the firmware is still halted (no output yet), then
releases reset with `reset run` and captures for a fixed duration.  This
guarantees the capture starts at the very first byte of firmware boot,
instead of racing a freshly-opening serial port against an already-running
board.

Flow:
  1. openocd daemon starts and holds the MCU halted (reset halt).
  2. Serial port is opened and its RX FIFO flushed.
  3. openocd is told `reset run`; the firmware boots.
  4. USART bytes are captured until the duration elapses.
"""
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

import serial

STLINK_CFG_TEMPLATE = """source [find interface/stlink.cfg]
transport select hla_swd
source [find target/stm32f1x.cfg]
"""

TELNET_PORT = 4444
RESET_HALT_WAIT_S = 15.0
BAUD_DEFAULT = 115200
PORT_DEFAULT = '/dev/ttyACM0'
DURATION_DEFAULT = 10.0


def wait_for_marker(log_path, marker, timeout_s):
    """Poll the openocd log until `marker` appears.

    The telnet port comes up before the `-c` command list finishes running;
    waiting on the port alone lets the MCU boot from openocd's own init
    reset before we can flush the serial FIFO.  A marker echoed at the end
    of the command list guarantees `reset halt` has fully executed.
    """
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with open(log_path, 'r') as f:
                if marker in f.read():
                    return True
        except OSError:
            pass
        time.sleep(0.1)
    return False


def wait_for_telnet(host, port, timeout_s):
    """Poll until openocd's telnet server accepts connections."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def detect_serial_port():
    """Pick the first available ST-Link VCP / USB serial device.

    Order of preference: /dev/serial/by-id/ (ST-Link named symlinks),
    then plain /dev/ttyACM* and /dev/ttyUSB*.  Returns None if nothing
    is present or more than one device exists and the choice is ambiguous.
    """
    import glob
    by_id = sorted(glob.glob('/dev/serial/by-id/*'))
    if by_id:
        return by_id[0]
    acm = sorted(glob.glob('/dev/ttyACM*'))
    usb = sorted(glob.glob('/dev/ttyUSB*'))
    candidates = acm + usb
    if len(candidates) == 1:
        return candidates[0]
    return None


def env_default(name, fallback):
    """Read an env var, empty string treated as unset."""
    val = os.environ.get(name, '')
    return val if val else fallback


def flush_serial_input(ser, quiet_s=0.4, max_s=2.0):
    """Drain the serial input until it stays empty for `quiet_s`.

    A single `reset_input_buffer()` (tcflush TCIFLUSH) only clears the tty
    layer.  On a CDC-ACM VCP the bytes the MCU emitted while openocd's
    `init` was still bringing the target up can be stuck deeper in the USB
    stack, so one flush is not enough: keep draining until no new bytes
    arrive for `quiet_s` (the MCU is halted, so silence proves the pipe is
    truly empty).
    """
    deadline = time.time() + max_s
    last_rx = time.time()
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            ser.read(n)
            last_rx = time.time()
        elif time.time() - last_rx >= quiet_s:
            break
        else:
            time.sleep(0.02)
    ser.reset_input_buffer()


def send_openocd_command(command):
    """Send a command to openocd's telnet console and return its output."""
    with socket.create_connection(('127.0.0.1', TELNET_PORT), timeout=3.0) as s:
        s.sendall((command + '\n').encode())
        time.sleep(0.2)
        s.settimeout(0.5)
        out = b''
        while True:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            out += chunk
    return out.decode(errors='replace')


def main():
    parser = argparse.ArgumentParser(
        description='Capture USART output starting at MCU boot.')
    parser.add_argument('--port', default=None,
                        help='serial device (default: SERIAL_CAPTURE_PORT '
                             'env, else auto-detect)')
    parser.add_argument('--baud', type=int, default=None,
                        help='baud rate (default: SERIAL_CAPTURE_BAUD env, '
                             'else %(default)s)' % {'default': BAUD_DEFAULT})
    parser.add_argument('--duration', type=float, default=None,
                        help='capture seconds after reset release '
                             '(default: SERIAL_CAPTURE_DURATION env, '
                             'else %(default)s)' % {'default': DURATION_DEFAULT})
    parser.add_argument('--cfg', default=None,
                        help='openocd config file; auto-generated if omitted')
    parser.add_argument('--output', default=None,
                        help='write capture to file instead of stdout')
    args = parser.parse_args()

    port = args.port or env_default('SERIAL_CAPTURE_PORT', '')
    baud = args.baud if args.baud is not None else \
        int(env_default('SERIAL_CAPTURE_BAUD', str(BAUD_DEFAULT)))
    duration = args.duration if args.duration is not None else \
        float(env_default('SERIAL_CAPTURE_DURATION', str(DURATION_DEFAULT)))

    if not port:
        port = detect_serial_port()
    if not port:
        sys.stderr.write('ERROR: no serial port given (--port or '
                         'SERIAL_CAPTURE_PORT) and none auto-detectable\n')
        return 1

    cfg_path = args.cfg
    cleanup_cfg = False
    if cfg_path is None:
        fd, cfg_path = tempfile.mkstemp(suffix='.cfg')
        with os.fdopen(fd, 'w') as f:
            f.write(STLINK_CFG_TEMPLATE)
        cleanup_cfg = True

    log_path = None
    fd, log_path = tempfile.mkstemp(suffix='.log')
    os.close(fd)
    log_file = open(log_path, 'w')

    ocd = subprocess.Popen(
        ['openocd', '-f', cfg_path, '-c', 'init; reset halt; echo HALTED'],
        stdout=log_file, stderr=log_file,
    )

    try:
        if not wait_for_telnet('127.0.0.1', TELNET_PORT, RESET_HALT_WAIT_S):
            sys.stderr.write('ERROR: openocd telnet did not come up in time\n')
            with open(log_path) as f:
                sys.stderr.write(f.read())
            return 1
        if not wait_for_marker(log_path, 'HALTED', RESET_HALT_WAIT_S):
            sys.stderr.write('ERROR: openocd did not finish reset halt\n')
            with open(log_path) as f:
                sys.stderr.write(f.read())
            return 1

        print(f'[hold] MCU halted, opening {port} @ {baud}...',
              file=sys.stderr)
        ser = serial.Serial(port, baud, timeout=0.1)
        flush_serial_input(ser)

        print('[release] issuing reset run...', file=sys.stderr)
        send_openocd_command('reset run')

        out = open(args.output, 'w') if args.output else sys.stdout
        try:
            start = time.time()
            while time.time() - start < duration:
                data = ser.read(4096)
                if data:
                    out.write(data.decode('ascii', errors='replace'))
                    out.flush()
        finally:
            if args.output:
                out.close()
            ser.close()
        return 0
    finally:
        ocd.terminate()
        try:
            ocd.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            ocd.kill()
            ocd.wait()
        log_file.close()
        os.unlink(log_path)
        if cleanup_cfg:
            os.unlink(cfg_path)


if __name__ == '__main__':
    sys.exit(main())
