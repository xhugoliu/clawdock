# 近台固件：ESP32-P4 应用与输入诊断

近台固件包含应用选择器、输入诊断台、鼠标、会话监看和一个占位应用。诊断台在屏幕上显示按键、摇杆和触控，在串口输出原始数据。测试范围：

- SW1、SW2、SW3、摇杆中键：低电平表示按下，以约 20 ms 间隔采样，连续 3 次相同后输出状态变化。
- 摇杆 X/Y：屏幕显示带死区的标准化数值（-1000 至 1000），串口每约 1 秒输出未经死区处理的 ADC 最小值、最大值和读取错误数。窗口极值不保证包含机械极限。
- 摇杆位置示意使用启动时的稳定中心和本次运行逐方向观察到的最大偏移，绘制在等比例圆形坐标区；启动时请松开摇杆，再贴边转一两圈以学习行程。中心两侧各 60 ADC 为死区，屏幕读数和示意点在死区内保持为零；死区外重新缩放，极限仍对应 ±1000。初始行程不足 500 ADC 时使用临时分母；首圈学习时示意点可能越过参考圆，重启会重新校准。参考圆不是硬边界；示意图经过缩放，不代表物理角度或未经处理的原始轨迹。
- 保留原有按键和摇杆界面，在有效触点位置叠加圆圈，并显示芯片报告的触点数量、进入诊断应用以来的最大报告数和可解析坐标数。最多解析和绘制两个坐标；更高的报告数仍会保留并提示。
- 约每 20 ms 读取一次 CST826 原始报告，串口在报告数、有效坐标数变化和持续触摸时输出寄存器原始数据及有效坐标。双帧缓冲界面约每 100 ms 更新一次；时间间隔随任务调度和 I2C 响应变化。
- 串口输出芯片修订号和内部可用堆内存；不初始化音频或 Wi-Fi，也不读写应用数据分区。
- 所有应用共用屏幕休眠计时。连续 29 分钟无输入时显示黑底白字的 `Zzz...`，满 30 分钟时关闭背光、让显示面板休眠并暂停绘制。按键按下或松开、摇杆移动、触点按下、移动或抬起都会重置计时，也可唤醒屏幕。持续按住、摇杆或触点停在原位以及会话更新均不会重置计时。设备和输入采样继续运行。

按键输入映射依据 [原项目输入驱动](https://github.com/ai-maker-lab/HachimoDock/blob/9a22e0f/firmware/main/pet_p4_input.c)：SW1 GPIO50、SW2 GPIO49、SW3 GPIO5、中键 GPIO4、X GPIO21、Y GPIO20。仅适用于已核对相同接线的板卡。短于消抖时间的按压可能不会被捕获，本测试不测响应延迟。

## 应用选择和生命周期

- 启动时进入 `INPUT LAB`。同时按住 SW1、SW2、SW3，三个键消抖后立即打开选择器，继续按住并用摇杆上下移动；列表首尾循环，每次移动后需让摇杆回中。三个 SW 键全部松开时切换到选中的应用；选回当前应用可继续原状态。
- `MOUSE` 是第一个应用，`SESSIONS` 是第二个应用。`APP THREE` 保留用于验证输入归属，按 SW1、SW2 或 SW3 增加计数。切换到另一个应用时，结束旧应用并清空其界面运行状态；选回当前应用并松开按键会继续原状态。固件重启从诊断台开始。
- 应用是编入固件的前台模块，目前不创建应用后台任务。输入采集、摇杆校准、显示驱动和休眠由系统统一管理；选择器打开时应用输入和诊断输出暂停。诊断台始终展示物理输入状态，选择器使用自己的输入规则。
- 按键消抖后，从首个 SW 按键起约 180 ms 内三个键均按下可识别并击，先后顺序不限；三个键均确认按下后立即显示选择器，不额外等待长按；已识别的并击不会交给应用。未构成并击时，缓冲的按键事件会按顺序交给当前应用。选择器显示后，三个键全部松开才提交选择；切换或唤醒后，还需释放按键和触点、摇杆回中，才接受下一次应用操作。屏幕休眠时第一次输入只用于唤醒。
- 固件界面标签使用内置英文字体。会话标题由 Mac 渲染。应用编入固件。

应用选择和生命周期的自动测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main firmware/tests/app_manager_test.c firmware/main/app_manager.c firmware/main/app_runtime.c firmware/main/mouse.c -o /tmp/jintai-app-manager-test
/tmp/jintai-app-manager-test
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main firmware/tests/screen_idle_test.c firmware/main/screen_idle.c -o /tmp/jintai-screen-idle-test
/tmp/jintai-screen-idle-test
```

## 设备界面样式

画布为纯黑，面板和文字使用灰阶，橙色用于小面积的交互反馈。左侧保留 32 px 页面边距，抵消设备两侧黑边的宽度差。标题、底栏和面板可以贴近上、下、右侧边缘。应用选择器和会话列表共用焦点行，页标题和底部操作提示也使用共用绘制函数。[颜色、间距和字体角色](main/app_ui_theme.h)

固件的英文标签使用 Noto Sans 的 22、27、36 px 抗锯齿字形。字体数据预生成并随固件嵌入。修改字体后可用 Pillow 11.3.0 运行下方命令生成字形。[SIL OFL 1.1](main/app_ui_font.OFL.txt) · [Noto Sans 源字体](https://github.com/google/fonts/blob/7af0d90b4d532ffa4b041937b560c6df4e7b1e95/ofl/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf)

```sh
python3 firmware/scripts/build_ui_font.py /path/to/NotoSans.ttf firmware/main/app_ui_font.bin
```

## 会话监看

通过 CH343 USB-UART 连接并运行 [Mac 菜单栏应用](../host/README.md)，在设备选择 `SESSIONS`。每屏最多 3 条，摇杆上下浏览列表中的最多 6 个会话。标题显示来源。运行中的会话优先占用列表名额，随后是近 60 秒内结束的会话。屏幕会区分等待 Mac、连接超时、会话来源不可用和暂无活跃会话。

每条会话显示标题和最近回复的一行摘要，状态位于标题行右侧。选中后按 SW2 打开详情，用摇杆上下翻页，按 SW3 返回列表。三键并击仍会打开应用选择器。详情显示最近一条可见回复，最多 24 页。

原生 HID 连接需要切换 USB 路径，会话监看使用 CH343 串口路径。

## 鼠标应用

选择 `MOUSE` 后，可使用现有 USB-UART 连接配合 Mac 桥接程序，也可切换到原生 USB HS OTG、作为五键 HID 鼠标使用。摇杆默认控制光标，持续推动时光标逐渐加速并有最高速度，回中后重置加速；SW1 切换光标和滚轮模式；滚轮模式下上下滚动，左右控制水平滚动。Mac 桥接根据摇杆幅度发送连续像素滚动，需使用同版本的固件和桥接程序。SW2 是右键，SW3 是左键；SW1+SW2 是鼠标 4（返回），SW2+SW3 是鼠标 5（前进）。单键和双键均支持按住，两个并击键全部松开时释放侧键。各系统、应用对侧键和水平滚动的行为可能不同。

为区分单键、双键与三键选择器，从第一个按键消抖后起有约 180 ms 的并击识别窗口；窗口内按下的双键会抑制各自的单键动作，三键优先打开应用选择器。单键短按会在松开后发送，单键按住超过窗口后发送按下。已经送出单键或双键的情况下，后续加入的键不会触发三键选择器；先全部松开再重新并击。切换应用或唤醒时释放鼠标按键，切换到其他应用后再回来会重置为光标模式。在 `MOUSE` 中，触摸屏也可作为触控板：单指滑动移动光标，轻点左键，双指滑动滚动，双指轻点右键。连续两次轻点可双击；双指操作后需全部抬起才开始下一次手势。触摸与摇杆、物理按键可以同时使用；触摸唤醒屏幕后需先抬手，再进行操作。摇杆校准方法沿用诊断台；初次使用可先沿四个方向推到边缘，使系统学习行程。

### 现有 USB-UART 连接与 Mac

无需调整硬件。用 USB 线连接设备，在 Mac 的终端运行：

```sh
host/start-mouse-bridge.sh
```

首次运行会编译并启动菜单栏应用 `host/Jintai.app`；以后可在 Finder 中双击启动。点击屏幕右上角的近台图标，可查看连接和辅助功能权限、启停鼠标桥接、选择串口或退出。多个兼容串口同时连接时，需要在菜单中选择；设备重新连接后应用会尝试连接上次选中的串口。需要在 macOS 辅助功能设置中允许“近台”。设备重启后仍需重新选择 `MOUSE`。应用占用串口时不能同时烧录或使用其他串口监视器；退出应用后即可使用。构建、权限及测试说明见 [Mac 应用说明](../host/README.md)。

2026-11-17 已在当前 Mac 和 USB-UART 连接上实测光标、左右键、滚轮及浏览器返回/前进，详见[实验记录](../experiments/2026-11-17-usb-mouse.md)。

### 原生 USB 连接

对于与原项目相同 USB 切换电路的板卡，需将 Type-C 路径切换至原生 USB（跳线短接）并连接电脑；跳线断开时是 CH343 USB-UART，无法作为 HID 鼠标。切换前请核对实际板卡接线，原项目的[连接说明](https://github.com/ai-maker-lab/HachimoDock/blob/9a22e0f/firmware/README.md)提供参考。此两种路径共用接口，切到原生 USB 后 USB-UART 日志不可通过同一接口读取。原生 USB 枚举和其他系统的行为尚未实机验证。

触控板手势的主机测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main firmware/tests/touchpad_test.c firmware/main/touchpad.c -o /tmp/jintai-touchpad-test
/tmp/jintai-touchpad-test
```

鼠标和并击逻辑的主机测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main firmware/tests/mouse_test.c firmware/main/app_manager.c firmware/main/app_runtime.c firmware/main/mouse.c -o /tmp/jintai-mouse-test
/tmp/jintai-mouse-test
```

## 构建

需要 Python 3.10+ 和 PlatformIO Core 6.1.19，首次构建需要联网下载 ESP-IDF 和工具链。以仓库根目录为工作目录，例如：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.19
.venv/bin/pio run -d firmware
```

构建产物在 `firmware/.pio/build/p4_v3/`，其中 `firmware.bin` 是应用镜像。环境选择 ESP32-P4 v3，平台版本与原项目本机参考源码一致；本固件并非原项目固件。构建脚本会检查芯片版本范围，并为镜像标注 v3.0–v3.99；它会移除 PlatformIO 自动生成的完整合并镜像，避免误认为它是恢复包。

2026-11-17 已在本机用 ESP-IDF 5.5.4 编译成功，并检查应用镜像校验和及 v3 版本范围。构建时内存统计工具报告 `cannot assign memory region sram_seg to chip memory type`，编译仍成功。此前诊断版本已在本机设备烧录并验证按键和摇杆输入；应用选择和鼠标版本的实机观察分别见[应用选择实验记录](../experiments/2026-11-17-app-switching.md)和[鼠标实验记录](../experiments/2026-11-17-usb-mouse.md)。此前验证详见[输入实验记录](../experiments/2026-11-17-input-firmware.md)和[屏幕实验记录](../experiments/2026-11-17-display-firmware.md)。

## 实机运行与恢复

已在本机设备验证过烧录和输入读取；恢复原项目固件尚未实机验证，暂不提供一键上传命令。固件的默认分区布局与原设备布局不保证一致；完整烧录或写入分区表可能覆盖原有数据，也不能假定自动回滚适用。先核对设备板型、当前分区和恢复方案，并备份需要保留的内容。原项目 [v0.1.217 发布页](https://github.com/ai-maker-lab/HachimoDock/releases/tag/v0.1.217)提供 R206 各板型完整烧录包，完整恢复会覆盖设备数据。

实际运行时通过 USB-UART 以 115200 baud 读取串口日志，按下和松开每个按键，再推动摇杆观察每秒的 X/Y 范围。原固件日常通信波特率不同，不要用本固件的日志速率判断原固件是否正常。首次实机测试应记录设备、固件构建版本、日志与测试条件。

## 摇杆计算测试

```sh
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main firmware/tests/joystick_test.c firmware/main/joystick.c -o /tmp/jintai-joystick-test
/tmp/jintai-joystick-test
```

具体测试条件和原始范围见[摇杆实验记录](../experiments/2026-11-17-joystick-visualization.md)。

## 触控报告范围

面板采用 CST826，读取 I2C 地址 `0x38`、寄存器 `0x02` 起的 13 个字节；只按已有的两个触点槽位尝试解析坐标。`TOUCH` 是报告的触点数，`VALID` 是解析为有效坐标的数量，`MAX` 是本次进入诊断应用以来最大的报告数。高于 2 的报告数会记录，但不会将额外字节假设为坐标。芯片报告触点抬起或不再报告时，圆圈随之消失，不补造触点。芯片资料提到双点能力，具体面板与芯片固件仍以实测为准。[CST826 数据手册](https://surenoo.tech/download/04_STP/Controller/CST826.pdf)

触控解析的主机测试可在仓库根目录运行：

```sh
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main firmware/tests/touch_format_test.c firmware/main/touch_format.c -o /tmp/jintai-touch-format-test
/tmp/jintai-touch-format-test
```

## 第三方代码

ST7701 MIPI 驱动和面板初始化命令参考并包含原项目的 Apache-2.0 组件代码，来源及许可证详见 [驱动说明](components/st7701/NOTICE.md) 和 [许可证](components/st7701/LICENSE)。
