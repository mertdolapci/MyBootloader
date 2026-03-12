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
#include <stdio.h>
#include <string.h>
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define DHT22_PORT GPIOB
#define DHT22_PIN  GPIO_PIN_1

#define DHT22_TIMEOUT 100 // Timeout for sensor response (us)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

void delay_us(uint16_t us);

uint8_t DHT22_ReadData(void);

void DHT22_SetPinOutput(void);
void DHT22_SetPinInput(void);

float DHT22_GetTemperature(void);
float DHT22_GetHumidity(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


int _write(int file, char *ptr, int len)
{
	int DataIdx;

	for (DataIdx = 0; DataIdx < len; DataIdx++)
	{
		ITM_SendChar(*ptr++);

	}
	return len;
}

/*int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}*/


void Flash_Erase_AppArea(void)
{
    HAL_FLASH_Unlock();  // Flash yazma/silme korumasını kaldır

    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;

    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;  // 2.7–3.6V için
    EraseInitStruct.Sector = FLASH_SECTOR_3;               // 0x0800C000
    EraseInitStruct.NbSectors = 1;                         //  Sadece S3
    HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    HAL_FLASH_Lock();  // Flash korumasını tekrar aç
}

void Flash_Write(uint32_t address, uint8_t *data, uint32_t length)
{
    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < length; i += 1)
    {
        uint8_t word = 0xFF;
        memcpy(&word, data + i, 1);  // Copy 1 Byte

        HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address + i, word);


        uint8_t verify = *(uint8_t*)address;
        if (status != HAL_OK || verify != word)
        {
            // Error Handling
        	printf("[APP] >>> Flash Write Error at 0x%08lX (Exp=0x%02X, Got=0x%02X)\r\n",
        	                   address + i, word, verify);
            break;
        }
        else
        {
        	printf("[APP] >>> Flash Write Successful for Value: 0x%02X\r\n", *data);
        }
    }


    HAL_FLASH_Lock();
}

void delay_us(uint16_t us) {
    __HAL_TIM_SET_COUNTER(&htim2, 0);  // Sayaç sıfırla
    HAL_TIM_Base_Start(&htim2);        // Start timer
    while (__HAL_TIM_GET_COUNTER(&htim2) < us);  // Wait until time elapsed
    HAL_TIM_Base_Stop(&htim2);         // Stop timer
}

uint8_t DHT22_data[5];  // nem ve sıcaklık için 5 byte

/*

void delay_test(void)
{
    uint32_t start = HAL_GetTick(); // ms cinsinden
    for (int i = 0; i < 1000; i++) {
        delay_us(1000); // 1 ms toplamda 1000 defa
    }
    uint32_t end = HAL_GetTick();
    //printf("Geçen süre: %lu ms\r\n", end - start);
}

*/

void DHT22_SetPinOutput(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT22_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT22_PORT, &GPIO_InitStruct);
}

void DHT22_SetPinInput(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT22_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT22_PORT, &GPIO_InitStruct);
}

uint8_t DHT22_ReadData(void) {
  uint8_t byte_index, bit;
  uint32_t timeout;

  // --- DHT22 Start Signal ---
  DHT22_SetPinOutput();
  HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_RESET);
  delay_us(1000); // At least 1ms for start signal
  HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET);

  DHT22_SetPinInput();
  delay_us(30); // Wait for sensor response

  // --- Wait for sensor response (LOW) ---
  timeout = 0;
  while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) && (timeout++ < DHT22_TIMEOUT))
    delay_us(1);
  if (timeout >= DHT22_TIMEOUT)
    return 0; // No response from sensor

  // --- Wait for sensor response (HIGH) ---
  timeout = 0;
  while (!HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) && timeout++ < DHT22_TIMEOUT)
    delay_us(1);
  while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) && timeout++ < DHT22_TIMEOUT)
    delay_us(1);

  // --- Read 40 bits (5 bytes) ---
  for (byte_index = 0; byte_index < 5; byte_index++) {
    uint8_t byte = 0;
    for (bit = 0; bit < 8; bit++) {
      // Wait for LOW
      while (!HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN));
      delay_us(30); // Wait for bit duration
      if (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN))
        byte |= (1 << (7 - bit));
      // Wait for HIGH to end
      timeout = 0;
      while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) && timeout++ < DHT22_TIMEOUT)
        delay_us(1);
    }
    DHT22_data[byte_index] = byte;
  }

  // --- Checksum validation ---
  if (DHT22_data[4] != ((DHT22_data[0] + DHT22_data[1] + DHT22_data[2] + DHT22_data[3]) & 0xFF))
    return 0; // Invalid data

  return 1; // Success
}

float DHT22_GetTemperature(void) {
    int16_t temp = (DHT22_data[2] & 0x7F) << 8 | DHT22_data[3];
    float temperature = temp / 10.0;

    if (DHT22_data[2] & 0x80)
    	temperature *= -1;

    return temperature;
}

float DHT22_GetHumidity(void) {
    uint16_t hum = DHT22_data[0] << 8 | DHT22_data[1];
    return hum / 10.0;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */

#define CONFIG_ADDRESS 0x0800C000

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
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  HAL_TIM_Base_Start(&htim2);
  __HAL_TIM_SET_COUNTER(&htim2, 0);

  printf("\r\n");
  printf("---------------------------------------------------------\r\n");
  printf("                 Application Firmware v2.0               \r\n");
  printf("                 Running user program...                 \r\n");
  printf("---------------------------------------------------------\r\n");

  printf("[APP] >>> Configuration Write to Flash...\r\n");
  uint8_t FlashVal = 0xAC;

  Flash_Erase_AppArea();
  Flash_Write(CONFIG_ADDRESS, &FlashVal, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	// delay_test();
    if (DHT22_ReadData()) {
        float temp = DHT22_GetTemperature();
        float hum  = DHT22_GetHumidity();
        printf("[APP] >>> Temp: %.1f °C, Hum: %.1f %%\r\n", temp, hum);
    } else {
    	printf("[APP] >>> DHT22 Read Error!\r\n");
    }
    HAL_Delay(2000);
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 15;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
  /* Report error via UART and halt */
  const char *msg = "Error occurred!\r\n";
  HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
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

