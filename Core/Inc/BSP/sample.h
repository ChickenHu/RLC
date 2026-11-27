#pragma once

#include "APP/sync.h"
#include "main.h"

typedef enum {
    SAMPLE_DUT_CURRENT,
    SAMPLE_DUT_VOLTAGE,
    SAMPLE_DRIVE_VOLTAGE,
} SampleSource;

/* Initialize ADC2 after MX_ADC2_Init. */
HAL_StatusTypeDef Sample_Init(void);

/* Start a continuous capture. The first whole window is discarded. The buffer
 * holds one snapshot followed by two DMA halves, each cycles * adc_points
 * uint16_t samples. Align it to a uint32_t boundary and keep it valid until
 * acquisition stops. Call the sample API from the main loop. */
HAL_StatusTypeDef Sample_Start(const Mode_t *mode, SampleSource source,
                               uint32_t cycles, uint16_t *buffer,
                               uint32_t buffer_capacity);

/* HAL_OK publishes a snapshot at the start of the buffer. Every snapshot
 * starts at the same DDS phase. Release it to receive the next window, or
 * stop after accepting it. HAL_ERROR/HAL_TIMEOUT end the capture.
 * A timeout of zero waits indefinitely while still checking hardware errors. */
HAL_StatusTypeDef Sample_Poll(uint32_t timeout_ms);
void Sample_ReleaseWindow(void);
HAL_StatusTypeDef Sample_Stop(void);

/* Hold the DAC at its DC midpoint while changing the fixture range. */
HAL_StatusTypeDef Sample_MuteDrive(void);
