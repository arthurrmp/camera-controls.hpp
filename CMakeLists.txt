cmake_minimum_required(VERSION 3.14)
project(camera_controls LANGUAGES CXX)

add_library(camera_controls INTERFACE)
add_library(camera_controls::camera_controls ALIAS camera_controls)
target_include_directories(camera_controls INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(camera_controls INTERFACE cxx_std_17)

include(GNUInstallDirs)
install(FILES include/camera_controls.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

option(CAMERA_CONTROLS_BUILD_EXAMPLE "Build the minimal example" OFF)
if(CAMERA_CONTROLS_BUILD_EXAMPLE)
    add_executable(minimal examples/minimal.cpp)
    target_link_libraries(minimal PRIVATE camera_controls)
endif()
