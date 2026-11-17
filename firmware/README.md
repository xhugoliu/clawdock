# ESP32-P4 输入与触控诊断固件

这是独立实验固件，在屏幕上观察按键、摇杆和触控，在串口观察原始数据，不连接原项目客户端。测试范围：

- SW1、SW2、SW3、摇杆中键：低电平表示按下，以约 20 ms 间隔采样，连续 3 次相同后输出状态变化。
- 摇杆 X/Y：原始 ADC 读数，每约 1 秒输出本窗口内最小值、最大值和读取错误数。原始值未经电压或中心校准；窗口极值不保证包含机械极限。
- 保留原有按键和摇杆界面，在有效触点位置叠加圆圈，并显示芯片报告的触点数量、启动以来的最大报告数和可解析坐标数。最多解析和绘制两个坐标；更高的报告数仍会保留并提示。
- 约每 20 ms 读取一次 CST826 原始报告，串口在报告数、有效坐标数变化和持续触摸时输出寄存器原始数据及有效坐标。双帧缓冲界面约每 100 ms 更新一次；时间间隔随任务调度和 I2C 响应变化。
- 串口输出芯片修订号和内部可用堆内存；不初始化音频或 Wi-Fi，也不读写应用数据分区。

按键输入映射依据 [原项目输入驱动](https://github.com/ai-maker-lab/HachimoDock/blob/9a22e0f/firmware/main/pet_p4_input.c)：SW1 GPIO50、SW2 GPIO49、SW3 GPIO5、中键 GPIO4、X GPIO21、Y GPIO20。仅适用于已核对相同接线的板卡。短于消抖时间的按压可能不会被捕获，本测试不测响应延迟。

## 构建

需要 Python 3.10+ 和 PlatformIO Core 6.1.19，首次构建需要联网下载 ESP-IDF 和工具链。以仓库根目录为工作目录，例如：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.19
.venv/bin/pio run -d firmware
```

构建产物在 `firmware/.pio/build/p4_v3/`，其中 `firmware.bin` 是应用镜像。环境选择 ESP32-P4 v3，平台版本与原项目本机参考源码一致；本固件并非原项目固件。构建脚本会检查芯片版本范围，并为镜像标注 v3.0–v3.99；它会移除 PlatformIO 自动生成的完整合并镜像，避免误认为它是恢复包。

2026-11-17 已在本机用 ESP-IDF 5.5.4 编译成功，并检查应用镜像校验和及 v3 版本范围。构建时内存统计工具报告 `cannot assign memory region sram_seg to chip memory type`，编译仍成功。已在本机设备烧录并验证按键和摇杆输入，详见[输入实验记录](../experiments/2026-11-17-input-firmware.md)和[屏幕实验记录](../experiments/2026-11-17-display-firmware.md)。

## 实机运行与恢复

已在本机设备验证过烧录和输入读取；恢复原项目固件尚未实机验证，暂不提供一键上传命令。固件的默认分区布局与原设备布局不保证一致；完整烧录或写入分区表可能覆盖原有数据，也不能假定自动回滚适用。先核对设备板型、当前分区和恢复方案，并备份需要保留的内容。原项目 [v0.1.217 发布页](https://github.com/ai-maker-lab/HachimoDock/releases/tag/v0.1.217)提供 R206 各板型完整烧录包，完整恢复会覆盖设备数据。

实际运行时通过 USB-UART 以 115200 baud 读取串口日志，按下和松开每个按键，再推动摇杆观察每秒的 X/Y 范围。原固件日常通信波特率不同，不要用本固件的日志速率判断原固件是否正常。首次实机测试应记录设备、固件构建版本、日志与测试条件。

## 触控报告范围

面板采用 CST826，读取 I2C 地址 `0x38`、寄存器 `0x02` 起的 13 个字节；只按已有的两个触点槽位尝试解析坐标。`TOUCH` 是报告的触点数，`VALID` 是解析为有效坐标的数量，`MAX` 是本次启动以来最大的报告数。高于 2 的报告数会记录，但不会将额外字节假设为坐标。芯片报告触点抬起或不再报告时，圆圈随之消失，不补造触点。芯片资料提到双点能力，具体面板与芯片固件仍以实测为准。[CST826 数据手册](https://surenoo.tech/download/04_STP/Controller/CST826.pdf)

触控解析的主机测试可在仓库根目录运行：

```sh
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main firmware/tests/touch_format_test.c firmware/main/touch_format.c -o /tmp/hachimodock-touch-format-test
/tmp/hachimodock-touch-format-test
```

## 第三方代码

ST7701 MIPI 驱动和面板初始化命令参考并包含原项目的 Apache-2.0 组件代码，来源及许可证详见 [驱动说明](components/st7701/NOTICE.md) 和 [许可证](components/st7701/LICENSE)。
