import IOBluetooth
import Foundation
// usage: rf ADDR CHANNEL COMMAND...  -> opens RFCOMM, sends each command line, prints replies for a few seconds
let addr = CommandLine.arguments[1]
let chan = UInt8(CommandLine.arguments[2]) ?? 1
let cmds = Array(CommandLine.arguments.dropFirst(3))
class D: NSObject, IOBluetoothRFCOMMChannelDelegate {
    var ch: IOBluetoothRFCOMMChannel?
    func rfcommChannelOpenComplete(_ rfcommChannel: IOBluetoothRFCOMMChannel!, status error: IOReturn) {
        print("open complete status=\(error)")
        if error != 0 { exit(6) }
        for c in cmds {
            var s = Array((c + "\n").utf8)
            let r = rfcommChannel.writeSync(&s, length: UInt16(s.count))
            print("sent '\(c)' rc=\(r)")
            Thread.sleep(forTimeInterval: 1.0)
        }
    }
    func rfcommChannelData(_ rfcommChannel: IOBluetoothRFCOMMChannel!, data dataPointer: UnsafeMutableRawPointer!, length dataLength: Int) {
        let d = Data(bytes: dataPointer, count: dataLength)
        print("recv: " + (String(data: d, encoding: .utf8) ?? "<\(dataLength) bytes>").trimmingCharacters(in: .whitespacesAndNewlines))
    }
    func rfcommChannelClosed(_ rfcommChannel: IOBluetoothRFCOMMChannel!) { print("closed"); exit(0) }
}
guard let dev = IOBluetoothDevice(addressString: addr) else { print("bad address"); exit(2) }
print("paired=\(dev.isPaired()) connected=\(dev.isConnected())")
let d = D()
var ch: IOBluetoothRFCOMMChannel?
let r = dev.openRFCOMMChannelAsync(&ch, withChannelID: chan, delegate: d)
print("open rc=\(r)")
RunLoop.main.run(until: Date(timeIntervalSinceNow: 20))
print("done")
exit(0)
