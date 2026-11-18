import Foundation
import Darwin

/// Nonblocking serial transport; callbacks execute on the main queue.
final class SerialConnection {
    private var fd: Int32 = -1
    private var source: DispatchSourceRead?
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
        withUnsafeMutableBytes(of: &settings.c_cc) { cc in cc[Int(VMIN)] = 1; cc[Int(VTIME)] = 0 }
        cfsetspeed(&settings, speed_t(B115200))
        guard tcsetattr(handle, TCSANOW, &settings) == 0 else {
            let code = errno; close(handle); throw POSIXError(POSIXErrorCode(rawValue: code) ?? .EIO)
        }
        fd = handle; self.path = path
        let readSource = DispatchSource.makeReadSource(fileDescriptor: handle, queue: .main)
        readSource.setEventHandler { [weak self] in self?.readAvailable() }
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
    func disconnect(notify: Bool = true) {
        let wasConnected = fd >= 0
        fd = -1; path = nil
        source?.cancel(); source = nil
        parser.reset()
        if notify && wasConnected { onDisconnect?() }
    }
    deinit { disconnect(notify: false) }
}
