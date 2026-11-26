#! python3

H_TEMPLATE = """
typedef union {{
    uint16_t value[{}];
    struct {{
        uint8_t low_byte;
        uint8_t high_byte;
    }} bytes[{}];
}} SINE{}_t;

extern const SINE{}_t SINE_WAVE_{};
"""

C_TEMPLATE = """
const SINE{}_t SINE_WAVE_{}={{
    .bytes={{
        #embed "../../Inc/Assets/sin_table_{}.bin"
    }}
}};
"""

with open("Core/Inc/Assets/dds.h.inc", "w") as header, open(
    "Core/Inc/Assets/dds.c.inc", "w"
) as source:
    for POINT_NUM in [32, 80, 160, 256]:
        header.write(
            H_TEMPLATE.format(POINT_NUM, POINT_NUM, POINT_NUM, POINT_NUM, POINT_NUM)
            + "\r\n"
        )
        source.write(C_TEMPLATE.format(POINT_NUM, POINT_NUM, POINT_NUM) + "\r\n")
