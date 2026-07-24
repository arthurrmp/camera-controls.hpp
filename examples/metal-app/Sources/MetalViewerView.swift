import AppKit
import QuartzCore

/// CAMetalLayer-backed NSView. Mouse and wheel events go to the library's
/// mouse layer; the renderer is plain Metal, no engine.
final class MetalViewerView: NSView {
    private var renderer: Renderer?
    private var displayLink: CADisplayLink?
    private var lastTimestamp: CFTimeInterval = 0

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    private var metalLayer: CAMetalLayer { layer as! CAMetalLayer }

    override init(frame: CGRect) {
        super.init(frame: frame)
        wantsLayer = true
        renderer = Renderer(layer: metalLayer)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func makeBackingLayer() -> CALayer { CAMetalLayer() }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        displayLink?.invalidate()
        displayLink = nil
        guard window != nil else { return }
        let link = displayLink(target: self, selector: #selector(tick(_:)))
        link.add(to: .main, forMode: .common)
        displayLink = link
        resize()
    }

    @objc private func tick(_ link: CADisplayLink) {
        let dt = lastTimestamp == 0 ? 1.0 / 60.0 : link.timestamp - lastTimestamp
        lastTimestamp = link.timestamp
        renderer?.render(dt)
    }

    override func layout() {
        super.layout()
        resize()
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        resize()
    }

    private func resize() {
        guard bounds.width > 0, bounds.height > 0 else { return }
        let scale = window?.backingScaleFactor ?? 2.0
        metalLayer.contentsScale = scale
        let width = UInt32(bounds.width * scale)
        let height = UInt32(bounds.height * scale)
        metalLayer.drawableSize = CGSize(width: CGFloat(width), height: CGFloat(height))
        renderer?.resizeWidth(width, height: height, scale: scale)
    }

    // MARK: - Mouse forwarding (positions in points, top-left origin)

    private func point(_ event: NSEvent) -> CGPoint {
        convert(event.locationInWindow, from: nil)
    }

    /// NSEvent numbers buttons left 0, right 1, middle 2. The library
    /// numbers them left 0, middle 1, right 2.
    private func libraryButton(_ event: NSEvent) -> Int32 {
        switch event.buttonNumber {
        case 1: return 2
        case 2: return 1
        default: return 0
        }
    }

    override func mouseDown(with event: NSEvent) {
        let p = point(event)
        renderer?.mouseDown(0, x: p.x, y: p.y)
    }

    override func rightMouseDown(with event: NSEvent) {
        let p = point(event)
        renderer?.mouseDown(2, x: p.x, y: p.y)
    }

    override func otherMouseDown(with event: NSEvent) {
        let p = point(event)
        renderer?.mouseDown(libraryButton(event), x: p.x, y: p.y)
    }

    override func mouseDragged(with event: NSEvent) { dragged(event) }
    override func rightMouseDragged(with event: NSEvent) { dragged(event) }
    override func otherMouseDragged(with event: NSEvent) { dragged(event) }

    private func dragged(_ event: NSEvent) {
        let p = point(event)
        renderer?.mouseMovedX(p.x, y: p.y)
    }

    override func mouseUp(with event: NSEvent) { renderer?.mouseUp() }
    override func rightMouseUp(with event: NSEvent) { renderer?.mouseUp() }
    override func otherMouseUp(with event: NSEvent) { renderer?.mouseUp() }

    override func scrollWheel(with event: NSEvent) {
        let deltaY = event.hasPreciseScrollingDeltas
            ? event.scrollingDeltaY
            : event.scrollingDeltaY * 10.0
        let p = point(event)
        renderer?.mouseWheel(-deltaY, x: p.x, y: p.y)
    }

    override func magnify(with event: NSEvent) {
        let p = point(event)
        renderer?.mouseWheel(-event.magnification * 300.0, x: p.x, y: p.y)
    }

    deinit {
        displayLink?.invalidate()
    }
}
