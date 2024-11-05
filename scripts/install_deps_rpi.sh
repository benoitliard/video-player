#!/bin/bash

# Couleurs pour les messages
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Installation des dépendances pour Raspberry Pi...${NC}"

# Mise à jour du système
sudo apt update && sudo apt upgrade -y

# Installation des dépendances de développement
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libuv1-dev \
    libssl-dev \
    zlib1g-dev

# Installation des dépendances FFmpeg avec support V4L2
sudo apt install -y \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    libv4l-dev

# Installation de SDL2
sudo apt install -y \
    libsdl2-dev

# Installation de uWebSockets depuis les sources
echo -e "${YELLOW}Installation de uWebSockets...${NC}"
git clone --recursive https://github.com/uNetworking/uWebSockets.git
cd uWebSockets
make
sudo make install
cd ..
rm -rf uWebSockets

# Vérification de l'installation
echo -e "${GREEN}Vérification de l'installation...${NC}"
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Installation réussie!${NC}"
    echo -e "${YELLOW}Pour compiler le projet:${NC}"
    echo "1. mkdir build"
    echo "2. cd build"
    echo "3. cmake .."
    echo "4. make -j4"
else
    echo -e "${RED}Erreur lors de l'installation${NC}"
    exit 1
fi 