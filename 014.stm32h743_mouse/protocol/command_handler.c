/* ---------------------------------------------------------------------------
 * command_handler.c - execute the UART command set.
 * -------------------------------------------------------------------------*/

#include "command_handler.h"
#include "mouse_core.h"
#include "mouse_input.h"
#include "mouse_report.h"
#include "bsp_uart.h"
#include "usb_hid_mouse.h"
#include "usb_desc_defs.h"

/* Sanity limits. A single MOVE is meant to be a nudge, not a way to drive the
 * cursor into a corner through the round-trip accumulator. */
#define CMD_MOVE_LIMIT     4096
#define CMD_WHEEL_LIMIT    127

/* ---------------------------------------------------------------------------
 * Small formatting helpers - deliberately not printf.
 *
 * newlib-nano's printf pulls in several kilobytes of formatting code for a
 * handful of integers, and the "%s" path is where the classic "printf worked
 * in Debug only" failures come from. Hand-rolled appenders cost ~200 bytes.
 * -------------------------------------------------------------------------*/
typedef struct
{
    char    *buf;
    uint32_t capacity;
    uint32_t length;
} text_builder_t;

static void tb_init(text_builder_t *tb, char *buf, uint32_t capacity)
{
    tb->buf      = buf;
    tb->capacity = capacity;
    tb->length   = 0u;
    if (capacity > 0u)
    {
        buf[0] = '\0';
    }
}

static void tb_putc(text_builder_t *tb, char c)
{
    if ((tb->length + 1u) >= tb->capacity)
    {
        return;
    }

    tb->buf[tb->length] = c;
    tb->length++;
    tb->buf[tb->length] = '\0';
}

static void tb_puts(text_builder_t *tb, const char *text)
{
    while ((text != 0) && (*text != '\0'))
    {
        tb_putc(tb, *text);
        text++;
    }
}

static void tb_put_u32(text_builder_t *tb, uint32_t value)
{
    char digits[11];
    uint32_t count = 0u;

    if (value == 0u)
    {
        tb_putc(tb, '0');
        return;
    }

    while ((value > 0u) && (count < sizeof(digits)))
    {
        digits[count] = (char) ('0' + (value % 10u));
        value /= 10u;
        count++;
    }

    while (count > 0u)
    {
        count--;
        tb_putc(tb, digits[count]);
    }
}

static void tb_put_i32(text_builder_t *tb, int32_t value)
{
    if (value < 0)
    {
        tb_putc(tb, '-');
        /* Negate as unsigned: -(-2147483648) overflows a signed int. */
        tb_put_u32(tb, (uint32_t) (-(value + 1)) + 1u);
        return;
    }

    tb_put_u32(tb, (uint32_t) value);
}

static void tb_put_hex8(text_builder_t *tb, uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    tb_putc(tb, '0');
    tb_putc(tb, 'x');
    tb_putc(tb, hex[(value >> 4) & 0x0Fu]);
    tb_putc(tb, hex[value & 0x0Fu]);
}

/* Reply buffers live in .bss. There is no dynamic allocation anywhere in this
 * project, so the sizes are the contract: the longest line is ~120 characters. */
#define REPLY_LINE_MAX   160u
static char s_reply[REPLY_LINE_MAX];

static void reply_ack(const char *verb)
{
    text_builder_t tb;

    tb_init(&tb, s_reply, REPLY_LINE_MAX);
    tb_puts(&tb, "ACK,");
    tb_puts(&tb, verb);
    tb_puts(&tb, "\r\n");
    (void) bsp_uart_write_str(s_reply);
}

/* ---------------------------------------------------------------------------
 * Parameter decoding
 * -------------------------------------------------------------------------*/

/* Strict decimal parse: optional sign, digits only, no overflow. Rejecting
 * "12abc" and "1e3" matters - a host bug that sends them should surface as
 * ERROR,INVALID_PARAMETER instead of silently moving the cursor by 12. */
static bool parse_i32(const char *text, int32_t *value)
{
    uint32_t index = 0u;
    uint32_t digits = 0u;
    int32_t  result = 0;
    bool     negative = false;

    if (text == 0)
    {
        return false;
    }

    if ((text[0] == '-') || (text[0] == '+'))
    {
        negative = (text[0] == '-');
        index = 1u;
    }

    while (text[index] != '\0')
    {
        if ((text[index] < '0') || (text[index] > '9'))
        {
            return false;
        }

        /* Bail out before the multiply can wrap. 100000000 * 10 + 9 still fits
         * in int32, so this guard alone is sufficient. */
        if (result > 100000000)
        {
            return false;
        }

        result = (result * 10) + (int32_t) (text[index] - '0');
        digits++;
        if (digits > 9u)
        {
            return false;
        }

        index++;
    }

    if (digits == 0u)
    {
        return false;
    }

    *value = (negative == true) ? -result : result;
    return true;
}

static bool button_from_name(const char *name, uint8_t *mask)
{
    if ((name[0] == 'L') && (name[1] == 'E') && (name[2] == 'F') &&
        (name[3] == 'T') && (name[4] == '\0'))
    {
        *mask = MOUSE_BUTTON_LEFT;
        return true;
    }
    if ((name[0] == 'R') && (name[1] == 'I') && (name[2] == 'G') &&
        (name[3] == 'H') && (name[4] == 'T') && (name[5] == '\0'))
    {
        *mask = MOUSE_BUTTON_RIGHT;
        return true;
    }
    if ((name[0] == 'M') && (name[1] == 'I') && (name[2] == 'D') &&
        (name[3] == 'D') && (name[4] == 'L') && (name[5] == 'E') &&
        (name[6] == '\0'))
    {
        *mask = MOUSE_BUTTON_MIDDLE;
        return true;
    }

    return false;
}

/* Rewrite the UART shadow and tell the core about the edge. The notify is what
 * turns a CLICK into two reports instead of none: DOWN and UP each enqueue a
 * snapshot, and mouse_core delivers both even if the endpoint is busy. */
static void uart_buttons_apply(uint8_t new_mask)
{
    uint8_t before = mouse_input_get()->combined;

    mouse_input_set_uart(new_mask);

    if (mouse_input_get()->combined != before)
    {
        mouse_core_notify_buttons();
    }
}

/* ---------------------------------------------------------------------------
 * Commands
 * -------------------------------------------------------------------------*/
static uart_cmd_result_t handle_move(const uart_command_t *command)
{
    int32_t dx;
    int32_t dy;

    if (command->argc != 2u)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if ((parse_i32(command->arg[0], &dx) == false) ||
        (parse_i32(command->arg[1], &dy) == false))
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if ((dx > CMD_MOVE_LIMIT) || (dx < -CMD_MOVE_LIMIT) ||
        (dy > CMD_MOVE_LIMIT) || (dy < -CMD_MOVE_LIMIT))
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    mouse_core_add_motion(dx, dy);
    reply_ack("MOVE");
    return UART_CMD_OK;
}

static uart_cmd_result_t handle_button(const uart_command_t *command)
{
    uint8_t mask = 0u;
    uint8_t shadow;
    bool down;

    if (command->argc != 2u)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if (button_from_name(command->arg[0], &mask) == false)
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    if ((command->arg[1][0] == 'D') && (command->arg[1][1] == 'O') &&
        (command->arg[1][2] == 'W') && (command->arg[1][3] == 'N') &&
        (command->arg[1][4] == '\0'))
    {
        down = true;
    }
    else if ((command->arg[1][0] == 'U') && (command->arg[1][1] == 'P') &&
             (command->arg[1][2] == '\0'))
    {
        down = false;
    }
    else
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    /* Only the UART shadow is touched. A finger still holding the physical
     * button keeps that bit set after the OR, which is exactly the point. */
    shadow = mouse_input_get()->uart;
    if (down == true)
    {
        shadow |= mask;
    }
    else
    {
        shadow = (uint8_t) (shadow & (uint8_t) ~mask);
    }
    uart_buttons_apply(shadow);

    reply_ack("BUTTON");
    return UART_CMD_OK;
}

static uart_cmd_result_t handle_click(const uart_command_t *command)
{
    uint8_t mask = 0u;
    uint8_t shadow;

    if (command->argc != 1u)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if (button_from_name(command->arg[0], &mask) == false)
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    shadow = mouse_input_get()->uart;

    uart_buttons_apply((uint8_t) (shadow | mask));       /* press   */
    uart_buttons_apply((uint8_t) (shadow & (uint8_t) ~mask));      /* release */

    reply_ack("CLICK");
    return UART_CMD_OK;
}

static uart_cmd_result_t handle_wheel(const uart_command_t *command)
{
    int32_t notches;

    if (command->argc != 1u)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if (parse_i32(command->arg[0], &notches) == false)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if ((notches > CMD_WHEEL_LIMIT) || (notches < -CMD_WHEEL_LIMIT))
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    mouse_core_add_wheel(notches);
    reply_ack("WHEEL");
    return UART_CMD_OK;
}

static uart_cmd_result_t handle_pan(const uart_command_t *command)
{
    int32_t notches;

    if (command->argc != 1u)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if (parse_i32(command->arg[0], &notches) == false)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if ((notches > CMD_WHEEL_LIMIT) || (notches < -CMD_WHEEL_LIMIT))
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    mouse_core_add_pan(notches);
    reply_ack("PAN");
    return UART_CMD_OK;
}

static uart_cmd_result_t handle_release(const uart_command_t *command)
{
    uint8_t before = mouse_input_get()->combined;

    if (command->argc != 1u)
    {
        return UART_CMD_INVALID_PARAMETER;
    }
    if ((command->arg[0][0] != 'A') || (command->arg[0][1] != 'L') ||
        (command->arg[0][2] != 'L') || (command->arg[0][3] != '\0'))
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    mouse_input_clear_uart();

    if (mouse_input_get()->combined != before)
    {
        mouse_core_notify_buttons();
    }

    /* Push it out now rather than on the next 1 ms tick: this command exists
     * precisely for the "the cursor is dragging and I want it to stop" case. */
    mouse_core_flush();

    reply_ack("RELEASE");
    return UART_CMD_OK;
}

static uart_cmd_result_t handle_status(const uart_command_t *command)
{
    text_builder_t tb;
    mouse_core_status_t core;
    mouse_input_t input;
    bsp_uart_stats_t uart;
    usb_hid_mouse_stats_t usb;

    if (command->argc != 0u)
    {
        return UART_CMD_INVALID_PARAMETER;
    }

    mouse_core_get_status(&core);
    input = *mouse_input_get();
    bsp_uart_get_stats(&uart);
    usb_hid_mouse_get_stats(&usb);

    tb_init(&tb, s_reply, REPLY_LINE_MAX);
    tb_puts(&tb, "STATUS,FW=");
    tb_put_u32(&tb, USB_FW_MAJOR);
    tb_putc(&tb, '.');
    tb_put_u32(&tb, USB_FW_MINOR);
    tb_puts(&tb, ",USB=");
    tb_put_u32(&tb, (usb_hid_mouse_mounted() == true) ? 1u : 0u);
    tb_puts(&tb, ",BTN=");
    tb_put_hex8(&tb, core.buttons_combined);
    tb_puts(&tb, ",PHY=");
    tb_put_hex8(&tb, input.physical);
    tb_puts(&tb, ",UART=");
    tb_put_hex8(&tb, input.uart);
    tb_puts(&tb, ",SENT=");
    tb_put_u32(&tb, core.reports_sent);
    tb_puts(&tb, ",FAIL=");
    tb_put_u32(&tb, core.reports_failed);
    tb_puts(&tb, ",QLEN=");
    tb_put_u32(&tb, core.button_queue_len);
    tb_puts(&tb, ",PEND=");
    tb_put_i32(&tb, core.pending_x);
    tb_putc(&tb, ',');
    tb_put_i32(&tb, core.pending_y);
    tb_putc(&tb, ',');
    tb_put_i32(&tb, core.pending_wheel);
    tb_putc(&tb, ',');
    tb_put_i32(&tb, core.pending_pan);
    tb_puts(&tb, ",RX=");
    tb_put_u32(&tb, uart.rx_bytes);
    tb_puts(&tb, ",RXOVF=");
    tb_put_u32(&tb, uart.rx_overflow);
    tb_puts(&tb, ",TXOVF=");
    tb_put_u32(&tb, uart.tx_overflow);
    tb_puts(&tb, ",USBOVF=");
    tb_put_u32(&tb, usb.not_ready);
    tb_puts(&tb, "\r\n");

    (void) bsp_uart_write_str(s_reply);
    return UART_CMD_OK;
}

/* ------------------------------------------------------------------------ */
static bool verb_is(const char *verb, const char *expected)
{
    uint32_t i = 0u;

    while ((verb[i] != '\0') && (expected[i] != '\0'))
    {
        if (verb[i] != expected[i])
        {
            return false;
        }
        i++;
    }

    return (verb[i] == '\0') && (expected[i] == '\0');
}

uart_cmd_result_t command_handler_execute(const uart_command_t *command)
{
    if (command == 0)
    {
        return UART_CMD_INVALID_COMMAND;
    }

    if (verb_is(command->verb, "MOVE") == true)
    {
        return handle_move(command);
    }
    if (verb_is(command->verb, "BUTTON") == true)
    {
        return handle_button(command);
    }
    if (verb_is(command->verb, "CLICK") == true)
    {
        return handle_click(command);
    }
    if (verb_is(command->verb, "WHEEL") == true)
    {
        return handle_wheel(command);
    }
    if (verb_is(command->verb, "PAN") == true)
    {
        return handle_pan(command);
    }
    if (verb_is(command->verb, "RELEASE") == true)
    {
        return handle_release(command);
    }
    if (verb_is(command->verb, "STATUS") == true)
    {
        return handle_status(command);
    }

    return UART_CMD_INVALID_COMMAND;
}
