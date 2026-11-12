/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F407VETx (STM32F4VE) Main Program Body
  *                   2-Button Force (0.0~10.0kg) + 3-Way Switch + Status LED + 6-LED Strip
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "interface_button.h"
#include <stdio.h>
#include <string.h>

// 自动检测工程是否生成了 usart.h (防止在 F4VE 上没开串口导致编译报错)
#if defined(__has_include)
  #if __has_include("usart.h")
    #include "usart.h"
    #define HAS_USART_HEADER 1
  #endif
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/**
 * @brief 在这里填写你在 STM32F4VE 板子上实际插的 4 个纯净引脚！
 *        可选宏：PA0, PA1, PB0, PB1, PB10, PB11, PB12, PB13, PB14,
 *               PC6, PC7, PD8, PD9, PD10, PD11, PD12, PD13, PE2, PE3, PE4
 */
const AnkleBtnPin_t my_pins[INPUT_COUNT] = {
    [BTN_UP]       = PD8,   // 加力按钮 (+0.5 kg)
    [BTN_DOWN]     = PD9,   // 减力按钮 (-0.5 kg)
    [SW_POS_LEFT]  = PD10,  // 三态拨动开关：左档引脚
    [SW_POS_RIGHT] = PD11   // 三态拨动开关：右档引脚
};

// 全局状态结构体 (可在 VS Code Debug 的 WATCH 窗口直接输入 ankle_ctrl 实时监视！)
AnkleCtrl_t ankle_ctrl = {
    .raw_value          = 0,                    // 0 ~ 20 (对应 0.0 ~ 10.0 kg)
    .min_raw            = 0,                    // 最小 0.0 kg
    .max_raw            = 20,                   // 最大 10.0 kg
    .display_percentage = 0,                    // 0 ~ 100%
    .active_leds        = 0,                    // 0 ~ 6 颗灯珠
    .sw_state           = SW_STATE_CENTER_OFF,  // 拨动开关初始档位
    .btn_conflict       = false,                // 防呆1：双键同按冲突标志
    .sw_fault           = false,                // 防呆2：拨动开关短路标志
    .ui_updated         = false
};

static bool last_conflict_printed = false;
static bool last_fault_printed    = false;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Safe_UART_Print(const char* str);
static void UART_PrintStatus(const AnkleCtrl_t* ctrl);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Safe_UART_Print(const char* str) {
#if defined(HAS_USART_HEADER)
    // 如果开了 USART2，通过 huart2 发送
    extern UART_HandleTypeDef huart2 __attribute__((weak));
    if (&huart2 != NULL && huart2.Instance != NULL) {
        HAL_UART_Transmit(&huart2, (uint8_t*)str, (uint16_t)strlen(str), 50);
    }
#else
    (void)str;
#endif
}

static const char* Get_SwitchStateName(ToggleSwitchState_t state) {
    switch (state) {
        case SW_STATE_LEFT_ON:    return "LEFT_ON  (Mode 1)";
        case SW_STATE_RIGHT_ON:   return "RIGHT_ON (Mode 2)";
        case SW_STATE_CENTER_OFF:
        default:                  return "CENTER_OFF (Idle)";
    }
}

static void UART_PrintStatus(const AnkleCtrl_t* ctrl) {
    char buf[160];
    int len = snprintf(buf, sizeof(buf),
                       "[F4VE UI] Switch: %-17s | Force: %2d.%d kg | Strip: %3d%% (%d/6 LEDs)\r\n",
                       Get_SwitchStateName(ctrl->sw_state),
                       ctrl->raw_value / 2,
                       (ctrl->raw_value % 2) * 5,
                       ctrl->display_percentage,
                       ctrl->active_leds);
    if (len > 0) {
        Safe_UART_Print(buf);
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
#if defined(HAS_USART_HEADER)
  extern void MX_USART2_UART_Init(void) __attribute__((weak));
  if (MX_USART2_UART_Init != NULL) {
      MX_USART2_UART_Init();
  }
#endif

  /* USER CODE BEGIN 2 */
  // 初始化 4 路输入、板载双 LED (PA6常亮 / PA7变力闪烁) 与 6 珠 WS2812B 灯带 (PB15)
  AnkleUI_Init(my_pins);

  const char* banner =
      "\r\n======================================================================\r\n"
      "  STM32F4VE (F407VET6) Ankle Trainer Controller Ready!\r\n"
      "  [Inputs]  UP=PD8 | DOWN=PD9 | SW_L=PD10 | SW_R=PD11 (NOPULL High-Z)\r\n"
      "  [Outputs] PowerLED=PA6 | StatusLED=PA7 | 6-LED Strip=PB15\r\n"
      "======================================================================\r\n";
  Safe_UART_Print(banner);

  // 开机同步一次拨动开关状态
  AnkleUI_Update(&ankle_ctrl);
  UART_PrintStatus(&ankle_ctrl);
  ankle_ctrl.ui_updated = false;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // 非阻塞轮询按钮、三态拨动开关、防呆互锁与 6 珠灯带更新
    AnkleUI_Update(&ankle_ctrl);

    // 防呆报警 1：加减力按钮同时按下互斥
    if (ankle_ctrl.btn_conflict && !last_conflict_printed) {
        Safe_UART_Print("[POKA-YOKE] WARNING: UP & DOWN pressed together! Force locked.\r\n");
        last_conflict_printed = true;
    } else if (!ankle_ctrl.btn_conflict) {
        last_conflict_printed = false;
    }

    // 防呆报警 2：三态拨动开关左右两路同时短路异常
    if (ankle_ctrl.sw_fault && !last_fault_printed) {
        Safe_UART_Print("[POKA-YOKE] FAULT: Switch LEFT & RIGHT shorted! Fallback to OFF.\r\n");
        last_fault_printed = true;
    } else if (!ankle_ctrl.sw_fault) {
        last_fault_printed = false;
    }

    // 状态或力度发生变化时更新输出
    if (ankle_ctrl.ui_updated) {
        UART_PrintStatus(&ankle_ctrl);
        ankle_ctrl.ui_updated = false;
    }

    HAL_Delay(1);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  *        严格对应你截图中的 STM32F407VETx 84MHz HSI 时钟树配置：
  *        HSI (16MHz) -> PLLM (/8) -> PLLN (*84) -> PLLP (/2) = 84MHz SYSCLK
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM            = 8;
  RCC_OscInitStruct.PLL.PLLN            = 84;
  RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ            = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
