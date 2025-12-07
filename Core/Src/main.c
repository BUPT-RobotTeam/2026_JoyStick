/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <main.h>
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
// 8个按钮对应的引脚配置
#define BUTTON_COUNT 8
const uint16_t button_pins[BUTTON_COUNT] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_3, GPIO_PIN_4,
    GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_8
};

volatile uint8_t key_exti_flag[BUTTON_COUNT] = {0};   // 每个按钮的EXTI中断标志
volatile uint8_t key_stable_state[BUTTON_COUNT] = {0}; // 每个按钮的消抖后状态（0=释放，1=按下）
volatile uint16_t debounce_cnt[BUTTON_COUNT] = {0};   // 每个按钮的消抖计数器

// ADC 校准偏移值
uint16_t adc_offset[4] = {0}; // 4个通道的偏移值
uint8_t calib_count = 0;     // 校准采样计数
uint32_t calib_sum[4] = {0}; // 校准采样累加和

// 统一数据包结构体
typedef struct {
    uint8_t buttons[BUTTON_COUNT];  // 8个按钮状态
    int16_t left_Y;                 // 左摇杆Y轴
    int16_t left_X;                 // 左摇杆X轴
    int16_t right_Y;                // 右摇杆Y轴
    int16_t right_X;                // 右摇杆X轴
} GamepadData_t;

volatile GamepadData_t gamepad_data = {0}; // 手柄数据包
volatile uint8_t adc_ready_flag = 0;       // ADC数据就绪标志
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void ADC_Calibration(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint16_t values[4];
char message[80] = "";  // 增加buffer大小以容纳统一数据包

//TIM6定时器中断每1s反转一下gpiopc13电平用于指示芯片正常工作
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM6) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  }

  if (htim->Instance == TIM3)
    {
        static uint32_t tick_1kHz = 0;
        ++tick_1kHz;
  
        /* ---- 8个按钮的消抖处理 ---- */
        for (uint8_t i = 0; i < BUTTON_COUNT; i++)
        {
            // 处理中断标志
            if (key_exti_flag[i])
            {
                key_exti_flag[i] = 0;
                debounce_cnt[i] = 15;  // 15 ms 后再采样
            }
      
            // 消抖计数
            if (debounce_cnt[i])
            {
                --debounce_cnt[i];
                if (debounce_cnt[i] == 0)
                {
                    // 再次读引脚，低=按下，高=释放
                    if (HAL_GPIO_ReadPin(GPIOB, button_pins[i]) == GPIO_PIN_RESET)
                        key_stable_state[i] = 1;  // 按下状态
                    else
                        key_stable_state[i] = 0;  // 释放状态
                }
            }
        }
  
        // 定期检查按钮状态,用于保证按钮持续按下时状态不变
        static uint32_t last_check_tick = 0;
        if (tick_1kHz - last_check_tick >= 100)  // 100ms
        {
            last_check_tick = tick_1kHz;
            for (uint8_t i = 0; i < BUTTON_COUNT; i++)
            {
                uint8_t current_state = (HAL_GPIO_ReadPin(GPIOB, button_pins[i]) == GPIO_PIN_RESET) ? 1 : 0;
                if (current_state != key_stable_state[i] && debounce_cnt[i] == 0)
                {
                    // 状态改变且不在消抖期，立即更新
                    key_stable_state[i] = current_state;
                }
            }
        }
  
        /* ---- 50 Hz 采集手柄数据 ---- */
        if (tick_1kHz % 20 == 0)      // 20 ms 到
        {
            // 更新按钮状态到数据包
            for (uint8_t i = 0; i < BUTTON_COUNT; i++)
            {
                gamepad_data.buttons[i] = key_stable_state[i];
            }
            
            // 触发ADC采集（在ADC完成回调中会组合发送完整数据包）
            HAL_ADC_Start_DMA(&hadc1, (uint32_t*)values, 4);
        }
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc){
  if (hadc==&hadc1) {
    // 使用校准后的ADC值进行计算，范围-100到100，中心为0
    // left_Y, left_X, right_Y, right_X
    int16_t left_Y = ((int32_t)(values[2] - adc_offset[2]) * 100) / 2048;
    int16_t left_X = -((int32_t)(values[3] - adc_offset[3]) * 100) / 2048;
    int16_t right_Y = -((int32_t)(values[0] - adc_offset[0]) * 100) / 2048;
    int16_t right_X = ((int32_t)(values[1] - adc_offset[1]) * 100) / 2048;

    // 死区处理：绝对值小于5时认为是误差，输出为0
    if (abs(left_Y) < 7) left_Y = 0;
    if (abs(left_X) < 7) left_X = 0;
    if (abs(right_Y) < 7) right_Y = 0;
    if (abs(right_X) < 7) right_X = 0;

    // 更新摇杆数据
    gamepad_data.left_Y = left_Y;
    gamepad_data.left_X = left_X;
    gamepad_data.right_Y = right_Y;
    gamepad_data.right_X = right_X;
    
    // 组合发送统一数据包
    sprintf(message,"B:%d%d%d%d%d%d%d%d,A:%d,%d,%d,%d\n", 
            gamepad_data.buttons[0], gamepad_data.buttons[1],
            gamepad_data.buttons[2], gamepad_data.buttons[3],
            gamepad_data.buttons[4], gamepad_data.buttons[5],
            gamepad_data.buttons[6], gamepad_data.buttons[7],
            gamepad_data.left_Y, gamepad_data.left_X,
            gamepad_data.right_Y, gamepad_data.right_X);
    
    HAL_UART_Transmit_DMA(&huart3,(uint8_t*)message,strlen(message));
  }
}

void ADC_Calibration(void)
{
    uint16_t temp_values[4];


    // 初始化校准变量
    memset(calib_sum, 0, sizeof(calib_sum));
    calib_count = 0;

    // 读取20个ADC采样值进行校准
    for(int i = 0; i < 20; i++)
    {
        HAL_ADC_Start_DMA(&hadc1, (uint32_t*)temp_values, 4);
        HAL_Delay(10); // 等待ADC转换完成

        for(int ch = 0; ch < 4; ch++)
        {
            calib_sum[ch] += temp_values[ch];
        }
        calib_count++;
    }

    // 计算平均值作为偏移
    for(int ch = 0; ch < 4; ch++)
    {
        adc_offset[ch] = calib_sum[ch] / calib_count;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // 检查是哪个按钮触发了中断
    for (uint8_t i = 0; i < BUTTON_COUNT; i++)
    {
        if (GPIO_Pin == button_pins[i])
        {
            key_exti_flag[i] = 1;  // 仅置位对应按钮的标志，立即退出
            break;
        }
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

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_TIM3_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  // ADC校准：获取摇杆中位偏移值
  ADC_Calibration();
  
  // 启动定时器
  HAL_TIM_Base_Start_IT(&htim3);
  HAL_TIM_Base_Start_IT(&htim6);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
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
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
