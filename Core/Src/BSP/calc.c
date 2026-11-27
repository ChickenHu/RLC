#include "BSP/calc.h"

static float SampleMean(const uint16_t *samples, uint32_t count) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < count; ++i)
        sum += samples[i];
    return sum / (float)count;
}

Complex Calc_Fundamental(const uint16_t *samples, uint32_t points, uint32_t cycles) {
    Complex result = {0};
    if (!samples || points < 3U || !cycles) return result;

    uint32_t count = points * cycles;
    float mean = SampleMean(samples, count);

    for (uint32_t point = 0; point < points; ++point) {
        float centered = 0.0f;
        for (uint32_t cycle = 0; cycle < cycles; ++cycle)
            centered += (float)samples[cycle * points + point] - mean;
        float phase = TWO_PI * (float)point / (float)points;
        result.re += centered * cosf(phase);
        result.im -= centered * sinf(phase);
    }
    result.re *= 2.0f / (float)count;
    result.im *= 2.0f / (float)count;
    return result;
}

static float Power(Complex value) {
    return value.re * value.re + value.im * value.im;
}

static Complex Multiply(Complex a, Complex b) {
    return (Complex){a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}

static Complex Subtract(Complex a, Complex b) {
    return (Complex){a.re - b.re, a.im - b.im};
}

bool Calc_FundamentalCaptureStable(const uint16_t *samples, uint32_t points, uint32_t cycles,
                                   float floor_codes, float relative) {
    if (!samples || points < 3U || cycles < 3U) return false;
    uint32_t group_cycles = cycles / 3U;
    uint32_t group_samples = group_cycles * points;
    const uint16_t *middle = samples + (cycles - group_cycles) / 2U * points;
    const uint16_t *end = samples + (cycles - group_cycles) * points;
    Complex last = Calc_Fundamental(end, points, group_cycles);
    float tolerance = fmaxf(floor_codes, relative * sqrtf(Power(last)));
    float mean = SampleMean(end, group_samples);
    return Power(Subtract(Calc_Fundamental(samples, points, group_cycles), last)) <= tolerance * tolerance &&
           Power(Subtract(Calc_Fundamental(middle, points, group_cycles), last)) <= tolerance * tolerance &&
           fabsf(SampleMean(samples, group_samples) - mean) <= tolerance &&
           fabsf(SampleMean(middle, group_samples) - mean) <= tolerance;
}

static bool Divide(Complex a, Complex b, float min_power, Complex *result) {
    float power = Power(b);
    if (!isfinite(power) || power < min_power) return false;
    Complex product = Multiply(a, (Complex){b.re, -b.im});
    Complex quotient = {product.re / power, product.im / power};
    if (!isfinite(quotient.re) || !isfinite(quotient.im)) return false;
    *result = quotient;
    return true;
}

static Complex CurrentScale(uint32_t freq_hz, MeasurementRange range) {
    /* In the nominal model the two paths share the ADC transfer function.
     * V_D = 2 * Vdut; I_D = -Idut / Yfb. Include the permanent feedback. */
    static const float resistance[RANGE_COUNT] = {33.0f, 470.0f, 3300.0f, 33000.0f, 330000.0f};
    static const float capacitance[RANGE_COUNT] = {470e-12f, 100e-12f, 22e-12f, 22e-12f, 22e-12f};
    float conductance = 1.0f / resistance[range] + 1.0f / 990000.0f;
    float susceptance = TWO_PI * (float)freq_hz * capacitance[range];
    return (Complex){-2.0f * conductance, -2.0f * susceptance};
}

float Calc_FeedbackMagnitude(uint32_t freq_hz, MeasurementRange range) {
    if ((unsigned)range >= RANGE_COUNT) return 0.0f;
    return 2.0f / sqrtf(Power(CurrentScale(freq_hz, range)));
}

bool Calc_Impedance(Complex voltage, Complex current, uint32_t freq_hz,
                    MeasurementRange range, Complex *impedance) {
    if (!impedance || !freq_hz || (unsigned)range >= RANGE_COUNT || Power(voltage) < 1.0f)
        return false;
    Complex scale = CurrentScale(freq_hz, range);
    return Divide(voltage, Multiply(current, scale), Power(scale), impedance);
}

bool Calc_RecordShort(CalcCalibration *cal, Complex voltage, Complex current) {
    if (!cal || !cal->freq_hz || (unsigned)cal->range >= RANGE_COUNT) return false;
    Complex scale = CurrentScale(cal->freq_hz, cal->range);
    if (!Divide(voltage, Multiply(current, scale), Power(scale), &cal->short_impedance))
        return false;
    cal->has_short = true;
    return true;
}

bool Calc_RecordOpen(CalcCalibration *cal, Complex voltage, Complex current) {
    if (!cal || !cal->freq_hz || (unsigned)cal->range >= RANGE_COUNT) return false;
    Complex scaled_current = Multiply(current, CurrentScale(cal->freq_hz, cal->range));
    /* Store I/V so an ideal open (zero current) is valid. */
    if (!Divide(scaled_current, voltage, 1.0f, &cal->open_raw_admittance)) return false;
    cal->has_open = true;
    return true;
}

bool Calc_CorrectImpedance(const CalcCalibration *cal, Complex voltage,
                           Complex current, Complex *impedance, float *signal_peak) {
    if (!cal || !impedance || !cal->freq_hz || (unsigned)cal->range >= RANGE_COUNT ||
        !cal->has_short || !cal->has_open)
        return false;

    /* Yopen = Yraw / (1 - Zshort * Yraw). */
    Complex open_denom = Subtract((Complex){1.0f, 0.0f},
                                  Multiply(cal->short_impedance, cal->open_raw_admittance));
    Complex open_admittance;
    if (!Divide(cal->open_raw_admittance, open_denom, 1e-12f, &open_admittance))
        return false;

    /* Vfixture = V - Zshort*I; Idut = I - Yopen*Vfixture. */
    Complex scale = CurrentScale(cal->freq_hz, cal->range);
    Complex scaled_current = Multiply(current, scale);
    Complex fixture_voltage = Subtract(voltage, Multiply(cal->short_impedance, scaled_current));
    Complex dut_current = Subtract(scaled_current, Multiply(open_admittance, fixture_voltage));
    float scale_power = Power(scale);
    if (!Divide(fixture_voltage, dut_current, scale_power, impedance)) return false;
    if (signal_peak)
        *signal_peak = sqrtf(fminf(Power(fixture_voltage), Power(dut_current) / scale_power));
    return true;
}
