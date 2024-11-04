cmake_minimum_required(VERSION 3.10)
project(video_player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ajouter après le project()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    add_definitions(-D__arm__)
    # Optimisations pour RPi 5
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a76 -mtune=cortex-a76")
endif()

# Ajouter V4L2 pour RPi
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    pkg_check_modules(V4L2 REQUIRED libv4l2)
    target_link_libraries(video_player PRIVATE ${V4L2_LIBRARIES})
endif()

# Utiliser pkg-config pour trouver FFMPEG
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

# Trouver SDL2
find_package(SDL2 REQUIRED)

# Trouver OpenSSL
find_package(OpenSSL REQUIRED)

# Configuration pour uWebSockets
find_path(UWEBSOCKETS_INCLUDE_DIR uWS/App.h
    PATHS 
    /usr/local/include
    /opt/homebrew/include
    REQUIRED
)

# Trouver la bibliothèque statique uSockets
find_library(USOCKETS_LIBRARY
    NAMES libusockets.a usockets
    PATHS /usr/local/lib
    REQUIRED
)

# Trouver libuv
find_path(LIBUV_INCLUDE_DIR uv.h
    PATHS
    /usr/local/include
    /opt/homebrew/include
    REQUIRED
)

find_library(LIBUV_LIBRARY
    NAMES uv libuv
    PATHS
    /usr/local/lib
    /opt/homebrew/lib
    REQUIRED
)

# Définir l'exécutable
add_executable(video_player
    src/main.cpp
    src/VideoPlayer.cpp
)

# Organisation des includes
target_include_directories(video_player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${UWEBSOCKETS_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${LIBUV_INCLUDE_DIR}
)

# Lier les bibliothèques
target_link_libraries(video_player PRIVATE
    ${SDL2_LIBRARIES}
    PkgConfig::FFMPEG
    ${USOCKETS_LIBRARY}
    z
    ${OPENSSL_SSL_LIBRARY}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${LIBUV_LIBRARY}
) 