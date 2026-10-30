/* ---------------------------------------------------------------------------
 * uart_protocol.c - line framing and the command grammar.
 * -------------------------------------------------------------------------*/

#include "uart_protocol.h"
#include "command_handler.h"
#include "bsp_uart.h"

#define CHUNK_SIZE   32u

/* Line assembly state. One logical object: the partially received command. */
typedef struct
{
    char     line[UART_PROTOCOL_LINE_MAX];  /* NUL-terminated once complete */
    uint32_t length;
    bool     overflowed;                    /* line exceeded the buffer     */
} line_state_t;

static line_state_t g_line;

/* ------------------------------------------------------------------------ */
static char to_upper(char c)
{
    if ((c >= 'a') && (c <= 'z'))
    {
        return (char) (c - 'a' + 'A');
    }
    return c;
}

static bool is_space(char c)
{
    return (c == ' ') || (c == '\t');
}

/* Copy `length` characters, upper-casing as we go, and NUL-terminate. */
static void copy_upper(char *dst, const char *src, uint32_t length)
{
    for (uint32_t i = 0u; i < length; i++)
    {
        dst[i] = to_upper(src[i]);
    }
    dst[length] = '\0';
}

/* ------------------------------------------------------------------------ */
bool uart_protocol_parse_line(const char *line, uart_command_t *command)
{
    const char *cursor = line;
    uint8_t field = 0u;      /* 0 = verb, 1.. = argument index      */
    uint32_t length;

    if ((line == 0) || (command == 0))
    {
        return false;
    }

    command->argc    = 0u;
    command->verb[0] = '\0';
    for (uint32_t i = 0u; i < UART_PROTOCOL_MAX_ARGS; i++)
    {
        command->arg[i][0] = '\0';
    }

    for (;;)
    {
        const char *start = cursor;
        const char *end;

        while ((*cursor != '\0') && (*cursor != ','))
        {
            cursor++;
        }
        end = cursor;

        /* Whitespace around a field is not data: "MOVE, 100 , 50" is the same
         * command as "MOVE,100,50". Interior spaces are kept, so a stray one
         * still fails the numeric conversion instead of being papered over. */
        while ((start < end) && (is_space(*start) == true))
        {
            start++;
        }
        while ((end > start) && (is_space(end[-1]) == true))
        {
            end--;
        }

        length = (uint32_t) (end - start);

        if (field == 0u)
        {
            /* An empty verb means the line is not a command at all. */
            if ((length == 0u) || (length >= UART_PROTOCOL_VERB_LEN))
            {
                return false;
            }
            copy_upper(command->verb, start, length);
        }
        else
        {
            if ((field > UART_PROTOCOL_MAX_ARGS) ||
                (length >= UART_PROTOCOL_ARG_LEN))
            {
                return false;
            }
            /* An empty argument stays empty on purpose: the handler reports
             * ERROR,INVALID_PARAMETER with the actual reason. */
            copy_upper(command->arg[field - 1u], start, length);
        }

        if (*cursor == '\0')
        {
            break;
        }

        cursor++;              /* step over the ',' */
        field++;
        if (field > UART_PROTOCOL_MAX_ARGS)
        {
            return false;
        }
    }

    command->argc = field;
    return true;
}

/* ------------------------------------------------------------------------ */
void uart_protocol_reply(const char *text)
{
    if (text == 0)
    {
        return;
    }

    (void) bsp_uart_write_str(text);
}

void uart_protocol_reply_result(uart_cmd_result_t result, const char *verb)
{
    switch (result)
    {
        case UART_CMD_OK:
            uart_protocol_reply("ACK,");
            uart_protocol_reply((verb != 0) ? verb : "");
            uart_protocol_reply("\r\n");
            break;

        case UART_CMD_INVALID_PARAMETER:
            uart_protocol_reply("ERROR,INVALID_PARAMETER\r\n");
            break;

        case UART_CMD_BUFFER_FULL:
            uart_protocol_reply("ERROR,BUFFER_FULL\r\n");
            break;

        case UART_CMD_EXEC_FAILED:
            uart_protocol_reply("ERROR,EXEC_FAILED\r\n");
            break;

        case UART_CMD_INVALID_COMMAND:
        default:
            uart_protocol_reply("ERROR,INVALID_COMMAND\r\n");
            break;
    }
}

/* ------------------------------------------------------------------------ */
static void line_reset(void)
{
    g_line.length = 0u;
    g_line.line[0] = '\0';
    g_line.overflowed = false;
}

static void line_execute(void)
{
    uart_command_t command;
    uart_cmd_result_t result;

    if (g_line.overflowed)
    {
        line_reset();
        uart_protocol_reply_result(UART_CMD_BUFFER_FULL, 0);
        return;
    }

    if (g_line.length == 0u)
    {
        line_reset();
        return;      /* blank line: not an error, just nothing to do */
    }

    if (uart_protocol_parse_line(g_line.line, &command) == false)
    {
        line_reset();
        uart_protocol_reply_result(UART_CMD_INVALID_COMMAND, 0);
        return;
    }

    result = command_handler_execute(&command);
    if (result != UART_CMD_OK)
    {
        uart_protocol_reply_result(result, command.verb);
    }
    /* On success command_handler has already written its own reply, because
     * STATUS answers with a payload rather than a bare ACK. */

    line_reset();
}

static void line_feed(char c)
{
    if ((c == '\n') || (c == '\r'))
    {
        line_execute();
        return;
    }

    if (g_line.overflowed)
    {
        return;      /* waiting for the newline that ends this bad line */
    }

    if ((g_line.length + 1u) >= UART_PROTOCOL_LINE_MAX)
    {
        g_line.overflowed = true;
        return;
    }

    g_line.line[g_line.length] = c;
    g_line.length++;
    g_line.line[g_line.length] = '\0';
}

/* ------------------------------------------------------------------------ */
void uart_protocol_init(void)
{
    line_reset();
}

void uart_protocol_poll(void)
{
    uint8_t chunk[CHUNK_SIZE];
    uint32_t count;

    while ((count = bsp_uart_read(chunk, CHUNK_SIZE)) > 0u)
    {
        for (uint32_t i = 0u; i < count; i++)
        {
            line_feed((char) chunk[i]);
        }
    }
}
