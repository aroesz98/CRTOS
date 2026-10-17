# Kernel modules (layer 1: drivers). Each ALIAS is a device tree "compatible" string.
# (Example modules: examples/drivers; the display check touchpaint: tests/drivers.)
#
# Modules any board can use are here; the ones of a board's chip in <board>.cmake.

include(${CMAKE_CURRENT_SOURCE_DIR}/${CRTOS_BOARD}.cmake)

crtos_module(flashfs SOURCES fs/flashfs.c ALIASES crtos,flashfs)

# input (on I2C, GPIO, a serial port)
crtos_module(ft5406 SOURCES input/ft5406.c ALIASES focaltech,ft5406)
crtos_module(gt911 SOURCES input/gt911.c ALIASES goodix,gt911)
crtos_module(gpio-keys SOURCES input/gpio-keys.c ALIASES gpio-keys)
crtos_module(esp32-pad SOURCES input/esp32-pad.c ALIASES crtos,esp32-pad)

# the TCP/IP stack: lwIP (IPv4) with its sockets, behind the kernel's socket calls
set(LWIP_DIR ${CRTOS_TOP}/third_party/lwip/src)
set(LWIP_SOURCES
  core/init.c core/def.c core/dns.c core/inet_chksum.c core/ip.c core/mem.c core/memp.c core/netif.c core/pbuf.c
  core/raw.c core/stats.c core/sys.c core/tcp.c core/tcp_in.c core/tcp_out.c core/timeouts.c core/udp.c
  core/ipv4/dhcp.c core/ipv4/etharp.c core/ipv4/icmp.c core/ipv4/igmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c
  core/ipv4/ip4_frag.c
  api/api_lib.c api/api_msg.c api/err.c api/netbuf.c api/netdb.c api/sockets.c api/tcpip.c
  netif/ethernet.c)
list(TRANSFORM LWIP_SOURCES PREPEND ${LWIP_DIR}/)
crtos_module(net-lwip SOURCES net/lwip/crtos_lwip.c net/lwip/lwip_side.c net/lwip/sys_arch.c ${LWIP_SOURCES}
             INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/net/lwip ${LWIP_DIR}/include
             ALIASES crtos,lwip)

# checks: sh /sd/crtos/share/flashfs/flashtest.sh
crtos_file(${CMAKE_CURRENT_SOURCE_DIR}/fs/test/flashtest.sh share/flashfs/flashtest.sh)
