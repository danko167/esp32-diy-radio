# PlatformIO extra script: the home folder out of the firmware. The Arduino
# core's log messages carry their source file's full path (__FILE__), and with
# PlatformIO installed under the home folder, every firmware held the name of
# whoever built it ("C:/Users/<name>/.platformio/..."). They read "~/..." now.

import os

Import("env")  # noqa: F821 (provided by PlatformIO)

home = os.path.expanduser("~")
maps = {home, home.replace("\\", "/")}  # the compiler sees either form on Windows
env.Append(CCFLAGS=[f"-fmacro-prefix-map={path}=~" for path in sorted(maps)])  # noqa: F821
