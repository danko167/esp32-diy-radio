# PlatformIO extra script: flags only the linker reads. Here rather than in
# build_flags, where changing one would recompile every library.

Import("env")  # noqa: F821 (provided by PlatformIO)

env.Append(LINKFLAGS=[  # noqa: F821
    # The Arduino core clears lwIP's DNS cache without its lock: src/net/dns_cache.cpp
    "-Wl,--wrap=dns_clear_cache",
])
