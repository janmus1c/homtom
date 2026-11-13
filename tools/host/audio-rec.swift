import AVFoundation
import Foundation
// usage: rec SECONDS FREQ_HZ  -> prints rms_db and goertzel_db at FREQ
let secs = Double(CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "3") ?? 3
let freq = Double(CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "1000") ?? 1000
let engine = AVAudioEngine()
let input = engine.inputNode
let fmt = input.outputFormat(forBus: 0)
var samples = [Float]()
let lock = NSLock()
input.installTap(onBus: 0, bufferSize: 4096, format: fmt) { buf, _ in
    guard let ch = buf.floatChannelData?[0] else { return }
    lock.lock(); samples.append(contentsOf: UnsafeBufferPointer(start: ch, count: Int(buf.frameLength))); lock.unlock()
}
do { try engine.start() } catch { print("engine start failed: \(error)"); exit(2) }
RunLoop.main.run(until: Date(timeIntervalSinceNow: secs))
engine.stop()
lock.lock(); let s = samples; lock.unlock()
if s.isEmpty { print("no samples (permission?)"); exit(3) }
let sr = fmt.sampleRate
var sum = 0.0; var peak: Float = 0
for v in s { sum += Double(v*v); peak = max(peak, abs(v)) }
let rms = (sum / Double(s.count)).squareRoot()
let w = 2 * Double.pi * freq / sr
let coeff = 2 * cos(w)
var s1 = 0.0, s2 = 0.0
for v in s { let s0 = Double(v) + coeff*s1 - s2; s2 = s1; s1 = s0 }
let power = s1*s1 + s2*s2 - coeff*s1*s2
let mag = (power.squareRoot()) / Double(s.count) * 2
func db(_ x: Double) -> Double { 20 * log10(max(x, 1e-9)) }
print(String(format: "samples=%d sr=%.0f rms_db=%.1f peak=%.3f tone_%dHz_db=%.1f", s.count, sr, db(rms), peak, Int(freq), db(mag)))
// optional third argument "scan": strongest frequency between 0.5*FREQ and 1.5*FREQ (0.5 Hz steps, middle second of the
// recording), so a tone played at a nominal sample rate reveals the real one: real_fs = nominal_fs * found / FREQ
if CommandLine.arguments.count > 3 && CommandLine.arguments[3] == "scan" {
    let n = min(s.count, Int(sr))
    let seg = Array(s[(s.count - n) / 2 ..< (s.count - n) / 2 + n])
    var best = (f: 0.0, p: 0.0)
    var f = freq * 0.5
    while f <= freq * 1.5 {
        let c = 2 * cos(2 * Double.pi * f / sr)
        var a = 0.0, b = 0.0
        for v in seg { let t = Double(v) + c * a - b; b = a; a = t }
        let p = a * a + b * b - c * a * b
        if p > best.p { best = (f, p) }
        f += 0.5
    }
    print(String(format: "strongest=%.1fHz ratio=%.4f level_db=%.1f", best.f, best.f / freq, db(best.p.squareRoot() / Double(n) * 2)))
}
