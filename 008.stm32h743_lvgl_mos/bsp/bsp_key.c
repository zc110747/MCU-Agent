/**
  ******************************************************************************
  * @file    bsp_key.c
  * @brief   Virtual key abstraction - see bsp_key.h for the rationale.
  *
  *  Concurrency
  *  -----------
  *  bsp_key_inject() may be called from the USART1 interrupt handler while the
  *  main loop is inside bsp_key_pop().  The queue is a single-producer /
  *  single-consumer ring with a power-of-two size, so head and tail are only
  *  ever written by one side each and no critical section is needed.  The
  *  held-state bitmask is a single word, updated atomically enough on a 32 bit
  *  core for this purpose.
  ******************************************************************************
  */
#include "bsp_key.h"
#include "main.h"
#include <string.h>

#define KEY_QUEUE_LEN       32U     /* must be a power of two */
#define KEY_QUEUE_MASK      (KEY_QUEUE_LEN - 1U)

static volatile key_event_t s_queue[KEY_QUEUE_LEN];
static volatile uint32_t    s_head;         /* producer */
static volatile uint32_t    s_tail;         /* consumer */

static volatile uint32_t    s_state;        /* held-key bitmask       */
static volatile uint32_t    s_release_at[KEY_COUNT];  /* 0 = no timer */

/** Console-facing names.  Order must match key_id_t. */
static const char *const s_names[KEY_COUNT] =
{
    "up", "down", "left", "right",
    "a", "b", "select", "start",
    "ok", "back", "menu"
};

/** Extra spellings accepted on the wire, mapped onto the canonical ids. */
typedef struct
{
    const char *alias;
    uint8_t     id;
} key_alias_t;

static const key_alias_t s_aliases[] =
{
    { "u",      KEY_UP     },
    { "d",      KEY_DOWN   },
    { "l",      KEY_LEFT   },
    { "r",      KEY_RIGHT  },
    { "enter",  KEY_OK     },
    { "esc",    KEY_BACK   },
    { "sel",    KEY_SELECT },
    { "home",   KEY_MENU   },
};

void bsp_key_init(void)
{
    s_head  = 0U;
    s_tail  = 0U;
    s_state = 0U;
    memset((void *)s_release_at, 0, sizeof(s_release_at));
}

static void queue_push(key_id_t id, key_edge_t edge)
{
    uint32_t next = (s_head + 1U) & KEY_QUEUE_MASK;

    if (next == s_tail)
    {
        /* Full: drop the newest event.  Losing a key beats corrupting the
         * ring or blocking inside an interrupt. */
        return;
    }

    s_queue[s_head].id   = (uint8_t)id;
    s_queue[s_head].edge = (uint8_t)edge;
    s_head               = next;
}

void bsp_key_inject(key_id_t id, key_edge_t edge)
{
    uint32_t bit;

    if ((uint32_t)id >= (uint32_t)KEY_COUNT)
    {
        return;
    }

    bit = 1UL << (uint32_t)id;

    if (edge == KEY_EV_DOWN)
    {
        /* Auto-repeat from a terminal would otherwise flood the queue with
         * duplicated downs; only the first one is an edge. */
        if ((s_state & bit) == 0U)
        {
            s_state |= bit;
            queue_push(id, KEY_EV_DOWN);
        }
        s_release_at[id] = 0U;      /* an explicit press cancels any tap timer */
    }
    else
    {
        if ((s_state & bit) != 0U)
        {
            s_state &= ~bit;
            queue_push(id, KEY_EV_UP);
        }
        s_release_at[id] = 0U;
    }
}

void bsp_key_tap(key_id_t id, uint16_t hold_ms)
{
    uint32_t hold = (hold_ms == 0U) ? KEY_TAP_DEFAULT_MS : (uint32_t)hold_ms;

    if ((uint32_t)id >= (uint32_t)KEY_COUNT)
    {
        return;
    }

    bsp_key_inject(id, KEY_EV_DOWN);

    /* Deadline of 0 means "no timer", so never hand out 0. */
    s_release_at[id] = HAL_GetTick() + hold;
    if (s_release_at[id] == 0U)
    {
        s_release_at[id] = 1U;
    }
}

void bsp_key_release_all(void)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)KEY_COUNT; i++)
    {
        bsp_key_inject((key_id_t)i, KEY_EV_UP);
    }

    s_tail = s_head;    /* discard pending edges as well */
}

void bsp_key_poll(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t i;

    for (i = 0U; i < (uint32_t)KEY_COUNT; i++)
    {
        uint32_t deadline = s_release_at[i];

        /* Signed difference so the comparison survives the 49-day tick wrap. */
        if ((deadline != 0U) && ((int32_t)(now - deadline) >= 0))
        {
            s_release_at[i] = 0U;
            bsp_key_inject((key_id_t)i, KEY_EV_UP);
        }
    }
}

int bsp_key_pop(key_event_t *ev)
{
    if ((ev == NULL) || (s_tail == s_head))
    {
        return 0;
    }

    ev->id   = s_queue[s_tail].id;
    ev->edge = s_queue[s_tail].edge;
    s_tail   = (s_tail + 1U) & KEY_QUEUE_MASK;

    return 1;
}

uint32_t bsp_key_state(void)
{
    return s_state;
}

const char *bsp_key_name(key_id_t id)
{
    if ((uint32_t)id >= (uint32_t)KEY_COUNT)
    {
        return "?";
    }
    return s_names[id];
}

int bsp_key_from_name(const char *name)
{
    uint32_t i;

    if (name == NULL)
    {
        return -1;
    }

    for (i = 0U; i < (uint32_t)KEY_COUNT; i++)
    {
        if (strcmp(name, s_names[i]) == 0)
        {
            return (int)i;
        }
    }

    for (i = 0U; i < (sizeof(s_aliases) / sizeof(s_aliases[0])); i++)
    {
        if (strcmp(name, s_aliases[i].alias) == 0)
        {
            return (int)s_aliases[i].id;
        }
    }

    return -1;
}
