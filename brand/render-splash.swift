// Render a homtom boot splash: centred mark above the wordmark, written as PNG and raw RGB565.
// Usage: swift render-splash.swift <font.ttf> <tile-hex> <out-basename> [variant] [icon.svg]
// Variants: no-tile (default), tile-small-radius, tile-large-radius, house-shape-bg, circle, tile-ring, device
import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
let fontURL = URL(fileURLWithPath: args[1])
let tileHex = args[2]
let out = args[3]
let variant = args.count > 4 ? args[4] : "no-tile"
let svgOut = args.count > 5 ? args[5] : nil
let width = 480, height = 272

func colour(_ hex: String) -> CGColor {
    let v = UInt32(hex.dropFirst(hex.hasPrefix("#") ? 1 : 0), radix: 16)!
    return CGColor(red: CGFloat(v >> 16 & 0xFF) / 255, green: CGFloat(v >> 8 & 0xFF) / 255,
                   blue: CGFloat(v & 0xFF) / 255, alpha: 1)
}

let ink = colour("0E161E"), bone = colour("F5F3EE")
let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                    space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.setFillColor(ink)
ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
// Work in top-left coordinates like the SVG.
ctx.translateBy(x: 0, y: CGFloat(height))
ctx.scaleBy(x: 1, y: -1)

struct Treatment {
    var shape: CGPath?
    var svgShape = ""
    var filled = true
    var ringWidth: CGFloat = 0
    var markBlue = false
    var markScale: CGFloat = 1
    var device = false
}

func num(_ v: CGFloat) -> String { String(format: "%g", (Double(v) * 100).rounded() / 100) }

func houseShape(scale s: CGFloat) -> (CGPath, String) {
    let pts = [(27, 48), (50, 28), (73, 48), (73, 72), (27, 72)].map {
        CGPoint(x: 50 + (CGFloat($0.0) - 50) * s, y: 50 + (CGFloat($0.1) - 50) * s)
    }
    let path = CGMutablePath()
    path.addLines(between: pts)
    path.closeSubpath()
    return (path, "M" + pts.map { "\(num($0.x)) \(num($0.y))" }.joined(separator: "L") + "Z")
}

// Navigation arrow, shrunk about its own centre so it sits smaller inside the house.
let arrowScale: CGFloat = 0.78
let arrowPts = [(50, 47), (56, 62), (50, 58.5), (44, 62)].map {
    CGPoint(x: 50 + (CGFloat($0.0) - 50) * arrowScale, y: 58 + (CGFloat($0.1) - 58) * arrowScale)
}
let arrowStroke: CGFloat = 2.5

// Flat GO 630 silhouette in a 160x100 box: body, then the inset screen.
let deviceBody = CGRect(x: 0, y: 0, width: 160, height: 100)
let deviceScreen = CGRect(x: 9, y: 9, width: 142, height: 82)

func treatment(_ name: String) -> Treatment {
    func rect(_ r: CGFloat) -> Treatment {
        Treatment(shape: CGPath(roundedRect: CGRect(x: 0, y: 0, width: 100, height: 100),
                                cornerWidth: r, cornerHeight: r, transform: nil),
                  svgShape: "<rect width=\"100\" height=\"100\" rx=\"\(num(r))\" fill=\"#\(tileHex)\"/>")
    }
    switch name {
    case "no-tile":
        return Treatment(markBlue: true, markScale: 1.5)
    case "tile-small-radius":
        return rect(6)
    case "tile-large-radius":
        return rect(24)
    case "house-shape-bg":
        let (path, d) = houseShape(scale: 1.45)
        return Treatment(shape: path, svgShape: "<path d=\"\(d)\" fill=\"#\(tileHex)\" stroke=\"#\(tileHex)\" stroke-width=\"12\" stroke-linejoin=\"round\"/>",
                         ringWidth: 12)
    case "circle":
        return Treatment(shape: CGPath(ellipseIn: CGRect(x: 4, y: 4, width: 92, height: 92), transform: nil),
                         svgShape: "<circle cx=\"50\" cy=\"50\" r=\"46\" fill=\"#\(tileHex)\"/>")
    case "tile-ring":
        return Treatment(shape: CGPath(roundedRect: CGRect(x: 3, y: 3, width: 94, height: 94),
                                       cornerWidth: 22, cornerHeight: 22, transform: nil),
                         svgShape: "<rect x=\"3\" y=\"3\" width=\"94\" height=\"94\" rx=\"22\" fill=\"none\" stroke=\"#\(tileHex)\" stroke-width=\"6\"/>",
                         filled: false, ringWidth: 6)
    case "device":
        return Treatment(markScale: 0.55, device: true)
    default:
        fatalError("unknown variant \(name)")
    }
}

let t = treatment(variant)
let accent = colour(tileHex)

CTFontManagerRegisterFontsForURL(fontURL as CFURL, .process, nil)
let descriptors = CTFontManagerCreateFontDescriptorsFromURL(fontURL as CFURL) as! [CTFontDescriptor]
let size: CGFloat = t.device ? 20 : 34
let font = CTFontCreateWithFontDescriptor(descriptors[0], size, nil)
let text = NSAttributedString(string: "homtom", attributes: [
    NSAttributedString.Key(kCTFontAttributeName as String): font,
    NSAttributedString.Key(kCTForegroundColorAttributeName as String): bone,
    NSAttributedString.Key(kCTKernAttributeName as String): -0.045 * size,
])
let line = CTLineCreateWithAttributedString(text)
let bounds = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)

// Layout: mark and wordmark form one group centred vertically. Units are the 100x100 mark box
// (160x100 for the device); k is pixels per unit, kk pixels per mark unit.
let tileSize: CGFloat = 84, deviceWidth: CGFloat = 330
let k = t.device ? deviceWidth / deviceBody.width : tileSize / 100
let kk = k * t.markScale
let markHalf = 24.25 * t.markScale  // house outline plus half stroke, either side of y=50
var visTop = 50 - markHalf, visBottom = 50 + markHalf
if let shape = t.shape {
    let b = shape.boundingBoxOfPath
    visTop = b.minY - t.ringWidth / 2
    visBottom = b.maxY + t.ringWidth / 2
}
let gap: CGFloat = t.device ? 9 : 20
let markH = (visBottom - visTop) * k
let groupH = markH + gap + bounds.height
let groupTop = (CGFloat(height) - groupH) / 2
let boxX = (CGFloat(width) - (t.device ? deviceWidth : tileSize)) / 2
let boxY = t.device ? (CGFloat(height) - deviceBody.height * k) / 2 : groupTop - visTop * k
let markY = t.device ? groupTop + markHalf * k : boxY + 50 * k  // pixel row of mark-space y=50

// Device screen: the accent darkened to a flat inset tone.
let rgb = UInt32(tileHex, radix: 16)!
let screenHex = String(format: "%02X%02X%02X", (rgb >> 16 & 0xFF) * 6 / 10, (rgb >> 8 & 0xFF) * 6 / 10, (rgb & 0xFF) * 6 / 10)

ctx.saveGState()
ctx.translateBy(x: boxX, y: boxY)
ctx.scaleBy(x: k, y: k)
if t.device {
    ctx.setFillColor(accent)
    ctx.addPath(CGPath(roundedRect: deviceBody, cornerWidth: 14, cornerHeight: 14, transform: nil))
    ctx.fillPath()
    ctx.setFillColor(colour(screenHex))
    ctx.addPath(CGPath(roundedRect: deviceScreen, cornerWidth: 6, cornerHeight: 6, transform: nil))
    ctx.fillPath()
} else if let shape = t.shape {
    ctx.setFillColor(accent)
    ctx.setStrokeColor(accent)
    ctx.setLineJoin(.round)
    ctx.addPath(shape)
    ctx.setLineWidth(t.ringWidth)
    if t.filled {
        ctx.drawPath(using: t.ringWidth > 0 ? .fillStroke : .fill)
    } else {
        ctx.strokePath()
    }
}
ctx.restoreGState()

let markColour = t.markBlue ? accent : bone
ctx.saveGState()
ctx.translateBy(x: CGFloat(width) / 2, y: markY)
ctx.scaleBy(x: kk, y: kk)
ctx.translateBy(x: -50, y: -50)
ctx.setStrokeColor(markColour)
ctx.setFillColor(markColour)
ctx.setLineCap(.round)
ctx.setLineJoin(.round)
ctx.setLineWidth(4.5)
ctx.addLines(between: [CGPoint(x: 27, y: 48), CGPoint(x: 50, y: 28), CGPoint(x: 73, y: 48),
                       CGPoint(x: 73, y: 72), CGPoint(x: 27, y: 72)])
ctx.closePath()
ctx.strokePath()
ctx.setLineWidth(arrowStroke)
ctx.addLines(between: arrowPts)
ctx.closePath()
ctx.drawPath(using: .fillStroke)
ctx.restoreGState()

if let svgOut = svgOut {
    let mark = t.markBlue ? "#" + tileHex : "#F5F3EE"
    let arrow = "M" + arrowPts.map { "\(num($0.x)) \(num($0.y))" }.joined(separator: "L") + "Z"
    let markSvg = "<g fill=\"none\" stroke=\"\(mark)\" stroke-width=\"4.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" "
        + "transform=\"translate(%@ %@) scale(\(num(kk / k))) translate(-50 -50)\">"
        + "<path d=\"M27 48 50 28l23 20v24H27Z\"/><path d=\"\(arrow)\" fill=\"\(mark)\" stroke-width=\"\(num(arrowStroke))\"/></g>"
    let svg: String
    if t.device {
        let my = (markY - boxY) / k
        svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 160 100\" width=\"160\" height=\"100\">"
            + "<rect width=\"160\" height=\"100\" rx=\"14\" fill=\"#\(tileHex)\"/>"
            + "<rect x=\"9\" y=\"9\" width=\"142\" height=\"82\" rx=\"6\" fill=\"#\(screenHex)\"/>"
            + String(format: markSvg, "80", num(my)) + "</svg>\n"
    } else {
        svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" width=\"100\" height=\"100\">\(t.svgShape)"
            + String(format: markSvg, "50", "50") + "</svg>\n"
    }
    try! svg.write(toFile: svgOut, atomically: true, encoding: .utf8)
}

ctx.saveGState()
ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
ctx.textPosition = CGPoint(x: (CGFloat(width) - bounds.width) / 2 - bounds.minX,
                           y: groupTop + markH + gap + bounds.maxY)
CTLineDraw(line, ctx)
ctx.restoreGState()

let image = ctx.makeImage()!
let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: out + ".png") as CFURL,
                                           UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dest, image, nil)
CGImageDestinationFinalize(dest)

let raw = ctx.data!.assumingMemoryBound(to: UInt8.self)
var fb = Data(capacity: width * height * 2)
for i in 0..<(width * height) {
    let r = UInt16(raw[i * 4]), g = UInt16(raw[i * 4 + 1]), b = UInt16(raw[i * 4 + 2])
    let v = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
    fb.append(UInt8(v & 0xFF))
    fb.append(UInt8(v >> 8))
}
try! fb.write(to: URL(fileURLWithPath: out + ".fb"))
