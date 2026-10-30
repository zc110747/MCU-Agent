/* ---------------------------------------------------------------------------
 * command_handler.h - semantics of the UART command set.
 *
 * Grammar (see docs/protocol.md for the full contract):
 *
 *     MOVE,<dx>,<dy>              relative move, counts
 *     BUTTON,<LEFT|RIGHT|MIDDLE>,<DOWN|UP>
 *     CLICK,<LEFT|RIGHT|MIDDLE>
 *     WHEEL,<n>                   n > 0 scrolls away from the user
 *     PAN,<n>                     horizontal wheel (reserved)
 *     RELEASE,ALL                 drop every UART-held button
 *     STATUS                      one-line device state dump
 *
 * Every handler goes through mouse_core / mouse_input. Nothing here may call
 * the USB layer directly - that is what keeps "a command is just another input
 * source" true, and it is why the UART path and the optical path cannot
 * disagree about what the host is being told.
 * -------------------------------------------------------------------------*/

#ifndef COMMAND_HANDLER_H_
#define COMMAND_HANDLER_H_

#include "uart_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Execute one parsed command. On UART_CMD_OK the handler has already queued its
 * own reply line; on any error the caller emits the canonical error text. */
uart_cmd_result_t command_handler_execute(const uart_command_t *command);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_HANDLER_H_ */
