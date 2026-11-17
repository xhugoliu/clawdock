# HachimoDock 硬件实验室

记录一台 HachimoDock 设备的硬件识别、固件开发与性能实验。每项实验区分实机观测、参考资料和未验证的推断，记录测试条件与限制。

## 诊断固件

- [ESP32-P4 v3 输入与触控诊断固件](firmware/README.md)：显示按键、摇杆与触点并记录原始数据；构建及实机验证状态见文档。

## 实验记录

- [2026-11-17：硬件识别与首次基线](experiments/2026-11-17-hardware-baseline.md)
- [2026-11-17：输入诊断固件实机测试](experiments/2026-11-17-input-firmware.md)
- [2026-11-17：屏幕显示与输入测试](experiments/2026-11-17-display-firmware.md)
- [2026-11-17：触控上限测试](experiments/2026-11-17-touch-limit.md)
- [2026-11-17：摇杆转圈与示意图校准](experiments/2026-11-17-joystick-visualization.md)

## 计划

- 扩展独立诊断固件，优先验证屏幕显示、触控、按键和摇杆。
- 核对开发板与屏幕等部件型号，逐项测量相关能力和瓶颈。
- 扬声器暂时搁置：目前没有明确用途，不主动排查无声问题或安排音频测试。

## 参考项目

设备原项目为 [HachimoDock](https://github.com/ai-maker-lab/HachimoDock)，相关硬件、固件及第三方材料以各自的许可证和使用条件为准。本仓库包含独立实验固件；屏幕驱动及面板初始化代码包含原项目中的 Apache-2.0 组件，详见固件说明。
