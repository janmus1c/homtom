// Render homtom replacements for the group-16 storage-mode images of the GO 630 bootloader.
// Every overlay is drawn on a full 480x272 composite of the layers it is drawn over on the device,
// then cropped at the fixed position the scene table gives it, so the opaque rectangles join seamlessly.
// Usage: swift render-art.swift <fonts-dir> <out-dir>
// Output: <out-dir>/id###_WxH.rgba (raw RGBA, opaque, row-major) at the stock size of each id.
import CoreGraphics
import CoreText
import Foundation

let args = CommandLine.arguments
let fontsDir = URL(fileURLWithPath: args[1])
let outDir = URL(fileURLWithPath: args[2])
try! FileManager.default.createDirectory(at: outDir, withIntermediateDirectories: true)

let rgb = CGColorSpaceCreateDeviceRGB()
func colour(_ hex: UInt32) -> CGColor {
    CGColor(colorSpace: rgb, components: [CGFloat(hex >> 16 & 0xFF) / 255, CGFloat(hex >> 8 & 0xFF) / 255,
                                           CGFloat(hex & 0xFF) / 255, 1])!
}

let ink = colour(0x0E161E), bone = colour(0xF5F3EE), blue = colour(0x18BCF2)
let panel = colour(0x131D26), slate = colour(0x22323F), mute = colour(0x8E9AA6), coral = colour(0xE5484D)

for f in ["Poppins-SemiBold", "Poppins-Regular", "Poppins-Light"] {
    CTFontManagerRegisterFontsForURL(fontsDir.appendingPathComponent(f + ".ttf") as CFURL, .process, nil)
}
func poppins(_ name: String, _ size: CGFloat) -> CTFont { CTFontCreateWithName(name as CFString, size, nil) }
let semibold = "Poppins-SemiBold", regular = "Poppins-Regular"

enum Align { case left, centre, right }

func line(_ s: String, _ font: CTFont, _ c: CGColor, tracking: CGFloat) -> CTLine {
    let attrs: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): c,
        NSAttributedString.Key(kCTKernAttributeName as String): tracking,
    ]
    return CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
}

func text(_ ctx: CGContext, _ s: String, font: CTFont, colour c: CGColor, x: CGFloat, baseline: CGFloat,
          align: Align = .left, tracking: CGFloat = 0) {
    let l = line(s, font, c, tracking: tracking)
    let w = CGFloat(CTLineGetTypographicBounds(l, nil, nil, nil))
    var px = x
    if align == .centre { px = x - w / 2 } else if align == .right { px = x - w }
    ctx.saveGState()
    ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
    ctx.textPosition = CGPoint(x: px, y: baseline)
    CTLineDraw(l, ctx)
    ctx.restoreGState()
}

// The homtom mark (brand/homtom-mark.svg geometry), centred on (cx, cy), k pixels per unit.
func mark(_ ctx: CGContext, cx: CGFloat, cy: CGFloat, k: CGFloat, _ c: CGColor) {
    ctx.saveGState()
    ctx.translateBy(x: cx, y: cy)
    ctx.scaleBy(x: k, y: k)
    ctx.translateBy(x: -50, y: -50)
    ctx.setStrokeColor(c)
    ctx.setFillColor(c)
    ctx.setLineCap(.round)
    ctx.setLineJoin(.round)
    ctx.setLineWidth(4.5)
    ctx.addLines(between: [CGPoint(x: 27, y: 48), CGPoint(x: 50, y: 28), CGPoint(x: 73, y: 48),
                           CGPoint(x: 73, y: 72), CGPoint(x: 27, y: 72)])
    ctx.closePath()
    ctx.strokePath()
    ctx.setLineWidth(3)
    ctx.addLines(between: [CGPoint(x: 50, y: 47), CGPoint(x: 56, y: 62), CGPoint(x: 50, y: 58.5), CGPoint(x: 44, y: 62)])
    ctx.closePath()
    ctx.drawPath(using: .fillStroke)
    ctx.restoreGState()
}

func rrect(_ r: CGRect, _ rad: CGFloat) -> CGPath { CGPath(roundedRect: r, cornerWidth: rad, cornerHeight: rad, transform: nil) }
func fill(_ c: CGContext, _ p: CGPath, _ col: CGColor) { c.addPath(p); c.setFillColor(col); c.fillPath() }
func stroke(_ c: CGContext, _ p: CGPath, _ col: CGColor, _ w: CGFloat) {
    c.addPath(p); c.setStrokeColor(col); c.setLineWidth(w); c.setLineCap(.round); c.setLineJoin(.round); c.strokePath()
}
func seg(_ c: CGContext, _ a: CGPoint, _ b: CGPoint, _ col: CGColor, _ w: CGFloat) {
    let p = CGMutablePath(); p.move(to: a); p.addLine(to: b); stroke(c, p, col, w)
}

// SD card outline with the cut corner at top right.
func cardPath(_ r: CGRect, cut: CGFloat, radius: CGFloat) -> CGPath {
    let p = CGMutablePath()
    p.move(to: CGPoint(x: r.minX + radius, y: r.minY))
    p.addLine(to: CGPoint(x: r.maxX - cut, y: r.minY))
    p.addLine(to: CGPoint(x: r.maxX, y: r.minY + cut))
    p.addArc(tangent1End: CGPoint(x: r.maxX, y: r.maxY), tangent2End: CGPoint(x: r.minX, y: r.maxY), radius: radius)
    p.addArc(tangent1End: CGPoint(x: r.minX, y: r.maxY), tangent2End: CGPoint(x: r.minX, y: r.minY), radius: radius)
    p.addArc(tangent1End: CGPoint(x: r.minX, y: r.minY), tangent2End: CGPoint(x: r.maxX, y: r.minY), radius: radius)
    p.closeSubpath()
    return p
}

func sdCard(_ c: CGContext, _ r: CGRect, scale s: CGFloat) {
    let p = cardPath(r, cut: 12 * s, radius: 5 * s)
    fill(c, p, slate)
    stroke(c, p, bone, 2.5 * s)
    for i in 0..<4 {
        c.setFillColor(blue)
        c.fill(CGRect(x: r.minX + (9 + CGFloat(i) * 8) * s, y: r.minY + 8 * s, width: 3.5 * s, height: 9 * s))
    }
    mark(c, cx: r.midX, cy: r.minY + r.height * 0.62, k: 0.62 * s, blue)
}

// Layer drawing, in screen coordinates. Each opaque image paints its own background first.
let panelRect = CGRect(x: 296, y: 30, width: 174, height: 236)

func layer218(_ c: CGContext) {
    c.setFillColor(ink); c.fill(CGRect(x: 0, y: 0, width: 480, height: 272))
    mark(c, cx: 384, cy: 150, k: 3.1, panel)
    mark(c, cx: 112, cy: 112, k: 1.55, blue)
    text(c, "homtom", font: poppins(semibold, 30), colour: bone, x: 112, baseline: 200, align: .centre, tracking: -1.35)
}

func layer219(_ c: CGContext) {
    c.setFillColor(ink); c.fill(CGRect(x: 0, y: 0, width: 294, height: 262))
    text(c, "Storage mode", font: poppins(semibold, 23), colour: bone, x: 26, baseline: 48, tracking: -0.6)
    text(c, "Connected to a computer", font: poppins(regular, 12), colour: mute, x: 26, baseline: 70)
    // Computer: monitor with the mark on its screen, on a stand.
    let mon = CGRect(x: 26, y: 120, width: 124, height: 80)
    fill(c, rrect(mon, 8), slate)
    stroke(c, rrect(mon, 8), bone, 3)
    mark(c, cx: mon.midX, cy: mon.midY + 1, k: 0.78, blue)
    seg(c, CGPoint(x: mon.midX, y: 201.5), CGPoint(x: mon.midX, y: 212), bone, 3)
    seg(c, CGPoint(x: mon.midX - 20, y: 213), CGPoint(x: mon.midX + 20, y: 213), bone, 3)
    // Cable from the computer to the drive.
    let cable = CGMutablePath()
    cable.move(to: CGPoint(x: 151.5, y: 186)); cable.addLine(to: CGPoint(x: 172, y: 186))
    cable.addQuadCurve(to: CGPoint(x: 184, y: 174), control: CGPoint(x: 184, y: 186))
    stroke(c, cable, blue, 3)
    // Drive: the unit seen as a disk, with the SD-style cut corner.
    let drv = CGRect(x: 184, y: 132, width: 82, height: 58)
    let dp = cardPath(drv, cut: 12, radius: 8)
    fill(c, dp, slate)
    stroke(c, dp, bone, 3)
    c.setFillColor(blue)
    c.addEllipse(in: CGRect(x: drv.maxX - 18, y: drv.maxY - 17, width: 7, height: 7)); c.fillPath()
    for i in 0..<3 {
        c.setFillColor(mute)
        c.fill(CGRect(x: drv.minX + 12, y: drv.minY + 16 + CGFloat(i) * 9, width: 34 - CGFloat(i) * 8, height: 3))
    }
    text(c, "Eject the drive on the computer", font: poppins(regular, 11.5), colour: mute, x: 26, baseline: 238)
    text(c, "to start homtom.", font: poppins(regular, 11.5), colour: mute, x: 26, baseline: 254)
}

func layer220(_ c: CGContext) {
    c.setFillColor(ink); c.fill(CGRect(x: 0, y: 0, width: 270, height: 117))
    text(c, "Copying data", font: poppins(semibold, 23), colour: bone, x: 26, baseline: 48, tracking: -0.6)
    c.setFillColor(coral)
    c.addEllipse(in: CGRect(x: 26, y: 61, width: 8, height: 8)); c.fillPath()
    text(c, "Do not disconnect", font: poppins(regular, 12), colour: coral, x: 40, baseline: 70)
}

func layer221(_ c: CGContext) {
    let cx: CGFloat = 376, cy: CGFloat = 96
    c.setStrokeColor(slate); c.setLineWidth(5)
    c.addEllipse(in: CGRect(x: cx - 20, y: cy - 20, width: 40, height: 40)); c.strokePath()
    let arc = CGMutablePath()
    arc.addArc(center: CGPoint(x: cx, y: cy), radius: 20, startAngle: -.pi / 2, endAngle: .pi * 0.9, clockwise: false)
    stroke(c, arc, blue, 5)
    text(c, "Please wait", font: poppins(regular, 11), colour: bone, x: cx, baseline: 138, align: .centre)
}

func driveBadge(_ c: CGContext, x: CGFloat, label: String, sd: Bool) {
    let cx = x + 31.5
    if sd {
        let r = CGRect(x: cx - 10, y: 227, width: 20, height: 25)
        let p = cardPath(r, cut: 6, radius: 2.5)
        fill(c, p, blue)
        for i in 0..<3 { c.setFillColor(ink); c.fill(CGRect(x: r.minX + 4 + CGFloat(i) * 4.5, y: r.minY + 4, width: 2, height: 5)) }
    } else {
        let r = CGRect(x: cx - 15, y: 230, width: 30, height: 20)
        fill(c, rrect(r, 4), blue)
        c.setFillColor(ink); c.fill(CGRect(x: r.minX + 5, y: r.maxY - 6, width: 12, height: 2.5))
        c.addEllipse(in: CGRect(x: r.maxX - 9, y: r.maxY - 7.5, width: 4.5, height: 4.5)); c.fillPath()
    }
    text(c, label, font: poppins(regular, 9.5), colour: bone, x: cx, baseline: 262, align: .centre)
}

func layer239(_ c: CGContext) {
    let r = CGRect(x: 311, y: 101, width: 68, height: 68)
    c.setFillColor(coral); c.addEllipse(in: r); c.fillPath()
    seg(c, CGPoint(x: r.minX + 23, y: r.minY + 23), CGPoint(x: r.maxX - 23, y: r.maxY - 23), bone, 6)
    seg(c, CGPoint(x: r.maxX - 23, y: r.minY + 23), CGPoint(x: r.minX + 23, y: r.maxY - 23), bone, 6)
}

// SD icon at 300,70 (76x123): card with a coral strike that builds up over ids 223-227.
func layerSD(_ c: CGContext, step: Int) {
    let r = CGRect(x: 310, y: 88, width: 56, height: 76)
    sdCard(c, r, scale: 1.25)
    let a1 = CGPoint(x: 306, y: 182), b1 = CGPoint(x: 370, y: 78)
    let a2 = CGPoint(x: 306, y: 78), b2 = CGPoint(x: 370, y: 182)
    func part(_ a: CGPoint, _ b: CGPoint, _ t: CGFloat) {
        guard t > 0 else { return }
        seg(c, a, CGPoint(x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t), coral, 7)
    }
    let t: [(CGFloat, CGFloat)] = [(0, 0), (0.45, 0), (1, 0), (1, 0.45), (1, 0.8), (1, 1)]
    part(a1, b1, t[step].0)
    part(a2, b2, t[step].1)
}

// SD animation at 223,158 (69x94): the card slides down out of a slot at the top, six frames.
func layerAnim(_ c: CGContext, frame: Int) {
    let ox: CGFloat = 223, oy: CGFloat = 158
    c.saveGState()
    c.clip(to: CGRect(x: ox, y: oy + 10, width: 69, height: 84))
    let shown: [CGFloat] = [6, 20, 34, 48, 62, 76]
    let r = CGRect(x: ox + 13.5, y: oy + 10 + shown[frame] - 66, width: 42, height: 60)
    sdCard(c, r, scale: 0.9)
    c.restoreGState()
    fill(c, rrect(CGRect(x: ox + 4, y: oy + 4, width: 61, height: 6), 3), slate)
    c.setFillColor(blue); c.fill(CGRect(x: ox + 14, y: oy + 8, width: 41, height: 2))
}

// Model label at 337,8 (ids 229-231, 134x12) with the id 263 patch at 443,8 (9x12) cut from the same composite.
func layerLabel(_ c: CGContext) {
    text(c, "GO 630", font: poppins(regular, 9.5), colour: mute, x: 470, baseline: 18, align: .right)
    mark(c, cx: 424, cy: 13, k: 0.24, blue)
}

let W = 480, H = 272
func composite(_ draw: (CGContext) -> Void) -> CGContext {
    let ctx = CGContext(data: nil, width: W, height: H, bitsPerComponent: 8, bytesPerRow: W * 4, space: rgb,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.translateBy(x: 0, y: CGFloat(H))
    ctx.scaleBy(x: 1, y: -1)
    ctx.setShouldAntialias(true)
    ctx.setShouldSmoothFonts(false)
    draw(ctx)
    return ctx
}

func save(_ ctx: CGContext, id: Int, x: Int, y: Int, w: Int, h: Int) {
    let src = ctx.data!.assumingMemoryBound(to: UInt8.self)
    var out = Data(capacity: w * h * 4)
    for row in y..<(y + h) {
        out.append(UnsafeBufferPointer(start: src + (row * W + x) * 4, count: w * 4))
    }
    try! out.write(to: outDir.appendingPathComponent(String(format: "id%03d_%dx%d.rgba", id, w, h)))
}

save(composite { layer218($0) }, id: 218, x: 0, y: 0, w: 480, h: 272)
let c219 = composite { layer218($0); layer219($0) }
save(c219, id: 219, x: 0, y: 0, w: 294, h: 262)
save(composite { layer218($0); layer219($0); layer220($0) }, id: 220, x: 0, y: 0, w: 270, h: 117)
save(composite { layer218($0); layer221($0) }, id: 221, x: 332, y: 67, w: 88, h: 79)
save(composite { layer218($0); driveBadge($0, x: 301, label: "Internal", sd: false) }, id: 228, x: 301, y: 222, w: 63, h: 42)
save(composite { layer218($0); driveBadge($0, x: 365, label: "SD card", sd: true) }, id: 238, x: 365, y: 222, w: 63, h: 42)
save(composite { layer218($0); layer239($0) }, id: 239, x: 300, y: 90, w: 90, h: 90)
for s in 0..<6 { save(composite { layer218($0); layerSD($0, step: s) }, id: 222 + s, x: 300, y: 70, w: 76, h: 123) }
for f in 0..<6 { save(composite { layer218($0); layerAnim($0, frame: f) }, id: 232 + f, x: 223, y: 158, w: 69, h: 94) }
let lab = composite { layer218($0); layerLabel($0) }
for id in 229...231 { save(lab, id: id, x: 337, y: 8, w: 134, h: 12) }
save(lab, id: 263, x: 443, y: 8, w: 9, h: 12)
