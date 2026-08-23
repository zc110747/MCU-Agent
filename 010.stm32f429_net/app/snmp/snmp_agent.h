/**
  ******************************************************************************
  * @file    snmp_agent.h
  * @brief   SNMP v2c Agent entry point (FreeRTOS task, lwIP netconn UDP).
  ******************************************************************************
  */
#ifndef __SNMP_AGENT_H__
#define __SNMP_AGENT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Default UDP port for SNMP agents (RFC 3411). */
#define SNMP_AGENT_PORT    161

/* Create the SNMP agent task. Call after vTaskStartScheduler() (netconn API
 * requires the tcpip thread). Returns pdPASS/pdFAIL. */
int snmp_agent_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __SNMP_AGENT_H__ */
