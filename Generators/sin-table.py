#! python3
# coding=utf-8


import numpy as np
import math

for POINT_NUM in [16, 32, 80, 160, 256]:

    pi = math.pi

    # 一个周期 POINT_NUM 个点
    # The 16- and 80-point modes use one point per phase.
    n = np.linspace(0, 2 * pi, POINT_NUM, endpoint=POINT_NUM not in (16, 80))
    # At 10 kHz the reconstruction filter passes nearly the full DAC
    # amplitude. Limit Mode3 to a nominal 0.5 Vrms before that filter.
    peak_scale = 0.5 * math.sqrt(2) / (3.3 / 2) 

    # 计算POINT_NUM个点的正弦值
    a = map(math.sin, n)

    r = []
    for i in a:
        # 调整幅值至在0~1区间
        i = i * peak_scale + 1

        # 按3.3V电压调整幅值
        i *= 3.3 / 2

        # 求取dac数值，12位dac LSB = 3.3V/2**12
        ri = round(i * 2**12 / 3.3)
 
        # 检查参数
        if ri >= 4095:  
            ri = 4095

        # 得到dac数值序列
        r.append(ri)

    print(list(map(int, r)))

    # 写入序列到文件
    with open(f"Core/Inc/Assets/sin_table_{POINT_NUM}.bin", "wb+") as f:
        f.write(np.asarray(r, dtype="<u2").tobytes())
