#include "BSP/sample.h"
#include "APP/sync.h"
#include "adc.h"
#include "main.h"
#include "stm32g4xx_hal_adc.h"
#include <stdint.h>
#include <sys/stat.h>

/*
 * ADC 采样参考（以下代码放在注释中，不改变当前固件行为）。
 *
 * 一、当前工程配置，见 Core/Src/adc.c
 *   hadc1：ADC1_IN1，PA0，AUX_ADC，辅助输入，采样时间 2.5 个 ADC 周期。
 *   hadc2：ADC2_IN3，PA6，MAIN_ADC，主输入，采样时间 12.5 个 ADC 周期。
 *   两路均为 12 位、右对齐、单端输入，原始值范围为 0~4095。
 *   软件触发，每次启动只转换一次；未启用 ADC DMA 或 ADC 转换中断。
 *   main.c 中的 TIM6 + DMA 当前驱动 DAC 输出，不会自动采集 ADC。
 *
 * 二、初始化后校准一次
 * 在 main.c 的 USER CODE BEGIN 2 内（MX_ADC1_Init/MX_ADC2_Init 后）添加：
 *
 *   if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
 *       Error_Handler();
 *   if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK)
 *       Error_Handler();
 *
 * 校准时 ADC 应未启动，不需要在每次读取前重复校准。
 *
 * 三、单次读取函数参考
 * 若要启用，可将下面函数移出注释，在本文件添加 #include "adc.h"，
 * 并在 sample.h 中包含 adc.h、声明函数，再在 main.c 包含 BSP/sample.h。
 * timeout_ms 是等待转换完成的超时时间，单位为毫秒。
 * 仅在返回 HAL_OK 时使用 *raw；失败时保持调用方原有数据。
 * 此函数适用于当前软件触发、单次转换配置，在主循环中调用。
 *
 * HAL_StatusTypeDef Sample_ReadOnce(ADC_HandleTypeDef *hadc,
 *                                   uint16_t *raw, uint32_t timeout_ms)
 * {
 *     if (hadc == NULL || raw == NULL)
 *         return HAL_ERROR;
 *
 *     // 启动一次转换。选择 &hadc2 读取主输入，&hadc1 读取辅助输入。
 *     HAL_StatusTypeDef status = HAL_ADC_Start(hadc);
 *     if (status != HAL_OK)
 *         return status;
 *
 *     // 等待 EOC（转换完成），不要直接在启动后读取旧结果。
 *     status = HAL_ADC_PollForConversion(hadc, timeout_ms);
 *     uint16_t value = 0;
 *     if (status == HAL_OK)
 *         value = (uint16_t)HAL_ADC_GetValue(hadc);
 *
 *     // 超时也尝试停止 ADC，保留最先发生的错误。
 *     HAL_StatusTypeDef stop_status = HAL_ADC_Stop(hadc);
 *     if (status != HAL_OK)
 *         return status;
 *     if (stop_status != HAL_OK)
 *         return stop_status;
 *
 *     *raw = value;
 *     return HAL_OK;
 * }
 *
 * 四、主循环调用参考（完成上述校准并启用函数后）
 * 在 main.c 的 USER CODE BEGIN 3 内添加：
 *
 *   uint16_t main_raw = 0;
 *   if (Sample_ReadOnce(&hadc2, &main_raw, 10) == HAL_OK)
 *   {
 *       // 3.3f 仅为参考值，应替换为实际 VREF+ 电压。
 *       float main_voltage = (float)main_raw * 3.3f / 4095.0f;
 *       // 在这里处理 main_raw / main_voltage，例如存入缓冲区或串口发送。
 *       // 若要在调试器持续观察，可保存到用户定义的全局变量中。
 *       (void)main_voltage;
 *   }
 *   else
 *   {
 *       // 在这里记录或处理采样失败，不把旧数据当成新采样。
 *   }
 *   HAL_Delay(10);  // 仅演示低速采集，循环周期还包括转换和处理耗时。
 *
 * 五、用于本项目测量时注意
 *   电压公式得到的是 ADC 引脚电压；前端分压、放大和直流偏置需另行换算。
 *   ADC 输入电压须满足芯片允许范围，不能直接接负电压交流信号。
 *   当前两路独立，先后读取不代表同时采样；计算相位差时需考虑时间差。
 *   轮询加 HAL_Delay 适合验证读数，不适合严格等间隔的波形采样。
 *   后续若测交流波形，可配置定时器触发 + ADC DMA 缓冲区；仅调用
 *   HAL_ADC_Start_DMA 并不足够，还需配置 ADC 触发源、DMA 及相应中断。
 *   当前采样时间较短；若信号源阻抗较高，应按模拟前端调整采样时间。
 */
uint32_t CurrentBuffer[256 * 10];
volatile bool ADC_Ready = false;
volatile bool ADC_Failed = false;
void Sample_GetCurrent(const Mode_t *mode, int cycle, int deprecate) {
	HAL_ADC_Start_DMA(&MAIN_ADC, CurrentBuffer, mode->dds_points * cycle);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
	if (hadc->Instance == ADC2)
		ADC_Ready = true;
    HAL_ADC_Stop_DMA(&MAIN_ADC);
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc) {
	if (hadc->Instance == ADC2)
		ADC_Failed = true;
    HAL_ADC_Stop_DMA(&MAIN_ADC);
}