#ifndef INC_NETCFG_H
#define INC_NETCFG_H
#define NETCFG_IP_LEN   16
#define NETCFG_MAC_LEN  18
typedef struct {
  char ip[NETCFG_IP_LEN];
  char mask[NETCFG_IP_LEN];
  char gw[NETCFG_IP_LEN];
  char mac[NETCFG_MAC_LEN];
} netcfg_t;
#endif
