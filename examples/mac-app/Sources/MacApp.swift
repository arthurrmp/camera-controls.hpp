import SwiftUI

@main
struct CameraControlsExampleMacApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 640, minHeight: 480)
        }
    }
}

struct ContentView: View {
    @State private var fps: Double = 0
    @State private var frameMs: Double = 0

    var body: some View {
        ZStack {
            ViewerRepresentable(onStats: { fps = $0; frameMs = $1 })
                .ignoresSafeArea()

            VStack {
                HStack {
                    statsBadge
                    Spacer()
                }
                Spacer()
                HStack {
                    guideCard
                    Spacer()
                }
            }
            .padding(20)
        }
    }

    private var statsBadge: some View {
        Text("\(Int(fps.rounded())) fps · \(String(format: "%.1f", frameMs)) ms")
            .font(.caption.monospacedDigit())
            .foregroundStyle(.secondary)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .glassEffect()
    }

    private var guideCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            guideRow("cursorarrow.motionlines", "Drag to orbit")
            guideRow("arrow.up.and.down.and.arrow.left.and.right", "Right-drag to pan")
            guideRow("magnifyingglass", "Scroll or pinch to zoom at the cursor")
            guideRow("circle.circle", "Middle-drag to zoom")
        }
        .padding(18)
        .glassEffect(.regular, in: RoundedRectangle(cornerRadius: 22))
    }

    private func guideRow(_ symbol: String, _ text: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: symbol)
                .font(.body)
                .frame(width: 24)
                .foregroundStyle(.secondary)
            Text(text)
                .font(.subheadline)
        }
    }
}

struct ViewerRepresentable: NSViewRepresentable {
    var onStats: ((Double, Double) -> Void)?

    func makeNSView(context: Context) -> MacViewerView {
        let view = MacViewerView(frame: .zero)
        view.onStats = onStats
        return view
    }

    func updateNSView(_ view: MacViewerView, context: Context) {}
}
