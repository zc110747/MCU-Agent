/* ---------------------------------------------------------------------------
 * uart_protocol.h - byte stream -> one command line.
 *
 * Framing rules:
 *   - a command ends at '\n'; a preceding '\r' is dropped, so CRLF and LF both
 *     work,
 *   - the verb and every argument are upper-cased and trimmed,
 *   - a line longer than UART_PROTOCOL_LINE_MAX is discarded up to the next
 *     newline and answered with ERROR,BUFFER_FULL.
 *
 * Splitting bytes into lines here is what makes partial and coalesced packets
 * a non-issue: the DMA may hand over half a command, or five commands and half
 * of a sixth, and the parser still sees exactly the lines the host typed.
 *
 * The parser knows nothing about mice - it produces a verb and up to three
 * opaque arguments. Semantics live in command_handler.c.
 * -------------------------------------------------------------------------*/

#ifndef UART_PROTOCOL_H_
#define UART_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_PROTOCOL_LINE_MAX   64u
#define UART_PROTOCOL_VERB_LEN   12u
#define UART_PROTOCOL_MAX_ARGS   3u
#define UART_PROTOCOL_ARG_LEN    12u

typedef enum
{
    UART_CMD_OK = 0,
    UART_CMD_INVALID_COMMAND,
    UART_CMD_INVALID_PARAMETER,
    UART_CMD_BUFFER_FULL,
    UART_CMD_EXEC_FAILED
} uart_cmd_result_t;

typedef struct
{
    char    verb[UART_PROTOCOL_VERB_LEN];                        /* upper case */
    char    arg[UART_PROTOCOL_MAX_ARGS][UART_PROTOCOL_ARG_LEN];  /* upper case */
    uint8_t argc;
} uart_command_t;

void uart_protocol_init(void);

/* Drain whatever the UART driver has received and execute every complete line.
 * Non-blocking; call once per main-loop pass. */
void uart_protocol_poll(void);

/* Parse one line in place. Returns false when the line is empty, has no verb,
 * or carries more than UART_PROTOCOL_MAX_ARGS fields. Exposed so the same
 * grammar can be unit-tested on a PC without the hardware. */
bool uart_protocol_parse_line(const char *line, uart_command_t *command);

/* Append text to the reply stream (goes out through bsp_uart_write). */
void uart_protocol_reply(const char *text);

/* Emit the canonical reply for a result code. */
void uart_protocol_reply_result(uart_cmd_result_t result, const char *verb);

#ifdef __cplusplus
}
#endif

#endif /* UART_PROTOCOL_H_ */
