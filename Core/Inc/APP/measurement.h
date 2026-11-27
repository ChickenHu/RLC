#pragma once
#include "APP/sync.h"
#include "stm32g4xx_hal.h"

HAL_StatusTypeDef Measurement_Init(const Mode_t *mode, GPIO_PinState start_pressed_level);
HAL_StatusTypeDef Measurement_Update(void);

typedef enum {
    CONTROL_AUTOMATIC, CONTROL_MANUAL, CONTROL_WAVEFORM
} TestControl;

/* Shared acquisition engine with the diagnostic display enabled. */
HAL_StatusTypeDef Measurement_InitTest(const Mode_t *mode, GPIO_PinState start_pressed_level,
                                       TestControl control);
