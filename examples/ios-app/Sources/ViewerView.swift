import UIKit
import QuartzCore

/// CAMetalLayer-backed view. A CADisplayLink drives the render loop and
/// requests 120Hz on ProMotion screens.
///
/// Touch handling reads the raw touches, as the web library reads raw
/// pointer events: one finger rotates, two fingers dolly and truck, and a
/// finger can join or leave mid-gesture. UIKit's pan and pinch recognizers
/// cannot express that handover; their state machines end with the touch
/// sequence.
final class ViewerView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }

    var metalLayer: CAMetalLayer { layer as! CAMetalLayer }

    private var viewer: Viewer?
    private var displayLink: CADisplayLink?
    private var lastTimestamp: CFTimeInterval = 0

    /// Called about twice per second with (frames per second, frame ms).
    var onStats: ((Double, Double) -> Void)?
    private var statFrames = 0
    private var statStart: CFTimeInterval = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        metalLayer.pixelFormat = .bgra8Unorm
        isMultipleTouchEnabled = true
        viewer = Viewer(layer: metalLayer)
        // The Avocado comes from setup.sh; the cube is the committed
        // fallback so the app builds without it.
        if let url = Bundle.main.url(forResource: "Avocado", withExtension: "glb")
            ?? Bundle.main.url(forResource: "cube", withExtension: "glb"),
           let data = try? Data(contentsOf: url) {
            _ = viewer?.loadModel(data)
        }
        if let url = Bundle.main.url(forResource: "default_env_ibl", withExtension: "ktx"),
           let data = try? Data(contentsOf: url) {
            _ = viewer?.loadEnvironment(data)
        }
        // One-finger zoom: a tap primes a short window. A touch that starts
        // inside the window becomes a dolly that pivots on the tap point.
        let tap = UITapGestureRecognizer(target: self, action: #selector(primeTapZoom(_:)))
        tap.cancelsTouchesInView = false
        addGestureRecognizer(tap)

        // Metal work is not allowed in the background. Rendering there
        // poisons the swap chain and the view comes back frozen.
        NotificationCenter.default.addObserver(
            self, selector: #selector(appDidEnterBackground),
            name: UIApplication.didEnterBackgroundNotification, object: nil)
        NotificationCenter.default.addObserver(
            self, selector: #selector(appWillEnterForeground),
            name: UIApplication.willEnterForegroundNotification, object: nil)
    }

    @objc private func appDidEnterBackground() {
        displayLink?.isPaused = true
    }

    @objc private func appWillEnterForeground() {
        lastTimestamp = 0
        statStart = 0
        statFrames = 0
        displayLink?.isPaused = false
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    func start() {
        guard displayLink == nil else { return }
        let link = CADisplayLink(target: self, selector: #selector(tick(_:)))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 80, maximum: 120, preferred: 120)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    func stop() {
        displayLink?.invalidate()
        displayLink = nil
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

    override func layoutSubviews() {
        super.layoutSubviews()
        let scale = window?.screen.scale ?? UIScreen.main.scale
        metalLayer.contentsScale = scale
        let width = UInt32(bounds.width * scale)
        let height = UInt32(bounds.height * scale)
        metalLayer.drawableSize = CGSize(width: CGFloat(width), height: CGFloat(height))
        viewer?.resizeWidth(width, height: height)
    }

    // MARK: - Touches

    private enum TouchMode { case idle, rotate, anchoredZoom, pinch }
    private var mode: TouchMode = .idle
    private var activeTouches: [UITouch] = []

    private var lastGrab: CGPoint = .zero          // physical px
    private var lastTapTime: CFTimeInterval = 0
    private var lastTapLocation: CGPoint = .zero   // points
    private var lastZoomY: CGFloat = 0             // points
    private var zoomAnchor: CGPoint = .zero        // physical px
    private var lastPinchDistance: CGFloat = 0     // points
    private var lastPinchMid: CGPoint = .zero      // physical px

    @objc private func primeTapZoom(_ gesture: UITapGestureRecognizer) {
        lastTapTime = CACurrentMediaTime()
        lastTapLocation = gesture.location(in: self)
    }

    private func physicalPoint(_ point: CGPoint) -> CGPoint {
        let scale = metalLayer.contentsScale
        return CGPoint(x: point.x * scale, y: point.y * scale)
    }

    /// Touch distance in points (the dolly curve uses density-independent
    /// pixels) and midpoint in physical px (the truck normalizes by the
    /// physical viewport height).
    private func pinchState() -> (distance: CGFloat, mid: CGPoint) {
        let a = activeTouches[0].location(in: self)
        let b = activeTouches[1].location(in: self)
        let scale = metalLayer.contentsScale
        return (hypot(a.x - b.x, a.y - b.y),
                CGPoint(x: (a.x + b.x) * 0.5 * scale, y: (a.y + b.y) * 0.5 * scale))
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches where !activeTouches.contains(touch) {
            activeTouches.append(touch)
        }
        syncMode()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        switch mode {
        case .rotate:
            guard let touch = activeTouches.first else { return }
            // Deltas are (last - current), both sides in physical px.
            let point = physicalPoint(touch.location(in: self))
            viewer?.rotateDx(lastGrab.x - point.x, dy: lastGrab.y - point.y)
            lastGrab = point
        case .anchoredZoom:
            guard let touch = activeTouches.first else { return }
            // Dolly deltas are in points; the anchor is in physical px.
            let y = touch.location(in: self).y
            viewer?.anchoredDollyDelta(lastZoomY - y,
                                       anchorX: zoomAnchor.x,
                                       anchorY: zoomAnchor.y)
            lastZoomY = y
        case .pinch:
            guard activeTouches.count >= 2 else { return }
            let state = pinchState()
            if lastPinchDistance > 0 {
                viewer?.pinchDollyDelta(lastPinchDistance - state.distance)
                viewer?.pinchTruckDx(lastPinchMid.x - state.mid.x,
                                     dy: lastPinchMid.y - state.mid.y)
            }
            lastPinchDistance = state.distance
            lastPinchMid = state.mid
        case .idle:
            break
        }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        activeTouches.removeAll { touches.contains($0) }
        syncMode()
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        activeTouches.removeAll { touches.contains($0) }
        syncMode()
    }

    /// Sets the mode from the touch count and re-baselines the gesture, so
    /// that fingers can join and leave without a jump.
    private func syncMode() {
        switch activeTouches.count {
        case 0:
            switch mode {
            case .rotate: viewer?.endRotate()
            case .anchoredZoom, .pinch: viewer?.endPinch()
            case .idle: break
            }
            mode = .idle
        case 1:
            if mode == .pinch { viewer?.endPinch() }
            let location = activeTouches[0].location(in: self)
            let nearTap = hypot(location.x - lastTapLocation.x,
                                location.y - lastTapLocation.y) < 60
            if mode == .idle, CACurrentMediaTime() - lastTapTime < 0.35, nearTap {
                mode = .anchoredZoom
                lastZoomY = location.y
                zoomAnchor = physicalPoint(lastTapLocation)
            } else if mode != .anchoredZoom {
                mode = .rotate
                lastGrab = physicalPoint(location)
            }
        default:
            if mode == .rotate { viewer?.endRotate() }
            if mode == .anchoredZoom { viewer?.endPinch() }
            mode = .pinch
            let state = pinchState()
            lastPinchDistance = state.distance
            lastPinchMid = state.mid
        }
    }

    deinit {
        displayLink?.invalidate()
        viewer?.shutdown()
    }
}
