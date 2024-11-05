#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to check if a command succeeded
check_error() {
    if [ $? -ne 0 ]; then
        echo -e "${RED}Error: $1${NC}"
        exit 1
    fi
}

# Update package list
echo -e "${YELLOW}Updating package list...${NC}"
sudo apt-get update
check_error "Failed to update package list"

# Install basic development tools
echo -e "${YELLOW}Installing development tools...${NC}"
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config
check_error "Failed to install development tools"

# Install FFmpeg dependencies
echo -e "${YELLOW}Installing FFmpeg dependencies...${NC}"
sudo apt-get install -y \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev
check_error "Failed to install FFmpeg dependencies"

# Install SDL2
echo -e "${YELLOW}Installing SDL2...${NC}"
sudo apt-get install -y \
    libsdl2-2.0-0 \
    libsdl2-dev
check_error "Failed to install SDL2"

# Install V4L2 for hardware acceleration
echo -e "${YELLOW}Installing V4L2...${NC}"
sudo apt-get install -y \
    libv4l-dev
check_error "Failed to install V4L2"

# Install WebSocket dependencies
echo -e "${YELLOW}Installing WebSocket dependencies...${NC}"
sudo apt-get install -y \
    libssl-dev \
    zlib1g-dev \
    libuv1-dev
check_error "Failed to install WebSocket dependencies"

# Install uWebSockets from source
echo -e "${YELLOW}Installing uWebSockets...${NC}"
if [ ! -d "uWebSockets" ]; then
    # Remove any existing installation
    sudo rm -rf /usr/local/include/uSockets
    sudo rm -rf /usr/local/include/uWS
    sudo rm -f /usr/local/lib/libusockets.a
    sudo rm -f /usr/local/include/libusockets.h

    # Clone repositories
    git clone --recursive https://github.com/uNetworking/uWebSockets.git
    check_error "Failed to clone uWebSockets"
    
    # Build and install uSockets
    cd uWebSockets/uSockets
    make
    check_error "Failed to build uSockets"
    
    # Install uSockets
    sudo cp uSockets.a /usr/local/lib/libusockets.a
    check_error "Failed to copy uSockets library"
    
    # Install uSockets headers - Modified this part
    sudo cp -r src/* /usr/local/include/
    check_error "Failed to copy uSockets headers"
    
    # Install uWebSockets
    cd ..
    sudo mkdir -p /usr/local/include/uWS
    sudo cp -r src/* /usr/local/include/uWS/
    check_error "Failed to copy uWebSockets headers"
    
    cd ..
fi

# Optimizations for Raspberry Pi
echo -e "${YELLOW}Configuring optimizations for Raspberry Pi...${NC}"

# Increase GPU memory
if ! grep -q "gpu_mem=256" /boot/config.txt; then
    echo "gpu_mem=256" | sudo tee -a /boot/config.txt
fi

# Enable performance mode
if ! grep -q "force_turbo=1" /boot/config.txt; then
    echo "force_turbo=1" | sudo tee -a /boot/config.txt
fi

# Verify installation
echo -e "${GREEN}Verifying installation...${NC}"
if command -v ffmpeg >/dev/null 2>&1 && command -v sdl2-config >/dev/null 2>&1; then
    echo -e "${GREEN}Installation successful!${NC}"
    echo -e "${YELLOW}To build the project:${NC}"
    echo "1. mkdir build"
    echo "2. cd build"
    echo "3. cmake .."
    echo "4. make -j4"
else
    echo -e "${RED}Installation error${NC}"
    exit 1
fi 