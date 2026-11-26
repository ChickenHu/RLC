
typedef union {
    uint16_t value[32];
    struct {
        uint8_t low_byte;
        uint8_t high_byte;
    } bytes[32];
} SINE32_t;

extern const SINE32_t SINE_WAVE_32;


typedef union {
    uint16_t value[80];
    struct {
        uint8_t low_byte;
        uint8_t high_byte;
    } bytes[80];
} SINE80_t;

extern const SINE80_t SINE_WAVE_80;


typedef union {
    uint16_t value[160];
    struct {
        uint8_t low_byte;
        uint8_t high_byte;
    } bytes[160];
} SINE160_t;

extern const SINE160_t SINE_WAVE_160;


typedef union {
    uint16_t value[256];
    struct {
        uint8_t low_byte;
        uint8_t high_byte;
    } bytes[256];
} SINE256_t;

extern const SINE256_t SINE_WAVE_256;

