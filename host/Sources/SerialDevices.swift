import Foundation
import IOKit
import IOKit.serial

struct SerialDevice: Equatable {
    let path: String
    var name: String { (path as NSString).lastPathComponent }
}

enum SerialDevices {
    /// Match the CH343 USB-UART family used by the verified board. When several
    /// exist, the user selects one: this VID/PID does not identify firmware.
    static func discover() -> [SerialDevice] {
        guard let matching = IOServiceMatching(kIOSerialBSDServiceValue) else { return [] }
        var iterator: io_iterator_t = 0
        guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == KERN_SUCCESS else { return [] }
        defer { IOObjectRelease(iterator) }
        var devices: [SerialDevice] = []
        while case let entry = IOIteratorNext(iterator), entry != 0 {
            defer { IOObjectRelease(entry) }
            let options = IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents)
            func property(_ key: String) -> AnyObject? {
                IORegistryEntrySearchCFProperty(entry, kIOServicePlane, key as CFString, kCFAllocatorDefault, options)
            }
            guard let path = property(kIOCalloutDeviceKey) as? String,
                  let vendor = property("idVendor") as? NSNumber,
                  let product = property("idProduct") as? NSNumber,
                  vendor.intValue == 0x1a86, product.intValue == 0x55d3 else { continue }
            devices.append(SerialDevice(path: path))
        }
        return devices.sorted { $0.path < $1.path }
    }
}
