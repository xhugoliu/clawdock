# 近台 · Jintai

近台是一个桌面交互终端项目。目前已实现 ESP32-P4 设备端的应用选择，以及显示、触控、按键和摇杆诊断；支持通过 USB-UART 和 [Mac 菜单栏应用](host/README.md)控制鼠标，也支持原生 USB HID 鼠标。

项目基于一台 HachimoDock 设备开展开发与验证。实验记录区分实机观测、参考资料和未验证的推断，并注明测试条件与限制。

## 诊断固件

- [ESP32-P4 v3 应用与输入诊断固件](firmware/README.md)：包含应用选择、输入诊断台、鼠标应用和两个占位应用；构建及实机验证状态见文档。

## 实验记录

- [2026-11-17：硬件识别与首次基线](experiments/2026-11-17-hardware-baseline.md)
- [2026-11-17：输入诊断固件实机测试](experiments/2026-11-17-input-firmware.md)
- [2026-11-17：屏幕显示与输入测试](experiments/2026-11-17-display-firmware.md)
- [2026-11-17：触控上限测试](experiments/2026-11-17-touch-limit.md)
- [2026-11-17：摇杆转圈与示意图校准](experiments/2026-11-17-joystick-visualization.md)
- [2026-11-17：内置应用与选择器实机检查](experiments/2026-11-17-app-switching.md)
- [2026-11-17：USB 鼠标应用构建与设备启动](experiments/2026-11-17-usb-mouse.md)
- [2026-11-17：鼠标应用触控板](experiments/2026-11-17-touchpad.md)
- [2026-11-18：Mac 菜单栏应用](experiments/2026-11-18-mac-menu-bar.md)

## 计划

- 在硬件验证的基础上，逐步开发设备端界面与交互功能。
- 继续探索与电脑的双向交互。
- 核对开发板与屏幕等部件型号，逐项测量相关能力和瓶颈。
- 扬声器暂时搁置：目前没有明确用途，不主动排查无声问题或安排音频测试。

## 参考项目

设备原项目为 [HachimoDock](https://github.com/ai-maker-lab/HachimoDock)，相关硬件、固件及第三方材料以各自的许可证和使用条件为准。本仓库包含独立实验固件；屏幕驱动及面板初始化代码包含原项目中的 Apache-2.0 组件，详见固件说明。
