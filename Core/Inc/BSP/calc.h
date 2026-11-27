#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    float re;
    float im;
} Complex;

#define TWO_PI 6.28318530718f

/* Fundamental peak phasor in ADC codes, using exp(-j*phase). */
Complex Calc_Fundamental(const uint16_t *samples, uint32_t points, uint32_t cycles);

/* Compare fundamental phasors and DC levels from three parts of a capture. */
bool Calc_FundamentalCaptureStable(const uint16_t *samples, uint32_t points, uint32_t cycles,
                                   float floor_codes, float relative);

typedef enum {
    RANGE_G0, RANGE_G1, RANGE_G2, RANGE_G3, RANGE_G4, RANGE_COUNT
} MeasurementRange;

/* Magnitude of the nominal feedback impedance, including the permanent path. */
float Calc_FeedbackMagnitude(uint32_t freq_hz, MeasurementRange range);

/* Nominal impedance in ohms from phase-aligned ADC phasors.
 * Returns false if either fundamental peak is below one ADC code. */
bool Calc_Impedance(Complex voltage, Complex current, uint32_t freq_hz,
                    MeasurementRange range, Complex *impedance);

/* One fixture calibration at a fixed frequency and range. */
typedef struct {
    uint32_t freq_hz;
    MeasurementRange range;
    Complex short_impedance;
    Complex open_raw_admittance;
    bool has_short;
    bool has_open;
} CalcCalibration;

bool Calc_RecordShort(CalcCalibration *cal, Complex voltage, Complex current);
bool Calc_RecordOpen(CalcCalibration *cal, Complex voltage, Complex current);
/* Optional signal_peak reports the weaker corrected phasor in ADC codes. */
bool Calc_CorrectImpedance(const CalcCalibration *cal, Complex voltage,
                           Complex current, Complex *impedance, float *signal_peak);
