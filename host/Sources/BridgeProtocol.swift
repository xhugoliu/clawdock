import Foundation

/// A line-based transport: additional bridge modules can consume their own messages.
final class BridgeProtocol {
    private var pending = Data()
    var onLine: ((String) -> Void)?

    func receive(_ bytes: Data) {
        pending.append(bytes)
        while let newline = pending.firstIndex(of: 0x0a) {
            let lineData = pending[..<newline]
            pending.removeSubrange(...newline)
            let line = String(decoding: lineData, as: UTF8.self)
                .trimmingCharacters(in: .newlines)
            if !line.isEmpty { onLine?(line) }
        }
        if pending.count > 4096 { pending.removeAll() }
    }

    func reset() { pending.removeAll() }
}

protocol BridgeModule: AnyObject {
    func receive(line: String)
    func reset()
}
