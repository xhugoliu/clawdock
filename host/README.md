# 近台 macOS 菜单栏应用

近台在屏幕右上角显示菜单栏图标，通过 USB-UART 与设备交换消息。菜单提供鼠标桥接和 Codex 会话监看开关。应用不在 Dock 显示图标，也不会自动设置开机启动。

## 启动

需要 macOS 12 或更高版本，以及带 Swift 编译器的 Xcode Command Line Tools。在仓库根目录运行：

```sh
host/start-mouse-bridge.sh
```

脚本首次运行或源码更新后会构建 `host/Jintai.app`，然后启动应用。以后也可以在 Finder 中双击该应用。点击右上角图标，可查看连接状态、启停鼠标桥接、选择串口、重新连接或退出。

首次使用鼠标桥接需要在“系统设置 → 隐私与安全性 → 辅助功能”中允许“近台”。菜单中会显示授权状态；如系统没有自动列出应用，可以手动添加本目录的 `Jintai.app`。本地构建使用临时签名，重新构建或更换路径后如果权限失效，需要重新授权。

应用识别当前板卡使用的 CH343 USB 串口型号（VID `1A86`、PID `55D3`），首次只有一个候选串口时自动连接；多个候选串口时请在“设备串口”中选择。该型号也可能用于其他设备，串口连接成功并不等于验证了设备固件身份。所选串口会保留，断开后重新出现时自动连接；更换设备导致串口名称变化时请重新选择。串口被应用占用时不能同时用于烧录或串口监视；可从菜单退出后进行。

## Codex 会话监看

在设备选中 `CODEX`。近台只读检查 `~/.codex/sessions` 和 `session_index.jsonl`，也支持启动环境中的 `CODEX_HOME`。会话标题优先使用 Codex 的标题索引，缺失时显示项目目录名。Mac 将标题和最近一条可见回复渲染为位图，供设备显示摘要和详情。详情可显示最近回复的前 2400 个字符。整个监看功能无需辅助功能授权。

近台根据本地日志中的轮次事件判断运行状态。超过 10 分钟没有新状态信号的运行记录会退出列表。只在云端执行且没有对应本地轮次日志的会话无法通过此方式显示。

## 开发与测试

- `Sources/SerialDevices.swift` 发现兼容串口，`SerialConnection.swift` 负责读取和断线处理。
- `Sources/BridgeProtocol.swift` 负责拆分串口消息；应用按 `BridgeModule` 将消息分发给各功能模块，鼠标实现位于 `MouseBridge.swift`。
- `Sources/CodexSessions.swift` 读取本地日志，`CodexBridge.swift` 响应固件的 `JT_CODEX 4` 请求，通过同一串口发送带 CRC 的 `JCX4` 列表或详情页。设备约每 2 秒同步一次，切换视图或翻页时立即请求，8 秒无有效快照后显示离线。
- `Sources/MenuBarApp.swift` 管理菜单、权限状态和设备选择；`main.swift` 提供应用入口及测试入口。

```sh
host/build-mouse-bridge.sh
python3 host/tests/serial_bridge_test.py
python3 host/tests/codex_serial_test.py
```

测试使用伪终端，不发送真实鼠标事件。设备型号识别、用户权限和完整交互仍需在 macOS 上实机检查。
