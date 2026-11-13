// Tile splash PNGs into a labelled grid.
// Usage: swift contact-sheet.swift <font.ttf> <out.png> <label=splash.png>...
import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
let fontURL = URL(fileURLWithPath: args[1])
let items = args.dropFirst(3).map { a -> (String, String) in
    let parts = a.split(separator: "=", maxSplits: 1).map(String.init)
    return (parts[0], parts[1])
}
let cols = 3, rows = (items.count + cols - 1) / cols
let cellW = 480, cellH = 272, gap = 24, labelH = 44
let width = cols * cellW + (cols + 1) * gap
let height = rows * (cellH + labelH) + (rows + 1) * gap

let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                    space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.setFillColor(CGColor(red: 0.07, green: 0.07, blue: 0.08, alpha: 1))
ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))

CTFontManagerRegisterFontsForURL(fontURL as CFURL, .process, nil)
let descriptors = CTFontManagerCreateFontDescriptorsFromURL(fontURL as CFURL) as! [CTFontDescriptor]
let font = CTFontCreateWithFontDescriptor(descriptors[0], 18, nil)

for (i, item) in items.enumerated() {
    let x = gap + (i % cols) * (cellW + gap)
    let top = gap + (i / cols) * (cellH + labelH + gap)
    let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: item.1) as CFURL, nil)!
    let img = CGImageSourceCreateImageAtIndex(src, 0, nil)!
    ctx.draw(img, in: CGRect(x: x, y: height - top - cellH, width: cellW, height: cellH))
    let text = NSAttributedString(string: item.0, attributes: [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(red: 0.9, green: 0.9, blue: 0.9, alpha: 1),
    ])
    ctx.textPosition = CGPoint(x: CGFloat(x), y: CGFloat(height - top - cellH - 30))
    CTLineDraw(CTLineCreateWithAttributedString(text), ctx)
}

let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: args[2]) as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dest, ctx.makeImage()!, nil)
CGImageDestinationFinalize(dest)
