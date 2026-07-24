import UIKit
import QuartzCore

/// CAMetalLayer-backed view. A CADisplayLink drives the render loop and
/// requests 120Hz on ProMotion screens. The gesture handlers send deltas to
/// the Viewer, which forwards them to camctl::CameraControls.
final class ViewerView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }

    var metalLayer: CAMetalLayer { layer as! CAMetalLayer }

    private var viewer: Viewer?
    private var displayLink: CADisplayLink?
    private var lastTimestamp: CFTimeInterval = 0

    /// Called about twice per second with (frames per second, frame ms).
    var onStats: ((Double, Double) -> Void)?
    /// Called once, on the first orbit or pinch.
    var onFirstInteraction: (() -> Void)?
    private var statFrames = 0
    private var statStart: CFTimeInterval = 0
    private var interacted = false

    override init(frame: CGRect) {
        super.init(frame: frame)
        metalLayer.pixelFormat = .bgra8Unorm
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
        setupGestures()
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

    private func noteInteraction() {
        guard !interacted else { return }
        interacted = true
        onFirstInteraction?()
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

    // MARK: - Gestures

    private func setupGestures() {
        addGestureRecognizer(UIPanGestureRecognizer(target: self, action: #selector(pan(_:))))
        addGestureRecognizer(UIPinchGestureRecognizer(target: self, action: #selector(pinch(_:))))
        // One-finger zoom: a tap primes a short window. A drag that starts
        // inside the window becomes a dolly that pivots on the tap point.
        let tap = UITapGestureRecognizer(target: self, action: #selector(primeTapZoom(_:)))
        tap.cancelsTouchesInView = false
        addGestureRecognizer(tap)
    }

    private var lastGrab: CGPoint = .zero
    private var lastTapTime: CFTimeInterval = 0
    private var lastTapLocation: CGPoint = .zero
    private var oneFingerZooming = false
    private var lastZoomY: CGFloat = 0
    private var zoomAnchor: CGPoint = .zero

    @objc private func primeTapZoom(_ gesture: UITapGestureRecognizer) {
        lastTapTime = CACurrentMediaTime()
        lastTapLocation = gesture.location(in: self)
    }

    /// Rotation deltas are physical pixels; the viewport height the Viewer
    /// divides by is physical too, so only the ratio matters.
    @objc private func pan(_ gesture: UIPanGestureRecognizer) {
        let location = gesture.location(in: self)
        let scale = metalLayer.contentsScale
        let point = CGPoint(x: location.x * scale, y: location.y * scale)
        switch gesture.state {
        case .began:
            noteInteraction()
            let sinceTap = CACurrentMediaTime() - lastTapTime
            let nearTap = hypot(location.x - lastTapLocation.x,
                                location.y - lastTapLocation.y) < 60
            if sinceTap < 0.35, nearTap {
                oneFingerZooming = true
                lastZoomY = location.y
                zoomAnchor = CGPoint(x: lastTapLocation.x * scale,
                                     y: lastTapLocation.y * scale)
            } else {
                lastGrab = point
            }
        case .changed:
            if oneFingerZooming {
                // Dolly deltas are in points; the anchor is in physical px.
                viewer?.anchoredDollyDelta(lastZoomY - location.y,
                                           anchorX: zoomAnchor.x,
                                           anchorY: zoomAnchor.y)
                lastZoomY = location.y
            } else {
                viewer?.rotateDx(lastGrab.x - point.x, dy: lastGrab.y - point.y)
                lastGrab = point
            }
        default:
            if oneFingerZooming {
                oneFingerZooming = false
                viewer?.endPinch()
            } else {
                viewer?.endRotate()
            }
        }
    }

    private var lastPinchDistance: CGFloat = 0
    private var lastPinchMid: CGPoint = .zero

    /// The dolly uses the touch distance in points. The truck uses the
    /// midpoint in physical px; it moves the zoom toward the pinch location.
    private func pinchState(_ gesture: UIPinchGestureRecognizer) -> (distance: CGFloat, mid: CGPoint)? {
        guard gesture.numberOfTouches >= 2 else { return nil }
        let a = gesture.location(ofTouch: 0, in: self)
        let b = gesture.location(ofTouch: 1, in: self)
        let scale = metalLayer.contentsScale
        return (hypot(a.x - b.x, a.y - b.y),
                CGPoint(x: (a.x + b.x) * 0.5 * scale, y: (a.y + b.y) * 0.5 * scale))
    }

    @objc private func pinch(_ gesture: UIPinchGestureRecognizer) {
        switch gesture.state {
        case .began:
            noteInteraction()
            guard let state = pinchState(gesture) else { return }
            lastPinchDistance = state.distance
            lastPinchMid = state.mid
        case .changed:
            guard let state = pinchState(gesture) else { return }
            if lastPinchDistance > 0 {
                viewer?.pinchDollyDelta(lastPinchDistance - state.distance)
                viewer?.pinchTruckDx(lastPinchMid.x - state.mid.x,
                                     dy: lastPinchMid.y - state.mid.y)
            }
            lastPinchDistance = state.distance
            lastPinchMid = state.mid
        default:
            viewer?.endPinch()
        }
    }

    deinit {
        displayLink?.invalidate()
        viewer?.shutdown()
    }
}
