/*
 * lwipopts.h - LwIP configuration for STM32F429 + LAN8720.
 *
 * Built WITHOUT an operating system (NO_SYS = 1): the raw API is driven
 * from the main loop. This avoids any cmsis_os / RTOS dependency.
 */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* ------------------------------------------------------------------ */
/* Core: no operating system                                          */
/* ------------------------------------------------------------------ */
#define NO_SYS                      1

/* Sequential / socket API off (we use the raw TCP API directly). */
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_DNS                    0

/* The pbuf/memp pool is allocated from the ETH RX interrupt and freed
 * from the main loop, so it needs a real critical section. We provide
 * SYS_ARCH_PROTECT / SYS_ARCH_UNPROTECT as inline-asm macros here, which
 * saves/restores PRIMASK (so interrupts are only re-enabled if they were
 * enabled before, keeping the ETH ISR atomic). */
#define SYS_LIGHTWEIGHT_PROT        1
#define SYS_ARCH_DECL_PROTECT(lev)  sys_prot_t lev
#define SYS_ARCH_PROTECT(lev) do {               \
    unsigned long __p;                           \
    __asm__ volatile ("mrs %0, PRIMASK" : "=r" (__p)); \
    __asm__ volatile ("cpsid i" ::: "memory");   \
    (lev) = (sys_prot_t)__p;                     \
  } while (0)
#define SYS_ARCH_UNPROTECT(lev) do {            \
    if (!((unsigned long)(lev) & 1UL))          \
      __asm__ volatile ("cpsie i" ::: "memory"); \
  } while (0)

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
/* Memory                                                            */
/* ------------------------------------------------------------------ */
#define MEM_SIZE                    (16 * 1024)
#define MEMP_NUM_PBUF               32
#define MEMP_NUM_RAW_PCB            1
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            5
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            24
#define MEMP_NUM_REASSDATA          5
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_SYS_TIMEOUT        16
#define MEMP_NUM_NETBUF             1
#define MEMP_NUM_NETCONN            1
#define MEMP_NUM_TCPIP_MSG_API      1
#define MEMP_NUM_TCPIP_MSG_INPKT    1

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
#define TCP_WND                     (2 * TCP_MSS)
#define TCP_MAXRTX                  12
#define TCP_SYNMAXRTX               6
#define TCP_MSS                     1460
#define TCP_CALCULATE_EFF_SEND_MSS  1
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN           (4 * (TCP_SND_BUF / TCP_MSS))
#define TCP_LISTEN_BACKLOG          1
#define TCP_DEFAULT_LISTEN_BACKLOG  5
#define TCP_MSL                     10000UL
#define TCP_TMR_INTERVAL            1

/* PBUF */
#define PBUF_LINK_HLEN              14
#define PBUF_POOL_BUFSIZE          256
#define PBUF_POOL_SIZE             32
#define ETH_PAD_SIZE               0

#endif /* __LWIPOPTS_H__ */
