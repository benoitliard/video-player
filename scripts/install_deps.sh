#!/bin/bash

# Mettre à jour le système
echo "Mise à jour du système..."
sudo apt-get update
sudo apt-get upgrade -y

# Installer les dépendances de base
echo "Installation des dépendances de base..."
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config

# Installer les dépendances FFmpeg
echo "Installation des dépendances FFmpeg..."
sudo apt-get install -y \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    libavfilter-dev

# Installer SDL2
echo "Installation de SDL2..."
sudo apt-get install -y \
    libsdl2-dev

# Installer les dépendances WebSocket
echo "Installation des dépendances WebSocket..."
sudo apt-get install -y \
    libwebsocketpp-dev \
    libjsoncpp-dev \
    libboost-all-dev \
    libssl-dev

# Installer les dépendances pour l'accélération matérielle
echo "Installation des dépendances pour l'accélération matérielle..."
sudo apt-get install -y \
    libdrm-dev \
    libgbm-dev \
    libv4l-dev

# Nettoyer le cache apt
echo "Nettoyage du cache..."
sudo apt-get clean
sudo apt-get autoremove -y

echo "Installation terminée !"
echo "Pour compiler le projet :"
echo "mkdir build"
echo "cd build"
echo "cmake .."
echo "make -j4" 