#pragma once

#include <stdint.h>
#define FREQ_1E2 100
#define FREQ_1E3 1000
#define FREQ_1E4 10000
#define FREQ_1E5 100000

typedef struct {
    const uint16_t *dds_table;
    uint16_t dds_points;  // DAC updates per waveform period.
    uint16_t adc_points;  // ADC samples per waveform period.
    uint16_t capture_cycles;  // Settled waveform periods used for I/Q.
    uint16_t dac_arr;
    uint16_t adc_arr;
    uint32_t dds_freq;
} Mode_t;

/*
 * Mode1 FREQ_1E2
 * Mode2 FREQ_1E3
 * Mode3 FREQ_1E4
 * Mode4 FREQ_1E5
 */
#include "Assets/sync.h.inc"

