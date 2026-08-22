/*
 * lwipopts.h - LwIP configuration for STM32F429 + LAN8720 + FreeRTOS.
 *
 * Built WITH an OS (NO_SYS = 0): tcpip_thread runs the core stack, the
 * Ethernet input is fed via tcpip_input() from the ETH IRQ, and the HTTP
 * server uses the netconn API in its own task.  sys_arch.c (FreeRTOS)
 * provides the OS abstraction.  LwIP heap + memp pools live in SDRAM
 * (section .lwip_memp_pool, see linker script + mem.c/memp.c patches).
 */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* ------------------------------------------------------------------ */
/* Core: FreeRTOS multi-threaded                                      */
/* ------------------------------------------------------------------ */
#define NO_SYS                      0

/* Sequential API (netconn) on; socket API off (netconn is lighter). */
#define LWIP_NETCONN                1
#define LWIP_SOCKET                 0
#define LWIP_DNS                    0

/* Enable recv/send timeouts on netconn (needed for telnet idle-disconnect
 * polling). These add 4 B + 4 B per netconn struct — negligible. */
#define LWIP_SO_RCVTIMEO            1
#define LWIP_SO_SNDTIMEO            1

/* The pbuf/memp pools are touched from both the ETH IRQ and the
 * tcpip_thread, so real critical sections are needed.  Provided by
 * sys_arch.h/sys_arch.c (FreeRTOS vPortEnterCritical). */
#define SYS_LIGHTWEIGHT_PROT        1

/* ------------------------------------------------------------------ */
/* Platform / processor                                               */
/* ------------------------------------------------------------------ */
#define MEM_ALIGNMENT               4
#define LWIP_PLATFORM_BYTESWAP      0

/* ------------------------------------------------------------------ */
/* Checksums                                                          */
/* ------------------------------------------------------------------ */
/* ETH_USE_HW_CHECKSUM selects who computes the L3/L4 checksums.
 *
 *   0 = software (lwIP).  DEFAULT.  Correct in every case.
 *   1 = STM32 ETH MAC checksum offload.  Saves a little CPU, but
 *       *** BREAKS OUTGOING IP FRAGMENTATION ***.
 *
 * Why HW offload breaks fragments (measured on this board, not theory):
 * the MAC inserts the TCP/UDP/ICMP checksum by scanning the frame it is
 * given.  When lwIP fragments a datagram, each frame holds only a slice
 * of the L4 payload, so the MAC checksums the slice instead of the whole
 * datagram and emits a corrupt value.  The peer then silently drops the
 * reply.  Observed symptom was a hard cliff at the fragmentation
 * boundary: `ping -l 1472` replied, `ping -l 1473` timed out 100%.
 * With software checksums, 1473 / 2000 / 4000 / 8000 B all reply.
 *
 * If you flip this to 1, also set TxConfig.ChecksumCtrl in
 * app/lwip/ethernetif.c back to ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC.
 * The two settings MUST agree: if lwIP computes a checksum *and* the MAC
 * inserts one, the MAC overwrites lwIP's correct value and everything
 * breaks -- including non-fragmented traffic.
 *
 * Note this cannot be made per-packet: CHECKSUM_GEN_* are compile-time
 * switches, so lwIP cannot "only compute checksums for fragments".
 */
#define ETH_USE_HW_CHECKSUM         0

#if ETH_USE_HW_CHECKSUM
#define CHECKSUM_BY_HARDWARE        1
#define CHECKSUM_GEN_IP             0
#define CHECKSUM_GEN_UDP            0
#define CHECKSUM_GEN_TCP            0
#define CHECKSUM_GEN_ICMP           0
#define CHECKSUM_CHECK_IP           0
#define CHECKSUM_CHECK_UDP          0
#define CHECKSUM_CHECK_TCP          0
#define CHECKSUM_CHECK_ICMP         0
#else
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1
#endif

/* ------------------------------------------------------------------ */
/* Feature selection                                                  */
/* ------------------------------------------------------------------ */
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   0
#define LWIP_AUTOIP                 0
#define LWIP_IGMP                   0
#define LWIP_BROADCAST_PING         0
#define LWIP_MULTICAST_PING         0
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_STATS                  0
#define LWIP_DBG_MIN_LEVEL          LWIP_DBG_LEVEL_SERIOUS

/* ------------------------------------------------------------------ */
/* Memory (all LwIP heap/memp pools live in SDRAM, see mem.c/memp.c    */
/* section patches + linker script)                                    */
/* ------------------------------------------------------------------ */
#define MEM_SIZE                    (128 * 1024)
#define MEMP_NUM_PBUF               64
#define MEMP_NUM_RAW_PCB            1
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            12
#define MEMP_NUM_TCP_PCB_LISTEN     6
#define MEMP_NUM_TCP_SEG            40
#define MEMP_NUM_REASSDATA          5
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_SYS_TIMEOUT        24
#define MEMP_NUM_NETBUF             8
#define MEMP_NUM_NETCONN            12
#define MEMP_NUM_TCPIP_MSG_API      8
#define MEMP_NUM_TCPIP_MSG_INPKT    8

/* ARP */
#define ARP_TABLE_SIZE              10
#define ARP_QUEUEING                1
#define ETHARP_TRUST_IP_MAC         0

/* IP
 * IP_FRAG_USES_STATIC_BUF / IP_FRAG_MAX_MTU used to live here.  Both are
 * lwIP 1.4 options that were removed in 2.x -- they were dead config and
 * only made it look like fragmentation had been tuned.  Removed.
 * The options that actually matter are set explicitly below rather than
 * left to opt.h defaults.
 */
#define IP_FORWARD                  0
#define IP_DEFAULT_TTL              64
#define IP_REASSEMBLY               1
#define IP_FRAG                     1
/* Fragments are held as zero-copy PBUF_REF pbufs pointing into the ETH DMA
 * buffers, so this also bounds how many RX buffers reassembly can pin.
 * RX_POOL holds 20 buffers; 16 leaves headroom for in-flight traffic and
 * covers a 8 KB datagram (6 fragments) with room to spare. */
#define IP_REASS_MAX_PBUFS          16
#define IP_REASS_MAXAGE             3

/* ICMP */
#define ICMP_TTL                    (IP_DEFAULT_TTL)

/* TCP */
#define TCP_TTL                     (IP_DEFAULT_TTL)
#define TCP_WND                     (6 * TCP_MSS)
#define TCP_MAXRTX                  12
#define TCP_SYNMAXRTX               6
#define TCP_MSS                     1460
#define TCP_CALCULATE_EFF_SEND_MSS  1
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN           (4 * (TCP_SND_BUF / TCP_MSS))
#define TCP_LISTEN_BACKLOG          1
#define TCP_DEFAULT_LISTEN_BACKLOG  5
#define TCP_MSL                     10000UL
#define TCP_TMR_INTERVAL            1

/* PBUF */
#define PBUF_LINK_HLEN              14
#define PBUF_POOL_BUFSIZE          256
#define PBUF_POOL_SIZE             96
#define ETH_PAD_SIZE               0

/* ------------------------------------------------------------------ */
/* tcpip_thread / netconn task sizes (units: stack words)             */
/* ------------------------------------------------------------------ */
#define TCPIP_THREAD_STACKSIZE      1024
#define TCPIP_THREAD_PRIO           3
#define TCPIP_MBOX_SIZE             16    /* tcpip_thread message queue */
#define DEFAULT_THREAD_STACKSIZE    1024
#define DEFAULT_THREAD_PRIO         3
#define DEFAULT_ACCEPTMBOX_SIZE     4
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_RAW_RECVMBOX_SIZE   8

#endif /* __LWIPOPTS_H__ */
