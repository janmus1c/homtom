// Render the 24 homtom NOR bootloader images as RGBA PNGs (plus raw .rgba) at the exact stock sizes.
// Usage: swift render-bootloader.swift <fonts-dir> <out-dir>
// Output: <out-dir>/id###_WxH.png and id###_WxH.rgba (raw 8-bit RGBA, row-major, opaque).
import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
let fontsDir = URL(fileURLWithPath: args[1])
let outDir = URL(fileURLWithPath: args[2])
try! FileManager.default.createDirectory(at: outDir, withIntermediateDirectories: true)

let deviceRGB = CGColorSpaceCreateDeviceRGB()
func colour(_ hex: UInt32) -> CGColor {
    CGColor(colorSpace: deviceRGB, components: [CGFloat(hex >> 16 & 0xFF) / 255, CGFloat(hex >> 8 & 0xFF) / 255,
                                                 CGFloat(hex & 0xFF) / 255, 1])!
}

let ink = colour(0x0E161E), bone = colour(0xF5F3EE), blue = colour(0x18BCF2), red = colour(0xE5484D)
let deepBlue = colour(0x0E7091), slate = colour(0x1B2A38), slate2 = colour(0x2A3B4C), grey = colour(0x5B6B7A)

for f in ["Poppins-SemiBold", "Poppins-Regular", "Poppins-Light"] {
    CTFontManagerRegisterFontsForURL(fontsDir.appendingPathComponent(f + ".ttf") as CFURL, .process, nil)
}
func poppins(_ name: String, _ size: CGFloat) -> CTFont { CTFontCreateWithName(name as CFString, size, nil) }

enum Align { case left, centre, right }

func text(_ ctx: CGContext, _ s: String, font: CTFont, colour c: CGColor, x: CGFloat, baseline: CGFloat,
          align: Align = .left, tracking: CGFloat = 0) {
    let attrs: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): c,
        NSAttributedString.Key(kCTKernAttributeName as String): tracking,
    ]
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
    let w = CTLineGetTypographicBounds(line, nil, nil, nil)
    var px = x
    if align == .centre { px = x - CGFloat(w) / 2 } else if align == .right { px = x - CGFloat(w) }
    ctx.saveGState()
    ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
    ctx.textPosition = CGPoint(x: px, y: baseline)
    CTLineDraw(line, ctx)
    ctx.restoreGState()
}

func textWidth(_ s: String, font: CTFont) -> CGFloat {
    let attrs: [NSAttributedString.Key: Any] = [NSAttributedString.Key(kCTFontAttributeName as String): font]
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
    return CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
}

// The homtom mark in its 100x100 box, centred on (cx, cy), k pixels per unit.
func mark(_ ctx: CGContext, cx: CGFloat, cy: CGFloat, k: CGFloat, _ c: CGColor, filled: Bool = true) {
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
    ctx.setLineWidth(2.5)
    ctx.addLines(between: [CGPoint(x: 50, y: 49.42), CGPoint(x: 54.68, y: 61.12), CGPoint(x: 50, y: 58.39),
                           CGPoint(x: 45.32, y: 61.12)])
    ctx.closePath()
    ctx.drawPath(using: .fillStroke)
    ctx.restoreGState()
}

func roundRect(_ ctx: CGContext, _ r: CGRect, _ rad: CGFloat, fill c: CGColor) {
    ctx.setFillColor(c)
    ctx.addPath(CGPath(roundedRect: r, cornerWidth: rad, cornerHeight: rad, transform: nil))
    ctx.fillPath()
}

// Flat GO 630 silhouette: body and inset screen in a 160x100 box scaled to the given width.
func device(_ ctx: CGContext, x: CGFloat, y: CGFloat, width: CGFloat) -> CGFloat {
    let k = width / 160
    ctx.saveGState()
    ctx.translateBy(x: x, y: y)
    ctx.scaleBy(x: k, y: k)
    roundRect(ctx, CGRect(x: 0, y: 0, width: 160, height: 100), 14, fill: blue)
    roundRect(ctx, CGRect(x: 9, y: 9, width: 142, height: 82), 6, fill: deepBlue)
    ctx.restoreGState()
    mark(ctx, cx: x + 80 * k, cy: y + 44.19 * k, k: k * 0.55, bone)
    return 100 * k
}

// SD card outline path: width w, height h, cut corner top-right.
func cardPath(_ r: CGRect, cut: CGFloat, radius: CGFloat) -> CGPath {
    let p = CGMutablePath()
    p.move(to: CGPoint(x: r.minX + radius, y: r.minY))
    p.addLine(to: CGPoint(x: r.maxX - cut, y: r.minY))
    p.addLine(to: CGPoint(x: r.maxX, y: r.minY + cut))
    p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - radius))
    p.addArc(tangent1End: CGPoint(x: r.maxX, y: r.maxY), tangent2End: CGPoint(x: r.maxX - radius, y: r.maxY), radius: radius)
    p.addLine(to: CGPoint(x: r.minX + radius, y: r.maxY))
    p.addArc(tangent1End: CGPoint(x: r.minX, y: r.maxY), tangent2End: CGPoint(x: r.minX, y: r.maxY - radius), radius: radius)
    p.addLine(to: CGPoint(x: r.minX, y: r.minY + radius))
    p.addArc(tangent1End: CGPoint(x: r.minX, y: r.minY), tangent2End: CGPoint(x: r.minX + radius, y: r.minY), radius: radius)
    p.closeSubpath()
    return p
}

func contacts(_ ctx: CGContext, rect r: CGRect, y: CGFloat, count: Int, w: CGFloat, h: CGFloat, _ c: CGColor) {
    let gap = (r.width - 2 * 8 - CGFloat(count) * w) / CGFloat(count - 1)
    ctx.setFillColor(c)
    for i in 0..<count {
        ctx.fill(CGRect(x: r.minX + 8 + CGFloat(i) * (w + gap), y: y, width: w, height: h))
    }
}

func cross(_ ctx: CGContext, from a: CGPoint, to b: CGPoint, width: CGFloat) {
    ctx.setStrokeColor(red)
    ctx.setLineWidth(width)
    ctx.setLineCap(.round)
    ctx.move(to: a)
    ctx.addLine(to: b)
    ctx.strokePath()
}

func render(_ id: Int, _ w: Int, _ h: Int, _ draw: (CGContext) -> Void) {
    let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                        space: CGColorSpaceCreateDeviceRGB(),
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.setFillColor(ink)
    ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
    ctx.translateBy(x: 0, y: CGFloat(h))
    ctx.scaleBy(x: 1, y: -1)
    ctx.setShouldAntialias(true)
    ctx.setShouldSmoothFonts(false)
    draw(ctx)
    let name = String(format: "id%03d_%dx%d", id, w, h)
    let dest = CGImageDestinationCreateWithURL(outDir.appendingPathComponent(name + ".png") as CFURL,
                                               UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, ctx.makeImage()!, nil)
    CGImageDestinationFinalize(dest)
    try! Data(bytes: ctx.data!, count: w * h * 4).write(to: outDir.appendingPathComponent(name + ".rgba"))
}

let semibold = "Poppins-SemiBold", regular = "Poppins-Regular"

// id 0: wordmark lockup, 237x51.
render(0, 237, 51) { c in
    let font = poppins(semibold, 31)
    let tw = textWidth("homtom", font: font) - 31 * 0.045 * 6
    let markW: CGFloat = 46, gap: CGFloat = 8
    let x0 = (237 - (markW + gap + tw)) / 2
    mark(c, cx: x0 + markW / 2, cy: 25.5, k: 0.92, blue)
    text(c, "homtom", font: font, colour: bone, x: x0 + markW + gap, baseline: 36, tracking: -0.045 * 31)
}

// id 218: device on ink with wordmark, 480x272.
render(218, 480, 272) { c in
    let dh = device(c, x: 75, y: 28, width: 330)
    text(c, "homtom", font: poppins(semibold, 20), colour: bone, x: 240, baseline: 28 + dh + 9 + 15,
         align: .centre, tracking: -0.9)
}

// id 219: device seated in a low dock with a cable, 294x262.
render(219, 294, 262) { c in
    _ = device(c, x: 37, y: 50, width: 220)
    roundRect(c, CGRect(x: 29, y: 152, width: 236, height: 30), 10, fill: slate2)
    roundRect(c, CGRect(x: 19, y: 176, width: 256, height: 34), 14, fill: slate)
    c.setFillColor(blue)
    c.fill(CGRect(x: 133, y: 190, width: 28, height: 4))
    c.setStrokeColor(blue)
    c.setLineWidth(6)
    c.setLineCap(.round)
    c.setLineJoin(.round)
    c.move(to: CGPoint(x: 275, y: 193))
    c.addLine(to: CGPoint(x: 282, y: 193))
    c.addLine(to: CGPoint(x: 282, y: 228))
    c.strokePath()
    roundRect(c, CGRect(x: 273, y: 226, width: 18, height: 20), 4, fill: blue)
}

// id 220: wait cue, 270x117.
render(220, 270, 117) { c in
    mark(c, cx: 135, cy: 40, k: 0.95, blue)
    roundRect(c, CGRect(x: 60, y: 90, width: 150, height: 8), 4, fill: slate2)
    roundRect(c, CGRect(x: 60, y: 90, width: 84, height: 8), 4, fill: blue)
}

// ids 232-237: SD card sliding in from the slot at the top, 69x94.
let cardBottoms: [CGFloat] = [8, 24, 38, 52, 66, 82]
for (i, b) in cardBottoms.enumerated() {
    render(232 + i, 69, 94) { c in
        c.saveGState()
        c.clip(to: CGRect(x: 0, y: 8, width: 69, height: 86))
        let r = CGRect(x: 13.5, y: b - 58, width: 42, height: 58)
        c.addPath(cardPath(r, cut: 10, radius: 4))
        c.setFillColor(slate)
        c.fillPath()
        c.addPath(cardPath(r.insetBy(dx: 1.25, dy: 1.25), cut: 9, radius: 3))
        c.setStrokeColor(bone)
        c.setLineWidth(2.5)
        c.setLineJoin(.round)
        c.strokePath()
        contacts(c, rect: r, y: r.minY + 8, count: 4, w: 3, h: 8, blue)
        mark(c, cx: r.midX, cy: r.minY + 37, k: 0.42, blue)
        c.restoreGState()
        roundRect(c, CGRect(x: 0, y: 0, width: 69, height: 8), 0, fill: slate2)
        c.setFillColor(blue)
        c.fill(CGRect(x: 0, y: 6, width: 69, height: 2))
    }
}

// ids 222-227: SD card icons, 76x123.
func sdIcon(_ c: CGContext, bone isBone: Bool) {
    let r = CGRect(x: 8, y: 20, width: 60, height: 83)
    c.addPath(cardPath(r, cut: 13, radius: 6))
    c.setFillColor(isBone ? bone : blue)
    c.fillPath()
    contacts(c, rect: r, y: r.minY + 9, count: 5, w: 4, h: 11, ink)
    mark(c, cx: r.midX, cy: r.minY + 54, k: 0.95, isBone ? blue : bone)
}
render(222, 76, 123) { c in sdIcon(c, bone: false) }
render(223, 76, 123) { c in
    sdIcon(c, bone: false)
    cross(c, from: CGPoint(x: 12, y: 112), to: CGPoint(x: 64, y: 10), width: 9)
}
render(224, 76, 123) { c in
    sdIcon(c, bone: false)
    cross(c, from: CGPoint(x: 12, y: 10), to: CGPoint(x: 64, y: 112), width: 9)
}
func crossed(_ c: CGContext, bone isBone: Bool) {
    sdIcon(c, bone: isBone)
    cross(c, from: CGPoint(x: 12, y: 112), to: CGPoint(x: 64, y: 10), width: 9)
    cross(c, from: CGPoint(x: 12, y: 10), to: CGPoint(x: 64, y: 112), width: 9)
}
render(225, 76, 123) { c in crossed(c, bone: false) }
render(226, 76, 123) { c in crossed(c, bone: false) }
render(227, 76, 123) { c in crossed(c, bone: true) }

// ids 228, 238: disk glyphs, 63x42.
func disk(_ c: CGContext, body: CGColor, base: CGColor) {
    roundRect(c, CGRect(x: 3, y: 3, width: 57, height: 36), 7, fill: body)
    c.saveGState()
    c.addPath(CGPath(roundedRect: CGRect(x: 3, y: 3, width: 57, height: 36), cornerWidth: 7, cornerHeight: 7, transform: nil))
    c.clip()
    c.setFillColor(base)
    c.fill(CGRect(x: 3, y: 28, width: 57, height: 11))
    c.restoreGState()
    c.setFillColor(bone)
    c.addEllipse(in: CGRect(x: 49, y: 31.5, width: 4, height: 4))
    c.fillPath()
    c.fill(CGRect(x: 10, y: 32.5, width: 18, height: 2))
}
render(228, 63, 42) { c in
    disk(c, body: blue, base: deepBlue)
    mark(c, cx: 31.5, cy: 15.5, k: 0.3, bone)
}
render(238, 63, 42) { c in
    disk(c, body: grey, base: slate2)
    text(c, "?", font: poppins(semibold, 21), colour: bone, x: 31.5, baseline: 24, align: .centre)
}

// id 239: error glyph, 90x90.
render(239, 90, 90) { c in
    c.setFillColor(red)
    c.addEllipse(in: CGRect(x: 5, y: 5, width: 80, height: 80))
    c.fillPath()
    c.setStrokeColor(bone)
    c.setLineWidth(9)
    c.setLineCap(.round)
    c.move(to: CGPoint(x: 29, y: 29)); c.addLine(to: CGPoint(x: 61, y: 61))
    c.move(to: CGPoint(x: 61, y: 29)); c.addLine(to: CGPoint(x: 29, y: 61))
    c.strokePath()
}

// ids 229-231: model labels, 134x12.
for id in 229...231 {
    render(id, 134, 12) { c in
        text(c, "homtom", font: poppins(semibold, 10), colour: bone, x: 0.5, baseline: 9.6, tracking: -0.2)
    }
}

// id 263: tiny glyph, 9x12 (the stock glyph is a "6").
render(263, 9, 12) { c in
    text(c, "6", font: poppins(semibold, 11), colour: bone, x: 4.5, baseline: 10, align: .centre)
}

// id 221: hourglass and "Please wait" in six languages, 88x79.
render(221, 88, 79) { c in
    c.setStrokeColor(blue)
    c.setFillColor(blue)
    c.setLineWidth(2.5)
    c.setLineCap(.round)
    c.setLineJoin(.round)
    // hourglass: caps, two triangles
    c.move(to: CGPoint(x: 3, y: 3)); c.addLine(to: CGPoint(x: 17, y: 3))
    c.move(to: CGPoint(x: 3, y: 27)); c.addLine(to: CGPoint(x: 17, y: 27))
    c.strokePath()
    c.addLines(between: [CGPoint(x: 4.5, y: 4.5), CGPoint(x: 15.5, y: 4.5), CGPoint(x: 10, y: 15)])
    c.closePath()
    c.strokePath()
    c.addLines(between: [CGPoint(x: 4.5, y: 25.5), CGPoint(x: 15.5, y: 25.5), CGPoint(x: 10, y: 15)])
    c.closePath()
    c.fillPath()
    let lines = ["Please wait", "Bitte warten", "Attendez svp", "Attendere per favore", "Espera por favor"]
    var size: CGFloat = 8
    let widest = lines.map { textWidth($0, font: poppins(regular, size)) }.max()!
    if widest > 85 { size = size * 85 / widest }
    let f = poppins(regular, size)
    for (i, s) in lines.enumerated() {
        text(c, s, font: f, colour: bone, x: 87, baseline: 8 + CGFloat(i) * 11, align: .right)
    }
    let cjk = CTFontCreateWithName("PingFang TC" as CFString, 13, nil)
    text(c, "請稍待", font: cjk, colour: bone, x: 87, baseline: 70, align: .right)
}
