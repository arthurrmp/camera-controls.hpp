cmake_minimum_required(VERSION 3.14)
project(camera_controls_vulkan_example LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(vulkan-cube main.cpp)
target_compile_features(vulkan-cube PRIVATE cxx_std_17)
target_include_directories(vulkan-cube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(vulkan-cube PRIVATE Vulkan::Vulkan glfw)
