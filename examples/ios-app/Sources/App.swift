import SwiftUI

@main
struct CameraControlsExampleApp: App {
    var body: some Scene {
        WindowGroup {
            ViewerRepresentable()
                .ignoresSafeArea()
        }
    }
}

struct ViewerRepresentable: UIViewRepresentable {
    func makeUIView(context: Context) -> ViewerView {
        let view = ViewerView(frame: .zero)
        view.start()
        return view
    }

    func updateUIView(_ view: ViewerView, context: Context) {}

    static func dismantleUIView(_ view: ViewerView, coordinator: ()) {
        view.stop()
    }
}
