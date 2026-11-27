#include "APP/test.h"
#include "APP/measurement.h"

HAL_StatusTypeDef Test_Init(const Mode_t *mode, GPIO_PinState start_pressed_level) {
    return Measurement_InitTest(mode, start_pressed_level, CONTROL_AUTOMATIC);
}

HAL_StatusTypeDef ManualTest_Init(const Mode_t *mode, GPIO_PinState start_pressed_level) {
    return Measurement_InitTest(mode, start_pressed_level, CONTROL_MANUAL);
}

HAL_StatusTypeDef WaveformTest_Init(const Mode_t *mode, GPIO_PinState start_pressed_level) {
    return Measurement_InitTest(mode, start_pressed_level, CONTROL_WAVEFORM);
}

HAL_StatusTypeDef Test_Update(void) {
    return Measurement_Update();
}
