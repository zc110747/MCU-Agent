/* ---------------------------------------------------------------------------
 * bsp_button.c - polled, debounced GPIO buttons.
 * -------------------------------------------------------------------------*/

#include "bsp_button.h"
#include "mouse_report.h"

typedef struct
{
    GPIO_TypeDef *port;     /* NULL = this button is not fitted            */
    uint16_t      pin;
    uint8_t       mask;     /* MOUSE_BUTTON_* bit for this button           */
} button_hw_t;

typedef struct
{
    uint8_t  stable;        /* debounced state, 1 = pressed                 */
    uint8_t  raw;           /* level seen during the last poll              */
    uint32_t change_ms;     /* when `raw` last differed from `stable`       */
    uint32_t press_ms;      /* when `stable` last became 1                  */
} button_state_t;

/* Indexed by bsp_button_id_t. A NULL port means "not wired on this board";
 * poll() and the accessors skip such entries, so enabling a button later is
 * a header-only change. */
static const button_hw_t s_hw[BSP_BUTTON_ID_COUNT] =
{
    { BTN_LEFT_PORT,  BTN_LEFT_PIN,  MOUSE_BUTTON_LEFT  },
    { BTN_RIGHT_PORT, BTN_RIGHT_PIN, MOUSE_BUTTON_RIGHT },
#if BSP_BUTTON_MIDDLE_ENABLED
    { BTN_MIDDLE_PORT, BTN_MIDDLE_PIN, MOUSE_BUTTON_MIDDLE }
#else
    { 0,              0u,              MOUSE_BUTTON_MIDDLE }
#endif
};

static button_state_t s_state[BSP_BUTTON_ID_COUNT] =
{
    { 0u, 0u, 0u, 0u },
    { 0u, 0u, 0u, 0u },
    { 0u, 0u, 0u, 0u }
};

static bool read_pressed(uint32_t index)
{
    const button_hw_t *hw = &s_hw[index];
    GPIO_PinState level;

    if (hw->port == 0)
    {
        return false;
    }

    level = HAL_GPIO_ReadPin(hw->port, hw->pin);

#if BSP_BUTTON_ACTIVE_LOW
    return (level == GPIO_PIN_RESET);
#else
    return (level == GPIO_PIN_SET);
#endif
}

void bsp_button_init(void)
{
    GPIO_InitTypeDef gpio = {0u};

    BTN_LEFT_CLK_EN();
    BTN_RIGHT_CLK_EN();
#if BSP_BUTTON_MIDDLE_ENABLED
    BTN_MIDDLE_CLK_EN();
#endif

    /* LEFT and RIGHT are initialised in one call, so they must share a port.
     * The static assertion below turns a future rewiring mistake into a
     * compile error instead of a pin that silently stays floating. */
    _Static_assert(BTN_LEFT_PORT == BTN_RIGHT_PORT,
                   "left/right buttons must share a GPIO port");

    for (uint32_t i = 0u; i < (uint32_t) BSP_BUTTON_ID_COUNT; i++)
    {
        s_state[i].stable    = 0u;
        s_state[i].raw       = 0u;
        s_state[i].change_ms = 0u;
        s_state[i].press_ms  = 0u;
    }

    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;      /* idle high, pressed pulls to GND */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = BTN_LEFT_PIN | BTN_RIGHT_PIN;
    HAL_GPIO_Init(BTN_LEFT_PORT, &gpio);

#if BSP_BUTTON_MIDDLE_ENABLED
    gpio.Pin = BTN_MIDDLE_PIN;
    HAL_GPIO_Init(BTN_MIDDLE_PORT, &gpio);
#endif
}

void bsp_button_poll(uint32_t now_ms)
{
    for (uint32_t i = 0u; i < (uint32_t) BSP_BUTTON_ID_COUNT; i++)
    {
        button_state_t *st = &s_state[i];
        bool pressed;

        if (s_hw[i].port == 0)
        {
            continue;
        }

        pressed = read_pressed(i);

        if ((uint8_t) pressed != st->raw)
        {
            st->raw       = (uint8_t) pressed;
            st->change_ms = now_ms;
        }

        /* The level has to hold for the full window before it counts. A bounce
         * burst keeps resetting change_ms, so it never reaches stability. */
        if (((uint8_t) pressed != st->stable) &&
            ((now_ms - st->change_ms) >= BSP_BUTTON_DEBOUNCE_MS))
        {
            st->stable = (uint8_t) pressed;
            if (st->stable != 0u)
            {
                st->press_ms = now_ms;
            }
        }
    }
}

uint8_t bsp_button_get_buttons(void)
{
    uint8_t mask = 0u;

    for (uint32_t i = 0u; i < (uint32_t) BSP_BUTTON_ID_COUNT; i++)
    {
        if (s_hw[i].port == 0)
        {
            continue;
        }
        if (s_state[i].stable != 0u)
        {
            mask |= s_hw[i].mask;
        }
    }

    return mask;
}

uint32_t bsp_button_hold_ms(bsp_button_id_t id, uint32_t now_ms)
{
    if ((uint32_t) id >= (uint32_t) BSP_BUTTON_ID_COUNT)
    {
        return 0u;
    }
    if (s_state[id].stable == 0u)
    {
        return 0u;
    }

    return now_ms - s_state[id].press_ms;
}

bool bsp_button_raw_pressed(bsp_button_id_t id)
{
    if ((uint32_t) id >= (uint32_t) BSP_BUTTON_ID_COUNT)
    {
        return false;
    }

    return read_pressed((uint32_t) id);
}
