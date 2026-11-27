#include "APP/measurement.h"
#include "BSP/calc.h"
#include "BSP/key.h"
#include "BSP/oled.h"
#include "BSP/sample.h"
#include "main.h"

static KEY_HandleTypeDef start_key;
static KEY_HandleTypeDef open_key;
static KEY_HandleTypeDef short_key;
enum {
    PLOT_WIDTH = 76, PLOT_GROUPS = 6, PLOT_MARGIN = 2, RESULT_X = 82,
    CAPTURE_BUFFER_SAMPLES = 8704,
    MIN_SIGNAL_CODES = 8, ADC_RAIL_MARGIN = 8,
    MIN_PILOT_CODES = 4, MIN_100_KHZ_SIGNAL_CODES = 32,
    WAVEFORM_SETTLE_MS = 30, WAVEFORM_PERIODS = 2,
    HIGH_FREQ_SETTLE_MS = 30, HIGH_FREQ_AVERAGE_WINDOWS = 8,
    MEASUREMENT_CAPTURE_TIMEOUT_MS = 1000,
    START_HOLD_MS = 700,
    RELAY_SETTLE_MS = 20,
    RESISTIVE_ANGLE_DEG = 5, MAX_PASSIVE_ANGLE_DEG = 100,
};
static Mode_t test_mode;
enum { MODE_100_HZ, MODE_1_KHZ, MODE_10_KHZ, MODE_100_KHZ, MODE_COUNT };
static const Mode_t *const modes[] = {
    [MODE_100_HZ] = &Mode1, [MODE_1_KHZ] = &Mode2, [MODE_10_KHZ] = &Mode3,
    [MODE_100_KHZ] = &Mode4,
};
/* Use 100 kHz as the last automatic fallback. */
static const uint8_t frequency_order[MODE_COUNT] = {
    MODE_1_KHZ, MODE_100_HZ, MODE_10_KHZ, MODE_100_KHZ,
};
typedef enum { TEST_MEASURE, TEST_OPEN, TEST_SHORT } TestOperation;
static CalcCalibration calibration[MODE_COUNT][RANGE_COUNT];
static TestOperation operation;
static uint8_t active_mode, next_mode;
static uint8_t frequency_index, manual_frequency_index;
static const MeasurementRange range_order[RANGE_COUNT] = {
    RANGE_G1, RANGE_G0, RANGE_G2, RANGE_G3, RANGE_G4
};
static MeasurementRange active_range, selected_range, high_frequency_range;
static unsigned range_index;
static unsigned tried_ranges, result_rank;
static unsigned tried_frequencies;
static float result_signal;
typedef struct {
    MeasurementRange range, alternate_range;
    uint8_t mode, alternate_mode;
} MeasurementRoute;
static MeasurementRoute route;
static bool preferred_at_frequency;
static float best_signal;
static bool have_result;
static uint8_t result_frame[sizeof(OLED1.FrameBuffer) / 2U];
static unsigned recorded_ranges;
static const char *range_failure;
static const char *calibration_failure;
static uint32_t failed_frequency;
static MeasurementRange failed_range;
static bool initialized, busy, start_pressed, open_pressed, short_pressed;
static bool compact_ui;
static bool waveform_only, capture_settled;
static bool manual_test, manual_ready, open_sweep_done, short_sweep_done;
static bool automatic_adjustment, start_hold_handled, adjustment_toggle_pending;
static uint32_t start_pressed_at;
static bool manual_frequency_used;
static MeasurementRange manual_range, manual_high_frequency_range;
static uint32_t capture_started_at;
static SampleSource source;
static Complex voltage_phasor;
static bool previous_stable, saw_window;
static Complex previous_phasor;
static unsigned batch_windows;
static Complex batch_phasor_sum;
static float previous_mean, batch_mean_sum;
static _Alignas(uint32_t) uint16_t samples[CAPTURE_BUFFER_SAMPLES];

/* A small font for plot labels and measurements. */
static void DrawScaledText(uint8_t x, uint8_t y, const char *text, uint8_t scale) {
    static const char characters[] = "0123456789LHPRWETIVUA-XFSOCG.kMmupnNz";
    static const uint8_t glyphs[][3] = {
        {31, 17, 31}, {18, 31, 16}, {29, 21, 23}, {21, 21, 31},
        {7, 4, 31}, {23, 21, 29}, {31, 21, 29}, {1, 1, 31},
        {31, 21, 31}, {23, 21, 31}, {31, 16, 16}, {31, 4, 31},
        {31, 5, 7}, {31, 5, 26}, {31, 8, 31}, {31, 21, 17}, {1, 31, 1},
        {17, 31, 17}, {15, 16, 15}, {31, 16, 31}, {30, 5, 30}, {4, 4, 4},
        {27, 4, 27}, {31, 5, 1}, {23, 21, 29}, {31, 17, 31}, {31, 17, 17},
        {31, 17, 29},
        {0, 16, 0}, {31, 8, 20}, {31, 6, 31}, {28, 12, 28},
        {28, 16, 28}, {28, 20, 8}, {28, 4, 24}, {31, 2, 30}, {20, 28, 20},
    };
    for (; *text; ++text, x += 4U * scale) {
        const char *glyph = strchr(characters, *text);
        if (!glyph) continue;
        for (uint8_t col = 0; col < 3U; ++col)
            for (uint8_t row = 0; row < 5U; ++row)
                if (glyphs[glyph - characters][col] & (1U << row))
                    for (uint8_t dy = 0; dy < scale; ++dy)
                        for (uint8_t dx = 0; dx < scale; ++dx)
                            OLED_DrawPixel(OLED1.FrameBufferPtr, x + col * scale + dx,
                                           y + row * scale + dy);
    }
}

static void DrawTextAt(uint8_t x, uint8_t y, const char *text) {
    DrawScaledText(x, y, text, 1U);
}

static void DrawCentered(uint8_t y, const char *text, uint8_t scale) {
    unsigned width = (4U * strlen(text) - 1U) * scale;
    DrawScaledText((128U - width) / 2U, y, text, scale);
}

static void DrawMeasurementInProgress(void) {
    static const uint16_t measuring[3][16] = {
        {0x0000, 0x2001, 0x29F2, 0x2914, 0x2950, 0x2951, 0x2952, 0x2954,
         0x2950, 0x2950, 0x2954, 0x2844, 0x2062, 0x20B2, 0x2119, 0x3A0D},
        {0x0000, 0x0000, 0x0FFC, 0x0804, 0x0FFC, 0x0804, 0x0FFC, 0x0000,
         0x3FFF, 0x0884, 0x0FFC, 0x0884, 0x1FFE, 0x0080, 0x3FFF, 0x0000},
        {0x0000, 0x0040, 0x0040, 0x0040, 0x1FFF, 0x1041, 0x1041, 0x1041,
         0x1041, 0x1FFF, 0x1041, 0x0040, 0x0040, 0x0040, 0x0040, 0x0040},
    };
    OLED_Clear(&OLED1);
    for (uint8_t glyph = 0; glyph < 3U; ++glyph)
        for (uint8_t row = 0; row < 16U; ++row)
            for (uint8_t col = 0; col < 16U; ++col)
                if (measuring[glyph][row] & (1U << col))
                    OLED_DrawPixel(OLED1.FrameBufferPtr, 38U + 18U * glyph + col, 24U + row);
    OLED_UpdateState(&OLED1);
}

static void DrawMeasurementSettings(void) {
    char range[] = {'G', '0' + active_range, '\0'};
    const char *frequency = test_mode.dds_freq == FREQ_1E2 ? "100 Hz" :
                            test_mode.dds_freq == FREQ_1E3 ? "1 kHz" :
                            test_mode.dds_freq == FREQ_1E4 ? "10 kHz" : "100 kHz";
    DrawScaledText(8U, 50U, frequency, 2U);
    DrawScaledText(104U, 50U, range, 2U);
}

static void DrawText(uint8_t x, const char *text) {
    DrawTextAt(x, 1U, text);
}

static void DrawStatus(const char *status) {
    if (compact_ui && manual_ready && operation == TEST_MEASURE) {
        OLED_Clear(&OLED1);
        if (*status) DrawCentered(22U, status, 3U);
        DrawMeasurementSettings();
        OLED_UpdateState(&OLED1);
        return;
    }
    OLED_Delete(OLED1.FrameBufferPtr, RESULT_X, 55U, 16U, 8U);
    DrawTextAt(RESULT_X, 56U,status);
    OLED_UpdateState(&OLED1);
}

static void DrawAdjustmentMode(void) {
    uint8_t y = compact_ui ? 4U : 56U;
    OLED_Delete(OLED1.FrameBufferPtr, 100U, y, 16U, 5U);
    DrawTextAt(100U, y, automatic_adjustment ? "AUTO" : "MAN");
}

static bool UsesManualAdjustment(void) {
    return manual_ready && !automatic_adjustment;
}

static void DrawCapture(uint8_t top_y, uint8_t bottom_y) {
    if (compact_ui && operation == TEST_MEASURE) return;
    uint32_t points = test_mode.dds_points;
    uint32_t cycles = test_mode.capture_cycles;
    uint32_t count = points * (waveform_only ? WAVEFORM_PERIODS : cycles);
    const uint16_t *capture = samples;
    uint32_t min = 4095U, max = 0U;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t value = capture[i];
        if (value < min) min = value;
        if (value > max) max = value;
    }
    uint32_t peak_to_peak = max - min;
    /* Keep at least 64 codes on screen so a few LSBs of noise stay small. */
    uint32_t span = peak_to_peak < 64U ? 64U : peak_to_peak;
    int32_t bottom_code = (int32_t)min - (int32_t)((span - peak_to_peak) / 2U);

    top_y += PLOT_MARGIN;
    bottom_y -= PLOT_MARGIN;
    uint32_t height = bottom_y - top_y;
    uint32_t groups = waveform_only ? WAVEFORM_PERIODS : (cycles < PLOT_GROUPS ? cycles : PLOT_GROUPS);
    uint8_t previous = 0U;
    for (uint32_t x = 0; x < PLOT_WIDTH; ++x) {
        /* The waveform test uses consecutive samples. The measurement
         * preview averages equal phases within each group. */
        uint32_t position = x * groups * points;
        uint32_t group = position / (PLOT_WIDTH * points);
        uint32_t phase = (position / PLOT_WIDTH) % points;
        uint32_t next_phase = (phase + 1U) % points;
        uint32_t fraction = position % PLOT_WIDTH;
        uint32_t first_cycle = waveform_only ? group : group * cycles / groups;
        uint32_t end_cycle = waveform_only ? group + 1U : (group + 1U) * cycles / groups;
        uint32_t sum = 0U;
        for (uint32_t cycle = first_cycle; cycle < end_cycle; ++cycle) {
            const uint16_t *period = capture + cycle * points;
            uint16_t next = waveform_only && next_phase == 0U ?
                            (group + 1U < groups ? period[points] : period[phase]) :
                            period[next_phase];
            sum += period[phase] * (PLOT_WIDTH - fraction) + next * fraction;
        }
        uint32_t weight = (end_cycle - first_cycle) * PLOT_WIDTH;
        int32_t average = (int32_t)((sum + weight / 2U) / weight);
        uint32_t scaled = (uint32_t)(average - bottom_code) * height;
        uint8_t row = bottom_y - (scaled + span / 2U) / span;
        if (x == 0U) previous = row;
        uint8_t top = previous < row ? previous : row;
        uint8_t bottom = previous > row ? previous : row;
        for (uint8_t y = top; y <= bottom; ++y)
            OLED_DrawPixel(OLED1.FrameBufferPtr, (uint8_t)x, y);
        previous = row;
    }
}

static HAL_StatusTypeDef StartCapture(void) {
    previous_stable = false;
    saw_window = false;
    HAL_StatusTypeDef status = Sample_Start(&test_mode, source, test_mode.capture_cycles,
                                            samples, sizeof(samples) / sizeof(samples[0]));
    if (status != HAL_OK) {
        DrawStatus("E");
        return status;
    }
    busy = true;
    capture_started_at = HAL_GetTick();
    capture_settled = false;
    batch_windows = 0U;
    batch_phasor_sum = (Complex){0};
    batch_mean_sum = 0.0f;
    if (operation != TEST_MEASURE) {
        OLED_Delete(OLED1.FrameBufferPtr, RESULT_X, 32U, 4U, 5U);
        DrawTextAt(RESULT_X, 32U, source == SAMPLE_DUT_VOLTAGE ? "U" : "I");
    }
    if (waveform_only || operation != TEST_MEASURE)
        DrawStatus(operation == TEST_OPEN ? "OW" : operation == TEST_SHORT ? "SW" : "W");
    return HAL_BUSY;
}

static void DrawResult(uint8_t y, char label, float number, uint8_t digits) {
    char text[9] = {label, ' '};
    uint32_t limit = 1U;
    for (uint8_t i = 0U; i < digits; ++i) limit *= 10U;
    float rounded = fabsf(number) + 0.5f;
    if (rounded >= (float)limit) {
        text[1] = number < 0.0f ? '-' : ' ';
        text[2] = 'H';
        text[3] = 'I';
        DrawTextAt(RESULT_X, y, text);
        return;
    }
    uint32_t value = (uint32_t)rounded;
    if (number < 0.0f && value != 0U) text[1] = '-';
    for (uint8_t i = digits + 1U; i >= 2U; --i) {
        text[i] = '0' + value % 10U;
        value /= 10U;
    }
    DrawTextAt(RESULT_X, y, text);
}

static void DrawComponent(Complex impedance, float angle, uint32_t frequency) {
    static const char *const resistance_units[] = {"mOHM", "OHM", "kOHM", "MOHM", NULL};
    static const char *const inductance_units[] = {"nH", "uH", "mH", "H", NULL};
    static const char *const capacitance_units[] = {"pF", "nF", "uF", "mF", "F", NULL};
    const char *const *unit;
    float value;
    char type;
    if (fabsf(angle) > MAX_PASSIVE_ANGLE_DEG) {
        if (compact_ui) DrawCentered(21U, "---", 3U);
        else DrawTextAt(RESULT_X, 32U, "---");
        return;
    }
    float omega = TWO_PI * (float)frequency;
    if (angle > RESISTIVE_ANGLE_DEG) {
        type = 'L';
        unit = inductance_units;
        value = impedance.im / omega * 1e9f;
    } else if (angle < -RESISTIVE_ANGLE_DEG) {
        type = 'C';
        unit = capacitance_units;
        value = -1e12f / (omega * impedance.im);
    } else {
        type = 'R';
        unit = resistance_units;
        value = impedance.re * 1e3f;
    }
    while (value >= 999.95f && unit[1]) {
        value /= 1000.0f;
        ++unit;
    }
    if (!isfinite(value) || value >= 999.95f) {
        if (compact_ui) DrawCentered(21U, "HI", 3U);
        else DrawTextAt(RESULT_X, 32U, "HI");
        return;
    }
    unsigned decimals = value < 9.9995f ? 3U : value < 99.995f ? 2U : 1U;
    unsigned scale = decimals == 3U ? 1000U : decimals == 2U ? 100U : 10U;
    unsigned rounded = (unsigned)(value * (float)scale + 0.5f);
    if (rounded > 9999U) rounded = 9999U;
    char text[12] = {type};
    char *digit = text + 1;
    if (compact_ui) *digit++ = ' ';
    for (unsigned place = 1000U; place > 0U; place /= 10U) {
        *digit++ = '0' + rounded / place % 10U;
        if (place == scale) *digit++ = '.';
    }
    strcpy(digit, *unit);
    if (compact_ui) DrawCentered(21U, text, strlen(text) <= 10U ? 3U : 2U);
    else DrawTextAt(RESULT_X, 32U, text);
}

static void SetRangeAddress(unsigned address) {
    HAL_GPIO_WritePin(RB0_GPIO_Port, RB0_Pin,
                      (address & 1U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(RB1_GPIO_Port, RB1_Pin,
                      (address & 2U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static HAL_StatusTypeDef SelectRange(MeasurementRange range) {
    if (range == selected_range) return HAL_OK;
    HAL_StatusTypeDef status = Sample_MuteDrive();
    if (status != HAL_OK) return status;
    HAL_GPIO_WritePin(R_INH_GPIO_Port, R_INH_Pin, GPIO_PIN_RESET);
    if (range == RANGE_G0 || selected_range == RANGE_G0) {
        HAL_GPIO_WritePin(RELAY_EN_GPIO_Port, RELAY_EN_Pin,
                          range == RANGE_G0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_Delay(RELAY_SETTLE_MS);
    }
    SetRangeAddress(range == RANGE_G0 ? 0U : range - RANGE_G1);
    if (range != RANGE_G0)
        HAL_GPIO_WritePin(R_INH_GPIO_Port, R_INH_Pin, GPIO_PIN_SET);
    selected_range = range;
    return HAL_OK;
}

static HAL_StatusTypeDef StartRange(void) {
    HAL_StatusTypeDef status = SelectRange(active_range);
    if (status != HAL_OK) {
        DrawStatus("E");
        return status;
    }
    tried_ranges |= 1U << active_range;
    OLED_Delete(OLED1.FrameBufferPtr, 0U, 8U, PLOT_WIDTH, 23U);
    OLED_Delete(OLED1.FrameBufferPtr, 0U, 41U, PLOT_WIDTH, 23U);
    if (operation != TEST_MEASURE) {
        OLED_Delete(OLED1.FrameBufferPtr, RESULT_X, 47U, 12U, 5U);
        DrawResult(47U, 'G', active_range, 1U);
    }
    source = SAMPLE_DUT_VOLTAGE;
    return StartCapture();
}

static MeasurementRange PreferredRange(void) {
    if (active_mode == MODE_100_KHZ) return high_frequency_range;
    return active_mode == route.alternate_mode ? route.alternate_range : route.range;
}

static bool HasCalibration(uint8_t mode, MeasurementRange range) {
    const CalcCalibration *cal = &calibration[mode][range];
    return cal->has_open && cal->has_short;
}

static bool CanScanRange(MeasurementRange range) {
    if (active_mode != MODE_100_KHZ) return true;
    if (range < RANGE_G1 || range > RANGE_G3) return false;
    return operation != TEST_MEASURE || HasCalibration(active_mode, range);
}

static HAL_StatusTypeDef StartFrequency(void) {
    test_mode = *modes[active_mode];
    tried_frequencies |= 1U << active_mode;
    range_index = 0U;
    tried_ranges = 0U;
    active_range = waveform_only ? selected_range : range_order[range_index];
    if (UsesManualAdjustment())
        active_range = compact_ui && active_mode == MODE_100_KHZ ?
                       manual_high_frequency_range : manual_range;
    else if (!waveform_only && operation == TEST_MEASURE && PreferredRange() < RANGE_COUNT)
        active_range = PreferredRange();
    if (!waveform_only && !UsesManualAdjustment() && !CanScanRange(active_range)) {
        active_range = RANGE_G1;
        while (active_range <= RANGE_G3 && !CanScanRange(active_range)) ++active_range;
        if (active_range > RANGE_G3) {
            DrawStatus("E");
            return HAL_ERROR;
        }
        if (operation == TEST_MEASURE) high_frequency_range = active_range;
    }
    best_signal = 0.0f;
    preferred_at_frequency = false;
    range_failure = NULL;
    for (MeasurementRange range = RANGE_G0; range < RANGE_COUNT; ++range) {
        if (operation == TEST_OPEN) calibration[active_mode][range].has_open = false;
        if (operation == TEST_SHORT) calibration[active_mode][range].has_short = false;
    }
    OLED_Clear(&OLED1);
    if (compact_ui && operation == TEST_MEASURE) return StartRange();
    DrawText(0U, "U");
    DrawTextAt(0U, 34U, "I");
    DrawResult(38U, 'F', (float)test_mode.dds_freq, 6U);
    if (waveform_only || UsesManualAdjustment()) DrawResult(47U, 'G', active_range, 1U);
    for (uint8_t y = 0; y < 64U; ++y)
        OLED_DrawPixel(OLED1.FrameBufferPtr, 78U, y);
    return StartRange();
}

static HAL_StatusTypeDef ShowMeasurement(void) {
    memcpy(OLED1.FrameBufferPtr, result_frame, sizeof(result_frame));
    OLED_UpdateState(&OLED1);
    return HAL_OK;
}

static void RecordRangeFailure(const char *reason) {
    range_failure = reason;
    if (operation == TEST_MEASURE || calibration_failure) return;
    calibration_failure = reason;
    failed_frequency = test_mode.dds_freq;
    failed_range = active_range;
}

static bool HasCalibratedRange(void) {
    for (uint8_t mode = 0U; mode < MODE_COUNT; ++mode) {
        for (MeasurementRange range = RANGE_G0; range < RANGE_COUNT; ++range) {
            if (HasCalibration(mode, range))
                return true;
        }
    }
    return false;
}

static HAL_StatusTypeDef NextFrequency(void) {
    if (operation == TEST_MEASURE) {
        const uint8_t priority[] = {route.mode, route.alternate_mode};
        unsigned priority_count = sizeof(priority) / sizeof(priority[0]);
        for (unsigned i = 0U; i < priority_count + MODE_COUNT; ++i) {
            uint8_t mode = i < priority_count ? priority[i] : frequency_order[i - priority_count];
            if (mode >= MODE_COUNT || (tried_frequencies & (1U << mode))) continue;
            if (mode == MODE_100_KHZ &&
                !HasCalibration(mode, RANGE_G1) && !HasCalibration(mode, RANGE_G2) &&
                !HasCalibration(mode, RANGE_G3)) continue;
            active_mode = mode;
            return StartFrequency();
        }
    } else if (frequency_index + 1U < MODE_COUNT) {
        active_mode = frequency_order[++frequency_index];
        return StartFrequency();
    }
    if (operation != TEST_MEASURE) {
        bool complete = recorded_ranges == (MODE_COUNT - 1U) * RANGE_COUNT + 3U;
        if (manual_test) {
            if (operation == TEST_OPEN) open_sweep_done = recorded_ranges > 0U;
            else short_sweep_done = recorded_ranges > 0U;
            manual_ready = open_sweep_done && short_sweep_done && HasCalibratedRange();
        }
        if (!complete && calibration_failure) {
            /* The remaining frame belongs to the last attempted range.
             * Show the failing combination without the unrelated waveforms. */
            OLED_Clear(&OLED1);
            DrawResult(13U, 'P', recorded_ranges, 2U);
            DrawResult(38U, 'F', (float)failed_frequency, 6U);
            DrawResult(47U, 'G', failed_range, 1U);
            DrawTextAt(RESULT_X, 25U, calibration_failure);
        }
        const char *status = complete ? (operation == TEST_OPEN ? "OP" : "SH") :
                             recorded_ranges ? (operation == TEST_OPEN ? "OS" : "SS") : "E";
        if (manual_ready) {
            status = complete ? (operation == TEST_OPEN ? "OP R" : "SH R") :
                                (operation == TEST_OPEN ? "OS R" : "SS R");
            DrawAdjustmentMode();
        }
        DrawStatus(status);
        return recorded_ranges ? HAL_OK : HAL_ERROR;
    }
    if (have_result) return ShowMeasurement();
    DrawStatus(range_failure ? range_failure : "L");
    return HAL_ERROR;
}

static HAL_StatusTypeDef FinishFrequency(void) {
    if (operation == TEST_MEASURE && best_signal >= MIN_SIGNAL_CODES && preferred_at_frequency) {
        if (route.mode >= MODE_COUNT || (tried_frequencies & (1U << route.mode)))
            return ShowMeasurement();
    }
    return NextFrequency();
}

static HAL_StatusTypeDef NextRange(void) {
    if (UsesManualAdjustment()) {
        DrawStatus(range_failure ? range_failure : "L");
        return HAL_ERROR;
    }
    if (operation == TEST_MEASURE) {
        MeasurementRange preferred = PreferredRange();
        if (preferred < RANGE_COUNT && CanScanRange(preferred) &&
            !(tried_ranges & (1U << preferred))) {
            active_range = preferred;
            return StartRange();
        }
        /* With no usable estimate yet, increase current gain before G0. */
        for (MeasurementRange range = RANGE_G1; range < RANGE_COUNT; ++range) {
            if (!CanScanRange(range) || (tried_ranges & (1U << range))) continue;
            active_range = range;
            return StartRange();
        }
        if (CanScanRange(RANGE_G0) && !(tried_ranges & (1U << RANGE_G0))) {
            active_range = RANGE_G0;
            return StartRange();
        }
        return FinishFrequency();
    }
    while (range_index + 1U < RANGE_COUNT) {
        MeasurementRange range = range_order[++range_index];
        if (!CanScanRange(range)) continue;
        active_range = range;
        return StartRange();
    }
    return FinishFrequency();
}

static MeasurementRange RangeForImpedance(float magnitude) {
    if (magnitude < 31.6f) return RANGE_G0;
    if (magnitude < 316.0f) return RANGE_G1;
    if (magnitude < 3162.0f) return RANGE_G2;
    if (magnitude < 31623.0f) return RANGE_G3;
    return RANGE_G4;
}

static MeasurementRange RangeFor100kHz(Complex impedance, uint32_t frequency, float angle) {
    static const float series_resistance[] = {680.0f, 4700.0f, 47000.0f};
    float resistance = fmaxf(0.0f, impedance.re);
    float ratio = (float)FREQ_1E5 / (float)frequency;
    float reactance = angle > RESISTIVE_ANGLE_DEG ? impedance.im * ratio :
                      angle < -RESISTIVE_ANGLE_DEG ? impedance.im / ratio : 0.0f;
    float dut_magnitude = hypotf(resistance, reactance);
    float best_score = -1.0f;
    MeasurementRange best = RANGE_G1;
    for (MeasurementRange range = RANGE_G1; range <= RANGE_G3; ++range) {
        if (!HasCalibration(MODE_100_KHZ, range)) continue;
        float source_path = hypotf(series_resistance[range - RANGE_G1] + resistance, reactance);
        float feedback = Calc_FeedbackMagnitude(FREQ_1E5, range);
        float score = fminf(2.0f * dut_magnitude, feedback) / source_path;
        if (score > best_score) {
            best = range;
            best_score = score;
        }
    }
    return best;
}

static MeasurementRoute RouteForImpedance(Complex impedance, uint32_t frequency) {
    float angle = atan2f(impedance.im, impedance.re) * (360.0f / TWO_PI);
    MeasurementRoute choice = {
        .range = RANGE_G0, .mode = MODE_1_KHZ,
        .alternate_range = RANGE_COUNT, .alternate_mode = MODE_COUNT,
    };
    if (angle > RESISTIVE_ANGLE_DEG) {
        float inductance_mh = 1000.0f * impedance.im / (TWO_PI * (float)frequency);
        /* At 1 kHz the modeled G0 voltage falls below 64 ADC peak codes
         * near 0.33 mH, and the G0/G1 weaker-channel levels cross near 16 mH. */
        choice.alternate_range = RANGE_G0;
        if (inductance_mh < 0.33f) {
            choice.mode = MODE_10_KHZ;
            choice.alternate_mode = MODE_1_KHZ;
        } else if (inductance_mh < 16.0f) {
            choice.alternate_mode = inductance_mh < 3.3f ? MODE_10_KHZ : MODE_100_HZ;
        } else {
            choice.range = RANGE_G1;
            choice.alternate_mode = MODE_100_HZ;
        }
        return choice;
    }
    if (angle >= -RESISTIVE_ANGLE_DEG) {
        choice.range = RangeForImpedance(hypotf(impedance.re, impedance.im));
        choice.mode = choice.range == RANGE_G0 ? MODE_10_KHZ : MODE_1_KHZ;
        choice.alternate_mode = choice.range == RANGE_G0 ? MODE_1_KHZ : MODE_100_HZ;
        choice.alternate_range = choice.range;
        return choice;
    }

    /* Geometric midpoints between the measured capacitance references. */
    float capacitance_nf = -1e9f / (TWO_PI * (float)frequency * impedance.im);
    if (capacitance_nf < 3.18f) {
        choice.range = RANGE_G4;
    } else if (capacitance_nf < 30.8f) {
        choice.range = RANGE_G3;
    } else if (capacitance_nf < 306.0f) {
        choice.range = RANGE_G2;
    } else if (capacitance_nf < 2782.0f) {
        choice.range = RANGE_G2;
        choice.mode = MODE_100_HZ;
        choice.alternate_range = RANGE_G0;
        choice.alternate_mode = MODE_10_KHZ;
    }
    return choice;
}

static bool CaptureAfterSettling(uint32_t settle_ms) {
    if (capture_settled) return true;
    /* Release any snapshot published before the settling interval ended. */
    capture_settled = (uint32_t)(HAL_GetTick() - capture_started_at) >= settle_ms;
    Sample_ReleaseWindow();
    return false;
}

static HAL_StatusTypeDef FinishWaveformCapture(void) {
    if (!CaptureAfterSettling(WAVEFORM_SETTLE_MS)) return HAL_BUSY;
    busy = false;
    if (Sample_Stop() != HAL_OK) {
        DrawStatus("E");
        return HAL_ERROR;
    }
    if (source == SAMPLE_DUT_VOLTAGE) {
        DrawCapture(8U, 30U);
        source = SAMPLE_DUT_CURRENT;
        return StartCapture();
    }
    DrawCapture(41U, 63U);
    DrawStatus("");
    return HAL_OK;
}

static HAL_StatusTypeDef FinishCapture(void) {
    saw_window = true;
    bool calibrating = operation != TEST_MEASURE;
    float stability_floor = calibrating ? 4.0f : 2.0f;
    float stability_relative = calibrating ? 0.04f : 0.02f;
    bool average_windows = test_mode.dds_freq >= FREQ_1E4;
    if (average_windows && !CaptureAfterSettling(HIGH_FREQ_SETTLE_MS))
        return HAL_BUSY;
    uint32_t points = test_mode.dds_points;
    uint32_t cycles = test_mode.capture_cycles;
    if (!average_windows) {
        bool stable_window =
            Calc_FundamentalCaptureStable(samples, points, cycles, stability_floor, stability_relative);
        if (!stable_window) {
            previous_stable = false;
            Sample_ReleaseWindow();
            return HAL_BUSY;
        }
    }

    Complex phasor = Calc_Fundamental(samples, points, cycles);
    uint32_t count = points * cycles;
    uint32_t sum = 0U;
    bool clipped = false;
    for (uint32_t i = 0; i < count; ++i) {
        clipped |= samples[i] <= ADC_RAIL_MARGIN || samples[i] >= 4095U - ADC_RAIL_MARGIN;
        sum += samples[i];
    }
    float mean = (float)sum / (float)count;
    if (average_windows && !clipped) {
        /* Compare groups of coherent windows at the higher frequencies. */
        batch_phasor_sum.re += phasor.re;
        batch_phasor_sum.im += phasor.im;
        batch_mean_sum += mean;
        if (++batch_windows < HIGH_FREQ_AVERAGE_WINDOWS) {
            Sample_ReleaseWindow();
            return HAL_BUSY;
        }
        phasor.re = batch_phasor_sum.re / HIGH_FREQ_AVERAGE_WINDOWS;
        phasor.im = batch_phasor_sum.im / HIGH_FREQ_AVERAGE_WINDOWS;
        mean = batch_mean_sum / HIGH_FREQ_AVERAGE_WINDOWS;
        batch_windows = 0U;
        batch_phasor_sum = (Complex){0};
        batch_mean_sum = 0.0f;
    }
    float tolerance = fmaxf(stability_floor, stability_relative * hypotf(phasor.re, phasor.im));
    bool settled = previous_stable && fabsf(mean - previous_mean) <= tolerance &&
                   hypotf(phasor.re - previous_phasor.re, phasor.im - previous_phasor.im) <= tolerance;
    if (average_windows && settled && !clipped) {
        phasor.re = 0.5f * (phasor.re + previous_phasor.re);
        phasor.im = 0.5f * (phasor.im + previous_phasor.im);
    }
    previous_stable = true;
    previous_phasor = phasor;
    previous_mean = mean;
    if (!settled && !(average_windows && clipped)) {
        Sample_ReleaseWindow();
        return HAL_BUSY;
    }

    busy = false;
    if (Sample_Stop() != HAL_OK) {
        DrawStatus("E");
        return HAL_ERROR;
    }
    if (clipped) {
        RecordRangeFailure(source == SAMPLE_DUT_VOLTAGE ? "UC" : "IC");
        return NextRange();
    }
    if (source == SAMPLE_DUT_VOLTAGE) {
        voltage_phasor = phasor;
        DrawCapture(8U, 30U);
        source = SAMPLE_DUT_CURRENT;
        return StartCapture();
    }

    DrawCapture(41U, 63U);
    CalcCalibration *cal = &calibration[active_mode][active_range];
    float voltage_peak = hypotf(voltage_phasor.re, voltage_phasor.im);
    float current_peak = hypotf(phasor.re, phasor.im);
    if (operation != TEST_MEASURE) {
        bool open = operation == TEST_OPEN;
        float required_peak = open ? voltage_peak : current_peak;
        if (required_peak < MIN_SIGNAL_CODES)
            RecordRangeFailure(open ? "UL" : "IL");
        else if (open ? Calc_RecordOpen(cal, voltage_phasor, phasor) :
                        Calc_RecordShort(cal, voltage_phasor, phasor))
            ++recorded_ranges;
        else
            RecordRangeFailure("E");
        return NextRange();
    }

    float signal = fminf(voltage_peak, current_peak);
    Complex impedance = {0};
    bool calibrated = cal->has_open && cal->has_short;
    bool valid = calibrated ? Calc_CorrectImpedance(cal, voltage_phasor, phasor, &impedance, &signal) :
                              Calc_Impedance(voltage_phasor, phasor, test_mode.dds_freq,
                                             active_range, &impedance);
    if (!UsesManualAdjustment() && route.range == RANGE_COUNT) {
        Complex estimate = impedance;
        if (fminf(voltage_peak, current_peak) >= MIN_PILOT_CODES &&
            (valid || Calc_Impedance(voltage_phasor, phasor, test_mode.dds_freq,
                                    active_range, &estimate))) {
            float estimate_angle = atan2f(estimate.im, estimate.re) * (360.0f / TWO_PI);
            if (fabsf(estimate_angle) <= MAX_PASSIVE_ANGLE_DEG) {
                route = RouteForImpedance(estimate, test_mode.dds_freq);
                high_frequency_range = RangeFor100kHz(estimate, test_mode.dds_freq, estimate_angle);
            }
        } else if (voltage_peak < MIN_PILOT_CODES && current_peak >= MIN_SIGNAL_CODES) {
            /* A weak voltage can identify the low range before R/L/C is known. */
            if (CanScanRange(RANGE_G0) && !(tried_ranges & (1U << RANGE_G0))) {
                active_range = RANGE_G0;
                return StartRange();
            }
        }
    }
    if (!valid) return NextRange();
    float angle = atan2f(impedance.im, impedance.re) * (360.0f / TWO_PI);
    if (fabsf(angle) > MAX_PASSIVE_ANGLE_DEG) return NextRange();
    bool preferred = active_range == PreferredRange();
    float min_signal = active_mode == MODE_100_KHZ && !UsesManualAdjustment() ?
                       MIN_100_KHZ_SIGNAL_CODES : MIN_SIGNAL_CODES;
    if (signal < min_signal || (!preferred && signal <= best_signal)) return NextRange();
    best_signal = signal;
    preferred_at_frequency = preferred;
    unsigned rank = calibrated ? 1U : 0U;
    if (preferred && active_mode != MODE_100_KHZ) {
        rank += active_mode == route.mode ? 6U :
                active_mode == route.alternate_mode ? 4U : 2U;
    }
    if (have_result && (rank < result_rank ||
        (rank == result_rank && (active_mode == MODE_100_KHZ ||
                                 (!preferred && signal <= result_signal)))))
        return preferred ? FinishFrequency() : NextRange();
    if (compact_ui) {
        OLED_Clear(&OLED1);
        DrawComponent(impedance, angle, test_mode.dds_freq);
        DrawMeasurementSettings();
    } else {
        OLED_Delete(OLED1.FrameBufferPtr, RESULT_X, 0U, 46U, 37U);
        OLED_Delete(OLED1.FrameBufferPtr, RESULT_X, 47U, 46U, 17U);
        DrawResult(1U, 'R', impedance.re, 6U);
        DrawResult(13U, 'X', impedance.im, 6U);
        DrawResult(25U, 'A', angle * 10.0f, 4U);
        DrawComponent(impedance, angle, test_mode.dds_freq);
        DrawResult(47U, 'G', active_range, 1U);
        if (calibrated) DrawTextAt(RESULT_X, 56U, "C");
        if (manual_ready) DrawAdjustmentMode();
    }
    memcpy(result_frame, OLED1.FrameBufferPtr, sizeof(result_frame));
    have_result = true;
    result_rank = rank;
    result_signal = signal;
    if (UsesManualAdjustment()) return ShowMeasurement();
    return preferred ? FinishFrequency() : NextRange();
}

static HAL_StatusTypeDef Init(const Mode_t *mode, GPIO_PinState start_pressed_level,
                              TestControl control, bool use_compact_ui) {
    if (!mode || !mode->dds_table || mode->dds_points < 3U || !mode->capture_cycles)
        return HAL_ERROR;
    next_mode = 0U;
    while (next_mode < MODE_COUNT &&
           modes[next_mode]->dds_freq != mode->dds_freq)
        ++next_mode;
    if (next_mode == MODE_COUNT) return HAL_ERROR;
    active_mode = next_mode;
    compact_ui = use_compact_ui;
    operation = TEST_MEASURE;
    waveform_only = control == CONTROL_WAVEFORM;
    manual_test = control == CONTROL_MANUAL;
    manual_ready = open_sweep_done = short_sweep_done = false;
    automatic_adjustment = use_compact_ui;
    start_hold_handled = adjustment_toggle_pending = false;
    manual_frequency_used = false;
    if (manual_test) {
        manual_frequency_index = 0U;
        while (manual_frequency_index < MODE_COUNT &&
               frequency_order[manual_frequency_index] != next_mode)
            ++manual_frequency_index;
        if (manual_frequency_index == MODE_COUNT) return HAL_ERROR;
    }
    manual_range = RANGE_G1;
    manual_high_frequency_range = RANGE_G1;
    selected_range = RANGE_G1;
    if (!waveform_only) {
        for (uint8_t i = 0U; i < MODE_COUNT; ++i)
            for (MeasurementRange range = RANGE_G0; range < RANGE_COUNT; ++range)
                calibration[i][range] = (CalcCalibration){.freq_hz = modes[i]->dds_freq, .range = range};
    }
    initialized = false;
    if (Sample_Init() != HAL_OK) return HAL_ERROR;
    OLED_Init(&hi2c1);
    DrawStatus("R");
    KEY_Init(&start_key, KEY_START_GPIO_Port, KEY_START_Pin, start_pressed_level);
    KEY_Init(&open_key, KEY_OPEN_GPIO_Port, KEY_OPEN_Pin, GPIO_PIN_RESET);
    KEY_Init(&short_key, KEY_SHORT_GPIO_Port, KEY_SHORT_Pin, GPIO_PIN_RESET);
    busy = start_pressed = open_pressed = short_pressed = false;
    initialized = true;
    return HAL_OK;
}

HAL_StatusTypeDef Measurement_Init(const Mode_t *mode, GPIO_PinState start_pressed_level) {
    return Init(mode, start_pressed_level, CONTROL_MANUAL, true);
}

HAL_StatusTypeDef Measurement_InitTest(const Mode_t *mode, GPIO_PinState start_pressed_level,
                                       TestControl control) {
    return Init(mode, start_pressed_level, control, false);
}

static bool KeyTriggered(KEY_HandleTypeDef *key, bool *was_pressed) {
    bool pressed = KEY_IsPressed(key);
    bool triggered = pressed && !*was_pressed;
    *was_pressed = pressed;
    return triggered;
}

static bool StartTriggered(void) {
    if (!manual_ready) return KeyTriggered(&start_key, &start_pressed);
    bool was_pressed = start_pressed;
    start_pressed = KEY_IsPressed(&start_key);
    uint32_t now = HAL_GetTick();
    if (start_pressed && !was_pressed) {
        start_pressed_at = now;
        start_hold_handled = false;
    }
    if ((start_pressed || was_pressed) && !start_hold_handled &&
        (uint32_t)(now - start_pressed_at) >= START_HOLD_MS) {
        start_hold_handled = true;
        adjustment_toggle_pending = !adjustment_toggle_pending;
    }
    return was_pressed && !start_pressed && !start_hold_handled;
}

HAL_StatusTypeDef Measurement_Update(void) {
    if (!initialized) return HAL_ERROR;
    bool start = StartTriggered();
    bool open = KeyTriggered(&open_key, &open_pressed);
    bool shorted = KeyTriggered(&short_key, &short_pressed);
    if (busy) {
        bool calibrating = operation != TEST_MEASURE;
        if (calibrating && start) {
            busy = false;
            HAL_StatusTypeDef status = Sample_Stop();
            DrawStatus(status == HAL_OK ? "STOP" : "E");
            return status;
        }
        if (manual_test && calibrating &&
            (operation == TEST_OPEN ? open : shorted)) {
            busy = false;
            if (Sample_Stop() != HAL_OK) {
                DrawStatus("E");
                return HAL_ERROR;
            }
            RecordRangeFailure(saw_window ? (source == SAMPLE_DUT_VOLTAGE ? "US" : "IS") : "T");
            return NextRange();
        }
        HAL_StatusTypeDef status = Sample_Poll(calibrating ? 0U : MEASUREMENT_CAPTURE_TIMEOUT_MS);
        if (status == HAL_BUSY) return status;
        if (status == HAL_OK) return waveform_only ? FinishWaveformCapture() : FinishCapture();
        busy = false;
        if (status == HAL_TIMEOUT && !waveform_only && saw_window) {
            RecordRangeFailure(source == SAMPLE_DUT_VOLTAGE ? "US" : "IS");
            return NextRange();
        }
        if (status == HAL_TIMEOUT && !waveform_only && !UsesManualAdjustment() &&
            active_mode == MODE_100_KHZ) {
            RecordRangeFailure("T");
            return NextRange();
        }
        if (status == HAL_TIMEOUT && saw_window) {
            if (source == SAMPLE_DUT_VOLTAGE) DrawCapture(8U, 30U);
            else DrawCapture(41U, 63U);
            DrawStatus(source == SAMPLE_DUT_VOLTAGE ? "US" : "IS");
        } else {
            DrawStatus(status == HAL_TIMEOUT ? "T" : "E");
        }
        return status;
    }
    if (adjustment_toggle_pending) {
        adjustment_toggle_pending = false;
        automatic_adjustment = !automatic_adjustment;
        DrawAdjustmentMode();
        OLED_UpdateState(&OLED1);
        return HAL_OK;
    }
    if (start || open || shorted) {
        if (manual_test && !manual_ready && !open && !shorted) return HAL_OK;
        if (manual_ready && automatic_adjustment && !start) return HAL_OK;
        bool measuring = waveform_only || manual_ready || (!manual_test && start);
        operation = measuring ? TEST_MEASURE : open ? TEST_OPEN : TEST_SHORT;
        have_result = false;
        route = (MeasurementRoute){
            .range = RANGE_COUNT, .mode = MODE_COUNT,
            .alternate_range = RANGE_COUNT, .alternate_mode = MODE_COUNT,
        };
        tried_frequencies = 0U;
        high_frequency_range = RANGE_G1;
        recorded_ranges = 0U;
        calibration_failure = NULL;
        if (UsesManualAdjustment()) {
            if (start && manual_frequency_used)
                manual_frequency_index = (manual_frequency_index + 1U) % MODE_COUNT;
            active_mode = frequency_order[manual_frequency_index];
            bool limited_range = compact_ui && active_mode == MODE_100_KHZ;
            MeasurementRange *range = limited_range ? &manual_high_frequency_range : &manual_range;
            MeasurementRange minimum = limited_range ? RANGE_G1 : RANGE_G0;
            MeasurementRange maximum = limited_range ? RANGE_G3 : RANGE_G4;
            if (!start && open && *range > minimum) --*range;
            else if (!start && shorted && *range < maximum) ++*range;
            manual_frequency_used = true;
        } else if (!waveform_only) {
            if (operation == TEST_OPEN) open_sweep_done = false;
            if (operation == TEST_SHORT) short_sweep_done = false;
            frequency_index = 0U;
            active_mode = frequency_order[frequency_index];
        } else if (start) {
            active_mode = next_mode;
            next_mode = (next_mode + 1U) % MODE_COUNT;
        } else {
            MeasurementRange range = selected_range;
            if (open && range > RANGE_G0) --range;
            else if (shorted && range < RANGE_G4) ++range;
            else return HAL_OK;
            HAL_StatusTypeDef status = SelectRange(range);
            if (status != HAL_OK) {
                DrawStatus("E");
                return status;
            }
        }
        if (compact_ui && operation == TEST_MEASURE) DrawMeasurementInProgress();
        return StartFrequency();
    }
    return HAL_OK;
}
