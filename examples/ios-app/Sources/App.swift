import SwiftUI

@main
struct CameraControlsExampleApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

struct ContentView: View {
    @State private var fps: Double = 0
    @State private var frameMs: Double = 0
    @State private var showGuide = true

    var body: some View {
        ZStack {
            ViewerRepresentable(
                onStats: { fps = $0; frameMs = $1 },
                onFirstInteraction: { hideGuide() })
                .ignoresSafeArea()

            VStack {
                HStack {
                    statsBadge
                    Spacer()
                }
                Spacer()
                if showGuide {
                    guideCard
                        .transition(.opacity.combined(with: .move(edge: .bottom)))
                }
            }
            .padding(20)
        }
        .task {
            // The guide also goes away by itself.
            try? await Task.sleep(nanoseconds: 15_000_000_000)
            hideGuide()
        }
    }

    private func hideGuide() {
        withAnimation(.easeOut(duration: 0.5)) { showGuide = false }
    }

    private var statsBadge: some View {
        Text("\(Int(fps.rounded())) fps · \(String(format: "%.1f", frameMs)) ms")
            .font(.caption.monospacedDigit())
            .foregroundStyle(.secondary)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(.ultraThinMaterial, in: Capsule())
    }

    private var guideCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            guideRow("hand.draw", "Drag to orbit")
            guideRow("arrow.down.left.and.arrow.up.right", "Pinch to zoom")
            guideRow("hand.tap", "Tap, then drag to zoom on a point")
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 22))
    }

    private func guideRow(_ symbol: String, _ text: String) -> some View {
        HStack(spacing: 14) {
            Image(systemName: symbol)
                .font(.body)
                .frame(width: 26)
                .foregroundStyle(.secondary)
            Text(text)
                .font(.subheadline)
        }
    }
}

struct ViewerRepresentable: UIViewRepresentable {
    var onStats: ((Double, Double) -> Void)?
    var onFirstInteraction: (() -> Void)?

    func makeUIView(context: Context) -> ViewerView {
        let view = ViewerView(frame: .zero)
        view.onStats = onStats
        view.onFirstInteraction = onFirstInteraction
        view.start()
        return view
    }

    func updateUIView(_ view: ViewerView, context: Context) {}

    static func dismantleUIView(_ view: ViewerView, coordinator: ()) {
        view.stop()
    }
}
