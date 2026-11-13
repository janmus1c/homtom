// Assemble review PNGs from storagegfx.py output: one PNG per storage-mode state (stock frames left,
// homtom frames right), a contact sheet of all states, and a sheet of every image id.
// Usage: swift sheets.swift <fonts-dir> <storagegfx-out-dir> <dest-dir>
import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

let a = CommandLine.arguments
let fonts = URL(fileURLWithPath: a[1]), src = URL(fileURLWithPath: a[2]), dest = URL(fileURLWithPath: a[3])
try! FileManager.default.createDirectory(at: dest, withIntermediateDirectories: true)
for f in ["Poppins-SemiBold", "Poppins-Regular"] {
    CTFontManagerRegisterFontsForURL(fonts.appendingPathComponent(f + ".ttf") as CFURL, .process, nil)
}
let rgb = CGColorSpaceCreateDeviceRGB()
func col(_ h: UInt32) -> CGColor {
    CGColor(colorSpace: rgb, components: [CGFloat(h >> 16 & 255) / 255, CGFloat(h >> 8 & 255) / 255, CGFloat(h & 255) / 255, 1])!
}
let paper = col(0xF5F3EE), ink = col(0x0E161E), mute = col(0x5B6B7A)

func font(_ n: String, _ s: CGFloat) -> CTFont { CTFontCreateWithName(n as CFString, s, nil) }
func text(_ c: CGContext, _ s: String, _ f: CTFont, _ colour: CGColor, _ x: CGFloat, _ y: CGFloat) {
    let attrs: [NSAttributedString.Key: Any] = [NSAttributedString.Key(kCTFontAttributeName as String): f,
                                                NSAttributedString.Key(kCTForegroundColorAttributeName as String): colour]
    let l = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
    c.saveGState(); c.textMatrix = CGAffineTransform(scaleX: 1, y: -1); c.textPosition = CGPoint(x: x, y: y)
    CTLineDraw(l, c); c.restoreGState()
}

func image(_ file: URL, _ w: Int, _ h: Int) -> CGImage {
    let d = try! Data(contentsOf: file)
    let p = CGDataProvider(data: d as CFData)!
    return CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 24, bytesPerRow: w * 3, space: rgb,
                   bitmapInfo: CGBitmapInfo(rawValue: 0), provider: p, decode: nil, shouldInterpolate: false,
                   intent: .defaultIntent)!
}

func canvas(_ w: Int, _ h: Int) -> CGContext {
    let c = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4, space: rgb,
                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    c.setFillColor(paper); c.fill(CGRect(x: 0, y: 0, width: w, height: h))
    c.translateBy(x: 0, y: CGFloat(h)); c.scaleBy(x: 1, y: -1)
    c.interpolationQuality = .none
    return c
}

func put(_ c: CGContext, _ img: CGImage, _ r: CGRect) {
    c.saveGState(); c.translateBy(x: r.minX, y: r.maxY); c.scaleBy(x: 1, y: -1)
    c.draw(img, in: CGRect(x: 0, y: 0, width: r.width, height: r.height)); c.restoreGState()
}

func save(_ c: CGContext, _ name: String) {
    let d = CGImageDestinationCreateWithURL(dest.appendingPathComponent(name) as CFURL, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(d, c.makeImage()!, nil); CGImageDestinationFinalize(d)
}

struct Frame: Decodable { let orig: String; let new: String; let delay: Int }
struct State: Decodable { let key: Int; let mode: String; let slug: String; let scene: Int; let meaning: String; let grade: String
    let ids: [Int]; let loop: Bool; let frames: [Frame] }
let states = try! JSONDecoder().decode([State].self, from: Data(contentsOf: src.appendingPathComponent("frames.json")))
let fdir = src.appendingPathComponent("frames")

func header(_ c: CGContext, _ s: State, _ x: CGFloat, _ y: CGFloat) {
    text(c, "\(s.mode)  \(s.slug)", font("Poppins-SemiBold", 18), ink, x, y)
    text(c, "scene \(s.scene), group 16 · ids \(s.ids.map(String.init).joined(separator: " ")) · \(s.loop ? "loops" : "ends") · evidence: \(s.grade)",
         font("Poppins-Regular", 11), mute, x, y + 18)
    text(c, s.meaning, font("Poppins-Regular", 12), ink, x, y + 36)
}

// One PNG per state: every frame, stock on the left, homtom on the right.
for s in states {
    let n = s.frames.count
    let scale: CGFloat = n > 3 ? 0.5 : 1
    let fw = 480 * scale, fh = 272 * scale, gap: CGFloat = 16
    let cols = n > 3 ? 2 : 1
    let rows = (n + cols - 1) / cols
    let pairW = fw * 2 + gap
    let W = Int(24 * 2 + CGFloat(cols) * pairW + CGFloat(cols - 1) * 32)
    let H = Int(92 + CGFloat(rows) * (fh + 30) + 10)
    let c = canvas(max(W, 700), H)
    header(c, s, 24, 30)
    for (k, f) in s.frames.enumerated() {
        let x = 24 + CGFloat(k % cols) * (pairW + 32), y = 92 + CGFloat(k / cols) * (fh + 30)
        text(c, "frame \(k + 1)\(f.delay > 0 ? "  (\(f.delay) ticks)" : "")   stock", font("Poppins-Regular", 10), mute, x, y + 10)
        text(c, "homtom", font("Poppins-Regular", 10), mute, x + fw + gap, y + 10)
        put(c, image(fdir.appendingPathComponent(f.orig), 480, 272), CGRect(x: x, y: y + 16, width: fw, height: fh))
        put(c, image(fdir.appendingPathComponent(f.new), 480, 272), CGRect(x: x + fw + gap, y: y + 16, width: fw, height: fh))
    }
    save(c, "\(s.mode.dropFirst(2).lowercased())-\(s.slug).png")
}

// Contact sheet: last frame of each state, stock and homtom, half size.
do {
    let fw: CGFloat = 240, fh: CGFloat = 136, rowH: CGFloat = fh + 28
    let H = Int(70 + CGFloat(states.count) * rowH)
    let c = canvas(24 + 360 + 24 + Int(fw) * 2 + 12 + 24, H)
    text(c, "homtom storage-mode screens: stock (left) and homtom (right)", font("Poppins-SemiBold", 18), ink, 24, 34)
    text(c, "Rendered from the NOR image table and the bootloader scene table (group 16). Key frame of each state (most content).",
         font("Poppins-Regular", 11), mute, 24, 54)
    for (i, s) in states.enumerated() {
        let y = 70 + CGFloat(i) * rowH
        text(c, "\(s.mode)  \(s.slug)", font("Poppins-SemiBold", 13), ink, 24, y + 18)
        text(c, "scene \(s.scene) · \(s.grade)", font("Poppins-Regular", 10), mute, 24, y + 34)
        var words = s.meaning.split(separator: " ").map(String.init), lines: [String] = [], cur = ""
        while !words.isEmpty { let w = words.removeFirst(); if (cur + " " + w).count > 52 { lines.append(cur); cur = w } else { cur = cur.isEmpty ? w : cur + " " + w } }
        lines.append(cur)
        for (k, l) in lines.enumerated() { text(c, l, font("Poppins-Regular", 10.5), ink, 24, y + 52 + CGFloat(k) * 14) }
        let f = s.frames[s.key]
        put(c, image(fdir.appendingPathComponent(f.orig), 480, 272), CGRect(x: 408, y: y + 6, width: fw, height: fh))
        put(c, image(fdir.appendingPathComponent(f.new), 480, 272), CGRect(x: 408 + fw + 12, y: y + 6, width: fw, height: fh))
    }
    save(c, "contact-sheet.png")
}

// Every replaced image id at 1:1, stock and homtom.
struct Img: Decodable { let id: Int; let w: Int; let h: Int; let rle: Int; let stock_rle: Int }
let imgs = try! JSONDecoder().decode([Img].self, from: Data(contentsOf: src.appendingPathComponent("images.json")))
do {
    var y: CGFloat = 70, rows: [(Img, CGFloat)] = []
    for m in imgs { rows.append((m, y)); y += CGFloat(m.h) + 40 }
    let c = canvas(24 + 480 * 2 + 40 + 24, Int(y))
    text(c, "Image ids: stock (left) and homtom (right), 1:1", font("Poppins-SemiBold", 18), ink, 24, 34)
    for (m, y) in rows {
        text(c, "id \(m.id)  \(m.w)x\(m.h)  RLE \(m.stock_rle) -> \(m.rle) bytes", font("Poppins-Regular", 11), mute, 24, y + 12)
        let dir = src.appendingPathComponent("images")
        put(c, image(dir.appendingPathComponent(String(format: "id%03d-orig.rgb", m.id)), m.w, m.h), CGRect(x: 24, y: y + 20, width: CGFloat(m.w), height: CGFloat(m.h)))
        put(c, image(dir.appendingPathComponent(String(format: "id%03d-new.rgb", m.id)), m.w, m.h), CGRect(x: 24 + 520, y: y + 20, width: CGFloat(m.w), height: CGFloat(m.h)))
    }
    save(c, "images-sheet.png")
}
