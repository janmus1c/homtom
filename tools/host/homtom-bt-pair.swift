import IOBluetooth
import Foundation
// usage: pair ADDR PIN
let addr = CommandLine.arguments[1]
let pinStr = CommandLine.arguments[2]
class D: NSObject, IOBluetoothDevicePairDelegate {
    func devicePairingStarted(_ sender: Any!) { print("pairing started") }
    func deviceInquiryComplete(_ sender: Any!) {}
    func devicePairingConnecting(_ sender: Any!) { print("connecting") }
    func devicePairingPINCodeRequest(_ sender: Any!) {
        print("PIN requested")
        guard let p = sender as? IOBluetoothDevicePair else { return }
        var pin = BluetoothPINCode()
        withUnsafeMutableBytes(of: &pin) { raw in
            for (i, b) in pinStr.utf8.enumerated() where i < 16 { raw[i] = b }
        }
        p.replyPINCode(pinStr.utf8.count, pinCode: &pin)
    }
    func devicePairingUserConfirmationRequest(_ sender: Any!, numericValue: BluetoothNumericValue) {
        print("user confirmation requested: \(numericValue)")
        (sender as? IOBluetoothDevicePair)?.replyUserConfirmation(true)
    }
    func devicePairingFinished(_ sender: Any!, error: IOReturn) {
        print("pairing finished, error=\(error)"); exit(error == 0 ? 0 : 4)
    }
}
guard let dev = IOBluetoothDevice(addressString: addr) else { print("bad address"); exit(2) }
print("device: \(dev.name ?? "unknown") paired=\(dev.isPaired())")
let d = D()
guard let pair = IOBluetoothDevicePair(device: dev) else { print("no pair object"); exit(3) }
pair.delegate = d
let r = pair.start()
print("start rc=\(r)")
RunLoop.main.run(until: Date(timeIntervalSinceNow: 60))
print("timeout")
exit(5)
