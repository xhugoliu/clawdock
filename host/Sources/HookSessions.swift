import Foundation

/// Reads per-session snapshots published by Claude Code hooks and the Pi extension.
final class HookSessions {
    private struct Event: Decodable {
        let id: String
        let state: String
        let startedAt: Date
        let updatedAt: Date
        let title: String
        let reply: String?
        let transcript: String?
    }

    private struct Transcript {
        var title: String?
        var reply: String?
        var activity: Date?
        var interruptedAt: Date?
    }

    private let fm = FileManager.default
    private let directory: URL
    private let decoder = JSONDecoder()
    private let time = ISO8601DateFormatter()
    private let fractionalTime = ISO8601DateFormatter()

    init() {
        let override = ProcessInfo.processInfo.environment["JINTAI_SESSION_EVENTS_DIR"]
        directory = override.flatMap { $0.hasPrefix("/") ? URL(fileURLWithPath: $0, isDirectory: true) : nil }
            ?? fm.homeDirectoryForCurrentUser.appendingPathComponent("Library/Caches/com.jintai.host/sessions", isDirectory: true)
        decoder.dateDecodingStrategy = .millisecondsSince1970
        fractionalTime.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    }

    func snapshot(at now: Date) -> [AgentSession]? {
        var rows: [AgentSession] = []
        var available = false
        let cutoff = now.addingTimeInterval(-AgentSessions.activeFor)
        for (folder, source) in [("claude", AgentSource.claude), ("pi", .pi)] {
            let url = directory.appendingPathComponent(folder, isDirectory: true)
            guard let files = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey],
                                                          options: .skipsHiddenFiles) else { continue }
            available = true
            for file in files where file.pathExtension == "json" {
                guard let properties = try? file.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]),
                      properties.isRegularFile == true, let size = properties.fileSize, size <= 64 * 1024,
                      let bytes = try? Data(contentsOf: file),
                      let event = try? decoder.decode(Event.self, from: bytes), !event.id.isEmpty,
                      event.startedAt <= event.updatedAt, event.updatedAt.timeIntervalSince(now) <= 60,
                      var state = Self.state(event.state) else { continue }
                if event.updatedAt < cutoff && (source != .claude || state != .running) { continue }
                let transcript = source == .claude ? readClaudeTranscript(event, cutoff: cutoff) : nil
                var updatedAt = event.updatedAt
                if state == .running, let transcript {
                    if let interruptedAt = transcript.interruptedAt {
                        state = .cancelled
                        updatedAt = interruptedAt
                    } else if let activity = transcript.activity {
                        updatedAt = max(updatedAt, activity)
                    }
                }
                guard updatedAt >= cutoff, updatedAt.timeIntervalSince(now) <= 60 else { continue }
                let id = AgentSession.id(for: source, nativeID: event.id)
                rows.append(AgentSession(id: id, source: source, title: transcript?.title ?? event.title, state: state,
                                         startedAt: event.startedAt, updatedAt: updatedAt,
                                         reply: AgentSession.visibleReply(event.reply ?? transcript?.reply)))
            }
        }
        return available ? rows : nil
    }

    private func readClaudeTranscript(_ event: Event, cutoff: Date) -> Transcript? {
        guard let path = event.transcript, path.hasPrefix("/"),
              (path as NSString).lastPathComponent == "\(event.id).jsonl",
              let attributes = try? fm.attributesOfItem(atPath: path),
              attributes[.type] as? FileAttributeType == .typeRegular,
              let modified = attributes[.modificationDate] as? Date, modified >= cutoff,
              let length = (attributes[.size] as? NSNumber)?.uint64Value,
              let handle = try? FileHandle(forReadingFrom: URL(fileURLWithPath: path)) else { return nil }
        defer { try? handle.close() }

        let capacity = 256 * 1024
        let start = length > capacity ? length - UInt64(capacity) : 0
        guard (try? handle.seek(toOffset: start)) != nil,
              let data = try? handle.read(upToCount: capacity), let end = data.lastIndex(of: 10) else { return nil }
        var result = Transcript()
        for line in data[..<end].split(separator: 10).dropFirst(start > 0 ? 1 : 0) {
            guard let row = (try? JSONSerialization.jsonObject(with: line)) as? [String: Any],
                  row["sessionId"] as? String == event.id,
                  row["isSidechain"] as? Bool != true, row["isMeta"] as? Bool != true else { continue }
            switch row["type"] as? String {
            case "custom-title", "ai-title":
                let title = row["customTitle"] as? String ?? row["aiTitle"] as? String
                if let title, !title.isEmpty { result.title = title }
            case "assistant", "user":
                guard let timestamp = row["timestamp"] as? String,
                      let date = fractionalTime.date(from: timestamp) ?? time.date(from: timestamp),
                      date >= event.startedAt, let message = row["message"] as? [String: Any] else { continue }
                result.activity = max(result.activity ?? date, date)
                let blocks = message["content"] as? [[String: Any]] ?? []
                let parts = blocks.filter { $0["type"] as? String == "text" }.compactMap { $0["text"] as? String }
                let text = message["content"] as? String ?? parts.joined(separator: "\n")
                if message["role"] as? String == "assistant", row["isApiErrorMessage"] as? Bool != true {
                    if let reply = AgentSession.visibleReply(text) { result.reply = reply }
                } else if message["role"] as? String == "user", row["promptSource"] == nil,
                          text == "[Request interrupted by user]" || text == "[Request interrupted by user for tool use]" {
                    result.interruptedAt = date
                }
            default: continue
            }
        }
        return result
    }

    private static func state(_ value: String) -> AgentSessionState? {
        switch value {
        case "running": return .running
        case "completed": return .completed
        case "failed": return .failed
        case "cancelled": return .cancelled
        default: return nil
        }
    }
}
