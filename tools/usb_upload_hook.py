# PlatformIO extra script: `pio run -t upload` over the cable first offers the
# firmware to the running radio (tools/usb_update.py), so its screen shows the
# progress. esptool stays as the fallback, for a new or broken board.
#
# On WSL the board's USB port only exists on the Windows side (as COMx, unless
# passed through with usbipd): then the port is looked up there, and
# usb_update.py runs the upload with Windows' Python.

import glob
import os
import re
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)


def windows_board_port():
    """The ESP32-S3's COM port on Windows (Espressif's USB vendor ID), or None."""
    try:
        names = subprocess.run(
            ["powershell.exe", "-NoProfile", "-Command",
             "Get-CimInstance Win32_PnPEntity | ? PNPDeviceID -like 'USB\\VID_303A*' | % Name"],
            capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    match = re.search(r"\((COM\d+)\)", names)
    return match.group(1) if match else None


if (env.GetProjectOption("upload_protocol", "esptool") == "esptool"  # noqa: F821
        and "upload" in COMMAND_LINE_TARGETS  # noqa: F821
        and "uploadfs" not in COMMAND_LINE_TARGETS):  # noqa: F821
    script = os.path.join(env.subst("$PROJECT_DIR"), "tools", "usb_update.py")  # noqa: F821
    env.Replace(  # noqa: F821
        UPLOADCMD='"$PYTHONEXE" "%s" $UPLOAD_PORT $SOURCE -- %s' % (script, env["UPLOADCMD"])  # noqa: F821
    )

    port = env.subst("$UPLOAD_PORT")  # noqa: F821
    if (os.path.exists("/proc/sys/fs/binfmt_misc/WSLInterop") and not port
            and not glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")):
        port = windows_board_port()
        if port:
            print("Found the board on Windows: %s" % port)
            env.Replace(UPLOAD_PORT=port)  # noqa: F821
        else:
            print("The board isn't on WSL's or Windows' USB")
