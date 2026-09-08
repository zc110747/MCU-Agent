/**
 * @file cmsis_dap.c
 * @brief CMSIS-DAP v1 protocol layer (SWD port).
 *
 * Command semantics mirror ARM's official DAP.c (v2.0.0), SWD-only.
 * Layering: DAP commands never touch SWD GPIOs directly - raw DP/AP
 * transfers go through the swd engine, target-level operations (reset)
 * through the debug engine, all under the debug engine mutex.
 */
#include "cmsis_dap.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "usb_device.h"
#include "swd.h"
#include "jtag.h"
#include "debug_engine.h"

static const char *TAG = "dap";

/* ------------------------------------------------------------------ */
/* DAP data                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t debug_port;         /* 0 disabled, 1 SWD, 2 JTAG */
    uint8_t jtag_index;        /* selected TAP in the JTAG chain */
    struct {
        uint8_t  idle_cycles;
        uint16_t retry_count;
        uint16_t match_retry;
        uint32_t match_mask;
    } transfer;
} dap_data_t;

static dap_data_t s_dap = {
    .debug_port = 0,
    .transfer = {
        .idle_cycles = 0,
        .retry_count = 100,
        .match_retry = 0,
        .match_mask = 0x00000000u,
    },
};

static volatile uint8_t s_transfer_abort = 0;

#define DAP_PORT_DISABLED 0u
#define DAP_PORT_SWD      1u
#define DAP_PORT_JTAG     2u

static inline uint8_t swd_retry(uint8_t request, uint32_t *data)
{
    uint8_t ack;
    uint32_t retry = s_dap.transfer.retry_count;
    do {
        ack = swd_transfer(request, data);
    } while (ack == SWD_ACK_WAIT && retry-- && !s_transfer_abort);
    return ack;
}

/* ------------------------------------------------------------------ */
/* DAP_Info                                                            */
/* ------------------------------------------------------------------ */
static const char DAP_FW_VER[] = "1.2.0";

static uint8_t dap_info(uint8_t id, uint8_t *info)
{
    uint8_t length = 0;
    /* InfoType keys are 1-based, matching ARM CMSIS-DAP DAP.h:
     * 1=Vendor 2=Product 3=Serial 4=FW Ver 0xF0=Caps 0xFE=PktCount 0xFF=PktSize */
    switch (id) {
    case 1: {   /* Vendor ID string (DAP_ID_VENDOR) */
        const char *v = CONFIG_DEBUG_PROBE_USB_MANUFACTURER;
        length = (uint8_t)strlen(v);
        memcpy(info, v, length);
        break;
    }
    case 2: {   /* Product ID string (DAP_ID_PRODUCT) */
        const char *v = CONFIG_DEBUG_PROBE_USB_PRODUCT;
        length = (uint8_t)strlen(v);
        memcpy(info, v, length);
        break;
    }
    case 3: {   /* Serial number string (eFuse MAC) (DAP_ID_SER_NUM) */
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        length = (uint8_t)snprintf((char *)info, DAP_PACKET_SIZE - 2,
                                   "%02X%02X%02X%02X%02X%02X",
                                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        break;
    }
    case 4:     /* CMSIS-DAP Firmware Version string (DAP_ID_FW_VER) */
        length = (uint8_t)sizeof(DAP_FW_VER) - 1u;
        memcpy(info, DAP_FW_VER, length);
        break;
    case 0xF0:  /* Capabilities: SWD | JTAG | atomic commands */
        info[0] = (1u << 0) | (1u << 1) | (1u << 4);
        length = 1;
        break;
    case 0xFF:  /* Packet size */
        info[0] = (uint8_t)(DAP_PACKET_SIZE >> 0);
        info[1] = (uint8_t)(DAP_PACKET_SIZE >> 8);
        length = 2;
        break;
    case 0xFE:  /* Packet count */
        info[0] = 1;    /* HID v1: single report buffer */
        length = 1;
        break;
    default:
        break;
    }
    return length;
}

/* ------------------------------------------------------------------ */
/* Simple commands                                                     */
/* ------------------------------------------------------------------ */
static uint32_t dap_host_status(const uint8_t *request, uint8_t *response)
{
    /* No discrete status LED in V1: log the state change */
    ESP_LOGD(TAG, "HostStatus type=%u value=%u", request[0], request[1]);
    response[0] = DAP_OK;
    return (2u << 16) | 1u;
}

static uint32_t dap_connect(const uint8_t *request, uint8_t *response)
{
    uint8_t port = request[0];
    if (port == 0 || port == DAP_PORT_SWD) {
        /* SWD bring-up: nRESET pulse -> line reset -> JTAG->SWD switch ->
         * line reset -> read DPIDR. Conservative clock for flying-wire; the
         * host re-programs speed via SWJ_Clock. */
        s_dap.debug_port = DAP_PORT_SWD;
        swd_set_idle();
        swd_set_clock(100000u);
        esp_err_t c = swd_connect();
        ESP_LOGI(TAG, "Connect: SWD port, swd_connect=%s",
                 c == ESP_OK ? "OK" : "FAIL");
        response[0] = (uint8_t)DAP_PORT_SWD;
        return (1u << 16) | 1u;
    }
    if (port == DAP_PORT_JTAG) {
        /* Pure JTAG bring-up (no SWD<->JTAG switching): nRESET pulse ->
         * JTAG line reset (Test-Logic-Reset) -> read IDCODE as self-test.
         * Conservative clock for flying-wire; host re-programs via SWJ_Clock. */
        s_dap.debug_port = DAP_PORT_JTAG;
        s_dap.jtag_index = 0;
        jtag_set_idle();
        jtag_set_clock(100000u);
        esp_err_t c = jtag_connect();
        ESP_LOGI(TAG, "Connect: JTAG port, jtag_connect=%s",
                 c == ESP_OK ? "OK" : "FAIL");
        response[0] = (uint8_t)DAP_PORT_JTAG;
        return (1u << 16) | 1u;
    }
    /* Unsupported port */
    s_dap.debug_port = DAP_PORT_DISABLED;
    response[0] = (uint8_t)DAP_PORT_DISABLED;
    return (1u << 16) | 1u;
}

static uint32_t dap_disconnect(uint8_t *response)
{
    s_dap.debug_port = DAP_PORT_DISABLED;
    swd_set_idle();
    jtag_set_idle();
    response[0] = DAP_OK;
    return 1u;
}

static uint32_t dap_delay(const uint8_t *request, uint8_t *response)
{
    uint32_t delay = (uint32_t)request[0] | ((uint32_t)request[1] << 8);
    esp_rom_delay_us(delay);
    response[0] = DAP_OK;
    return (2u << 16) | 1u;
}

static uint32_t dap_reset_target(uint8_t *response)
{
    esp_err_t err = debug_reset(false);
    response[0] = DAP_OK;
    response[1] = (err == ESP_OK) ? 0x00u : 0x01u;
    return 2u;
}

static uint32_t dap_swj_pins(const uint8_t *request, uint8_t *response)
{
    uint32_t value = request[0];
    uint32_t select = request[1];
    uint32_t wait = (uint32_t)request[2] | ((uint32_t)request[3] << 8) |
                    ((uint32_t)request[4] << 16) | ((uint32_t)request[5] << 24);

    /* SWCLK/TCK (bit0), SWDIO/TMS (bit1), TDI (bit2), nTRST (bit5), nRESET (bit7) */
    if (select & (1u << 0)) { swd_pin_swclk((value >> 0) & 1u); }       /* SWCLK / TCK */
    if (select & (1u << 1)) { swd_pin_swdio((value >> 1) & 1u); }       /* SWDIO / TMS */
    if (select & (1u << 2)) { jtag_pin_tdi((value >> 2) & 1u); }        /* TDI */
    if (select & (1u << 5)) { jtag_pin_ntrst_assert(((value >> 5) & 1u) == 0); } /* nTRST, active low */
    if (select & (1u << 7)) { swd_reset_assert(((value >> 7) & 1u) == 0); }       /* nRESET, active low */

    if (wait != 0) {
        if (wait > 3000000u) {
            wait = 3000000u;
        }
        uint32_t wait_us = wait;
        uint8_t swdio_sel = (select >> 1) & 1u;
        uint8_t swdio_val = (value >> 1) & 1u;
        while (wait_us--) {
            if (swdio_sel && (swd_pin_swdio_in() != swdio_val)) {
                continue;
            }
            break;
        }
    }

    uint32_t in = (uint32_t)swd_pin_swclk_in() |
                  ((uint32_t)swd_pin_swdio_in() << 1) |
                  ((uint32_t)jtag_pin_tdi_in() << 2) |
                  ((uint32_t)jtag_pin_tdo_in() << 3) |
                  ((uint32_t)(jtag_ntrst_read() ? 0u : 1u) << 5) |
                  ((uint32_t)(swd_nreset_read() ? 0u : 1u) << 7);
    response[0] = (uint8_t)in;
    return (6u << 16) | 1u;
}

static uint32_t dap_swj_clock(const uint8_t *request, uint8_t *response)
{
    uint32_t clock = (uint32_t)request[0] | ((uint32_t)request[1] << 8) |
                     ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 24);
    if (clock == 0) {
        response[0] = DAP_ERROR;
        return (4u << 16) | 1u;
    }
    swd_set_clock(clock);
    jtag_set_clock(clock);
    ESP_LOGD(TAG, "SWJ clock -> %u Hz", (unsigned)clock);
    response[0] = DAP_OK;
    return (4u << 16) | 1u;
}

static uint32_t dap_swj_sequence(const uint8_t *request, uint8_t *response)
{
    uint32_t count = request[0];
    if (count == 0) {
        count = 256;
    }
    swd_swdio_output(true);   /* ensure SWDIO is driven during the sequence */
    swd_swj_sequence(count, request + 1);
    response[0] = DAP_OK;
    count = (count + 7) >> 3;
    return ((count + 1) << 16) | 1u;
}

static uint32_t dap_swd_configure(const uint8_t *request, uint8_t *response)
{
    uint8_t value = request[0];
    swd_set_turnaround((value & 0x03u) + 1u);
    swd_set_data_phase((value & 0x04u) != 0);
    response[0] = DAP_OK;
    return (1u << 16) | 1u;
}

static uint32_t dap_swd_sequence(const uint8_t *request, uint8_t *response)
{
    uint32_t sequence_count;
    uint32_t request_count = 1;
    uint32_t response_count = 1;

    response[0] = DAP_OK;
    uint8_t *resp = response + 1;

    sequence_count = request[0];
    request++;
    while (sequence_count--) {
        uint32_t info = request[0];
        request++;
        request_count++;
        uint32_t nbits = info & 0x3Fu;
        if (nbits == 0) {
            nbits = 64;
        }
        uint32_t nbytes = (nbits + 7) / 8;
        if (info & 0x80u) {         /* capture SWDIO */
            swd_swdio_output(false);
            swd_sequence_in(nbits, resp);
            swd_swdio_output(true);
            resp += nbytes;
            response_count += nbytes;
        } else {                    /* drive SWDIO */
            swd_swdio_output(true);
            swd_sequence_out(nbits, request);
            request += nbytes;
            request_count += nbytes;
        }
    }
    swd_swdio_output(true);
    return (request_count << 16) | response_count;
}

static uint32_t dap_write_abort(const uint8_t *request, uint8_t *response)
{
    uint32_t data = (uint32_t)request[1] | ((uint32_t)request[2] << 8) |
                    ((uint32_t)request[3] << 16) | ((uint32_t)request[4] << 24);
    if (s_dap.debug_port == DAP_PORT_JTAG) {
        jtag_write_abort(data);
    } else {
        swd_transfer(SWD_REQ(SWD_DP_ADDR_ABORT, 0u, 0u), &data);
    }
    response[0] = DAP_OK;
    return (5u << 16) | 1u;
}

static uint32_t dap_transfer_configure(const uint8_t *request, uint8_t *response)
{
    s_dap.transfer.idle_cycles = request[0];
    s_dap.transfer.retry_count = (uint16_t)(request[1] | (request[2] << 8));
    s_dap.transfer.match_retry = (uint16_t)(request[3] | (request[4] << 8));
    swd_set_idle_cycles(s_dap.transfer.idle_cycles);
    jtag_set_idle_cycles(s_dap.transfer.idle_cycles);
    response[0] = DAP_OK;
    return (5u << 16) | 1u;
}

/* ------------------------------------------------------------------ */
/* DAP_Transfer (port of ARM DAP_SWD_Transfer)                         */
/* ------------------------------------------------------------------ */
static uint32_t dap_swd_transfer(const uint8_t *request, uint8_t *response)
{
    const uint8_t *request_head = request;
    uint32_t request_count;
    uint32_t request_value;
    uint8_t *response_head = response;
    uint32_t response_count = 0;
    uint32_t response_value = 0;
    uint32_t post_read = 0;
    uint32_t check_write = 0;
    uint32_t match_value;
    uint32_t match_retry;
    uint32_t data;

    response += 2;
    s_transfer_abort = 0;

    request++;              /* ignore DAP index */
    request_count = request[0];
    request++;

    for (; request_count != 0; request_count--) {
        request_value = request[0];
        request++;

        if (request_value & DAP_TRANSFER_RnW) {
            /* Read register */
            if (post_read) {
                if ((request_value & (DAP_TRANSFER_APnDP | DAP_TRANSFER_MATCH_VALUE)) == DAP_TRANSFER_APnDP) {
                    /* Read previous AP data and post next AP read */
                    response_value = swd_retry(request_value, &data);
                } else {
                    response_value = swd_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), &data);
                    post_read = 0;
                }
                if (response_value != DAP_TRANSFER_OK) {
                    break;
                }
                response[0] = (uint8_t)data;
                response[1] = (uint8_t)(data >> 8);
                response[2] = (uint8_t)(data >> 16);
                response[3] = (uint8_t)(data >> 24);
                response += 4;
            }
            if (request_value & DAP_TRANSFER_MATCH_VALUE) {
                match_value = (uint32_t)request[0] | ((uint32_t)request[1] << 8) |
                              ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 24);
                request += 4;
                match_retry = s_dap.transfer.match_retry;
                if (request_value & DAP_TRANSFER_APnDP) {
                    response_value = swd_retry(request_value, NULL);
                    if (response_value != DAP_TRANSFER_OK) {
                        break;
                    }
                }
                do {
                    response_value = swd_retry(request_value, &data);
                    if (response_value != DAP_TRANSFER_OK) {
                        break;
                    }
                } while (((data & s_dap.transfer.match_mask) != match_value) && match_retry-- && !s_transfer_abort);
                if ((data & s_dap.transfer.match_mask) != match_value) {
                    response_value |= DAP_TRANSFER_MISMATCH;
                }
                if (response_value != DAP_TRANSFER_OK) {
                    break;
                }
            } else {
                if (request_value & DAP_TRANSFER_APnDP) {
                    if (post_read == 0) {
                        response_value = swd_retry(request_value, NULL);
                        if (response_value != DAP_TRANSFER_OK) {
                            break;
                        }
                        post_read = 1;
                    }
                } else {
                    response_value = swd_retry(request_value, &data);
                    if (response_value != DAP_TRANSFER_OK) {
                        break;
                    }
                    response[0] = (uint8_t)data;
                    response[1] = (uint8_t)(data >> 8);
                    response[2] = (uint8_t)(data >> 16);
                    response[3] = (uint8_t)(data >> 24);
                    response += 4;
                }
            }
            check_write = 0;
        } else {
            /* Write register */
            if (post_read) {
                response_value = swd_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), &data);
                if (response_value != DAP_TRANSFER_OK) {
                    break;
                }
                response[0] = (uint8_t)data;
                response[1] = (uint8_t)(data >> 8);
                response[2] = (uint8_t)(data >> 16);
                response[3] = (uint8_t)(data >> 24);
                response += 4;
                post_read = 0;
            }
            data = (uint32_t)request[0] | ((uint32_t)request[1] << 8) |
                   ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 24);
            request += 4;
            if (request_value & DAP_TRANSFER_MATCH_MASK) {
                s_dap.transfer.match_mask = data;
                response_value = DAP_TRANSFER_OK;
            } else {
                response_value = swd_retry(request_value, &data);
                if (response_value != DAP_TRANSFER_OK) {
                    break;
                }
                check_write = 1;
            }
        }
        response_count++;
        if (s_transfer_abort) {
            break;
        }
    }

    for (; request_count != 0; request_count--) {
        request_value = request[0];
        request++;
        if (request_value & DAP_TRANSFER_RnW) {
            if (request_value & DAP_TRANSFER_MATCH_VALUE) {
                request += 4;
            }
        } else {
            request += 4;
        }
    }

    if (response_value == DAP_TRANSFER_OK) {
        if (post_read) {
            response_value = swd_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), &data);
            if (response_value != DAP_TRANSFER_OK) {
                goto end;
            }
            response[0] = (uint8_t)data;
            response[1] = (uint8_t)(data >> 8);
            response[2] = (uint8_t)(data >> 16);
            response[3] = (uint8_t)(data >> 24);
            response += 4;
        } else if (check_write) {
            response_value = swd_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), NULL);
        }
    }

end:
    response_head[0] = (uint8_t)response_count;
    response_head[1] = (uint8_t)response_value;
    return (((uint32_t)(request - request_head) << 16) | (uint32_t)(response - response_head));
}

/* ------------------------------------------------------------------ */
/* DAP_TransferBlock (port of ARM DAP_SWD_TransferBlock)               */
/* ------------------------------------------------------------------ */
static uint32_t dap_swd_transfer_block(const uint8_t *request, uint8_t *response)
{
    uint32_t request_count;
    uint32_t request_value;
    uint32_t response_count = 0;
    uint32_t response_value = 0;
    uint8_t *response_head = response;
    uint32_t data;

    response += 3;
    s_transfer_abort = 0;

    request++;              /* ignore DAP index */

    request_count = (uint32_t)request[0] | ((uint32_t)request[1] << 8);
    request += 2;
    if (request_count == 0) {
        goto end;
    }

    request_value = request[0];
    request++;

    if (request_value & DAP_TRANSFER_RnW) {
        /* Read register block */
        if (request_value & DAP_TRANSFER_APnDP) {
            response_value = swd_retry(request_value, NULL);
            if (response_value != DAP_TRANSFER_OK) {
                goto end;
            }
        }
        while (request_count--) {
            if (request_count == 0 && (request_value & DAP_TRANSFER_APnDP)) {
                request_value = SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u);
            }
            response_value = swd_retry(request_value, &data);
            if (response_value != DAP_TRANSFER_OK) {
                goto end;
            }
            response[0] = (uint8_t)data;
            response[1] = (uint8_t)(data >> 8);
            response[2] = (uint8_t)(data >> 16);
            response[3] = (uint8_t)(data >> 24);
            response += 4;
            response_count++;
        }
    } else {
        /* Write register block */
        while (request_count--) {
            data = (uint32_t)request[0] | ((uint32_t)request[1] << 8) |
                   ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 24);
            request += 4;
            response_value = swd_retry(request_value, &data);
            if (response_value != DAP_TRANSFER_OK) {
                goto end;
            }
            response_count++;
        }
        /* Check last write */
        response_value = swd_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), NULL);
    }

end:
    response_head[0] = (uint8_t)(response_count >> 0);
    response_head[1] = (uint8_t)(response_count >> 8);
    response_head[2] = (uint8_t)response_value;
    return (uint32_t)(response - response_head);
}

/* DAP_JTAG_TransferBlock (0x18) - defined below; forward declaration for
 * the JTAG branch of dap_transfer_block() */
static uint32_t dap_jtag_transfer_block(const uint8_t *request, uint8_t *response);

static uint32_t dap_transfer_block(const uint8_t *request, uint8_t *response)
{
    uint32_t num;
    if (s_dap.debug_port == DAP_PORT_SWD) {
        num = dap_swd_transfer_block(request, response);
    } else if (s_dap.debug_port == DAP_PORT_JTAG) {
        num = dap_jtag_transfer_block(request, response);
    } else {
        response[0] = 0;
        response[1] = 0;
        response[2] = 0;
        num = 3;
    }
    if (request[3] & DAP_TRANSFER_RnW) {
        num |= 4u << 16;
    } else {
        num |= (4u + (((uint32_t)request[1] | ((uint32_t)request[2] << 8)) * 4)) << 16;
    }
    return num;
}

/* ------------------------------------------------------------------ */
/* JTAG helpers + command handlers (port of ARM DAP.c JTAG_* funcs)    */
/* ------------------------------------------------------------------ */
static inline uint8_t jtag_retry(uint32_t request, uint32_t *data)
{
    uint8_t ack;
    uint32_t retry = s_dap.transfer.retry_count;
    do {
        ack = jtag_transfer(request, data);
    } while (ack == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
    return ack;
}

/* DAP_JTAG_Sequence (0x14) */
static uint32_t dap_jtag_sequence(const uint8_t *request, uint8_t *response)
{
    uint32_t sequence_count;
    uint32_t request_count = 1u;
    uint32_t response_count = 1u;
    uint32_t count;

    *response++ = DAP_OK;            /* status byte; advance past it */
    /* NOTE: dispatcher already passes request+1 (past the command ID),
     * so the first byte here is sequence_count. Do NOT skip again. */
    sequence_count = *request++;
    while (sequence_count--) {
        uint32_t sequence_info = *request++;
        count = sequence_info & 0x3Fu;
        if (count == 0u) {
            count = 64u;
        }
        count = (count + 7u) / 8u;
        jtag_sequence(sequence_info, request, response);
        request += count;
        request_count += count + 1u;
        if (sequence_info & 0x80u) {     /* JTAG_SEQUENCE_TDO */
            response += count;
            response_count += count;
        }
    }
    return ((request_count << 16) | response_count);
}

/* DAP_JTAG_Configure (0x15) */
static uint32_t dap_jtag_configure(const uint8_t *request, uint8_t *response)
{
    uint32_t count = *request++;
    jtag_configure((uint8_t)count, request);
    *response = DAP_OK;
    return (((count + 1u) << 16) | 1u);
}

/* DAP_JTAG_IDCODE (0x16) */
static uint32_t dap_jtag_idcode(const uint8_t *request, uint8_t *response)
{
    uint32_t data;
    uint8_t index = *request;

    if (s_dap.debug_port != DAP_PORT_JTAG) {
        goto id_error;
    }
    if (index >= jtag_get_count()) {
        goto id_error;
    }
    jtag_set_device_index(index);
    jtag_ir(JTAG_IR_IDCODE);
    data = jtag_read_idcode();
    response[0] = DAP_OK;
    response[1] = (uint8_t)(data >> 0);
    response[2] = (uint8_t)(data >> 8);
    response[3] = (uint8_t)(data >> 16);
    response[4] = (uint8_t)(data >> 24);
    return ((1u << 16) | 5u);

id_error:
    *response = DAP_ERROR;
    return ((1u << 16) | 1u);
}

/* DAP_JTAG_Transfer (0x17) - port of ARM DAP_JTAG_Transfer */
static uint32_t dap_jtag_transfer(const uint8_t *request, uint8_t *response)
{
    const uint8_t *request_head = request;
    uint32_t request_count;
    uint32_t request_value;
    uint32_t request_ir;
    uint8_t *response_head = response;
    uint32_t response_count = 0u;
    uint32_t response_value = 0u;
    uint32_t post_read = 0u;
    uint32_t match_value, match_retry, retry, data, ir = 0u;

    response += 2;
    s_transfer_abort = 0u;

    /* Device index (JTAG TAP) */
    uint8_t index = *request++;
    if (index >= jtag_get_count()) {
        goto end;
    }
    jtag_set_device_index(index);

    request_count = *request++;

    for (; request_count != 0u; request_count--) {
        request_value = *request++;
        request_ir = (request_value & DAP_TRANSFER_APnDP) ? JTAG_IR_APACC : JTAG_IR_DPACC;
        if (request_value & DAP_TRANSFER_RnW) {
            /* Read register */
            if (post_read) {
                retry = s_dap.transfer.retry_count;
                if ((ir == request_ir) &&
                    ((request_value & DAP_TRANSFER_MATCH_VALUE) == 0u)) {
                    do {
                        response_value = jtag_retry(request_value, &data);
                    } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
                } else {
                    if (ir != JTAG_IR_DPACC) {
                        ir = JTAG_IR_DPACC;
                        jtag_ir(ir);
                    }
                    do {
                        response_value = jtag_retry(JTAG_REQ_RDBUFF_READ, &data);
                    } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
                    post_read = 0u;
                }
                if (response_value != JTAG_TRANSFER_OK) {
                    break;
                }
                *response++ = (uint8_t)data;
                *response++ = (uint8_t)(data >> 8);
                *response++ = (uint8_t)(data >> 16);
                *response++ = (uint8_t)(data >> 24);
            }
            if (request_value & DAP_TRANSFER_MATCH_VALUE) {
                match_value = (uint32_t)request[0] | ((uint32_t)request[1] << 8) |
                              ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 24);
                request += 4;
                match_retry = s_dap.transfer.match_retry;
                if (ir != request_ir) {
                    ir = request_ir;
                    jtag_ir(ir);
                }
                retry = s_dap.transfer.retry_count;
                do {
                    response_value = jtag_retry(request_value, NULL);
                } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
                if (response_value != JTAG_TRANSFER_OK) {
                    break;
                }
                do {
                    retry = s_dap.transfer.retry_count;
                    do {
                        response_value = jtag_retry(request_value, &data);
                    } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
                    if (response_value != JTAG_TRANSFER_OK) {
                        break;
                    }
                } while (((data & s_dap.transfer.match_mask) != match_value) &&
                         match_retry-- && !s_transfer_abort);
                if ((data & s_dap.transfer.match_mask) != match_value) {
                    response_value |= JTAG_TRANSFER_MISMATCH;
                }
                if (response_value != JTAG_TRANSFER_OK) {
                    break;
                }
            } else {
                if (post_read == 0u) {
                    if (ir != request_ir) {
                        ir = request_ir;
                        jtag_ir(ir);
                    }
                    retry = s_dap.transfer.retry_count;
                    do {
                        response_value = jtag_retry(request_value, NULL);
                    } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
                    if (response_value != JTAG_TRANSFER_OK) {
                        break;
                    }
                    post_read = 1u;
                }
            }
        } else {
            /* Write register */
            if (post_read) {
                if (ir != JTAG_IR_DPACC) {
                    ir = JTAG_IR_DPACC;
                    jtag_ir(ir);
                }
                retry = s_dap.transfer.retry_count;
                do {
                    response_value = jtag_retry(JTAG_REQ_RDBUFF_READ, &data);
                } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
                if (response_value != JTAG_TRANSFER_OK) {
                    break;
                }
                *response++ = (uint8_t)data;
                *response++ = (uint8_t)(data >> 8);
                *response++ = (uint8_t)(data >> 16);
                *response++ = (uint8_t)(data >> 24);
                post_read = 0u;
            }
            data = (uint32_t)request[0] | ((uint32_t)request[1] << 8) |
                   ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 24);
            request += 4;
            if (request_value & DAP_TRANSFER_MATCH_MASK) {
                s_dap.transfer.match_mask = data;
                response_value = JTAG_TRANSFER_OK;
            } else {
                if (ir != request_ir) {
                    ir = request_ir;
                    jtag_ir(ir);
                }
                retry = s_dap.transfer.retry_count;
                do {
                    response_value = jtag_retry(request_value, &data);
                } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
                if (response_value != JTAG_TRANSFER_OK) {
                    break;
                }
            }
        }
        response_count++;
        if (s_transfer_abort) {
            break;
        }
    }

    for (; request_count != 0u; request_count--) {
        request_value = *request++;
        if (request_value & DAP_TRANSFER_RnW) {
            if (request_value & DAP_TRANSFER_MATCH_VALUE) {
                request += 4;
            }
        } else {
            request += 4;
        }
    }

    if (response_value == JTAG_TRANSFER_OK) {
        if (ir != JTAG_IR_DPACC) {
            ir = JTAG_IR_DPACC;
            jtag_ir(ir);
        }
        if (post_read) {
            retry = s_dap.transfer.retry_count;
            do {
                response_value = jtag_retry(JTAG_REQ_RDBUFF_READ, &data);
            } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
            if (response_value != JTAG_TRANSFER_OK) {
                goto end;
            }
            *response++ = (uint8_t)data;
            *response++ = (uint8_t)(data >> 8);
            *response++ = (uint8_t)(data >> 16);
            *response++ = (uint8_t)(data >> 24);
        } else {
            retry = s_dap.transfer.retry_count;
            do {
                response_value = jtag_retry(JTAG_REQ_RDBUFF_READ, NULL);
            } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
        }
    }

end:
    response_head[0] = (uint8_t)response_count;
    response_head[1] = (uint8_t)response_value;
    return (((uint32_t)(request - request_head) << 16) | (uint32_t)(response - response_head));
}

/* DAP_JTAG_TransferBlock (0x18) - port of ARM DAP_JTAG_TransferBlock */
static uint32_t dap_jtag_transfer_block(const uint8_t *request, uint8_t *response)
{
    uint32_t request_count, request_value, response_count = 0u, response_value = 0u, retry, data, ir;
    uint8_t *response_head = response;

    response += 3;
    s_transfer_abort = 0u;

    uint8_t index = *request++;
    if (index >= jtag_get_count()) {
        goto end;
    }
    jtag_set_device_index(index);

    request_count = (uint32_t)request[0] | ((uint32_t)request[1] << 8);
    request += 2;
    if (request_count == 0u) {
        goto end;
    }

    request_value = *request++;
    ir = (request_value & DAP_TRANSFER_APnDP) ? JTAG_IR_APACC : JTAG_IR_DPACC;
    jtag_ir(ir);

    if (request_value & DAP_TRANSFER_RnW) {
        retry = s_dap.transfer.retry_count;
        do {
            response_value = jtag_retry(request_value, NULL);
        } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
        if (response_value != JTAG_TRANSFER_OK) {
            goto end;
        }
        while (request_count--) {
            if (request_count == 0u) {
                if (ir != JTAG_IR_DPACC) {
                    jtag_ir(JTAG_IR_DPACC);
                }
                request_value = JTAG_REQ_RDBUFF_READ;
            }
            retry = s_dap.transfer.retry_count;
            do {
                response_value = jtag_retry(request_value, &data);
            } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
            if (response_value != JTAG_TRANSFER_OK) {
                goto end;
            }
            *response++ = (uint8_t)data;
            *response++ = (uint8_t)(data >> 8);
            *response++ = (uint8_t)(data >> 16);
            *response++ = (uint8_t)(data >> 24);
            response_count++;
        }
    } else {
        while (request_count--) {
            data = (uint32_t)request[0] | ((uint32_t)request[1] << 8) |
                   ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 24);
            request += 4;
            retry = s_dap.transfer.retry_count;
            do {
                response_value = jtag_retry(request_value, &data);
            } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
            if (response_value != JTAG_TRANSFER_OK) {
                goto end;
            }
            response_count++;
        }
        if (ir != JTAG_IR_DPACC) {
            jtag_ir(JTAG_IR_DPACC);
        }
        retry = s_dap.transfer.retry_count;
        do {
            response_value = jtag_retry(JTAG_REQ_RDBUFF_READ, NULL);
        } while (response_value == JTAG_TRANSFER_WAIT && retry-- && !s_transfer_abort);
    }

end:
    response_head[0] = (uint8_t)(response_count >> 0);
    response_head[1] = (uint8_t)(response_count >> 8);
    response_head[2] = (uint8_t)response_value;
    return ((uint32_t)(response - response_head));
}

/* DAP_JTAG_WriteAbort (0x19) */
static uint32_t dap_jtag_write_abort(const uint8_t *request, uint8_t *response)
{
    uint8_t index = *request;
    if (index >= jtag_get_count()) {
        *response = DAP_ERROR;
        return 1u;
    }
    jtag_set_device_index(index);
    uint32_t data = (uint32_t)request[1] | ((uint32_t)request[2] << 8) |
                    ((uint32_t)request[3] << 16) | ((uint32_t)request[4] << 24);
    jtag_write_abort(data);
    *response = DAP_OK;
    return 1u;
}

/* ------------------------------------------------------------------ */
/* Command dispatcher (mirrors ARM DAP_ProcessCommand)                 */
/* ------------------------------------------------------------------ */
static uint32_t dap_process_command(const uint8_t *request, uint8_t *response)
{
    uint32_t num;

    response[0] = request[0];

    switch (request[0]) {
    case ID_DAP_Info: {
        /* Spec (ARM CMSIS-DAP): response = [cmd, length, data...].
         * response[0] is already the command echo (set above). byte1 is the
         * payload length that OpenOCD/Keil parse - NOT an InfoType echo. */
        uint8_t info_type = request[1];
        num = dap_info(info_type, response + 2);
        response[1] = (uint8_t)num;
        ESP_LOGI(TAG, "DAP_Info id=%u len=%u", info_type, (unsigned)num);
        return (2u << 16) + 2u + num;
    }

    case ID_DAP_HostStatus:
        num = dap_host_status(request + 1, response + 1);
        break;

    case ID_DAP_Connect:
        num = dap_connect(request + 1, response + 1);
        break;

    case ID_DAP_Disconnect:
        num = dap_disconnect(response + 1);
        break;

    case ID_DAP_Delay:
        num = dap_delay(request + 1, response + 1);
        break;

    case ID_DAP_ResetTarget: {
        /* debug_reset runs inside debug engine (bus-safe) */
        num = dap_reset_target(response + 1);
        break;
    }

    case ID_DAP_SWJ_Pins:
        num = dap_swj_pins(request + 1, response + 1);
        break;

    case ID_DAP_SWJ_Clock:
        num = dap_swj_clock(request + 1, response + 1);
        break;

    case ID_DAP_SWJ_Sequence:
        num = dap_swj_sequence(request + 1, response + 1);
        break;

    case ID_DAP_SWD_Configure:
        num = dap_swd_configure(request + 1, response + 1);
        break;

    case ID_DAP_SWD_Sequence:
        num = dap_swd_sequence(request + 1, response + 1);
        break;

    case ID_DAP_TransferConfigure:
        num = dap_transfer_configure(request + 1, response + 1);
        break;

    case ID_DAP_Transfer:
        if (s_dap.debug_port == DAP_PORT_SWD) {
            num = dap_swd_transfer(request + 1, response + 1);
        } else if (s_dap.debug_port == DAP_PORT_JTAG) {
            num = dap_jtag_transfer(request + 1, response + 1);
        } else {
            response[1] = 0;
            response[2] = 0;
            num = (2u << 16) | 2u;
        }
        break;

    case ID_DAP_TransferBlock:
        num = dap_transfer_block(request + 1, response + 1);
        break;

    case ID_DAP_JTAG_Sequence:
        num = dap_jtag_sequence(request + 1, response + 1);
        break;

    case ID_DAP_JTAG_Configure:
        num = dap_jtag_configure(request + 1, response + 1);
        break;

    case ID_DAP_JTAG_IDCODE:
        num = dap_jtag_idcode(request + 1, response + 1);
        break;

    case ID_DAP_JTAG_Transfer:
        num = dap_jtag_transfer(request + 1, response + 1);
        break;

    case ID_DAP_JTAG_TransferBlock:
        num = dap_jtag_transfer_block(request + 1, response + 1);
        break;

    case ID_DAP_JTAG_WriteAbort:
        num = dap_jtag_write_abort(request + 1, response + 1);
        break;

    case ID_DAP_TransferAbort:
        s_transfer_abort = 1;
        num = 0;    /* no response for abort */
        break;

    case ID_DAP_WriteABORT:
        num = dap_write_abort(request + 1, response + 1);
        break;

    case ID_DAP_QueueCommands:
    case ID_DAP_ExecuteCommands: {
        /* Handled by the caller (execute), not here */
        response[0] = ID_DAP_Invalid;
        return (1u << 16) | 1u;
    }

    default:
        response[0] = ID_DAP_Invalid;
        return (1u << 16) | 1u;
    }

    return (1u << 16) + 1u + num;
}

uint32_t cmsis_dap_execute(const uint8_t *request, uint32_t request_len,
                           uint8_t *response)
{
    if (request_len == 0) {
        response[0] = ID_DAP_Invalid;
        return 1;
    }

    ESP_LOGI(TAG, ">> req cmd=0x%02X len=%u", request[0], (unsigned)request_len);

    if (request[0] == ID_DAP_QueueCommands || request[0] == ID_DAP_ExecuteCommands) {
        uint8_t *resp = response;
        resp[0] = request[0];
        resp[1] = request[1];
        uint32_t count = request[1];
        uint32_t num = (2u << 16) | 2u;
        const uint8_t *req = request + 2;
        resp += 2;
        while (count--) {
            uint32_t n = dap_process_command(req, resp);
            num += n;
            req += (uint16_t)(n >> 16);
            resp += (uint16_t)n;
        }
        (void)request_len;
        return num;
    }

    return dap_process_command(request, response);
}

/* ------------------------------------------------------------------ */
/* DAP task                                                            */
/* ------------------------------------------------------------------ */
static void dap_task(void *arg)
{
    QueueHandle_t rxq = (QueueHandle_t)arg;
    usb_dap_msg_t msg;
    uint8_t response[USB_DAP_PACKET_SIZE];

    ESP_LOGI(TAG, "CMSIS-DAP v%s task ready", DAP_FW_VER);
    for (;;) {
        if (xQueueReceive(rxq, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* Fresh buffer each pass: unused bytes stay 0 so Info strings are
         * implicitly null-terminated and numeric responses are clean. */
        memset(response, 0, sizeof(response));
        /* One debug entry point at a time (USB today, Wi-Fi tomorrow) */
        if (debug_engine_lock(1000) != ESP_OK) {
            ESP_LOGW(TAG, "debug engine busy, dropping request");
            continue;
        }
        /* cmsis_dap_execute encodes (request_bytes << 16) | response_bytes;
         * only the lower 16 bits are the actual response length. */
        uint32_t exec_ret = cmsis_dap_execute(msg.data, msg.len, response);
        uint16_t resp_len = (uint16_t)(exec_ret & 0xFFFFu);
        debug_engine_unlock();

        if (resp_len > 0) {
            esp_err_t err = usb_device_send_response(response, resp_len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "response send failed: %s", esp_err_to_name(err));
            }
        }
    }
}

esp_err_t cmsis_dap_init(QueueHandle_t rx_queue)
{
    if (rx_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xTaskCreate(dap_task, "dap_task", 6144, rx_queue, 9, NULL) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
