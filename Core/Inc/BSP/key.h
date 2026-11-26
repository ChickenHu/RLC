#ifndef BSP_KEY_H
#define BSP_KEY_H

#include "gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_DEBOUNCE_MS 20U

typedef struct {
	GPIO_TypeDef *port;
	uint16_t pin;
	GPIO_PinState pressed_level;
	uint32_t changed_at;
	bool candidate_pressed;
	bool stable_pressed;
} KEY_HandleTypeDef;

/* Initialize once after GPIO setup. pin must be a single GPIO_PIN_x mask.
 * Each key needs its own persistent handle. Initially reports released;
 * even a key held at startup must pass the debounce interval. */
void KEY_Init(KEY_HandleTypeDef *key, GPIO_TypeDef *port, uint16_t pin,
			  GPIO_PinState pressed_level);

/* Poll regularly (e.g. every 1-5 ms). Both press and release are debounced.
 * Nonblocking; returns true continuously while stably pressed.
 * Requires the HAL millisecond tick to be running. */
bool KEY_IsPressed(KEY_HandleTypeDef *key);

#ifdef __cplusplus
}
#endif

#endif /* BSP_KEY_H */
