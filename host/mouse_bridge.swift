import Foundation
import ApplicationServices
import AppKit
import Darwin

let debug = ProcessInfo.processInfo.environment["JINTAI_MOUSE_DEBUG"] == "1"
let suppressEvents = ProcessInfo.processInfo.environment["JINTAI_MOUSE_TEST_NO_POST"] == "1"
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
struct ClickHistory { var time: TimeInterval; var position: CGPoint; var count: Int64 }
var clickHistory: [Int: ClickHistory] = [:]
func postMouse(_ type: CGEventType, _ button: CGMouseButton, _ position: CGPoint, clickCount: Int64? = nil) {
    guard !suppressEvents, let event = CGEvent(mouseEventSource: source, mouseType: type,
                                               mouseCursorPosition: position, mouseButton: button) else { return }
    if let clickCount { event.setIntegerValueField(.mouseEventClickState, value: clickCount) }
    event.post(tap: .cghidEventTap)
}
func handle(_ values: [Int]) {
    guard values.count == 5, values[0] >= 0, values[0] <= 31,
          values.dropFirst().allSatisfy({ (-127...127).contains($0) }) else { return }
    if debug { print("report", values); fflush(stdout) }
    let next = values[0]
    let pos = mousePosition()
    for (mask, button, down, up) in buttonDefs where (buttons & mask) != (next & mask) {
        let pressed = (next & mask) != 0
        if pressed {
            let now = ProcessInfo.processInfo.systemUptime
            let previous = clickHistory[mask]
            let nearby = previous.map { abs($0.position.x - pos.x) <= 5 && abs($0.position.y - pos.y) <= 5 } ?? false
            let count = previous != nil && nearby && now - previous!.time <= NSEvent.doubleClickInterval ? previous!.count + 1 : 1
            clickHistory[mask] = ClickHistory(time: now, position: pos, count: count)
        }
        let count = clickHistory[mask]?.count ?? 1
        if debug { print("click", mask, pressed ? "down" : "up", count); fflush(stdout) }
        postMouse(pressed ? down : up, button, pos, clickCount: count)
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
var scrollRemainderX = 0.0
var scrollRemainderY = 0.0
func scrollAxis(_ value: Int, _ remainder: inout Double) -> Int32 {
    if value == 0 { remainder = 0; return 0 }
    let normalized = Double(value) / 1000.0
    let delta = normalized * abs(normalized) * 12.0
    if remainder * delta < 0 { remainder = 0 }
    remainder += delta
    let whole = Int32(remainder)
    remainder -= Double(whole)
    return whole
}
func handleScroll(_ values: [Int]) {
    guard values.count == 2, values.allSatisfy({ (-1000...1000).contains($0) }) else { return }
    let horizontal = scrollAxis(-values[0], &scrollRemainderX)
    let vertical = scrollAxis(values[1], &scrollRemainderY)
    if debug { print("scroll", [horizontal, vertical]); fflush(stdout) }
    if !suppressEvents && (vertical != 0 || horizontal != 0) {
        CGEvent(scrollWheelEvent2Source: source, units: .pixel, wheelCount: 2,
                wheel1: vertical, wheel2: horizontal, wheel3: 0)?.post(tap: .cghidEventTap)
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
        } else if parts.count == 3 && parts[0] == "JT_SCROLL" {
            handleScroll(parts.dropFirst().compactMap { Int($0) })
        } else if parts.count == 3 && parts[0] == "JT_TOUCH_SCROLL" {
            let values = parts.dropFirst().compactMap { Int($0) }
            if values.count == 2 && values.allSatisfy({ (-40...40).contains($0) }) {
                if debug { print("touch scroll", values); fflush(stdout) }
                if !suppressEvents && (values[0] != 0 || values[1] != 0) {
                    CGEvent(scrollWheelEvent2Source: source, units: .pixel, wheelCount: 2,
                            wheel1: Int32(values[1]), wheel2: Int32(values[0]), wheel3: 0)?.post(tap: .cghidEventTap)
                }
            }
        }
    }
    if pending.count > 4096 { pending = "" }
}
for (mask, button, _, up) in buttonDefs where (buttons & mask) != 0 { postMouse(up, button, mousePosition()) }
close(fd)
fputs("鼠标串口已断开。\n", stderr)
