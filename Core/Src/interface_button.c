#include "interface_button.h"

typedef struct {
    AnkleBtnPin_t pin_cfg;
    GPIO_PinState current_reading;
    GPIO_PinState stable_state;
    uint32_t      last_debounce_ms;
    uint32_t      press_start_ms;
    uint32_t      last_repeat_ms;
    bool          is_active;
    bool          armed;
} InputInternal_t;

static InputInternal_t inputs[INPUT_COUNT];

static bool     status_led_active   = false;
static uint32_t status_led_start_ms = 0;

static ToggleSwitchState_t last_sw_state = SW_STATE_CENTER_OFF;
static bool                first_sw_sync = true;

#define DEBOUNCE_DELAY_MS     20
#define AUTO_REPEAT_DELAY_MS  500
#define AUTO_REPEAT_RATE_MS   150

static uint8_t rgb_data[NUM_LEDS][3];

static const uint8_t COLOR_MAP[NUM_LEDS][3] = {
    {150, 0,   0},   // 第 1 颗：纯绿色
    {130, 40,  0},   // 第 2 颗：草绿色
    {110, 100, 0},   // 第 3 颗：亮黄色
    {60,  150, 0},   // 第 4 颗：亮橙色
    {20,  150, 0},   // 第 5 颗：深橙红
    {0,   150, 0}    // 第 6 颗：纯红色
};

static void Enable_GPIO_Clock(GPIO_TypeDef* port) {
    if (port == GPIOA)      __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
#ifdef GPIOE
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
#endif
}

__attribute__((optimize("O2"))) static void WS2812_Show(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    const uint32_t clk_mhz    = SystemCoreClock / 1000000UL;
    const uint32_t t0h_cycles = (clk_mhz * 350) / 1000;
    const uint32_t t1h_cycles = (clk_mhz * 800) / 1000;
    const uint32_t tot_cycles = (clk_mhz * 1250) / 1000;
    const uint32_t rst_cycles = clk_mhz * 500;

    __disable_irq();

    for (int i = 0; i < NUM_LEDS; i++) {
        for (int c = 0; c < 3; c++) {
            uint8_t byte = rgb_data[i][c];
            for (int b = 7; b >= 0; b--) {
                uint32_t start_cycle = DWT->CYCCNT;
                WS_PORT->BSRR = WS_PIN;

                uint32_t high_wait = ((byte >> b) & 0x01) ? t1h_cycles : t0h_cycles;
                while ((DWT->CYCCNT - start_cycle) < high_wait) {}

                WS_PORT->BSRR = ((uint32_t)WS_PIN << 16);

                while ((DWT->CYCCNT - start_cycle) < tot_cycles) {}
            }
        }
    }

    uint32_t rst_start = DWT->CYCCNT;
    WS_PORT->BSRR = ((uint32_t)WS_PIN << 16);
    while ((DWT->CYCCNT - rst_start) < rst_cycles) {}

    __enable_irq();
}

static void WS2812_SetLED(uint8_t index, uint8_t g, uint8_t r, uint8_t b) {
    if (index >= NUM_LEDS) return;
    rgb_data[index][0] = g;
    rgb_data[index][1] = r;
    rgb_data[index][2] = b;
}

static uint8_t WS2812_UpdateStrip(uint8_t raw_value) {
    uint8_t num_lit = 0;
    if (raw_value > 0) {
        num_lit = (uint8_t)(((uint16_t)raw_value * NUM_LEDS + 10) / 20);
        if (num_lit > NUM_LEDS) num_lit = NUM_LEDS;
    }

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        if (i < num_lit) {
            WS2812_SetLED(i, COLOR_MAP[i][0], COLOR_MAP[i][1], COLOR_MAP[i][2]);
        } else {
            // 当力为 0 或未达到该档位时，彻底熄灭为 (0,0,0)，不再发蓝紫光！
            WS2812_SetLED(i, 0, 0, 0);
        }
    }

    WS2812_Show();
    return num_lit;
}

static bool Step_ForceValue(AnkleCtrl_t* ctrl, int8_t dir) {
    if (dir > 0) {
        if (ctrl->raw_value < ctrl->max_raw) {
            ctrl->raw_value++;
            return true;
        }
    } else if (dir < 0) {
        if (ctrl->raw_value > ctrl->min_raw) {
            ctrl->raw_value--;
            return true;
        }
    }
    return false;
}

static void Trigger_StatusLED(uint32_t now) {
    // 只有当 Force 改变时，输出高电平 (SET) 点亮 Status LED！
    HAL_GPIO_WritePin(LED_STATUS_PORT, LED_STATUS_PIN, GPIO_PIN_SET);
    status_led_active   = true;
    status_led_start_ms = now;
}

void AnkleUI_Init(const AnkleBtnPin_t* pin_config) {
    uint32_t now = HAL_GetTick();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_PinState inactive_level = (BTN_ACTIVE_LEVEL == GPIO_PIN_RESET) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    for (int i = 0; i < INPUT_COUNT; i++) {
        inputs[i].pin_cfg = pin_config[i];
        Enable_GPIO_Clock(inputs[i].pin_cfg.port);

        GPIO_InitStruct.Pin   = inputs[i].pin_cfg.pin;
        GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull  = (BTN_ACTIVE_LEVEL == GPIO_PIN_RESET) ? GPIO_PULLUP : GPIO_PULLDOWN;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(inputs[i].pin_cfg.port, &GPIO_InitStruct);

        GPIO_PinState init_pin = HAL_GPIO_ReadPin(inputs[i].pin_cfg.port, inputs[i].pin_cfg.pin);
        inputs[i].current_reading  = init_pin;
        inputs[i].stable_state     = init_pin;
        inputs[i].last_debounce_ms = now;
        inputs[i].press_start_ms   = 0;
        inputs[i].last_repeat_ms   = 0;

        if (i == BTN_UP || i == BTN_DOWN) {
            inputs[i].is_active = false;
            inputs[i].armed     = (init_pin == inactive_level);
        } else {
            inputs[i].is_active = (init_pin == BTN_ACTIVE_LEVEL);
            inputs[i].armed     = true;
        }
    }

    // 初始化 Status LED (PA7)，开机默认输出低电平 (RESET) 熄灭！
    Enable_GPIO_Clock(LED_STATUS_PORT);
    GPIO_InitStruct.Pin   = LED_STATUS_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_STATUS_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_STATUS_PORT, LED_STATUS_PIN, GPIO_PIN_RESET);

    // 初始化 6 珠 WS2812B 灯条引脚 (PB15)
    Enable_GPIO_Clock(WS_PORT);
    GPIO_InitStruct.Pin   = WS_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(WS_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(WS_PORT, WS_PIN, GPIO_PIN_RESET);
    HAL_Delay(5);

    // 开机自检后，0kg 状态全部灯珠熄灭
    for (uint8_t step = 4; step <= 20; step += 4) {
        WS2812_UpdateStrip(step);
        HAL_Delay(50);
    }
    WS2812_UpdateStrip(0);
}

void AnkleUI_Update(AnkleCtrl_t* ctrl) {
    uint32_t now = HAL_GetTick();
    bool force_changed = false;
    bool single_edge_trig[2] = {false, false};
    GPIO_PinState inactive_level = (BTN_ACTIVE_LEVEL == GPIO_PIN_RESET) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    if (ctrl->max_raw == 0) ctrl->max_raw = 20;
    if (ctrl->raw_value > ctrl->max_raw) ctrl->raw_value = ctrl->max_raw;

    // 150ms 到期后，自动把 Status LED 拉回低电平 (RESET) 熄灭！
    if (status_led_active && ((now - status_led_start_ms) >= STATUS_LED_ON_MS)) {
        HAL_GPIO_WritePin(LED_STATUS_PORT, LED_STATUS_PIN, GPIO_PIN_RESET);
        status_led_active = false;
    }

    for (int i = 0; i < INPUT_COUNT; i++) {
        GPIO_PinState raw_pin = HAL_GPIO_ReadPin(inputs[i].pin_cfg.port, inputs[i].pin_cfg.pin);

        if (raw_pin != inputs[i].current_reading) {
            inputs[i].last_debounce_ms = now;
            inputs[i].current_reading  = raw_pin;
        }

        if ((now - inputs[i].last_debounce_ms) >= DEBOUNCE_DELAY_MS) {
            if (!inputs[i].armed) {
                if (raw_pin == inactive_level) {
                    inputs[i].armed        = true;
                    inputs[i].stable_state = inactive_level;
                    inputs[i].is_active    = false;
                }
                continue;
            }

            if (raw_pin != inputs[i].stable_state) {
                inputs[i].stable_state = raw_pin;

                if (inputs[i].stable_state == BTN_ACTIVE_LEVEL) {
                    inputs[i].is_active      = true;
                    inputs[i].press_start_ms = now;
                    inputs[i].last_repeat_ms = now;
                    if (i == BTN_UP || i == BTN_DOWN) {
                        single_edge_trig[i] = true;
                    }
                } else {
                    inputs[i].is_active      = false;
                    inputs[i].press_start_ms = 0;
                    inputs[i].last_repeat_ms = 0;
                }
            }
        }
    }

    bool left_on  = inputs[SW_POS_LEFT].is_active;
    bool right_on = inputs[SW_POS_RIGHT].is_active;

    ToggleSwitchState_t new_sw_state = SW_STATE_CENTER_OFF;
    if (left_on && right_on) {
        ctrl->sw_fault = true;
        new_sw_state   = SW_STATE_CENTER_OFF;
    } else {
        ctrl->sw_fault = false;
        if (left_on)       new_sw_state = SW_STATE_LEFT_ON;
        else if (right_on) new_sw_state = SW_STATE_RIGHT_ON;
        else               new_sw_state = SW_STATE_CENTER_OFF;
    }

    if (first_sw_sync || (new_sw_state != last_sw_state)) {
        ctrl->sw_state   = new_sw_state;
        last_sw_state    = new_sw_state;
        first_sw_sync    = false;
        ctrl->ui_updated = true;
    }

    if (inputs[BTN_UP].is_active && inputs[BTN_DOWN].is_active) {
        inputs[BTN_UP].press_start_ms   = now;
        inputs[BTN_UP].last_repeat_ms   = now;
        inputs[BTN_DOWN].press_start_ms = now;
        inputs[BTN_DOWN].last_repeat_ms = now;
        ctrl->btn_conflict              = true;
        return;
    }
    ctrl->btn_conflict = false;

    for (int i = 0; i < 2; i++) {
        int8_t dir = (i == BTN_UP) ? +1 : -1;

        if (single_edge_trig[i]) {
            if (Step_ForceValue(ctrl, dir)) {
                force_changed = true;
            }
        } else if (inputs[i].is_active) {
            if ((now - inputs[i].press_start_ms) >= AUTO_REPEAT_DELAY_MS) {
                if ((now - inputs[i].last_repeat_ms) >= AUTO_REPEAT_RATE_MS) {
                    inputs[i].last_repeat_ms = now;
                    if (Step_ForceValue(ctrl, dir)) {
                        force_changed = true;
                    }
                }
            }
        }
    }

    if (force_changed) {
        Trigger_StatusLED(now);
        ctrl->display_percentage = (uint8_t)(((uint16_t)ctrl->raw_value * 100) / ctrl->max_raw);
        ctrl->active_leds        = WS2812_UpdateStrip(ctrl->raw_value);
        ctrl->ui_updated         = true;
    }
}
