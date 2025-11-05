cmake_minimum_required(VERSION 3.10)
project(RLDXR_Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(RLDXR_Server src/main.cpp src/Capture.cpp src/Encoder.cpp src/Network.cpp)

find_package(PkgConfig REQUIRED)

# Find includes and libraries for FFmpeg and other dependencies
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVDEVICE REQUIRED libavdevice)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(OTHER_LIBS QUIET libva libva-drm x11)

# Add include directories
target_include_directories(RLDXR_Server PRIVATE
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVDEVICE_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

# Add library directories
target_link_directories(RLDXR_Server PRIVATE
    ${AVCODEC_LIBRARY_DIRS}
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVDEVICE_LIBRARY_DIRS}
    ${SWSCALE_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
)

if(OTHER_LIBS_FOUND)
    target_include_directories(RLDXR_Server PRIVATE ${OTHER_LIBS_INCLUDE_DIRS})
    target_link_directories(RLDXR_Server PRIVATE ${OTHER_LIBS_LIBRARY_DIRS})
endif()

# Link libraries with full paths to ensure correct linking
target_link_libraries(RLDXR_Server PRIVATE
    /usr/lib/x86_64-linux-gnu/libavformat.so
    /usr/lib/x86_64-linux-gnu/libavdevice.so
    /usr/lib/x86_64-linux-gnu/libavcodec.so
    /usr/lib/x86_64-linux-gnu/libswscale.so
    /usr/lib/x86_64-linux-gnu/libavutil.so
    pthread
    m
)

if(OTHER_LIBS_FOUND)
    target_link_libraries(RLDXR_Server PRIVATE ${OTHER_LIBS_LIBRARIES})
endif()


