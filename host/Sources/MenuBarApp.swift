import AppKit
import ApplicationServices

final class MenuBarApp: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let serial = SerialConnection()
    private let mouse = MouseBridge()
    private let sessions = SessionBridge()
    private var modules: [BridgeModule] = []
    private var timer: Timer?
    private var devices: [SerialDevice] = []
    private var connectionError: String?
    private var permission = false
    private var selectedPath: String? { UserDefaults.standard.string(forKey: "serialPath") }

    func applicationDidFinishLaunching(_ notification: Notification) {
        modules = [mouse, sessions]
        mouse.enabled = UserDefaults.standard.object(forKey: "mouseEnabled") as? Bool ?? true
        sessions.enabled = UserDefaults.standard.object(forKey: "codexEnabled") as? Bool ?? true
        sessions.send = { [weak self] in self?.serial.sendLatest($0) }
        serial.onLine = { [weak self] line in
            guard let self else { return }
            if let clock = clockResponse(to: line) { self.serial.sendClock(clock) }
            else { self.modules.forEach { $0.receive(line: line) } }
        }
        serial.onDisconnect = { [weak self] in
            guard let self else { return }
            self.modules.forEach { $0.reset() }
            self.connectionError = nil
            self.updateIcon()
        }
        let menu = NSMenu()
        menu.delegate = self
        statusItem.menu = menu
        statusItem.button?.image = Self.clawdIcon()
        permission = AXIsProcessTrusted()
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in self?.refresh() }
        if mouse.enabled && !permission { requestPermission() }
    }
    private static func clawdIcon() -> NSImage {
        let icon = NSImage(size: NSSize(width: 20, height: 16), flipped: true) { _ in
            let silhouette = NSBezierPath(rect: NSRect(x: 3, y: 2, width: 14, height: 9))
            silhouette.appendRect(NSRect(x: 1, y: 6, width: 2, height: 3))
            silhouette.appendRect(NSRect(x: 17, y: 6, width: 2, height: 3))
            for x in [CGFloat(4), 7, 11, 14] {
                silhouette.appendRect(NSRect(x: x, y: 11, width: 2, height: 3))
            }
            // Cut out the eyes so they follow the menu bar background.
            silhouette.windingRule = .evenOdd
            for x in [CGFloat(6), 12] {
                silhouette.appendRect(NSRect(x: x, y: 5, width: 2, height: 3))
            }
            NSColor.black.setFill()
            silhouette.fill()
            return true
        }
        icon.isTemplate = true
        icon.accessibilityDescription = "近台"
        return icon
    }
    func applicationWillTerminate(_ notification: Notification) {
        timer?.invalidate()
        modules.forEach { $0.reset() }
        serial.disconnect(notify: false)
    }
    private func refresh() {
        let trusted = AXIsProcessTrusted()
        if permission && !trusted { mouse.reset() }
        permission = trusted
        devices = SerialDevices.discover()
        if let path = serial.path, !devices.contains(where: { $0.path == path }) {
            modules.forEach { $0.reset() }
            serial.disconnect(notify: false)
        }
        if serial.path == nil {
            let target: SerialDevice?
            if let selectedPath { target = devices.first(where: { $0.path == selectedPath }) }
            else if devices.count == 1 {
                target = devices[0]
                UserDefaults.standard.set(devices[0].path, forKey: "serialPath")
            } else { target = nil }
            if let target { connect(target.path) }
        }
        updateIcon()
    }
    private func connect(_ path: String) {
        modules.forEach { $0.reset() }
        do { try serial.connect(path); connectionError = nil }
        catch { connectionError = error.localizedDescription }
    }
    private var status: String {
        if let path = serial.path {
            if mouse.enabled && !permission { return "已连接串口，鼠标需要辅助功能权限" }
            return "串口已连接：\((path as NSString).lastPathComponent)"
        }
        if !devices.isEmpty && selectedPath != nil && !devices.contains(where: { $0.path == selectedPath }) {
            return "已选设备未连接，请选择串口"
        }
        if devices.count > 1 && selectedPath == nil { return "发现多个串口，请选择设备" }
        if connectionError != nil { return "串口无法连接，正在重试" }
        return "等待设备连接"
    }
    private func updateIcon() {
        statusItem.button?.toolTip = "近台 — \(status)"
    }
    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        let heading = NSMenuItem(title: "近台", action: nil, keyEquivalent: "")
        heading.isEnabled = false
        menu.addItem(heading)
        let statusLine = NSMenuItem(title: status, action: nil, keyEquivalent: "")
        statusLine.isEnabled = false
        menu.addItem(statusLine)
        menu.addItem(.separator())
        let mouseItem = NSMenuItem(title: "启用鼠标桥接", action: #selector(toggleMouse), keyEquivalent: "")
        mouseItem.target = self
        mouseItem.state = mouse.enabled ? .on : .off
        menu.addItem(mouseItem)
        let sessionsItem = NSMenuItem(title: "启用会话监看", action: #selector(toggleSessions), keyEquivalent: "")
        sessionsItem.target = self
        sessionsItem.state = sessions.enabled ? .on : .off
        menu.addItem(sessionsItem)
        let permissionItem = NSMenuItem(title: permission ? "辅助功能权限：已授权" : "辅助功能权限：点击授权…",
                                        action: permission ? nil : #selector(openPermissionSettings), keyEquivalent: "")
        permissionItem.target = self
        permissionItem.isEnabled = !permission
        menu.addItem(permissionItem)
        let serialItem = NSMenuItem(title: "设备串口", action: nil, keyEquivalent: "")
        let submenu = NSMenu(title: "设备串口")
        if devices.isEmpty {
            let empty = NSMenuItem(title: "未发现兼容串口", action: nil, keyEquivalent: "")
            empty.isEnabled = false; submenu.addItem(empty)
        }
        for device in devices {
            let item = NSMenuItem(title: device.name, action: #selector(selectPort(_:)), keyEquivalent: "")
            item.target = self; item.representedObject = device.path
            item.state = device.path == serial.path ? .on : .off
            submenu.addItem(item)
        }
        serialItem.submenu = submenu
        menu.addItem(serialItem)
        let reconnect = NSMenuItem(title: "重新连接", action: #selector(reconnect), keyEquivalent: "")
        reconnect.target = self
        menu.addItem(reconnect)
        menu.addItem(.separator())
        let quit = NSMenuItem(title: "退出近台", action: #selector(quitApp), keyEquivalent: "q")
        quit.target = self
        menu.addItem(quit)
    }
    @objc private func toggleSessions() {
        sessions.enabled.toggle()
        sessions.reset()
        UserDefaults.standard.set(sessions.enabled, forKey: "codexEnabled")
    }
    @objc private func toggleMouse() {
        mouse.enabled.toggle()
        UserDefaults.standard.set(mouse.enabled, forKey: "mouseEnabled")
        if mouse.enabled && !permission { requestPermission() }
        updateIcon()
    }
    @objc private func selectPort(_ item: NSMenuItem) {
        guard let path = item.representedObject as? String else { return }
        UserDefaults.standard.set(path, forKey: "serialPath")
        connect(path)
        updateIcon()
    }
    @objc private func reconnect() {
        modules.forEach { $0.reset() }
        serial.disconnect(notify: false)
        connectionError = nil
        refresh()
    }
    private func requestPermission() {
        let options = ["AXTrustedCheckOptionPrompt" as CFString: true] as CFDictionary
        _ = AXIsProcessTrustedWithOptions(options)
    }
    @objc private func openPermissionSettings() {
        requestPermission()
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {
            NSWorkspace.shared.open(url)
        }
    }
    @objc private func quitApp() { NSApp.terminate(nil) }
}
