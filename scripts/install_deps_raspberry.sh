#!/bin/bash

# Couleurs pour les messages
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Installation des dépendances pour Raspberry Pi...${NC}"

# Vérifier si on est sur un Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/cpuinfo; then
    echo -e "${RED}Ce script doit être exécuté sur un Raspberry Pi${NC}"
    exit 1
fi

# Mise à jour du système
echo -e "${YELLOW}Mise à jour du système...${NC}"
sudo apt-get update
sudo apt-get upgrade -y

# Installation des dépendances de développement
echo -e "${YELLOW}Installation des outils de développement...${NC}"
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config

# Installation des dépendances pour FFmpeg
echo -e "${YELLOW}Installation des dépendances FFmpeg...${NC}"
sudo apt-get install -y \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libavdevice-dev \
    libavfilter-dev

# Installation de SDL2
echo -e "${YELLOW}Installation de SDL2...${NC}"
sudo apt-get install -y \
    libsdl2-dev

# Installation des dépendances pour WebSocket
echo -e "${YELLOW}Installation des dépendances WebSocket...${NC}"
sudo apt-get install -y \
    libssl-dev \
    zlib1g-dev

# Optimisations pour Raspberry Pi
echo -e "${YELLOW}Configuration des optimisations pour Raspberry Pi...${NC}"

# Augmenter la mémoire GPU
echo "gpu_mem=256" | sudo tee -a /boot/config.txt

# Activer le mode performance
echo "force_turbo=1" | sudo tee -a /boot/config.txt

# Vérification de l'installation
echo -e "${GREEN}Vérification de l'installation...${NC}"
if command -v ffmpeg >/dev/null 2>&1 && command -v sdl2-config >/dev/null 2>&1; then
    echo -e "${GREEN}Installation réussie!${NC}"
else
    echo -e "${RED}Erreur lors de l'installation${NC}"
    exit 1
fi 