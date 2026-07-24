# Vulkan example app

A cross-platform app that renders a cube with Vulkan and GLFW, no
engine, and moves the camera with `camera-controls.hpp`. `main.cpp`
builds the view matrix from the `lookAt` basis and forwards GLFW mouse
events to the library's mouse layer.

Controls: left-drag orbits, right-drag pans, middle-drag zooms, and the
scroll wheel zooms at the cursor.

## Build

Linux (Debian/Ubuntu):

```sh
sudo apt install libvulkan-dev libglfw3-dev cmake
cmake -B build && cmake --build build && ./build/vulkan-cube
```

macOS (through MoltenVK):

```sh
brew install glfw molten-vk vulkan-loader vulkan-headers
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json ./build/vulkan-cube
```

The SPIR-V is committed as `shader_vert.h` and `shader_frag.h`. To
regenerate after a shader change (`brew install shaderc` for glslc):

```sh
glslc shader.vert -o shader.vert.spv && xxd -i shader.vert.spv > shader_vert.h
glslc shader.frag -o shader.frag.spv && xxd -i shader.frag.spv > shader_frag.h
```
