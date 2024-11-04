#!/bin/bash

# Définition des couleurs pour les messages
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Installation des dépendances pour macOS...${NC}"

# Vérifier si Homebrew est installé
if ! command -v brew >/dev/null 2>&1; then
    echo -e "${YELLOW}Installation de Homebrew...${NC}"
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
fi

# Installation des dépendances principales
echo -e "${YELLOW}Installation des dépendances principales...${NC}"
brew install \
    ffmpeg \
    sdl2 \
    pkg-config \
    openssl \
    zlib \
    libuv

# Installation de uWebSockets depuis les sources
echo -e "${YELLOW}Installation de uWebSockets...${NC}"
if [ -d "uWebSockets" ]; then
    rm -rf uWebSockets
fi
git clone --recursive https://github.com/uNetworking/uWebSockets.git
cd uWebSockets

# Modifier les includes dans les fichiers sources
find . -type f -name "*.h" -exec sed -i '' 's/<libusockets\.h>/"libusockets.h"/g' {} +

# Compiler uSockets d'abord
cd uSockets
make
cd ..

# Créer les dossiers nécessaires
sudo mkdir -p /usr/local/include/uWS
sudo mkdir -p /usr/local/include/libusockets

# Copier les fichiers headers de uWebSockets
sudo cp src/*.h /usr/local/include/uWS/

# Copier les fichiers headers de uSockets
sudo cp uSockets/src/*.h /usr/local/include/libusockets/

# Créer un lien symbolique pour libusockets.h
sudo ln -sf /usr/local/include/libusockets/libusockets.h /usr/local/include/

# Copier les fichiers objets et créer la bibliothèque statique
sudo mkdir -p /usr/local/lib
cd uSockets
sudo ar rcs /usr/local/lib/libusockets.a *.o
cd ../..

# Vérification de l'installation
echo -e "${GREEN}Vérification de l'installation...${NC}"
MISSING_DEPS=0

# Vérifier chaque dépendance
for dep in ffmpeg sdl2-config pkg-config; do
    if ! command -v $dep >/dev/null 2>&1; then
        echo -e "${RED}$dep n'est pas installé correctement${NC}"
        MISSING_DEPS=1
    fi
done

# Vérifier si uWebSockets est installé
if [ ! -f "/usr/local/include/uWS/App.h" ] || [ ! -f "/usr/local/include/libusockets.h" ]; then
    echo -e "${RED}uWebSockets n'est pas installé correctement${NC}"
    MISSING_DEPS=1
fi

if [ $MISSING_DEPS -eq 0 ]; then
    echo -e "${GREEN}Installation réussie!${NC}"
    echo -e "${YELLOW}Configuration de l'environnement:${NC}"
    echo "Ajoutez ces lignes à votre ~/.zshrc ou ~/.bash_profile:"
    echo "export PKG_CONFIG_PATH=\"/usr/local/opt/openssl/lib/pkgconfig:/usr/local/lib/pkgconfig:\$PKG_CONFIG_PATH\""
    echo -e "${YELLOW}Pour compiler le projet, exécutez ces commandes depuis le dossier racine du projet:${NC}"
    echo "1. mkdir build"
    echo "2. cd build"
    echo "3. export PKG_CONFIG_PATH=\"/usr/local/opt/openssl/lib/pkgconfig:/usr/local/lib/pkgconfig:\$PKG_CONFIG_PATH\""
    echo "4. cmake -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl .."
    echo "5. make"
else
    echo -e "${RED}Certaines dépendances n'ont pas été installées correctement${NC}"
    exit 1
fi 