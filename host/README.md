# 近台 macOS 菜单栏应用

近台在屏幕右上角显示菜单栏图标，通过 USB-UART 与设备交换消息。菜单提供鼠标桥接和会话监看开关。应用不在 Dock 显示图标，也不会自动设置开机启动。

## 启动

需要 macOS 12 或更高版本，以及带 Swift 编译器的 Xcode Command Line Tools。在仓库根目录运行：

```sh
host/start-mouse-bridge.sh
```

脚本首次运行或源码更新后会构建 `host/Jintai.app`，然后启动应用。以后也可以在 Finder 中双击该应用。点击右上角图标，可查看连接状态、启停鼠标桥接、选择串口、重新连接或退出。

首次使用鼠标桥接需要在“系统设置 → 隐私与安全性 → 辅助功能”中允许“近台”。菜单中会显示授权状态；如系统没有自动列出应用，可以手动添加本目录的 `Jintai.app`。本地构建使用临时签名，重新构建或更换路径后如果权限失效，需要重新授权。

应用识别当前板卡使用的 CH343 USB 串口型号（VID `1A86`、PID `55D3`），首次只有一个候选串口时自动连接；多个候选串口时请在“设备串口”中选择。该型号也可能用于其他设备，串口连接成功并不等于验证了设备固件身份。所选串口会保留，断开后重新出现时自动连接；更换设备导致串口名称变化时请重新选择。串口被应用占用时不能同时用于烧录或串口监视；可从菜单退出后进行。

## 会话监看

在设备选中 `SESSIONS`。近台合并 Codex、OpenCode、Claude Code 和 Pi 的本机会话，标题注明来源。运行中的会话优先，近台观察到的已结束轮次保留 60 秒。Mac 把标题和最近一条可见回复渲染为位图，详情最多显示约 2400 字符。会话监看无需辅助功能授权。

- **Codex** 自动读取 `~/.codex/sessions` 和 `session_index.jsonl`，也支持启动环境中的 `CODEX_HOME`。标题索引缺失时显示项目目录名。运行记录超过 10 分钟没有新状态信号会退出列表。
- **OpenCode 2.x** 自动发现 `~/.local/state/opencode/service.json` 注册的本机服务，也支持启动环境中的 `XDG_STATE_HOME`。通过服务的只读会话 API 获取状态和回复。默认共享服务内的会话可以显示。以 `--standalone` 启动的独立服务超出这一发现范围。 [OpenCode API](https://opencode.ai/docs/api)
- **Claude Code** 由 hooks 上报主会话的轮次开始、完成和失败。配合本地会话日志读取标题、中间回复和中断标记。可以先在仓库根目录启动一次带 hooks 的 CLI。 [Claude Code hooks](https://code.claude.com/docs/en/hooks)

  ```sh
  claude --settings "$(node host/hooks/claude-settings.cjs)"
  ```

  若日常 CLI 会话也需要监看，将 `node host/hooks/claude-settings.cjs` 的输出合并进 `~/.claude/settings.json` 的 `hooks`，保留已有同名事件的配置。配置使用当前仓库和 Node 的绝对路径。

- **Pi 1.5** 由扩展上报轮次和最近可见回复，等 Pi 结束重试和排队任务后记录结果。可先从仓库根目录运行 `pi -e "$PWD/host/hooks/pi.ts"`。日常使用可把该扩展的绝对路径加入 `~/.pi/agent/settings.json` 的 `extensions` 数组。 [Pi 扩展文档](https://github.com/earendil-works/pi/blob/v1.5.0/packages/coding-agent/docs/extensions.md)

Claude Code 和 Pi 的 hooks 在 `~/Library/Caches/com.jintai.host/sessions` 下写入同用户可读的状态和有限回复摘要，启动新会话时清理超过一天的缓存。Claude Code 和 Pi 运行记录超过 10 分钟没有新信号也会退出列表。会话只在远端执行时，需要有相应的本地状态来源才能显示。

## 开发与测试

- `Sources/SerialDevices.swift` 发现兼容串口，`SerialConnection.swift` 负责读取和断线处理。
- `Sources/BridgeProtocol.swift` 负责拆分串口消息；应用按 `BridgeModule` 将消息分发给各功能模块，鼠标实现位于 `MouseBridge.swift`。
- `Sources/AgentSessions.swift` 合并会话来源，`SessionBridge.swift` 响应固件的 `JT_CODEX 4` 请求，通过同一串口发送带 CRC 的 `JCX4` 列表或详情页。设备约每 2 秒同步一次，切换视图或翻页时立即请求，8 秒无有效快照后显示离线。
- `Sources/MenuBarApp.swift` 管理菜单、权限状态和设备选择；`main.swift` 提供应用入口及测试入口。

```sh
host/build-mouse-bridge.sh
python3 host/tests/serial_bridge_test.py
python3 host/tests/codex_serial_test.py
```

测试使用伪终端和模拟 OpenCode 服务，不发送真实鼠标事件。设备型号识别、用户权限和完整交互可在 macOS 上实机检查。
