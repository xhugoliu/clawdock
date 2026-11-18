import CryptoKit
import Foundation

enum AgentSessionState: UInt8 {
    case running = 1, completed, failed, cancelled
}

enum AgentSource: String {
    case codex = "Codex"
    case openCode = "OpenCode"
    case claude = "Claude Code"
    case pi = "Pi"
}

struct AgentSession {
    let id: UUID
    let source: AgentSource
    let title: String
    let state: AgentSessionState
    let startedAt: Date
    let updatedAt: Date
    var reply: String?

    var displayTitle: String {
        let name = title.trimmingCharacters(in: .whitespacesAndNewlines)
        return name.isEmpty ? source.rawValue : name
    }

    static func id(for source: AgentSource, nativeID: String) -> UUID {
        let key = Data("com.jintai.session:\(source.rawValue):\(nativeID)".utf8)
        var bytes = Array(SHA256.hash(data: key).prefix(16))
        bytes[6] = (bytes[6] & 0x0f) | 0x80
        bytes[8] = (bytes[8] & 0x3f) | 0x80
        return UUID(uuid: (bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                           bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]))
    }

    static func visibleReply(_ text: String?) -> String? {
        guard let text, !text.isEmpty else { return nil }
        let prefix = text.prefix(2401)
        var value = String(prefix.prefix(2400)).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !value.isEmpty else { return nil }
        if prefix.count > 2400 { value += "…" }
        return value
    }

    static func listOrder(_ left: Self, _ right: Self) -> Bool {
        let leftRunning = left.state == .running
        let rightRunning = right.state == .running
        if leftRunning != rightRunning { return leftRunning }
        if left.updatedAt != right.updatedAt { return left.updatedAt > right.updatedAt }
        return left.id.uuidString < right.id.uuidString
    }
}

/// Merges sources on the bridge's monitor queue and keeps the device's selection stable.
final class AgentSessions {
    static let limit = 6
    static let activeFor: TimeInterval = 600
    static let retainFor: TimeInterval = 60

    private let codex = CodexSessions()
    private let openCode: OpenCodeSessions?
    private let hooks: HookSessions?
    private var observedSince: Date?
    private var visible: [UUID] = []
    private var shownRunning: Set<UUID> = []
    private var retained: [UUID: Date] = [:]
    private(set) var recent: [UUID: AgentSession] = [:]

    init(queue: DispatchQueue, codexOnly: Bool) {
        openCode = codexOnly ? nil : OpenCodeSessions(queue: queue)
        hooks = codexOnly ? nil : HookSessions()
    }

    func snapshot(at now: Date) -> [AgentSession]? {
        if observedSince == nil { observedSince = now }
        let sources = [codex.snapshot(at: now), openCode?.snapshot(at: now), hooks?.snapshot(at: now)]
            .compactMap { $0 }
        recent.removeAll(keepingCapacity: true)
        retained = retained.filter { $0.value > now }
        var candidates: [AgentSession] = []
        for row in sources.joined() {
            recent[row.id] = row
            if row.state == .running {
                retained[row.id] = nil
                candidates.append(row)
            } else {
                let expires = row.updatedAt.addingTimeInterval(Self.retainFor)
                if expires > now, shownRunning.contains(row.id) || row.startedAt >= (observedSince ?? now) {
                    retained[row.id] = expires
                }
                if retained[row.id] == expires { candidates.append(row) }
            }
        }

        let ranked = candidates.sorted(by: AgentSession.listOrder).prefix(Self.limit)
        let byID = Dictionary(uniqueKeysWithValues: ranked.map { ($0.id, $0) })
        visible = visible.filter { byID[$0] != nil }
        let previous = Set(visible)
        visible.append(contentsOf: ranked.map(\.id).filter { !previous.contains($0) })
        shownRunning = Set(visible.filter { byID[$0]?.state == .running })
        return sources.isEmpty ? nil : visible.compactMap { byID[$0] }
    }
}
