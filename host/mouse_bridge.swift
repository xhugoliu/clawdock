import Foundation
import ApplicationServices
import Darwin

let debug = ProcessInfo.processInfo.environment["JINTAI_MOUSE_DEBUG"] == "1"
let port = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "/dev/cu.usbmodem5CF71530561"
let prompt = ["AXTrustedCheckOptionPrompt" as CFString: true] as CFDictionary
if !AXIsProcessTrustedWithOptions(prompt) {
    fputs("请在系统设置 > 隐私与安全性 > 辅助功能中允许 Jintai Mouse，然后重新运行。\n", stderr)
    exit(2)
}
let fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
if fd < 0 { perror("无法打开设备串口"); exit(1) }
var settings = termios()
if tcgetattr(fd, &settings) != 0 { perror("读取串口配置失败"); exit(1) }
cfmakeraw(&settings)
withUnsafeMutableBytes(of: &settings.c_cc) { cc in
    cc[Int(VMIN)] = 1
    cc[Int(VTIME)] = 0
}
cfsetspeed(&settings, speed_t(B115200))
if tcsetattr(fd, TCSANOW, &settings) != 0 { perror("设置串口失败"); exit(1) }
var flags = fcntl(fd, F_GETFL)
_ = fcntl(fd, F_SETFL, flags & ~O_NONBLOCK)
let source = CGEventSource(stateID: .hidSystemState)
var buttons = 0
let buttonDefs: [(Int, CGMouseButton, CGEventType, CGEventType)] = [
    (1, .left, .leftMouseDown, .leftMouseUp),
    (2, .right, .rightMouseDown, .rightMouseUp),
    (8, CGMouseButton(rawValue: 3)!, .otherMouseDown, .otherMouseUp),
    (16, CGMouseButton(rawValue: 4)!, .otherMouseDown, .otherMouseUp)
]
func mousePosition() -> CGPoint { CGEvent(source: source)?.location ?? .zero }
func postMouse(_ type: CGEventType, _ button: CGMouseButton, _ position: CGPoint) {
    CGEvent(mouseEventSource: source, mouseType: type, mouseCursorPosition: position, mouseButton: button)?.post(tap: .cghidEventTap)
}
func handle(_ values: [Int]) {
    guard values.count == 5, values[0] >= 0, values[0] <= 31,
          values.dropFirst().allSatisfy({ (-127...127).contains($0) }) else { return }
    if debug { print("report", values); fflush(stdout) }
    let next = values[0]
    let pos = mousePosition()
    for (mask, button, down, up) in buttonDefs where (buttons & mask) != (next & mask) {
        postMouse((next & mask) != 0 ? down : up, button, pos)
    }
    buttons = next
    let dx = values[1], dy = values[2]
    if dx != 0 || dy != 0 {
        let target = CGPoint(x: pos.x + CGFloat(dx), y: pos.y + CGFloat(dy))
        let type: CGEventType = (next & 1) != 0 ? .leftMouseDragged : (next & 2) != 0 ? .rightMouseDragged : .mouseMoved
        let button: CGMouseButton = (next & 2) != 0 && (next & 1) == 0 ? .right : .left
        postMouse(type, button, target)
    }
    if values[3] != 0 || values[4] != 0 {
        CGEvent(scrollWheelEvent2Source: source, units: .line, wheelCount: 2,
                wheel1: Int32(values[3]), wheel2: Int32(values[4]), wheel3: 0)?.post(tap: .cghidEventTap)
    }
}
print("Jintai 鼠标桥接已连接：\(port)")
fflush(stdout)
var pending = ""
var buffer = [UInt8](repeating: 0, count: 1024)
while true {
    let count = read(fd, &buffer, buffer.count)
    if debug { print("read", count); fflush(stdout) }
    if count < 0 && errno == EINTR { continue }
    if count <= 0 { break }
    pending += String(decoding: buffer[0..<count], as: UTF8.self)
    while let newline = pending.firstIndex(where: { $0.isNewline }) {
        let line = String(pending[..<newline])
        pending.removeSubrange(...newline)
        if debug { print("line", line.debugDescription); fflush(stdout) }
        let parts = line.split(whereSeparator: { $0.isWhitespace })
        if parts.count == 6 && parts[0] == "JT_MOUSE" {
            let numbers = parts.dropFirst().compactMap { Int($0) }
            handle(numbers)
        }
    }
    if pending.count > 4096 { pending = "" }
}
for (mask, button, _, up) in buttonDefs where (buttons & mask) != 0 { postMouse(up, button, mousePosition()) }
close(fd)
fputs("鼠标串口已断开。\n", stderr)
