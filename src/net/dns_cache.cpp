// lwIP's dns_clear_cache(), always under lwIP's lock (linked in place of the
// real one with -Wl,--wrap=dns_clear_cache, see platformio.ini).
//
// The Arduino core calls it from Network.hostByName() in whichever task does
// the first lookup after the IP state changes, without the lock. Clearing the
// cache finishes every lookup still waiting: it runs their callbacks and closes
// their sockets. Without the lock, lwIP aborts ("Required to lock TCPIP core
// functionality") and the radio restarts. Seen right after joining Wi-Fi, when
// time sync and weather look up names at once; a stream reconnecting after a
// dropout can do the same. Still unlocked in arduino-esp32 master (3.3.11 here).

#include <lwip/priv/tcpip_priv.h>
#include <lwip/tcpip.h>

extern "C" void __real_dns_clear_cache(void);

extern "C" void __wrap_dns_clear_cache(void) {
    const bool held = sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER);
    if (!held) LOCK_TCPIP_CORE();
    __real_dns_clear_cache();
    if (!held) UNLOCK_TCPIP_CORE();
}
