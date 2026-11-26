#pragma once
#include "APP/sync.h"
#include "stm32g4xx_hal.h"

/* Call after peripheral initialization. Owns TIM6, DAC3 CH1, ADC2 and OLED1.
 * Captures ten periods and displays the last display_cycles (1..10).
 * mode is copied; its DDS table must remain valid. */
HAL_StatusTypeDef Test_Init(const Mode_t *mode, uint8_t display_cycles,
                           GPIO_PinState start_pressed_level);
/* Call frequently in the main loop. HAL_BUSY means acquisition is pending.
 * HAL_ERROR/HAL_TIMEOUT leave the last successful plot intact. */
HAL_StatusTypeDef Test_Update(void);
