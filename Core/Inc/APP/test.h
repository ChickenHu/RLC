#pragma once
#include "APP/sync.h"
#include "stm32g4xx_hal.h"

/* Call after peripheral initialization with a supported mode.
 * START selects a frequency and range automatically and displays impedance.
 * OPEN and SHORT acquire the corresponding standard at all enabled frequencies.
 * START cancels an ongoing calibration. */
HAL_StatusTypeDef Test_Init(const Mode_t *mode, GPIO_PinState start_pressed_level);
/* After both calibration sweeps, START cycles frequency, OPEN lowers the
 * range, and SHORT raises it. Hold OPEN or SHORT to measure.
 * Hold START to toggle automatic adjustment. In AUTO, tap START to measure.
 * While calibrating, press the same standard key again to skip a stalled range. */
HAL_StatusTypeDef ManualTest_Init(const Mode_t *mode, GPIO_PinState start_pressed_level);
/* Plot two consecutive ADC periods per channel. START cycles frequency,
 * OPEN lowers the range, and SHORT raises it. Starts at G1. */
HAL_StatusTypeDef WaveformTest_Init(const Mode_t *mode, GPIO_PinState start_pressed_level);
/* Call frequently in the main loop. HAL_BUSY means acquisition is pending.
 * HAL_ERROR/HAL_TIMEOUT show a failure status with any available waveforms. */
HAL_StatusTypeDef Test_Update(void);
