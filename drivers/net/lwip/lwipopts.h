/*
 * lwipopts.h - lwIP configuration of net-lwip.ko (the CRTOS kernel's TCP/IP stack).
 *
 * Threads: the tcpip thread runs timers and callbacks; with core locking the socket calls
 * of programs and the frames from the drivers run in their own threads under the core
 * lock, without a context switch. The kernel socket layer uses the lwIP sockets
 * non-blocking only and does the waiting itself (see drivers/net/lwip/crtos_lwip.c).
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 0
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_TCPIP_CORE_LOCKING 1
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1
#define TCPIP_THREAD_NAME "tcpip"
#define TCPIP_THREAD_STACKSIZE 3072
#define TCPIP_THREAD_PRIO 19
#define TCPIP_MBOX_SIZE 32
#define DEFAULT_THREAD_STACKSIZE 2048
#define DEFAULT_THREAD_PRIO 12
#define DEFAULT_RAW_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 16
#define DEFAULT_TCP_RECVMBOX_SIZE 128 /* (a segment each: the whole window and more) */
#define DEFAULT_ACCEPTMBOX_SIZE 8

/* memory: the heap from the kernel, pools static */
#define MEM_CUSTOM_ALLOCATOR 1
#define MEM_ALIGNMENT 4
#define MEMP_MEM_MALLOC 0
#define MEMP_NUM_PBUF 64
#define MEMP_NUM_RAW_PCB 4
#define MEMP_NUM_UDP_PCB 12
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_TCP_SEG 320
#define MEMP_NUM_REASSDATA 4
#define MEMP_NUM_FRAG_PBUF 8
#define MEMP_NUM_ARP_QUEUE 16
#define MEMP_NUM_NETBUF 16
#define MEMP_NUM_NETCONN 32
#define MEMP_NUM_SELECT_CB 4
#define MEMP_NUM_TCPIP_MSG_API 16
#define MEMP_NUM_TCPIP_MSG_INPKT 32
#define MEMP_NUM_NETDB 4
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4)
#define PBUF_POOL_SIZE 128 /* received frames: a window of the socket being read, the ring's */

void *crtos_lwip_malloc(unsigned size);
void crtos_lwip_free(void *p);
void *crtos_lwip_calloc(unsigned n, unsigned size);
#define MEM_CUSTOM_MALLOC crtos_lwip_malloc
#define MEM_CUSTOM_FREE crtos_lwip_free
#define MEM_CUSTOM_CALLOC crtos_lwip_calloc

/* protocols */
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define ARP_TABLE_SIZE 16
#define ARP_QUEUEING 1
#define ETHARP_SUPPORT_STATIC_ENTRIES 0
#define IP_FORWARD 0
#define IP_REASSEMBLY 1
#define IP_FRAG 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DHCP 1
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_ACD 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 1
#define LWIP_DNS 1
#define DNS_MAX_SERVERS 2
#define LWIP_DHCP_MAX_DNS_SERVERS 2

/* Windows of 64 segments (91 KB, scaled: RFC 7323): at 100 Mbit/s a window has to cover a round
 * trip - through a Wi-Fi access point 2-4 ms - and more. With 16 segments (23 KB, 1.9 ms) the
 * sender waited for the window most of the time (50 Mbit/s); 64 KB is the most without scaling. */
#define LWIP_WND_SCALE 1
#define TCP_RCV_SCALE 2
#define TCP_MSS 1460
#define TCP_WND (64 * TCP_MSS)
#define TCP_SND_BUF (64 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * TCP_SND_BUF) / TCP_MSS)
#define TCP_LISTEN_BACKLOG 1
#define LWIP_TCP_KEEPALIVE 1

/* interfaces */
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_SINGLE_NETIF 0
#define LWIP_NETIF_TX_SINGLE_PBUF 1 /* a TCP segment in one piece: no copy to make it contiguous */

/* APIs */
#define LWIP_NETCONN 1
#define LWIP_SOCKET 1
#define LWIP_COMPAT_SOCKETS 0
#define LWIP_POSIX_SOCKETS_IO_NAMES 0
#define LWIP_SOCKET_OFFSET 0
#define LWIP_SOCKET_SELECT 1
#define LWIP_SOCKET_POLL 1
#define LWIP_SO_RCVTIMEO 0 /* time-outs are the kernel's */
#define LWIP_SO_SNDTIMEO 0
#define LWIP_SO_RCVBUF 1
#define SO_REUSE 1
#define LWIP_SO_LINGER 0
#define LWIP_NETCONN_SEM_PER_THREAD 0
#define LWIP_NETCONN_FULLDUPLEX 0

/* checks: in software, unless the interface does them (enet-imxrt: all of them) */
#define LWIP_CHECKSUM_CTRL_PER_NETIF 1
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_TCP 1
#define CHECKSUM_CHECK_ICMP 1

#define LWIP_STATS 0
#define LWIP_DEBUG 0

/* the kernel waits for sockets itself: tell it when one may have changed (sockets.c) */
void crtos_lwip_socket_event(int s);
#define LWIP_HOOK_CRTOS_SOCKET_EVENT(s) crtos_lwip_socket_event(s)

#endif
