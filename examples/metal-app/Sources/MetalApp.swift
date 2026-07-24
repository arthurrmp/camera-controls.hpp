import SwiftUI

@main
struct CameraControlsExampleMetalApp: App {
    var body: some Scene {
        WindowGroup {
            ViewerRepresentable()
                .ignoresSafeArea()
                .frame(minWidth: 640, minHeight: 480)
        }
    }
}

struct ViewerRepresentable: NSViewRepresentable {
    func makeNSView(context: Context) -> MetalViewerView {
        MetalViewerView(frame: .zero)
    }

    func updateNSView(_ view: MetalViewerView, context: Context) {}
}
