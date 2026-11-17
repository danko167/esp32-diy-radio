"""Uploads firmware over the USB cable through the running firmware, like an
OTA update but on the cable, so the radio's screen shows the progress.

    python tools/usb_update.py PORT firmware.bin [-- fallback command...]

If the radio doesn't answer (no firmware yet, or a broken one), the fallback
command runs instead: PlatformIO passes its usual esptool upload (see
tools/usb_upload_hook.py). Only the standard library and pyserial are used.

On WSL with a Windows port (COM6), all of it runs with Windows' Python, which
needs `python -m pip install --user pyserial esptool` once.
"""

import hashlib
import os
import shutil
import subprocess
import sys
import time

import serial  # pyserial, part of PlatformIO's Python

from screenshot import open_port

CHUNK = 4096  # the radio acknowledges each one; must match src/app/usb_update.cpp


def wait_for(port, prefixes, timeout):
    """The first line starting with one of `prefixes` (other output is the radio's log)."""
    deadline = time.time() + timeout
    buf = b''
    while time.time() < deadline:
        # What's there (at least a byte): read(n) would wait out the timeout for n
        buf += port.read(max(1, port.in_waiting))
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            line = line.strip()
            if line.startswith(prefixes):
                return line
    return None


def ok(line, prefix):
    # A line can run into what follows, such as the radio's log or its boot
    # messages; older firmware also lost the line ending to Serial.flush()
    return line is not None and line.startswith(prefix)


def send_chunks(port, data, label):
    """`data` in acknowledged chunks, once the radio said #READY; False if it stopped."""
    shown = -1
    for offset in range(0, len(data), CHUNK):
        port.write(data[offset:offset + CHUNK])
        answer = wait_for(port, (b'#ACK', b'#FAIL'), 10)
        if not ok(answer, b'#ACK'):
            print(f'\n{label}: stopped at {offset} bytes ({answer or "no answer"})')
            return False
        percent = (offset + CHUNK) * 100 // len(data)
        if percent != shown and percent % 5 == 0:
            shown = percent
            print(f'\r{label}: {min(percent, 100)}%', end='', flush=True)
    print()
    return True


def update(port_name, path):
    data = open(path, 'rb').read()
    md5 = hashlib.md5(data).hexdigest()
    try:
        port = open_port(port_name)
    except serial.SerialException as e:
        print(f'usb_update: can\'t open {port_name}: {e}')
        return False
    with port:
        port.reset_input_buffer()
        # "U" alone first: firmware without this feature ignores it, while the
        # header's letters and digits would be dev console commands to it
        port.write(b'U')
        if not wait_for(port, (b'#HEADER',), 2):
            print('usb_update: the radio didn\'t answer')
            return False
        port.write(f'{len(data)},{md5}\n'.encode())
        answer = wait_for(port, (b'#READY', b'#FAIL'), 5)
        if not ok(answer, b'#READY'):
            print(f'usb_update: the radio didn\'t take the update ({answer or "no answer"})')
            return False

        start = time.time()
        if not send_chunks(port, data, 'usb_update'):
            return False
        answer = wait_for(port, (b'#DONE', b'#FAIL'), 30)
        if not ok(answer, b'#DONE'):
            print(f'usb_update: failed at the end ({answer or "no answer"})')
            return False
        seconds = time.time() - start
        print(f'usb_update: {len(data) // 1024} KB in {seconds:.1f} s, the radio restarts')
        return True


def run_on_windows(port_name, path, fallback):
    """WSL can't open a Windows COM port, so this script runs again with Windows'
    Python, and the fallback with its esptool. Paths become Windows paths."""
    python = shutil.which('python.exe')
    if not python:
        print('usb_update: the board is on Windows, but there is no python.exe to reach it')
        return 1

    def windows_path(arg):
        if not (arg.startswith('/') and os.path.exists(arg)):
            return arg
        return subprocess.run(['wslpath', '-w', arg], capture_output=True, text=True).stdout.strip()

    command = [python, windows_path(os.path.abspath(__file__)), port_name, windows_path(path)]
    if fallback:
        # PlatformIO's esptool comes first, then its options (all starting with -)
        options = next(i for i, arg in enumerate(fallback) if arg.startswith('-'))
        command += ['--', python, '-m', 'esptool'] + [windows_path(arg) for arg in fallback[options:]]
    return subprocess.call(command)


def main():
    args = sys.argv[1:]
    fallback = []
    if '--' in args:
        fallback = args[args.index('--') + 1:]
        args = args[:args.index('--')]
    port_name, path = args
    if sys.platform == 'linux' and port_name.upper().startswith('COM'):
        return run_on_windows(port_name, path, fallback)
    if update(port_name, path):
        return 0
    if not fallback:
        return 1
    print('usb_update: uploading with esptool instead')
    return subprocess.call(fallback)


if __name__ == '__main__':
    sys.exit(main())
