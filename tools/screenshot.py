"""Saves what the radio's screen shows as PNGs, over the USB serial port.

    python tools/screenshot.py COM5 [steps...]

Each step is either a dev console command (see devConsole() in src/main.cpp)
or a file name ending in .png, which saves a screenshot at that point:

    python tools/screenshot.py COM5 a i now.png T90,427 stations.png

wakes the radio, opens Internet Radio, saves it, taps at x=90 y=427 and saves
again. Commands get 1.5 s to take effect. Only the standard library is used.
"""

import struct
import sys
import time
import zlib

import serial  # pyserial, part of PlatformIO's Python


def write_png(path, width, height, rgb565):
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # no filter
        line = rgb565[y * width * 2:(y + 1) * width * 2]
        for (v,) in struct.iter_unpack('<H', line):
            rows += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(rows), 6)))
        f.write(chunk(b'IEND', b''))


def open_port(name):
    # Set the control lines before opening: toggling them resets the board
    port = serial.Serial()
    port.port = name
    port.baudrate = 115200
    port.timeout = 0.2
    port.dtr = False
    port.rts = False
    port.open()
    # If it reset anyway, wait until it has booted
    boot = b''
    quiet_since = time.time()
    while time.time() - quiet_since < 1:
        data = port.read(4096)
        if data:
            boot += data
            quiet_since = time.time()
        if b'setup done' in boot:
            time.sleep(2)
            break
    return port


def screenshot(port, path):
    port.reset_input_buffer()
    port.write(b'v')
    buf = b''
    deadline = time.time() + 20
    while b'SCREENSHOT ' not in buf or b'\n' not in buf[buf.find(b'SCREENSHOT '):]:
        buf += port.read(4096)
        if time.time() > deadline:
            sys.exit('no screenshot from the board')
    start = buf.find(b'SCREENSHOT ')
    header_end = buf.index(b'\n', start)
    width, height = map(int, buf[start + 11:header_end].split())
    data = buf[header_end + 1:]
    size = width * height * 2
    while len(data) < size:
        data += port.read(size - len(data))
        if time.time() > deadline:
            sys.exit('screenshot cut short')
    write_png(path, width, height, data[:size])
    print(f'saved {path}')


def main():
    port = open_port(sys.argv[1])
    for step in sys.argv[2:]:
        if step.endswith('.png'):
            screenshot(port, step)
        else:
            port.write(step.encode() + (b'\n' if step[0] in 'THS' else b''))
            time.sleep(1.5)


if __name__ == '__main__':
    main()
