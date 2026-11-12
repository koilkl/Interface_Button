#ifndef INTERFACE_BUTTON_H
#define INTERFACE_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(STM32F103xB) || defined(STM32F103xE)
#include "stm32f1xx_hal.h"
#else
#include "stm32f4xx_hal.h"
#endif

#include <stdint.h>
#include <stdbool.h>

// 使用内部上拉电阻 (按钮一端接 GPIO，另一端接 GND)
#define USE_EXTERNAL_SIGNAL_BOARD   0
#define BTN_ACTIVE_LEVEL            GPIO_PIN_RESET

#define INPUT_COUNT 4

typedef enum {
    BTN_UP      = 0,   // 加力按钮 (+0.5 kg)
    BTN_DOWN    = 1,   // 减力按钮 (-0.5 kg)
    SW_POS_LEFT = 2,   // 三态拨动开关：左档引脚
    SW_POS_RIGHT= 3    // 三态拨动开关：右档引脚
} AnkleInputChannel_t;

typedef enum {
    SW_STATE_CENTER_OFF = 0,
    SW_STATE_LEFT_ON    = 1,
    SW_STATE_RIGHT_ON   = 2
} ToggleSwitchState_t;

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} AnkleBtnPin_t;

#define PA0   (AnkleBtnPin_t){GPIOA, GPIO_PIN_0}
#define PA1   (AnkleBtnPin_t){GPIOA, GPIO_PIN_1}
#define PB0   (AnkleBtnPin_t){GPIOB, GPIO_PIN_0}
#define PB1   (AnkleBtnPin_t){GPIOB, GPIO_PIN_1}
#define PB10  (AnkleBtnPin_t){GPIOB, GPIO_PIN_10}
#define PB11  (AnkleBtnPin_t){GPIOB, GPIO_PIN_11}
#define PB12  (AnkleBtnPin_t){GPIOB, GPIO_PIN_12}
#define PB13  (AnkleBtnPin_t){GPIOB, GPIO_PIN_13}
#define PB14  (AnkleBtnPin_t){GPIOB, GPIO_PIN_14}
#define PC6   (AnkleBtnPin_t){GPIOC, GPIO_PIN_6}
#define PC7   (AnkleBtnPin_t){GPIOC, GPIO_PIN_7}
#define PD8   (AnkleBtnPin_t){GPIOD, GPIO_PIN_8}
#define PD9   (AnkleBtnPin_t){GPIOD, GPIO_PIN_9}
#define PD10  (AnkleBtnPin_t){GPIOD, GPIO_PIN_10}
#define PD11  (AnkleBtnPin_t){GPIOD, GPIO_PIN_11}
#define PD12  (AnkleBtnPin_t){GPIOD, GPIO_PIN_12}
#define PD13  (AnkleBtnPin_t){GPIOD, GPIO_PIN_13}
#define PE2   (AnkleBtnPin_t){GPIOE, GPIO_PIN_2}
#define PE3   (AnkleBtnPin_t){GPIOE, GPIO_PIN_3}
#define PE4   (AnkleBtnPin_t){GPIOE, GPIO_PIN_4}

// Status LED (PA7)：平时熄灭 (LOW)，当 Force 改变时亮起 150ms (HIGH)
#define LED_STATUS_PORT   GPIOA
#define LED_STATUS_PIN    GPIO_PIN_7
#define STATUS_LED_ON_MS  150

// 6颗 WS2812B 幻彩灯条数据脚：PB15
#define WS_PORT           GPIOB
#define WS_PIN            GPIO_PIN_15
#define NUM_LEDS          6

typedef struct {
    uint8_t             raw_value;           // 0 ~ 20 (对应 0.0 ~ 10.0 kg，步进 0.5 kg)
    uint8_t             min_raw;             // 0
    uint8_t             max_raw;             // 20
    uint8_t             display_percentage;  // 0 ~ 100%
    uint8_t             active_leds;         // 0 ~ 6
    ToggleSwitchState_t sw_state;            // 三态拨动开关档位
    bool                btn_conflict;        // 防呆1：双键同按冲突
    bool                sw_fault;            // 防呆2：拨动开关短路异常
    bool                ui_updated;          // 状态更新标志
} AnkleCtrl_t;

void AnkleUI_Init(const AnkleBtnPin_t* pin_config);
void AnkleUI_Update(AnkleCtrl_t* ctrl);

#ifdef __cplusplus
}
#endif

#endif /* INTERFACE_BUTTON_H */
