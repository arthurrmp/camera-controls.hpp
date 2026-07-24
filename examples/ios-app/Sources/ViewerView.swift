import UIKit
import QuartzCore

/// CAMetalLayer-backed view. A CADisplayLink drives the render loop and
/// requests 120Hz on ProMotion screens. Touches are forwarded to the
/// library, which decides the gesture itself, as the web original does
/// with pointer events.
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

        // Metal work is not allowed in the background. Rendering there
        // poisons the swap chain and the view comes back frozen.
        NotificationCenter.default.addObserver(
            self, selector: #selector(appDidEnterBackground),
            name: UIApplication.didEnterBackgroundNotification, object: nil)
        NotificationCenter.default.addObserver(
            self, selector: #selector(appWillEnterForeground),
            name: UIApplication.willEnterForegroundNotification, object: nil)
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

    @objc private func appDidEnterBackground() {
        displayLink?.isPaused = true
    }

    @objc private func appWillEnterForeground() {
        lastTimestamp = 0
        statStart = 0
        statFrames = 0
        displayLink?.isPaused = false
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
        viewer?.resizeWidth(width, height: height, scale: scale)
    }

    // MARK: - Touch forwarding (positions in points)

    private func touchId(_ touch: UITouch) -> Int64 {
        Int64(Int(bitPattern: Unmanaged.passUnretained(touch).toOpaque()))
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let p = touch.location(in: self)
            viewer?.touchBegan(touchId(touch), x: p.x, y: p.y, time: touch.timestamp)
        }
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let p = touch.location(in: self)
            viewer?.touchMoved(touchId(touch), x: p.x, y: p.y)
        }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            viewer?.touchEnded(touchId(touch), time: touch.timestamp)
        }
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            viewer?.touchCancelled(touchId(touch))
        }
    }

    deinit {
        displayLink?.invalidate()
        viewer?.shutdown()
    }
}
