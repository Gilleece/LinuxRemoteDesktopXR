cmake_minimum_required(VERSION 3.10)
project(RLDXR_Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavdevice libavutil libswscale)
pkg_check_modules(LIBVA REQUIRED IMPORTED_TARGET libva libva-drm)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

add_executable(RLDXR_Server src/main.cpp src/Capture.cpp src/Encoder.cpp src/Network.cpp)

target_link_libraries(RLDXR_Server PRIVATE
    PkgConfig::FFMPEG
    PkgConfig::LIBVA
    PkgConfig::X11
)

