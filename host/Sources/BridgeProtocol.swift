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

func bridgeFrame(kind: UInt8, payload: Data) -> Data {
    var body = Data([kind])
    body.appendLE(UInt16(payload.count))
    body.append(payload)
    var crc = UInt32.max
    for byte in body {
        crc ^= UInt32(byte)
        for _ in 0..<8 { crc = (crc >> 1) ^ (crc & 1 == 0 ? 0 : 0xedb88320) }
    }
    var frame = Data("JCX4".utf8)
    frame.append(body)
    var checksum = (~crc).littleEndian
    withUnsafeBytes(of: &checksum) { frame.append(contentsOf: $0) }
    return frame
}

func clockResponse(to line: String) -> Data? {
    guard line == "JT_TIME 1" else { return nil }
    let now = Date()
    let offset = TimeZone.autoupdatingCurrent.secondsFromGMT(for: now)
    let localMilliseconds = Int64(((now.timeIntervalSince1970 + Double(offset)) * 1000).rounded(.down))
    let day = Int64(86_400_000)
    var sinceMidnight = UInt32((localMilliseconds % day + day) % day).littleEndian
    return bridgeFrame(kind: 3, payload: withUnsafeBytes(of: &sinceMidnight) { Data($0) })
}

extension Data {
    mutating func appendLE(_ value: UInt16) {
        var littleEndian = value.littleEndian
        Swift.withUnsafeBytes(of: &littleEndian) { append(contentsOf: $0) }
    }
}
