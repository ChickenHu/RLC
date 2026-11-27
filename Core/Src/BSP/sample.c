#include "BSP/sample.h"
#include "adc.h"
#include "dac.h"
#include "main.h"
#include "tim.h"
#include <stdbool.h>
#include <string.h>

typedef enum {
    SAMPLE_IDLE,
    SAMPLE_CAPTURING,
} SampleState;

static SampleState state;
static bool initialized;
static volatile bool ready, failed, accepting_windows;
static bool discard_first;
static uint32_t started_at, window_samples;
static uint16_t *snapshot, *dma_samples;

HAL_StatusTypeDef Sample_Init(void) {
    if (state != SAMPLE_IDLE) return HAL_BUSY;
    initialized = false;
    /* ADC_DR is read as a word, but the right-aligned result fits in 16 bits. */
    hadc2.DMA_Handle->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hadc2.DMA_Handle->Init.Mode = DMA_CIRCULAR;
    HAL_StatusTypeDef status = HAL_DMA_Init(hadc2.DMA_Handle);
    if (status != HAL_OK) return status;
    /* Keep DMA requests enabled across timer triggers. */
    hadc2.Init.DMAContinuousRequests = ENABLE;
    status = HAL_ADC_Init(&hadc2);
    if (status == HAL_OK)
        status = HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    initialized = status == HAL_OK;
    return status;
}

HAL_StatusTypeDef Sample_Start(const Mode_t *mode, SampleSource source,
                               uint32_t cycles, uint16_t *buffer,
                               uint32_t buffer_capacity) {
    if (state != SAMPLE_IDLE) return HAL_BUSY;
    if (!initialized || !mode || !mode->dds_table || !mode->dds_points ||
        !cycles || !buffer || ((uintptr_t)buffer % sizeof(uint32_t)) != 0U ||
        cycles > UINT16_MAX)
        return HAL_ERROR;
    uint32_t sample_count = cycles * mode->dds_points;
    if (sample_count > buffer_capacity / 3U || sample_count > UINT16_MAX / 2U ||
        sample_count % 2U != 0U)
        return HAL_ERROR;

    uint8_t filter;
    switch (mode->dds_freq) {
        case FREQ_1E2: filter = 0U; break;
        case FREQ_1E3: filter = 1U; break;
        case FREQ_1E4: filter = 2U; break;
        case FREQ_1E5: filter = 3U; break;
        default: return HAL_ERROR;
    }

    switch (source) {
        case SAMPLE_DUT_CURRENT:
        case SAMPLE_DUT_VOLTAGE:
            /* Q5–Q8 invert: U6A selects I_D at PATH_5=1, V_D at 0.
               U6C selects the shared filtered output at LEVEL_5=0. */
            HAL_GPIO_WritePin(FB0_GPIO_Port, FB0_Pin,
                              (filter & 1U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
            HAL_GPIO_WritePin(FB1_GPIO_Port, FB1_Pin,
                              (filter & 2U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
            HAL_GPIO_WritePin(PATH_GPIO_Port, PATH_Pin,
                              source == SAMPLE_DUT_CURRENT ? GPIO_PIN_RESET : GPIO_PIN_SET);
            HAL_GPIO_WritePin(LEVEL_GPIO_Port, LEVEL_Pin, GPIO_PIN_SET);
            break;
        case SAMPLE_DRIVE_VOLTAGE:
            HAL_GPIO_WritePin(LEVEL_GPIO_Port, LEVEL_Pin, GPIO_PIN_RESET);
            break;
        default:
            return HAL_ERROR;
    }

    if (HAL_TIM_Base_Stop(&htim6) != HAL_OK) return HAL_ERROR;
    if (HAL_DAC_Stop_DMA(&hdac3, DAC_CHANNEL_1) != HAL_OK) return HAL_ERROR;

    /* UG also emits TRGO. Issue it before arming either DMA. */
    TIM_TypeDef *tim = htim6.Instance;
    tim->ARR = mode->tim_arr;
    htim6.Init.Period = mode->tim_arr;
    tim->CNT = 0U;
    tim->EGR = TIM_EGR_UG;
    /* TIM status flags clear on zero, DAC underrun clears on one. */
    tim->SR = ~TIM_SR_UIF;
    hdac3.Instance->SR = DAC_SR_DMAUDR1;
    /* The first trigger converts the preloaded code and requests table[0]. */
    if (HAL_DAC_SetValue(&hdac3, DAC_CHANNEL_1, DAC_ALIGN_12B_R,
                         mode->dds_table[mode->dds_points - 1U]) != HAL_OK ||
        HAL_DAC_Start_DMA(&hdac3, DAC_CHANNEL_1, (const uint32_t *)mode->dds_table,
                          mode->dds_points, DAC_ALIGN_12B_R) != HAL_OK)
        return HAL_ERROR;
    /* The circular DDS table does not need a CPU interrupt every half-cycle. */
    hdac3.DMA_Handle1->Instance->CCR &= ~(DMA_CCR_HTIE | DMA_CCR_TCIE);

    snapshot = buffer;
    dma_samples = buffer + sample_count;
    window_samples = sample_count;
    discard_first = true;
    ready = failed = false;
    started_at = HAL_GetTick();
    accepting_windows = true;
    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)dma_samples, 2U * sample_count) != HAL_OK ||
        HAL_TIM_Base_Start(&htim6) != HAL_OK) {
        accepting_windows = false;
        HAL_ADC_Stop_DMA(&hadc2);
        HAL_DAC_Stop_DMA(&hdac3, DAC_CHANNEL_1);
        return HAL_ERROR;
    }
    state = SAMPLE_CAPTURING;
    return HAL_OK;
}

HAL_StatusTypeDef Sample_Poll(uint32_t timeout_ms) {
    if (state == SAMPLE_IDLE) return HAL_ERROR;
    if (failed || (hdac3.Instance->SR & DAC_SR_DMAUDR1) ||
        hdac3.DMA_Handle1->ErrorCode != HAL_DMA_ERROR_NONE) {
        (void)Sample_Stop();
        return HAL_ERROR;
    }
    if (timeout_ms && (uint32_t)(HAL_GetTick() - started_at) >= timeout_ms) {
        return Sample_Stop() == HAL_OK ? HAL_TIMEOUT : HAL_ERROR;
    }
    return ready ? HAL_OK : HAL_BUSY;
}

void Sample_ReleaseWindow(void) {
    ready = false;
}

HAL_StatusTypeDef Sample_Stop(void) {
    if (state == SAMPLE_IDLE) return HAL_OK;
    accepting_windows = false;
    HAL_StatusTypeDef status = HAL_ADC_Stop_DMA(&hadc2);
    if (status == HAL_OK) state = SAMPLE_IDLE;
    return status;
}

HAL_StatusTypeDef Sample_MuteDrive(void) {
    if (state != SAMPLE_IDLE) return HAL_BUSY;
    if (HAL_TIM_Base_Stop(&htim6) != HAL_OK ||
        HAL_DAC_Stop_DMA(&hdac3, DAC_CHANNEL_1) != HAL_OK ||
        HAL_DAC_SetValue(&hdac3, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048U) != HAL_OK ||
        HAL_DAC_Start(&hdac3, DAC_CHANNEL_1) != HAL_OK)
        return HAL_ERROR;
    /* Update TRGO transfers the midpoint to DOR once, with DMA disabled. */
    htim6.Instance->EGR = TIM_EGR_UG;
    htim6.Instance->SR = ~TIM_SR_UIF;
    return HAL_OK;
}

static void CopyWindow(uint32_t offset) {
    if (!accepting_windows || ready) return;
    if (discard_first) {
        discard_first = false;
        return;
    }
    /* Copy the completed half while DMA fills the other half. Keep the
       snapshot unchanged until the main loop releases it. */
    memcpy(snapshot, dma_samples + offset, window_samples * sizeof(*snapshot));
    ready = true;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance == ADC2) CopyWindow(0U);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance == ADC2) CopyWindow(window_samples);
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance != ADC2) return;
    failed = true;
}
