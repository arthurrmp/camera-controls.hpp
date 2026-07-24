import AppKit
import QuartzCore

/// CAMetalLayer-backed NSView. Mouse and wheel events are forwarded to
/// the library, which maps buttons to gestures with its mouseButtons
/// mapping (left rotates, middle dollies, right trucks; the wheel and
/// the trackpad pinch dolly at the cursor).
final class MacViewerView: NSView {
    private var viewer: Viewer?
    private var displayLink: CADisplayLink?
    private var lastTimestamp: CFTimeInterval = 0

    /// Called about twice per second with (frames per second, frame ms).
    var onStats: ((Double, Double) -> Void)?
    private var statFrames = 0
    private var statStart: CFTimeInterval = 0

    // Top-left origin, so the coordinates match the library's screen
    // convention (and the web).
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    private var metalLayer: CAMetalLayer { layer as! CAMetalLayer }

    override init(frame: CGRect) {
        super.init(frame: frame)
        wantsLayer = true
        viewer = Viewer(layer: metalLayer)
        if let url = Bundle.main.url(forResource: "Avocado", withExtension: "glb")
            ?? Bundle.main.url(forResource: "cube", withExtension: "glb"),
           let data = try? Data(contentsOf: url) {
            _ = viewer?.loadModel(data)
        }
        if let url = Bundle.main.url(forResource: "default_env_ibl", withExtension: "ktx"),
           let data = try? Data(contentsOf: url) {
            _ = viewer?.loadEnvironment(data)
        }
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
        viewer?.render(dt)

        if statStart == 0 { statStart = link.timestamp }
        statFrames += 1
        let elapsed = link.timestamp - statStart
        if elapsed >= 0.5 {
            let fps = Double(statFrames) / elapsed
            onStats?(fps, 1000.0 / fps)
            statFrames = 0
            statStart = link.timestamp
        }
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
        viewer?.resizeWidth(width, height: height, scale: scale)
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
        viewer?.mouseDown(0, x: p.x, y: p.y)
    }

    override func rightMouseDown(with event: NSEvent) {
        let p = point(event)
        viewer?.mouseDown(2, x: p.x, y: p.y)
    }

    override func otherMouseDown(with event: NSEvent) {
        let p = point(event)
        viewer?.mouseDown(libraryButton(event), x: p.x, y: p.y)
    }

    override func mouseDragged(with event: NSEvent) { dragged(event) }
    override func rightMouseDragged(with event: NSEvent) { dragged(event) }
    override func otherMouseDragged(with event: NSEvent) { dragged(event) }

    private func dragged(_ event: NSEvent) {
        let p = point(event)
        viewer?.mouseMovedX(p.x, y: p.y)
    }

    override func mouseUp(with event: NSEvent) { viewer?.mouseUp() }
    override func rightMouseUp(with event: NSEvent) { viewer?.mouseUp() }
    override func otherMouseUp(with event: NSEvent) { viewer?.mouseUp() }

    /// AppKit wheel deltas have the opposite sign of DOM wheel deltas;
    /// line-based wheels scroll in lines, not pixels.
    override func scrollWheel(with event: NSEvent) {
        let deltaY = event.hasPreciseScrollingDeltas
            ? event.scrollingDeltaY
            : event.scrollingDeltaY * 10.0
        let p = point(event)
        viewer?.mouseWheel(-deltaY, x: p.x, y: p.y)
    }

    /// Trackpad pinch. The web gets this as a wheel event with ctrlKey;
    /// the factor approximates that conversion.
    override func magnify(with event: NSEvent) {
        let p = point(event)
        viewer?.mouseWheel(-event.magnification * 300.0, x: p.x, y: p.y)
    }

    deinit {
        displayLink?.invalidate()
        viewer?.shutdown()
    }
}
