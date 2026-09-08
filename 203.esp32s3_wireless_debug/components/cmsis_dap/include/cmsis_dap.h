/**
 * @file cmsis_dap.h
 * @brief CMSIS-DAP v1 command processor (SWD + JTAG ports).
 */
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_PACKET_SIZE 64

/* DAP Command IDs (aligned with ARM DAP.h v2.0.0) */
#define ID_DAP_Info                0x00u
#define ID_DAP_HostStatus          0x01u
#define ID_DAP_Connect             0x02u
#define ID_DAP_Disconnect          0x03u
#define ID_DAP_TransferConfigure   0x04u
#define ID_DAP_Transfer            0x05u
#define ID_DAP_TransferBlock       0x06u
#define ID_DAP_TransferAbort       0x07u
#define ID_DAP_WriteABORT          0x08u
#define ID_DAP_Delay               0x09u
#define ID_DAP_ResetTarget         0x0Au
#define ID_DAP_SWJ_Pins            0x10u
#define ID_DAP_SWJ_Clock           0x11u
#define ID_DAP_SWJ_Sequence        0x12u
#define ID_DAP_SWD_Configure       0x13u
#define ID_DAP_JTAG_Sequence       0x14u
#define ID_DAP_JTAG_Configure      0x15u
#define ID_DAP_JTAG_IDCODE         0x16u
#define ID_DAP_JTAG_Transfer       0x17u
#define ID_DAP_JTAG_TransferBlock  0x18u
#define ID_DAP_JTAG_WriteAbort     0x19u
#define ID_DAP_SWD_Sequence        0x1Du
#define ID_DAP_QueueCommands       0x7Eu
#define ID_DAP_ExecuteCommands     0x7Fu
#define ID_DAP_Invalid             0xFFu

#define DAP_OK    0x00u
#define DAP_ERROR 0xFFu

/* Transfer request / response bits */
#define DAP_TRANSFER_APnDP        (1u << 0)
#define DAP_TRANSFER_RnW          (1u << 1)
#define DAP_TRANSFER_A2           (1u << 2)
#define DAP_TRANSFER_A3           (1u << 3)
#define DAP_TRANSFER_MATCH_VALUE  (1u << 4)
#define DAP_TRANSFER_MATCH_MASK   (1u << 5)
#define DAP_TRANSFER_TIMESTAMP    (1u << 7)

#define DAP_TRANSFER_OK        (1u << 0)
#define DAP_TRANSFER_WAIT      (1u << 1)
#define DAP_TRANSFER_FAULT     (1u << 2)
#define DAP_TRANSFER_ERROR     (1u << 3)
#define DAP_TRANSFER_MISMATCH  (1u << 4)

/**
 * @brief Start the DAP task processing requests from @param rx_queue.
 */
esp_err_t cmsis_dap_init(QueueHandle_t rx_queue);

/**
 * @brief Execute one DAP command (or a QueueCommands bundle).
 * @return number of response bytes.
 */
uint32_t cmsis_dap_execute(const uint8_t *request, uint32_t request_len,
                           uint8_t *response);

#ifdef __cplusplus
}
#endif
