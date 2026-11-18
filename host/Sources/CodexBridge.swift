import CoreGraphics
import CoreText
import Foundation

final class CodexBridge: BridgeModule {
    private enum Request: Equatable {
        case list
        case detail(UUID, UInt8)

        init?(_ line: String) {
            if line == "JT_CODEX 4" { self = .list; return }
            let fields = line.split(separator: " ")
            guard fields.count == 4, fields[0] == "JT_CODEX", fields[1] == "4",
                  let id = UUID(uuidString: String(fields[2])), let page = UInt8(fields[3]) else { return nil }
            self = .detail(id, page)
        }
    }

    private let queue = DispatchQueue(label: "com.jintai.codex-monitor", qos: .utility)
    private let sessions = CodexSessions()
    private let encoder = CodexFrameEncoder()
    private var generation = 0
    private var busy = false
    private var pending: Request?
    var enabled = true
    var send: ((Data) -> Void)?

    func receive(line: String) {
        guard enabled, let request = Request(line) else { return }
        pending = request
        sendPending()
    }

    private func sendPending() {
        guard !busy, let request = pending else { return }
        busy = true
        pending = nil
        let generation = self.generation
        queue.async { [weak self] in
            guard let self else { return }
            let now = Date()
            let rows = self.sessions.snapshot(at: now)
            let frame: Data
            switch request {
            case .list: frame = self.encoder.list(rows)
            case let .detail(id, page):
                frame = self.encoder.detail(self.sessions.recent[id], id: id, requested: page)
            }
            DispatchQueue.main.async { [weak self] in
                guard let self, self.generation == generation else { return }
                self.busy = false
                if self.enabled && (self.pending == nil || self.pending == request) { self.send?(frame) }
                self.sendPending()
            }
        }
    }

    func reset() {
        generation += 1
        busy = false
        pending = nil
    }
}

/// JCX4 carries a list or one detail page. Monochrome bitmaps use zero runs and literal runs.
private final class CodexFrameEncoder {
    private let width = 544
    // Reserve the right side of the title row for the board's status label.
    private let titleWidth = 428
    private let titleHeight = 32
    private let previewHeight = 32
    private let detailHeight = 320
    private let titleFont = CTFontCreateUIFontForLanguage(.system, 30, nil)!
    private let previewFont = CTFontCreateUIFontForLanguage(.system, 27, nil)!
    private let detailFont = CTFontCreateUIFontForLanguage(.system, 26, nil)!
    private var titles: [String: Data] = [:]
    private var previews: [String: Data] = [:]
    private var detailText: String?
    private var pages: [Data] = []

    func list(_ sessions: [CodexSession]?) -> Data {
        let rows = Array((sessions ?? []).prefix(CodexSessions.limit))
        let usedTitles = Set(rows.map(\.title))
        let usedPreviews = Set(rows.compactMap(\.reply))
        titles = titles.filter { usedTitles.contains($0.key) }
        previews = previews.filter { usedPreviews.contains($0.key) }
        var payload = Data([sessions == nil ? 0 : 1, UInt8(rows.count)])
        for row in rows {
            appendID(row.id, to: &payload)
            appendSession(row, to: &payload)
            if let reply = row.reply {
                let pixels = previews[reply] ?? lineBitmap(reply, font: previewFont,
                                                           height: previewHeight, fitWithin: width)
                previews[reply] = pixels
                appendBitmap(pixels, to: &payload)
            } else {
                payload.appendLE(0)
            }
        }
        return frame(kind: 1, payload: payload)
    }

    func detail(_ session: CodexSession?, id: UUID, requested: UInt8) -> Data {
        var payload = Data()
        appendID(id, to: &payload)
        guard let session else {
            payload.append(contentsOf: [requested, 0])
            return frame(kind: 2, payload: payload)
        }
        if detailText != session.reply || pages.isEmpty {
            detailText = session.reply
            pages = session.reply.map(detailBitmaps) ?? [Data()]
        }
        let page = min(Int(requested), pages.count - 1)
        payload.append(contentsOf: [UInt8(page), UInt8(pages.count)])
        appendSession(session, to: &payload)
        appendBitmap(pages[page], to: &payload)
        return frame(kind: 2, payload: payload)
    }

    private func appendID(_ id: UUID, to output: inout Data) {
        var uuid = id.uuid
        withUnsafeBytes(of: &uuid) { output.append(contentsOf: $0) }
    }

    private func appendSession(_ row: CodexSession, to output: inout Data) {
        output.append(row.state.rawValue)
        let pixels = titles[row.title] ?? lineBitmap(row.title, font: titleFont,
                                                    height: titleHeight, fitWithin: titleWidth)
        titles[row.title] = pixels
        appendBitmap(pixels, to: &output)
    }

    private func appendBitmap(_ pixels: Data, to output: inout Data) {
        output.appendLE(UInt16(pixels.count))
        output.append(pixels)
    }

    private func attributed(_ text: String, font: CTFont) -> NSAttributedString {
        NSAttributedString(string: text, attributes: [
            NSAttributedString.Key(kCTFontAttributeName as String): font,
            NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 1, alpha: 1)
        ])
    }

    private func lineBitmap(_ text: String, font: CTFont, height: Int, fitWithin lineWidth: Int) -> Data {
        let clean = text.prefix(200).split(whereSeparator: \.isWhitespace).joined(separator: " ")
        let line = CTLineCreateWithAttributedString(attributed(clean.isEmpty ? "Codex" : clean, font: font))
        let ellipsis = CTLineCreateWithAttributedString(attributed("…", font: font))
        let fitted = CTLineCreateTruncatedLine(line, Double(lineWidth - 4), .end, ellipsis) ?? line
        return bitmap(height) { context in
            context.clip(to: CGRect(x: 0, y: 0, width: lineWidth, height: height))
            context.textPosition = CGPoint(x: 2, y: 6)
            CTLineDraw(fitted, context)
        }
    }

    private func detailBitmaps(_ text: String) -> [Data] {
        let attributed = attributed(text, font: detailFont)
        let setter = CTFramesetterCreateWithAttributedString(attributed)
        let path = CGPath(rect: CGRect(x: 2, y: 2, width: width - 4, height: detailHeight - 4), transform: nil)
        var result: [Data] = []
        var offset = 0
        while offset < attributed.length, result.count < 24 {
            let page = CTFramesetterCreateFrame(setter, CFRange(location: offset, length: 0), path, nil)
            let visible = CTFrameGetVisibleStringRange(page)
            guard visible.length > 0 else { break }
            result.append(bitmap(detailHeight) { CTFrameDraw(page, $0) })
            offset = visible.location + visible.length
        }
        return result.isEmpty ? [Data()] : result
    }

    private func bitmap(_ height: Int, draw: (CGContext) -> Void) -> Data {
        let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                bytesPerRow: width, space: CGColorSpaceCreateDeviceGray(),
                                bitmapInfo: CGImageAlphaInfo.none.rawValue)
        var output = [UInt8](repeating: 0, count: width / 8 * height)
        if let context, let pixels = context.data?.assumingMemoryBound(to: UInt8.self) {
            context.setFillColor(gray: 0, alpha: 1)
            context.fill(CGRect(x: 0, y: 0, width: width, height: height))
            draw(context)
            for y in 0..<height {
                for x in 0..<width where pixels[y * width + x] >= 96 {
                    output[y * (width / 8) + x / 8] |= 0x80 >> (x % 8)
                }
            }
        }
        return pack(output)
    }

    private func pack(_ bytes: [UInt8]) -> Data {
        var output = Data()
        var start = 0
        while start < bytes.count {
            let end = min(start + 128, bytes.count)
            var cursor = start
            if bytes[start] == 0 {
                while cursor < end, bytes[cursor] == 0 { cursor += 1 }
            }
            if cursor - start >= 2 {
                output.append(0x80 | UInt8(cursor - start - 1))
            } else {
                cursor = start + 1
                while cursor < end {
                    if bytes[cursor] == 0, cursor + 1 < bytes.count, bytes[cursor + 1] == 0 { break }
                    cursor += 1
                }
                output.append(UInt8(cursor - start - 1))
                output.append(contentsOf: bytes[start..<cursor])
            }
            start = cursor
        }
        return output
    }

    private func frame(kind: UInt8, payload: Data) -> Data {
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
}

private extension Data {
    mutating func appendLE(_ value: UInt16) {
        var littleEndian = value.littleEndian
        Swift.withUnsafeBytes(of: &littleEndian) { append(contentsOf: $0) }
    }
}
