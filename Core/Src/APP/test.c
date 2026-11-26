#include "APP/test.h"
#include "BSP/key.h"
#include "BSP/oled.h"
#include "BSP/sample.h"
#include "adc.h"
#include "dac.h"
#include "main.h"
#include "tim.h"

static KEY_HandleTypeDef start_key;
static Mode_t test_mode;
static uint8_t shown_cycles;
static bool initialized, busy, was_pressed;
static uint32_t started_at;

static void DrawCurrent(void) {
    uint32_t count = test_mode.dds_points * shown_cycles;
    uint32_t offset = test_mode.dds_points * (10U - shown_cycles);
    OLED_Clear(&OLED1);
    /* Bin extrema retain narrow peaks when more than 128 samples are shown.
     * Fixed vertical scale: ADC 0 at bottom, 4095 at top. */
    for (uint32_t x = 0; x < 128U; ++x) {
        uint32_t first = x * count / 128U;
        uint32_t end = (x + 1U) * count / 128U;
        if (end <= first) end = first + 1U;
        uint32_t low = 4095U, high = 0U;
        for (uint32_t i = first; i < end; ++i) {
            uint32_t value = CurrentBuffer[offset + i];
            if (value > 4095U) value = 4095U;
            if (value < low) low = value;
            if (value > high) high = value;
        }
        uint8_t top = 63U - high * 63U / 4095U;
        uint8_t bottom = 63U - low * 63U / 4095U;
        for (uint8_t y = top; y <= bottom; ++y)
            OLED_DrawPixel(OLED1.FrameBufferPtr, (uint8_t)x, y);
    }
    OLED_UpdateState(&OLED1);
}

HAL_StatusTypeDef Test_Init(const Mode_t *mode, uint8_t display_cycles,
                           GPIO_PinState start_pressed_level) {
    if (!mode || !mode->dds_table || mode->dds_points < 2U ||
        mode->dds_points > 256U || !mode->dds_freq ||
        display_cycles < 1U || display_cycles > 10U)
        return HAL_ERROR;
    initialized = false;
    HAL_TIM_Base_Stop(&htim6);
    HAL_DAC_Stop_DMA(&hdac3, DAC_CHANNEL_1);
    HAL_ADC_Stop_DMA(&hadc2);
    /* Keep requesting DMA transfers across external triggers. */
    hadc2.Init.DMAContinuousRequests = ENABLE;
    if (HAL_ADC_Init(&hadc2) != HAL_OK ||
        HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK)
        return HAL_ERROR;
    test_mode = *mode;
    shown_cycles = display_cycles;
    __HAL_TIM_SET_AUTORELOAD(&htim6, mode->tim_arr);
    __HAL_TIM_SET_COUNTER(&htim6, 0U);
    htim6.Instance->EGR = TIM_EGR_UG;
    if (HAL_DAC_Start_DMA(&hdac3, DAC_CHANNEL_1,
                          (uint32_t *)mode->dds_table, mode->dds_points,
                          DAC_ALIGN_12B_R) != HAL_OK)
        return HAL_ERROR;
    if (HAL_TIM_Base_Start(&htim6) != HAL_OK) {
        HAL_DAC_Stop_DMA(&hdac3, DAC_CHANNEL_1);
        return HAL_ERROR;
    }
    OLED_Init(&hi2c1);
    KEY_Init(&start_key, KEY_START_GPIO_Port, KEY_START_Pin, start_pressed_level);
    busy = was_pressed = false;
    ADC_Ready = ADC_Failed = false;
    initialized = true;
    return HAL_OK;
}

HAL_StatusTypeDef Test_Update(void) {
    if (!initialized) return HAL_ERROR;
    bool pressed = KEY_IsPressed(&start_key);
    bool trigger = pressed && !was_pressed;
    was_pressed = pressed;
    if (busy) {
        if (ADC_Failed) {
            HAL_ADC_Stop_DMA(&hadc2);
            busy = false;
            return HAL_ERROR;
        }
        if (ADC_Ready) {
            busy = false;
            DrawCurrent();
            return HAL_OK;
        }
        if ((uint32_t)(HAL_GetTick() - started_at) >
            10000U / test_mode.dds_freq + 100U) {
            HAL_ADC_Stop_DMA(&hadc2);
            busy = false;
            return HAL_TIMEOUT;
        }
        return HAL_BUSY;
    }
    if (trigger) {
        ADC_Ready = ADC_Failed = false;
        HAL_StatusTypeDef status = HAL_ADC_Start_DMA(
            &hadc2, CurrentBuffer, test_mode.dds_points * 10U);
        if (status != HAL_OK) return status;
        started_at = HAL_GetTick();
        busy = true;
        return HAL_BUSY;
    }
    return HAL_OK;
}
