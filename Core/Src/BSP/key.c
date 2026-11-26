#include "BSP/key.h"

void KEY_Init(KEY_HandleTypeDef *key, GPIO_TypeDef *port, uint16_t pin,
              GPIO_PinState pressed_level) {
    key->port = port;
    key->pin = pin;
    key->pressed_level = pressed_level;
    key->candidate_pressed = HAL_GPIO_ReadPin(port, pin) == pressed_level;
    key->stable_pressed = false;
    key->changed_at = HAL_GetTick();
}

bool KEY_IsPressed(KEY_HandleTypeDef *key) {
    bool pressed = HAL_GPIO_ReadPin(key->port, key->pin) == key->pressed_level;
    uint32_t now = HAL_GetTick();

    if (pressed != key->candidate_pressed) {
        key->candidate_pressed = pressed;
        key->changed_at = now;
    } else if ((uint32_t)(now - key->changed_at) >= KEY_DEBOUNCE_MS) {
        key->stable_pressed = key->candidate_pressed;
    }

    return key->stable_pressed;
}
