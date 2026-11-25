import Foundation

/// Observes the registered OpenCode 2.x service without starting it.
/// The monitor queue owns the cache and refresh state. Requests use immutable inputs.
final class OpenCodeSessions: @unchecked Sendable {
    private struct Envelope<DataType: Decodable>: Decodable {
        let data: DataType
    }

    private struct Connection: Decodable {
        let url: URL
        let password: String

        var isLocal: Bool {
            url.scheme == "http" && ["127.0.0.1", "[::1]", "::1"].contains(url.host ?? "") &&
            url.port != nil && (url.path.isEmpty || url.path == "/") &&
            url.user == nil && url.password == nil && url.query == nil && url.fragment == nil
        }
    }

    private struct Summary: Decodable {
        struct Times: Decodable {
            let busy: Double?
            let idle: Double?
            let updated: Double
            let archived: Double?
        }

        let id: String
        let parentID: String?
        let title: String?
        let outcome: String?
        let time: Times
    }

    private struct Message: Decodable {
        struct Times: Decodable { let created: Double }
        struct Block: Decodable {
            let type: String
            let text: String?
        }

        let type: String
        let time: Times
        let content: [Block]?
    }

    private struct Activity: Decodable { let type: String }

    private struct Form: Decodable {
        struct Field: Decodable {
            struct Option: Decodable { let value: String }
            let options: [Option]?
            let hidden: Bool?
        }
        let fields: [Field]

        var hasChoices: Bool {
            fields.contains { $0.hidden != true && $0.options?.isEmpty == false }
        }
    }

    private final class NoRedirects: NSObject, URLSessionTaskDelegate {
        func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                        newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
            completionHandler(nil)
        }
    }

    private let queue: DispatchQueue
    private let registration: URL
    private let http: URLSession
    private var nextRefresh = Date.distantPast
    private var refreshing = false
    private var cached: [AgentSession]?

    init(queue: DispatchQueue) {
        self.queue = queue
        let stateHome = ProcessInfo.processInfo.environment["XDG_STATE_HOME"].flatMap {
            $0.hasPrefix("/") ? URL(fileURLWithPath: $0, isDirectory: true) : nil
        } ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".local/state", isDirectory: true)
        registration = stateHome.appendingPathComponent("opencode/service.json")
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 1.5
        config.timeoutIntervalForResource = 2
        config.connectionProxyDictionary = [:]
        config.urlCache = nil
        config.httpCookieStorage = nil
        config.urlCredentialStorage = nil
        http = URLSession(configuration: config, delegate: NoRedirects(), delegateQueue: nil)
    }

    deinit { http.invalidateAndCancel() }

    func snapshot(at now: Date) -> [AgentSession]? {
        if !refreshing && now >= nextRefresh {
            refreshing = true
            nextRefresh = now.addingTimeInterval(2)
            let previous = cached ?? []
            Task {
                let rows = await load(at: now, previous: previous)
                queue.async { [self] in
                    cached = rows
                    refreshing = false
                }
            }
        }
        return cached
    }

    private func load(at now: Date, previous: [AgentSession]) async -> [AgentSession]? {
        guard let bytes = try? Data(contentsOf: registration), bytes.count <= 8192,
              let connection = try? JSONDecoder().decode(Connection.self, from: bytes), connection.isLocal,
              !connection.password.isEmpty else { return nil }

        async let activity = get([String: Activity].self, "/api/session/active", from: connection)
        async let recent = get([Summary].self, "/api/session", query: ["limit": "32", "order": "desc"], from: connection)
        guard let activity = await activity, var summaries = await recent else { return nil }
        let active = Set(activity.filter { $0.value.type == "running" }.keys)
        let known = Set(summaries.map(\.id))
        let missing = active.subtracting(known).filter(Self.isSessionID)
        await withTaskGroup(of: Summary?.self) { group in
            for id in missing {
                group.addTask { await self.get(Summary.self, "/api/session/\(id)", from: connection) }
            }
            for await summary in group {
                if let summary { summaries.append(summary) }
            }
        }

        let prior = Dictionary(uniqueKeysWithValues: previous.map { ($0.id, $0) })
        let cutoff = now.addingTimeInterval(-AgentSessions.activeFor)
        let mainSessions = summaries.filter { $0.parentID == nil && $0.time.archived == nil && Self.isSessionID($0.id) }
        let activeMainIDs = active.intersection(mainSessions.map(\.id))
        let waiting = await withTaskGroup(of: (String, Bool?).self) { group in
            for id in activeMainIDs {
                group.addTask {
                    let forms = await self.get([Form].self, "/api/session/\(id)/form", from: connection)
                    return (id, forms.map { $0.contains(where: \.hasChoices) })
                }
            }
            var results: [String: Bool] = [:]
            for await (id, hasChoices) in group { results[id] = hasChoices }
            return results
        }
        var rows: [AgentSession] = []
        var nativeIDs: [UUID: String] = [:]
        for info in mainSessions {
            let id = AgentSession.id(for: .openCode, nativeID: info.id)
            guard nativeIDs[id] == nil else { continue }
            let old = prior[id]
            let running = active.contains(info.id)
            let busy = info.time.busy.map(Self.date)
            let idle = info.time.idle.map(Self.date)
            let state: AgentSessionState
            let startedAt: Date
            let updatedAt: Date
            if running {
                let currentBusy = busy.flatMap { time in (idle ?? .distantPast) < time ? time : nil }
                startedAt = currentBusy ?? (old?.state.isActive == true ? old?.startedAt : nil) ?? now
                let wasWaiting = old?.state == .waitingForAnswer && old?.startedAt == startedAt
                state = (waiting[info.id] ?? wasWaiting) ? .waitingForAnswer : .running
                updatedAt = max(startedAt, Self.date(info.time.updated))
            } else {
                guard let idle, idle >= cutoff, (busy ?? .distantPast) <= idle else { continue }
                switch info.outcome {
                case "succeeded": state = .completed
                case "failed": state = .failed
                case "interrupted": state = .cancelled
                default: continue
                }
                startedAt = busy ?? .distantPast
                updatedAt = idle
            }
            let reply = old?.startedAt == startedAt ? old?.reply : nil
            nativeIDs[id] = info.id
            rows.append(AgentSession(id: id, source: .openCode, title: info.title ?? "OpenCode", state: state,
                                     startedAt: startedAt, updatedAt: updatedAt, reply: reply))
        }

        rows.sort(by: AgentSession.listOrder)
        let recentEnd = now.addingTimeInterval(-AgentSessions.retainFor)
        await withTaskGroup(of: (Int, String?).self) { group in
            for index in rows.indices.prefix(AgentSessions.limit) {
                let row = rows[index]
                guard let id = nativeIDs[row.id], row.state.isActive || row.updatedAt >= recentEnd else { continue }
                let old = prior[row.id]
                if !row.state.isActive && old?.state == row.state && old?.updatedAt == row.updatedAt && row.reply != nil {
                    continue
                }
                group.addTask { (index, await self.reply(id: id, since: row.startedAt, from: connection)) }
            }
            for await (index, reply) in group {
                if let reply { rows[index].reply = reply }
            }
        }
        return rows
    }

    private func reply(id: String, since: Date, from connection: Connection) async -> String? {
        let path = "/api/session/\(id)/message"
        let query = ["limit": "12", "order": "desc", "type": "assistant"]
        let messages = await get([Message].self, path, query: query, from: connection) ?? []
        for message in messages where message.type == "assistant" && Self.date(message.time.created) >= since {
            let text = message.content?.filter { $0.type == "text" }.compactMap(\.text).joined(separator: "\n")
            if let reply = AgentSession.visibleReply(text) { return reply }
        }
        return nil
    }

    private func get<T: Decodable>(_ type: T.Type, _ path: String, query: [String: String] = [:],
                                   from connection: Connection) async -> T? {
        guard var url = URLComponents(url: connection.url, resolvingAgainstBaseURL: false) else { return nil }
        url.path = path
        if !query.isEmpty { url.queryItems = query.map(URLQueryItem.init) }
        guard let target = url.url else { return nil }
        var request = URLRequest(url: target)
        let auth = Data("opencode:\(connection.password)".utf8).base64EncodedString()
        request.setValue("Basic \(auth)", forHTTPHeaderField: "Authorization")
        guard let (data, response) = try? await http.data(for: request),
              (response as? HTTPURLResponse)?.statusCode == 200 else { return nil }
        return (try? JSONDecoder().decode(Envelope<T>.self, from: data))?.data
    }

    private static func isSessionID(_ value: String) -> Bool {
        value.hasPrefix("ses_") && value.utf8.allSatisfy {
            (48...57).contains($0) || (65...90).contains($0) || (97...122).contains($0) || $0 == 95 || $0 == 45
        }
    }

    private static func date(_ milliseconds: Double) -> Date {
        Date(timeIntervalSince1970: milliseconds / 1000)
    }
}
