import Foundation
import Darwin

/// Nonblocking serial transport; callbacks execute on the main queue.
final class SerialConnection {
    private var fd: Int32 = -1
    private var source: DispatchSourceRead?
    private var writeRetry: DispatchSourceTimer?
    private var writing = Data()
    private var written = 0
    private var pendingWrite: Data?
    private let parser = BridgeProtocol()
    var onLine: ((String) -> Void)?
    var onDisconnect: (() -> Void)?
    private(set) var path: String?

    init() { parser.onLine = { [weak self] line in self?.onLine?(line) } }
    func connect(_ path: String) throws {
        disconnect(notify: false)
        let handle = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard handle >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        var settings = termios()
        guard tcgetattr(handle, &settings) == 0 else {
            let code = errno; close(handle); throw POSIXError(POSIXErrorCode(rawValue: code) ?? .EIO)
        }
        cfmakeraw(&settings)
        settings.c_cflag |= tcflag_t(CLOCAL | CREAD)
        settings.c_cflag &= ~tcflag_t(CSTOPB | CRTSCTS)
        withUnsafeMutableBytes(of: &settings.c_cc) { cc in cc[Int(VMIN)] = 1; cc[Int(VTIME)] = 0 }
        cfsetspeed(&settings, speed_t(B115200))
        guard tcsetattr(handle, TCSANOW, &settings) == 0 else {
            let code = errno; close(handle); throw POSIXError(POSIXErrorCode(rawValue: code) ?? .EIO)
        }
        fd = handle; self.path = path
        let readSource = DispatchSource.makeReadSource(fileDescriptor: handle, queue: .main)
        readSource.setEventHandler { [weak self] in
            if self?.fd == handle { self?.readAvailable() }
        }
        readSource.setCancelHandler { close(handle) }
        source = readSource
        readSource.resume()
    }
    private func readAvailable() {
        var bytes = [UInt8](repeating: 0, count: 1024)
        while fd >= 0 {
            let count = read(fd, &bytes, bytes.count)
            if count > 0 { parser.receive(Data(bytes.prefix(count))); continue }
            if count < 0 && errno == EINTR { continue }
            if count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) { return }
            disconnect()
            return
        }
    }
    /// Keep the in-flight snapshot intact and replace any queued snapshot with the latest one.
    func sendLatest(_ data: Data) {
        guard fd >= 0, !data.isEmpty else { return }
        pendingWrite = data
        writeAvailable()
    }

    private func retryWrite() {
        guard writeRetry == nil else { return }
        let handle = fd
        let timer = DispatchSource.makeTimerSource(queue: .main)
        timer.schedule(deadline: .now() + .milliseconds(10), repeating: .milliseconds(10))
        timer.setEventHandler { [weak self] in
            if self?.fd == handle { self?.writeAvailable() }
        }
        writeRetry = timer
        timer.resume()
    }

    private func writeAvailable() {
        while fd >= 0 {
            if written == writing.count {
                guard let next = pendingWrite else {
                    writeRetry?.cancel(); writeRetry = nil
                    writing = Data(); written = 0
                    return
                }
                writing = next
                written = 0
                pendingWrite = nil
            }
            let count = writing.withUnsafeBytes { bytes in
                write(fd, bytes.baseAddress?.advanced(by: written), bytes.count - written)
            }
            if count > 0 { written += count; continue }
            if count < 0 && errno == EINTR { continue }
            if count == 0 || errno == EAGAIN || errno == EWOULDBLOCK {
                retryWrite()
                return
            }
            disconnect()
            return
        }
    }

    func disconnect(notify: Bool = true) {
        let wasConnected = fd >= 0
        fd = -1; path = nil
        writeRetry?.cancel(); writeRetry = nil
        writing = Data(); written = 0; pendingWrite = nil
        source?.cancel(); source = nil
        parser.reset()
        if notify && wasConnected { onDisconnect?() }
    }
    deinit { disconnect(notify: false) }
}
