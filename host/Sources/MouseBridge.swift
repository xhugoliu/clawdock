import Foundation
import ApplicationServices
import AppKit

/// Interprets mouse reports from the device. Other modules share the serial stream.
final class MouseBridge: BridgeModule {
    var enabled = true {
        didSet { if !enabled { reset() } }
    }
    var allowed: () -> Bool = { AXIsProcessTrusted() }
    var debug = false
    var suppressEvents = false
    private let source = CGEventSource(stateID: .hidSystemState)
    private var buttons = 0
    private var remainderX = 0.0
    private var remainderY = 0.0
    private struct ClickHistory { var time: TimeInterval; var position: CGPoint; var count: Int64 }
    private var clickHistory: [Int: ClickHistory] = [:]
    private let buttonDefs: [(Int, CGMouseButton, CGEventType, CGEventType)] = [
        (1, .left, .leftMouseDown, .leftMouseUp),
        (2, .right, .rightMouseDown, .rightMouseUp),
        (8, CGMouseButton(rawValue: 3)!, .otherMouseDown, .otherMouseUp),
        (16, CGMouseButton(rawValue: 4)!, .otherMouseDown, .otherMouseUp)
    ]
    private func log(_ message: String) { if debug { print(message); fflush(stdout) } }
    private func position() -> CGPoint { CGEvent(source: source)?.location ?? .zero }
    private func postMouse(_ type: CGEventType, _ button: CGMouseButton, _ point: CGPoint, clickCount: Int64? = nil) {
        guard !suppressEvents, let event = CGEvent(mouseEventSource: source, mouseType: type,
                                                   mouseCursorPosition: point, mouseButton: button) else { return }
        if let clickCount { event.setIntegerValueField(.mouseEventClickState, value: clickCount) }
        event.post(tap: .cghidEventTap)
    }
    func reset() {
        let point = position()
        for (mask, button, _, up) in buttonDefs where buttons & mask != 0 {
            postMouse(up, button, point, clickCount: clickHistory[mask]?.count ?? 1)
        }
        buttons = 0
        remainderX = 0; remainderY = 0
        clickHistory.removeAll()
    }
    func receive(line: String) {
        guard enabled, allowed() else { return }
        let parts = line.split(whereSeparator: { $0.isWhitespace })
        guard let kind = parts.first else { return }
        let values = parts.dropFirst().compactMap { Int($0) }
        switch kind {
        case "JT_MOUSE" where parts.count == 6 && values.count == 5: handleMouse(values)
        case "JT_SCROLL" where parts.count == 3 && values.count == 2: handleScroll(values)
        case "JT_TOUCH_SCROLL" where parts.count == 3 && values.count == 2: handleTouchScroll(values)
        default: break
        }
    }
    private func handleMouse(_ values: [Int]) {
        guard (0...31).contains(values[0]), values.dropFirst().allSatisfy({ (-127...127).contains($0) }) else { return }
        log("report \(values)")
        let next = values[0], point = position()
        for (mask, button, down, up) in buttonDefs where (buttons & mask) != (next & mask) {
            let pressed = next & mask != 0
            if pressed {
                let now = ProcessInfo.processInfo.systemUptime
                let previous = clickHistory[mask]
                let nearby = previous.map { abs($0.position.x - point.x) <= 5 && abs($0.position.y - point.y) <= 5 } ?? false
                let count = previous != nil && nearby && now - previous!.time <= NSEvent.doubleClickInterval ? previous!.count + 1 : 1
                clickHistory[mask] = ClickHistory(time: now, position: point, count: count)
            }
            let count = clickHistory[mask]?.count ?? 1
            log("click \(mask) \(pressed ? "down" : "up") \(count)")
            postMouse(pressed ? down : up, button, point, clickCount: count)
        }
        buttons = next
        let dx = values[1], dy = values[2]
        if dx != 0 || dy != 0 {
            let target = CGPoint(x: point.x + CGFloat(dx), y: point.y + CGFloat(dy))
            let type: CGEventType = next & 1 != 0 ? .leftMouseDragged : next & 2 != 0 ? .rightMouseDragged : .mouseMoved
            let button: CGMouseButton = next & 2 != 0 && next & 1 == 0 ? .right : .left
            postMouse(type, button, target)
        }
        if !suppressEvents && (values[3] != 0 || values[4] != 0) {
            CGEvent(scrollWheelEvent2Source: source, units: .line, wheelCount: 2,
                    wheel1: Int32(values[3]), wheel2: Int32(values[4]), wheel3: 0)?.post(tap: .cghidEventTap)
        }
    }
    private func scrollAxis(_ value: Int, _ remainder: inout Double) -> Int32 {
        if value == 0 { remainder = 0; return 0 }
        let normalized = Double(value) / 1000.0
        let delta = normalized * abs(normalized) * 12.0
        if remainder * delta < 0 { remainder = 0 }
        remainder += delta
        let whole = Int32(remainder)
        remainder -= Double(whole)
        return whole
    }
    private func pixelScroll(_ x: Int32, _ y: Int32) {
        if !suppressEvents && (x != 0 || y != 0) {
            CGEvent(scrollWheelEvent2Source: source, units: .pixel, wheelCount: 2,
                    wheel1: y, wheel2: x, wheel3: 0)?.post(tap: .cghidEventTap)
        }
    }
    private func handleScroll(_ values: [Int]) {
        guard values.allSatisfy({ (-1000...1000).contains($0) }) else { return }
        let x = scrollAxis(-values[0], &remainderX), y = scrollAxis(values[1], &remainderY)
        log("scroll [\(x), \(y)]")
        pixelScroll(x, y)
    }
    private func handleTouchScroll(_ values: [Int]) {
        guard values.allSatisfy({ (-40...40).contains($0) }) else { return }
        log("touch scroll \(values)")
        pixelScroll(Int32(values[0]), Int32(values[1]))
    }
}
