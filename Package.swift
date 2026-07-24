// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "camera-controls",
    products: [
        .library(name: "CameraControls", targets: ["CameraControls"])
    ],
    targets: [
        .target(
            name: "CameraControls",
            path: ".",
            exclude: ["examples", "README.md", "LICENSE", "CMakeLists.txt"],
            sources: ["src/camera_controls.cpp"],
            publicHeadersPath: "include"
        )
    ],
    cxxLanguageStandard: .cxx17
)
