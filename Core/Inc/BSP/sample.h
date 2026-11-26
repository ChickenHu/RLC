#pragma once
#include "APP/sync.h"
#include <stdbool.h>
extern uint32_t CurrentBuffer[256 * 10];
extern volatile bool ADC_Ready;
extern volatile bool ADC_Failed;
void Sample_GetCurrent(const Mode_t *mode, int cycle, int deprecate);
