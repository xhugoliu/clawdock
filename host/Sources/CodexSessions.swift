import Foundation

enum CodexSessionState: UInt8 {
    case running = 1, completed, failed, cancelled
}

struct CodexSession {
    let id: UUID
    let title: String
    let state: CodexSessionState
    let updatedAt: Date
    let reply: String?
}

/// Reads only local rollout and title files. All access happens on the monitor queue.
final class CodexSessions {
    static let limit = 6
    private static let activeFor: TimeInterval = 600
    private static let retainFor: TimeInterval = 60
    private static let tailBytes = 1024 * 1024

    private struct Rollout {
        let id: UUID
        let project: String
        let inode: NSNumber?
        var offset: UInt64 = 0
        var turn: String?
        var startedAt = Date.distantPast
        var updatedAt = Date.distantPast
        var state: CodexSessionState?
        var modified = Date.distantPast
        var reply: String?
    }

    private let fm = FileManager.default
    private let directory: URL
    private let index: URL
    private let time = ISO8601DateFormatter()
    private let fractionalTime = ISO8601DateFormatter()
    private var rollouts: [URL: Rollout] = [:]
    private var ignoredRollouts: [URL: NSNumber] = [:]
    private var titles: [UUID: String] = [:]
    private var indexModified: Date?
    private var nextDiscovery = Date.distantPast
    private var observedSince: Date?
    private var visible: [UUID] = []
    private var shownRunning: Set<UUID> = []
    private var retained: [UUID: Date] = [:]
    private(set) var recent: [UUID: CodexSession] = [:]

    init(home: URL = URL(fileURLWithPath: ProcessInfo.processInfo.environment["CODEX_HOME"]
                        .flatMap { $0.isEmpty ? nil : ($0 as NSString).expandingTildeInPath }
                        ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex").path)) {
        directory = home.appendingPathComponent("sessions", isDirectory: true)
        index = home.appendingPathComponent("session_index.jsonl")
        fractionalTime.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    }

    func snapshot(at now: Date = Date()) -> [CodexSession]? {
        recent.removeAll(keepingCapacity: true)
        guard fm.isReadableFile(atPath: directory.path) else { return nil }
        if observedSince == nil { observedSince = now }
        refreshTitles()
        let fullScan = now >= nextDiscovery
        var candidates = Set(rollouts.keys).union(ignoredRollouts.keys)
        if fullScan {
            guard let files = fm.enumerator(at: directory, includingPropertiesForKeys: [.isRegularFileKey],
                                            options: [.skipsHiddenFiles]) else { return nil }
            for case let url as URL in files where isRollout(url) { candidates.insert(url) }
            nextDiscovery = now.addingTimeInterval(10)
        } else {
            // New turns usually start in today's directory. Full scans also find old threads being resumed.
            let calendar = Calendar.current
            for daysAgo in 0...1 {
                guard let day = calendar.date(byAdding: .day, value: -daysAgo, to: now) else { continue }
                let parts = calendar.dateComponents([.year, .month, .day], from: day)
                let path = String(format: "%04d/%02d/%02d", parts.year ?? 0, parts.month ?? 0, parts.day ?? 0)
                let folder = directory.appendingPathComponent(path)
                let files = (try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
                candidates.formUnion(files.filter(isRollout))
            }
        }

        let cutoff = now.addingTimeInterval(-Self.activeFor)
        for url in candidates {
            guard let attributes = try? fm.attributesOfItem(atPath: url.path),
                  attributes[.type] as? FileAttributeType == .typeRegular,
                  let modified = attributes[.modificationDate] as? Date, modified >= cutoff,
                  let size = (attributes[.size] as? NSNumber)?.uint64Value else {
                rollouts[url] = nil
                ignoredRollouts[url] = nil
                continue
            }
            update(url, size: size, inode: attributes[.systemFileNumber] as? NSNumber, modified: modified)
        }

        var latest: [UUID: Rollout] = [:]
        for row in rollouts.values where row.state != nil && row.updatedAt >= cutoff
            && row.updatedAt.timeIntervalSince(now) <= 60 {
            if let previous = latest[row.id] {
                if previous.updatedAt > row.updatedAt { continue }
                if previous.updatedAt == row.updatedAt {
                    if previous.state != .running { continue }
                    if row.state == .running && previous.modified >= row.modified { continue }
                }
            }
            latest[row.id] = row
        }

        retained = retained.filter { $0.value > now }
        var active: [CodexSession] = []
        var finished: [CodexSession] = []
        for row in latest.values {
            guard let state = row.state else { continue }
            let card = CodexSession(id: row.id, title: titles[row.id] ?? row.project, state: state,
                                    updatedAt: row.updatedAt, reply: row.reply)
            recent[row.id] = card
            if state == .running {
                retained[row.id] = nil
                active.append(card)
            } else {
                let expires = row.updatedAt.addingTimeInterval(Self.retainFor)
                if expires > now, shownRunning.contains(row.id) || row.startedAt >= (observedSince ?? now) {
                    retained[row.id] = expires
                }
                if retained[row.id] == expires { finished.append(card) }
            }
        }

        let ranked = (active.sorted(by: newestFirst) + finished.sorted(by: newestFirst)).prefix(Self.limit)
        let byID = Dictionary(uniqueKeysWithValues: ranked.map { ($0.id, $0) })
        visible = visible.filter { byID[$0] != nil }
        let previous = Set(visible)
        visible.append(contentsOf: ranked.map(\.id).filter { !previous.contains($0) })
        shownRunning = Set(visible.filter { byID[$0]?.state == .running })
        return visible.compactMap { byID[$0] }
    }

    private func newestFirst(_ left: CodexSession, _ right: CodexSession) -> Bool {
        if left.updatedAt != right.updatedAt { return left.updatedAt > right.updatedAt }
        return left.id.uuidString < right.id.uuidString
    }

    private func isRollout(_ url: URL) -> Bool {
        url.lastPathComponent.hasPrefix("rollout-") && url.pathExtension == "jsonl"
    }

    private func update(_ url: URL, size: UInt64, inode: NSNumber?, modified: Date) {
        if let ignored = ignoredRollouts[url], ignored == inode { return }
        var cursor = rollouts[url]
        if let old = cursor, old.inode != inode || old.offset > size || (old.offset == size && old.modified != modified) {
            cursor = nil
            rollouts[url] = nil
        }
        if let cursor, cursor.offset == size { return }
        guard let handle = try? FileHandle(forReadingFrom: url) else { return }
        defer { try? handle.close() }
        if cursor == nil {
            guard let header = try? handle.read(upToCount: Self.tailBytes),
                  let end = header.firstIndex(of: 10),
                  let meta = object(header[..<end]), meta["type"] as? String == "session_meta",
                  let payload = meta["payload"] as? [String: Any],
                  let id = (payload["id"] as? String).flatMap(UUID.init(uuidString:)) else { return }
            let source = payload["source"] as? [String: Any]
            if payload["thread_source"] as? String == "subagent" || source?["subagent"] != nil {
                ignoredRollouts[url] = inode
                return
            }
            let basename = (payload["cwd"] as? String).map { ($0 as NSString).lastPathComponent } ?? ""
            cursor = Rollout(id: id, project: basename.isEmpty || basename == "/" ? "Codex" : basename, inode: inode)
        }
        guard var row = cursor else { return }
        let bounded = size - row.offset > Self.tailBytes
        let start = bounded ? size - UInt64(Self.tailBytes) : row.offset
        if bounded {
            row.turn = nil
            row.startedAt = .distantPast
            row.updatedAt = .distantPast
            row.state = nil
            row.reply = nil
        }
        do {
            try handle.seek(toOffset: start)
            guard let data = try handle.read(upToCount: Self.tailBytes), let end = data.lastIndex(of: 10) else { return }
            var lineStart = bounded ? (data.firstIndex(of: 10) ?? end) + 1 : data.startIndex
            while lineStart <= end, let newline = data[lineStart...end].firstIndex(of: 10) {
                if let record = object(data[lineStart..<newline]) { apply(record, to: &row) }
                lineStart = newline + 1
            }
            row.offset = start + UInt64(end + 1)
            row.modified = modified
            rollouts[url] = row
        } catch { return }
    }

    private func apply(_ record: [String: Any], to row: inout Rollout) {
        guard let type = record["type"] as? String,
              type == "event_msg" || type == "turn_context" || type == "response_item",
              let timestamp = record["timestamp"] as? String,
              let date = fractionalTime.date(from: timestamp) ?? time.date(from: timestamp), date >= row.updatedAt,
              let payload = record["payload"] as? [String: Any] else { return }
        let turn = payload["turn_id"] as? String
        let event = payload["type"] as? String
        if type == "turn_context" || event == "task_started" || event == "turn_started" {
            guard let turn else { return }
            if row.turn != turn || row.state == nil {
                row.turn = turn
                row.startedAt = date
                row.state = .running
                row.reply = nil
            }
            guard row.state == .running else { return }
        } else if type == "response_item" {
            guard row.state == .running, event == "message", payload["role"] as? String == "assistant",
                  visiblePhase(payload), let content = payload["content"] as? [[String: Any]],
                  let text = replyText(content.compactMap { part -> String? in
                      guard part["type"] as? String == "output_text" else { return nil }
                      return part["text"] as? String
                  }.joined(separator: "\n")) else { return }
            row.reply = text
        } else if let event {
            switch event {
            case "task_complete", "turn_completed", "task_failed", "turn_failed", "turn_aborted", "turn_cancelled":
                if let turn, let current = row.turn, turn != current { return }
                row.turn = turn ?? row.turn
                if event == "task_complete" || event == "turn_completed" {
                    row.state = .completed
                    if let text = replyText(payload["last_agent_message"] as? String) { row.reply = text }
                }
                else if event == "task_failed" || event == "turn_failed" { row.state = .failed }
                else { row.state = .cancelled }
            case "agent_message":
                guard row.state == .running, turn == nil || turn == row.turn, visiblePhase(payload),
                      let text = replyText(payload["message"] as? String) else { return }
                row.reply = text
            case "token_count", "item_completed", "sub_agent_activity", "exec_command_begin", "exec_command_end":
                guard row.state == .running, turn == nil || turn == row.turn else { return }
            default: return
            }
        } else { return }
        row.updatedAt = date
    }

    private func visiblePhase(_ payload: [String: Any]) -> Bool {
        guard let phase = payload["phase"] as? String else { return true }
        return phase == "commentary" || phase == "final_answer"
    }

    private func replyText(_ text: String?) -> String? {
        guard let text, !text.isEmpty else { return nil }
        let prefix = text.prefix(2401)
        var value = String(prefix.prefix(2400)).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !value.isEmpty else { return nil }
        if prefix.count > 2400 { value += "…" }
        return value
    }

    private func refreshTitles() {
        guard let attributes = try? fm.attributesOfItem(atPath: index.path),
              let modified = attributes[.modificationDate] as? Date, modified != indexModified,
              let size = (attributes[.size] as? NSNumber)?.uint64Value,
              let handle = try? FileHandle(forReadingFrom: index) else { return }
        defer { try? handle.close() }
        let bounded = size > Self.tailBytes
        do {
            try handle.seek(toOffset: bounded ? size - UInt64(Self.tailBytes) : 0)
            guard let data = try handle.read(upToCount: Self.tailBytes) else { return }
            var next: [UUID: String] = [:]
            for line in data.split(separator: 10).dropFirst(bounded ? 1 : 0) {
                guard let entry = object(line), let raw = entry["id"] as? String,
                      let id = UUID(uuidString: raw), let title = entry["thread_name"] as? String,
                      !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { continue }
                next[id] = title
            }
            titles = next
            indexModified = modified
        } catch { return }
    }

    private func object(_ bytes: Data) -> [String: Any]? {
        (try? JSONSerialization.jsonObject(with: bytes)) as? [String: Any]
    }
}
