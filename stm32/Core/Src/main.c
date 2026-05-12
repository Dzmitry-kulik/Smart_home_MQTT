 /* USER CODE BEGIN Header */

/**

******************************************************************************

* @file : main.c

* @brief : STM32F411 with fake DHT11/BH1750, real MQ-2 & servo, UART to ESP

******************************************************************************

*/

/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "main.h"

#include <stdio.h>

#include <stdlib.h>

#include <string.h>


/* Private includes ----------------------------------------------------------*/

/* USER CODE BEGIN Includes */


/* USER CODE END Includes */


/* Private typedef -----------------------------------------------------------*/

/* USER CODE BEGIN PTD */


/* USER CODE END PTD */


/* Private define ------------------------------------------------------------*/

/* USER CODE BEGIN PD */

#define CMD_BUF_SIZE 64

#define DHT11_OK 0

#define DHT11_ERROR 1


// Константы для люксметра BH1750

#define BH1750_ADDR (0x23 << 1)

#define BH1750_POWER_ON 0x01

#define BH1750_OT_H_MODE 0x20


// Константы для Сервопривода (для таймера с периодом 20мс)

// Подберите под ваш таймер (обычно для 50 Гц: 500-2500 или 50-250)

#define SERVO_MIN_PULSE 500

#define SERVO_MAX_PULSE 2500

/* USER CODE END PD */


/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PM */


/* USER CODE END PM */


/* Private variables ---------------------------------------------------------*/

ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;

TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;


/* USER CODE BEGIN PV */

float dht_temperature = 0.0;

float dht_humidity = 0.0;

uint8_t servo_angle = 90;


char cmd_buffer[CMD_BUF_SIZE];

uint8_t cmd_index = 0;

uint8_t cmd_ready = 0;


uint32_t fake_rand_seed = 0;


// ИСПРАВЛЕНО: Глобальный буфер для корректной работы прерываний UART

uint8_t rx_byte = 0;

/* USER CODE END PV */


/* Private function prototypes -----------------------------------------------*/

void SystemClock_Config(void);

static void MX_GPIO_Init(void);

static void MX_USART1_UART_Init(void);

static void MX_ADC1_Init(void);

static void MX_I2C1_Init(void);

static void MX_TIM2_Init(void);

static void MX_TIM3_Init(void);

/* USER CODE BEGIN PFP */

uint8_t DHT11_Read(void);

void BH1750_Init(void);

float BH1750_ReadLux(void);

void Servo_SetAngle(uint8_t angle);

void delay_dht11(uint32_t us);

void ProcessCommand(char *cmd);

uint32_t fake_rand(void);

/* USER CODE END PFP */


/* Private user code ---------------------------------------------------------*/

/* USER CODE BEGIN 0 */

void delay_dht11(uint32_t us) {

__HAL_TIM_SET_COUNTER(&htim3, 0);

while (__HAL_TIM_GET_COUNTER(&htim3) < us);

}


void Set_Pin_Output(void) {

GPIO_InitTypeDef GPIO_InitStruct = {0};

GPIO_InitStruct.Pin = GPIO_PIN_1;

GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;

GPIO_InitStruct.Pull = GPIO_PULLUP;

GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}


void Set_Pin_Input(void) {

GPIO_InitTypeDef GPIO_InitStruct = {0};

GPIO_InitStruct.Pin = GPIO_PIN_1;

GPIO_InitStruct.Mode = GPIO_MODE_INPUT;

GPIO_InitStruct.Pull = GPIO_PULLUP;

HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}


uint8_t DHT11_Read(void) {

uint8_t bits[5] = {0};

Set_Pin_Output();

HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);

HAL_Delay(18);

HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);

delay_dht11(30);

Set_Pin_Input();


uint32_t timeout = 0;

while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET && timeout++ < 1000) delay_dht11(1);

if(timeout >= 1000) return DHT11_ERROR;

timeout = 0;

while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET && timeout++ < 1000) delay_dht11(1);

if(timeout >= 1000) return DHT11_ERROR;

timeout = 0;

while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET && timeout++ < 1000) delay_dht11(1);

if(timeout >= 1000) return DHT11_ERROR;


for(uint8_t i = 0; i < 5; i++) {

for(uint8_t j = 0; j < 8; j++) {

timeout = 0;

while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET && timeout++ < 1000) delay_dht11(1);

if(timeout >= 1000) return DHT11_ERROR;

delay_dht11(30);

if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET) {

bits[i] |= (1 << (7 - j));

timeout = 0;

while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET && timeout++ < 1000) delay_dht11(1);

if(timeout >= 1000) return DHT11_ERROR;

}

}

}


if(bits[4] == ((bits[0] + bits[1] + bits[2] + bits[3]) & 0xFF)) {

dht_humidity = (float)bits[0];

dht_temperature = (float)bits[2];

return DHT11_OK;

}

return DHT11_ERROR;

}


void BH1750_Init(void) {

uint8_t cmd = BH1750_POWER_ON;

HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, HAL_MAX_DELAY);

HAL_Delay(10);

}


float BH1750_ReadLux(void) {

uint8_t rx_data[2];

uint8_t cmd = BH1750_OT_H_MODE;

if (HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, HAL_MAX_DELAY) != HAL_OK)

return -1.0f;

HAL_Delay(180);

if (HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, rx_data, 2, HAL_MAX_DELAY) != HAL_OK)

return -2.0f;

uint16_t raw = (rx_data[0] << 8) | rx_data[1];

return raw / 1.2f;

}


void Servo_SetAngle(uint8_t angle) {

if (angle > 180) angle = 180;

uint32_t pulse_width = SERVO_MIN_PULSE + (angle * (SERVO_MAX_PULSE - SERVO_MIN_PULSE) / 180);

__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse_width);

servo_angle = angle;

}


void ProcessCommand(char *cmd) {

if (strncmp(cmd, "STOVE_MODE:", 11) == 0) {

int mode = atoi(cmd + 11);

if (mode >= 1 && mode <= 6) {

uint8_t angle = (mode - 1) * 36;

Servo_SetAngle(angle);

char ack[32];

sprintf(ack, "Servo set to %d\n", angle);

HAL_UART_Transmit(&huart1, (uint8_t*)ack, strlen(ack), 100);

}

}

else if (strcmp(cmd, "STOVE_OFF") == 0) {

Servo_SetAngle(0);

char ack[] = "Stove OFF, servo 0\n";

HAL_UART_Transmit(&huart1, (uint8_t*)ack, strlen(ack), 100);

}

}


// ИСПРАВЛЕНО: Полностью переработанный и стабильный Callback приема

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {

if (huart->Instance == USART1) {

if (rx_byte == '\n' || rx_byte == '\r') {

if (cmd_index > 0) {

cmd_buffer[cmd_index] = '\0';

cmd_ready = 1;

cmd_index = 0;

}

} else if (cmd_index < CMD_BUF_SIZE - 1) {

cmd_buffer[cmd_index++] = rx_byte;

}


// Перезапускаем прерывание на прием СЛЕДУЮЩЕГО байта строго в конце функции

HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

}

}


uint32_t fake_rand(void) {

fake_rand_seed = fake_rand_seed * 1103515245 + 12345;

return (fake_rand_seed >> 16) & 0x7FFF;

}

/* USER CODE END 0 */


/**

* @brief The application entry point.

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

MX_USART1_UART_Init();

MX_ADC1_Init();

MX_I2C1_Init();

MX_TIM2_Init();

MX_TIM3_Init();

/* USER CODE BEGIN 2 */

HAL_TIM_Base_Start(&htim3);

HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);


Set_Pin_Output();

HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);


BH1750_Init();

Servo_SetAngle(90);


// ИСПРАВЛЕНО: Стартуем прерывание, используя глобальную переменную rx_byte

HAL_UART_Receive_IT(&huart1, &rx_byte, 1);


fake_rand_seed = HAL_GetTick();


// Приветственное сообщение

char welcome[] = "STM32 ready, sending data...\n";

HAL_UART_Transmit(&huart1, (uint8_t*)welcome, strlen(welcome), 100);

/* USER CODE END 2 */


/* Infinite loop */

/* USER CODE BEGIN WHILE */

while (1)

{

if (cmd_ready) {

cmd_ready = 0;

ProcessCommand(cmd_buffer);

}


// Генерация фейковых данных датчиков

dht_temperature = 20.0f + (fake_rand() % 100) / 10.0f;

dht_humidity = 30.0f + (fake_rand() % 400) / 10.0f;

float light_lux = 50.0f + (fake_rand() % 450);

int light_int = (int)light_lux;

int light_frac = (int)((light_lux - light_int) * 10);

if (light_frac < 0) light_frac = -light_frac;


// Чтение MQ-2 (ADC)

HAL_ADC_Start(&hadc1);

HAL_ADC_PollForConversion(&hadc1, 100);

uint32_t mq2_adc = HAL_ADC_GetValue(&hadc1);

HAL_ADC_Stop(&hadc1);

float mq2_voltage = (mq2_adc * 3.3f) / 4095.0f;

int smoke_percent = (int)((mq2_voltage / 3.3f) * 100);

if (smoke_percent > 100) smoke_percent = 100;


int temp_int = (int)dht_temperature;

int temp_frac = (int)((dht_temperature - temp_int) * 10);

if (temp_frac < 0) temp_frac = -temp_frac;

int humidity = (int)dht_humidity;


// Формирование JSON

char uart_buf[256];

int len = sprintf(uart_buf,

"{\"t\":%d.%d,\"h\":%d,\"l\":%d.%d,\"s\":%d,\"sv\":%d,\"mq2_raw\":%lu,\"mq2_v\":%d.%02d,\"mq2_percent\":%d}\n",

temp_int, temp_frac, humidity,

light_int, light_frac,

smoke_percent, servo_angle,

mq2_adc, (int)mq2_voltage, (int)((mq2_voltage - (int)mq2_voltage) * 100),

smoke_percent);


HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, len, 100);


// Обработка тревоги при задымлении >30%

if (smoke_percent > 30) {

Servo_SetAngle(0);

HAL_Delay(3000);

Servo_SetAngle(90);

for (int i = 0; i < 5; i++) {

HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

HAL_Delay(100);

HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

HAL_Delay(100);

}

char alarm_buf[64];

sprintf(alarm_buf, "{\"alert\":\"SMOKE_DETECTED\",\"level\":%d}\n", smoke_percent);

HAL_UART_Transmit(&huart1, (uint8_t*)alarm_buf, strlen(alarm_buf), 100);

}


HAL_Delay(2000);

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

RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;

RCC_OscInitStruct.HSEState = RCC_HSE_ON;

RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;

RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

RCC_OscInitStruct.PLL.PLLM = 25;

RCC_OscInitStruct.PLL.PLLN = 200;

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

RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;

RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;


if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)

{

Error_Handler();

}

}


/**

* @brief ADC1 Initialization Function

* @param None

* @retval None

*/

static void MX_ADC1_Init(void)

{

ADC_ChannelConfTypeDef sConfig = {0};


/** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)

*/

hadc1.Instance = ADC1;

hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;

hadc1.Init.Resolution = ADC_RESOLUTION_12B;

hadc1.Init.ScanConvMode = DISABLE;

hadc1.Init.ContinuousConvMode = DISABLE;

hadc1.Init.DiscontinuousConvMode = DISABLE;

hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;

hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;

hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;

hadc1.Init.NbrOfConversion = 1;

hadc1.Init.DMAContinuousRequests = DISABLE;

hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;

if (HAL_ADC_Init(&hadc1) != HAL_OK)

{

Error_Handler();

}


/** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.

*/

sConfig.Channel = ADC_CHANNEL_7;

sConfig.Rank = 1;

sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;

if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)

{

Error_Handler();

}

}


/**

* @brief I2C1 Initialization Function

* @param None

* @retval None

*/

static void MX_I2C1_Init(void)

{

hi2c1.Instance = I2C1;

hi2c1.Init.ClockSpeed = 100000;

hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;

hi2c1.Init.OwnAddress1 = 0;

hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;

hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;

hi2c1.Init.OwnAddress2 = 0;

hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;

hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

if (HAL_I2C_Init(&hi2c1) != HAL_OK)

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

TIM_ClockConfigTypeDef sClockSourceConfig = {0};

TIM_MasterConfigTypeDef sMasterConfig = {0};

TIM_OC_InitTypeDef sConfigOC = {0};


htim2.Instance = TIM2;

htim2.Init.Prescaler = 0;

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

if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)

{

Error_Handler();

}

sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;

sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)

{

Error_Handler();

}

sConfigOC.OCMode = TIM_OCMODE_PWM1;

sConfigOC.Pulse = 0;

sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;

sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)

{

Error_Handler();

}

HAL_TIM_MspPostInit(&htim2);

}


/**

* @brief TIM3 Initialization Function

* @param None

* @retval None

*/

static void MX_TIM3_Init(void)

{

TIM_ClockConfigTypeDef sClockSourceConfig = {0};

TIM_MasterConfigTypeDef sMasterConfig = {0};


htim3.Instance = TIM3;

htim3.Init.Prescaler = 0;

htim3.Init.CounterMode = TIM_COUNTERMODE_UP;

htim3.Init.Period = 65535;

htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

if (HAL_TIM_Base_Init(&htim3) != HAL_OK)

{

Error_Handler();

}

sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;

if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)

{

Error_Handler();

}

sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;

sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)

{

Error_Handler();

}

}


/**

* @brief USART1 Initialization Function

* @param None

* @retval None

*/

static void MX_USART1_UART_Init(void)

{

huart1.Instance = USART1;

// ИСПРАВЛЕНО: Изменено со 115200 на 9600 для стабильности ESP SoftwareSerial

huart1.Init.BaudRate = 9600;

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

}


/**

* @brief GPIO Initialization Function

* @param None

* @retval None

*/

static void MX_GPIO_Init(void)

{

/* GPIO Ports Clock Enable */

__HAL_RCC_GPIOH_CLK_ENABLE();

__HAL_RCC_GPIOA_CLK_ENABLE();

__HAL_RCC_GPIOB_CLK_ENABLE();

__HAL_RCC_GPIOC_CLK_ENABLE(); // Включено для светодиода PC13 (Авария)

}


void Error_Handler(void)

{

__disable_irq();

while (1)

{

}

}
