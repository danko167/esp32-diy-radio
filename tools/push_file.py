"""Copies files from the computer to the radio's microSD card over the USB
cable, while the radio runs (its dev console's F command), or deletes them (K).

    python tools/push_file.py PORT LOCAL_FILE CARD_PATH [LOCAL_FILE CARD_PATH ...]
    python tools/push_file.py PORT --delete CARD_PATH [CARD_PATH ...]

    python tools/push_file.py COM5 chime.mp3 /sounds/chime.mp3

Folders on the card are made as needed. From Git Bash, put MSYS_NO_PATHCONV=1
in front, or it turns /sounds/... into a Windows path.
"""

import sys

import serial  # pyserial, part of PlatformIO's Python

from screenshot import open_port
from usb_update import ok, send_chunks, wait_for


def ask(port, command, header):
    """Sends the one-letter command, then its header once the radio asks."""
    port.reset_input_buffer()
    port.write(command)  # alone first, as in usb_update.py
    if not wait_for(port, (b'#HEADER',), 2):
        print('push_file: the radio didn\'t answer')
        return False
    port.write(header.encode())
    return True


def push(port, local, remote):
    data = open(local, 'rb').read()
    if not ask(port, b'F', f'{len(data)},{remote}\n'):
        return False
    answer = wait_for(port, (b'#READY', b'#FAIL'), 5)
    if not ok(answer, b'#READY'):
        print(f'push_file: {remote}: {answer or "no answer"}')
        return False
    if not send_chunks(port, data, remote):
        return False
    answer = wait_for(port, (b'#DONE', b'#FAIL'), 10)
    if not ok(answer, b'#DONE'):
        print(f'push_file: {remote}: {answer or "no answer"}')
        return False
    print(f'push_file: {remote}, {len(data) // 1024} KB')
    return True


def delete(port, remote):
    if not ask(port, b'K', f'{remote}\n'):
        return False
    answer = wait_for(port, (b'#DONE', b'#FAIL'), 5)
    print(f'push_file: deleted {remote}' if ok(answer, b'#DONE') else f'push_file: {remote}: {answer or "no answer"}')
    return ok(answer, b'#DONE')


def main():
    port_name, *args = sys.argv[1:] or [None]
    deleting = args[:1] == ['--delete']
    if deleting:
        args = args[1:]
    if not port_name or not args or (not deleting and len(args) % 2):
        sys.exit(__doc__)
    try:
        port = open_port(port_name)
    except serial.SerialException as e:
        sys.exit(f'push_file: can\'t open {port_name}: {e}')
    with port:
        if deleting:
            return 0 if all([delete(port, remote) for remote in args]) else 1
        for local, remote in zip(args[::2], args[1::2]):
            if not push(port, local, remote):
                return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
