cmake_minimum_required(VERSION 3.10)
project(video_player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimizations for RPi 5
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    add_definitions(-D__arm__)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a76 -mtune=cortex-a76")
endif()

# Find FFmpeg packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
    libavfilter
)

# Find SDL2
find_package(SDL2 REQUIRED)

# Définir les fichiers source
set(SOURCES
    src/main.cpp
    src/VideoPlayer.cpp
    src/core/AudioManager.cpp
    src/core/VideoDecoder.cpp
    src/core/Renderer.cpp
    src/utils/Logger.cpp
)

# Définir les fichiers header
set(HEADERS
    src/VideoPlayer.h
    src/core/AudioManager.h
    src/core/VideoDecoder.h
    src/core/Renderer.h
    src/utils/Logger.h
)

# Define executable
add_executable(video_player ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(video_player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${SDL2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(video_player PRIVATE
    ${SDL2_LIBRARIES}
    PkgConfig::FFMPEG
    avcodec
    avformat
    avutil
    swscale
    swresample
    avfilter
)

# Set RPi specific flags
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    target_compile_definitions(video_player PRIVATE RASPBERRY_PI)
endif()