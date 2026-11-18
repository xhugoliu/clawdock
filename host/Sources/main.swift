import Foundation
import AppKit

if let index = CommandLine.arguments.firstIndex(of: "--test-codex-serial"), index + 1 < CommandLine.arguments.count {
    let serial = SerialConnection()
    let codex = CodexBridge()
    codex.send = { frame in
        print("Codex TX bytes=\(frame.count)"); fflush(stdout)
        serial.sendLatest(frame)
    }
    serial.onLine = { line in print(line); fflush(stdout); codex.receive(line: line) }
    serial.onDisconnect = { codex.reset(); exit(0) }
    do {
        try serial.connect(CommandLine.arguments[index + 1])
        print("测试串口已连接"); fflush(stdout)
        RunLoop.main.run()
    } catch { fputs("测试串口无法打开：\(error)\n", stderr); exit(1) }
} else if CommandLine.arguments.contains("--list-ports") {
    for device in SerialDevices.discover() { print(device.path) }
} else if let index = CommandLine.arguments.firstIndex(of: "--test-serial"), index + 1 < CommandLine.arguments.count {
    let serial = SerialConnection()
    let mouse = MouseBridge()
    mouse.debug = true
    mouse.suppressEvents = true
    mouse.allowed = { true }
    serial.onLine = { mouse.receive(line: $0) }
    serial.onDisconnect = { mouse.reset(); exit(0) }
    do {
        try serial.connect(CommandLine.arguments[index + 1])
        print("测试串口已连接"); fflush(stdout)
        RunLoop.main.run()
    } catch { fputs("测试串口无法打开：\(error)\n", stderr); exit(1) }
} else {
    let app = NSApplication.shared
    let delegate = MenuBarApp()
    app.delegate = delegate
    app.setActivationPolicy(.accessory)
    app.run()
    withExtendedLifetime(delegate) {}
}
