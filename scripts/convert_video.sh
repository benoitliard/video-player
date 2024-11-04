#!/bin/bash

# Couleurs pour les messages
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Fonction d'aide
show_help() {
    echo -e "${GREEN}Usage: $0 [options] input_file${NC}"
    echo
    echo "Options:"
    echo "  -r, --resolution   Résolution cible (4k, 2k, 1080p) [défaut: 4k]"
    echo "  -o, --output      Fichier de sortie [défaut: input_converted.mp4]"
    echo "  -h, --help        Affiche cette aide"
    echo
    echo "Exemples:"
    echo "  $0 -r 4k input.mp4"
    echo "  $0 -r 2k -o output.mp4 input.mp4"
}

# Vérifier si ffmpeg est installé
if ! command -v ffmpeg &> /dev/null; then
    echo -e "${RED}Erreur: ffmpeg n'est pas installé${NC}"
    echo "Installez-le avec: sudo apt-get install ffmpeg"
    exit 1
fi

# Paramètres par défaut
RESOLUTION="4k"
OUTPUT=""

# Traiter les arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -r|--resolution)
            RESOLUTION="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            INPUT="$1"
            shift
            ;;
    esac
done

# Vérifier si un fichier d'entrée est spécifié
if [ -z "$INPUT" ]; then
    echo -e "${RED}Erreur: Aucun fichier d'entrée spécifié${NC}"
    show_help
    exit 1
fi

# Définir le fichier de sortie si non spécifié
if [ -z "$OUTPUT" ]; then
    OUTPUT="${INPUT%.*}_converted.mp4"
fi

# Paramètres de conversion selon la résolution
case $RESOLUTION in
    4k)
        echo -e "${YELLOW}Configuration pour 4K HEVC optimisée pour RPi 5${NC}"
        PARAMS="-c:v libx265 -preset medium -crf 23 -vf scale=3840:2160 -c:a aac -b:a 192k -tag:v hvc1"
        ;;
    2k)
        echo -e "${YELLOW}Configuration pour 2K H.264${NC}"
        PARAMS="-c:v libx264 -preset medium -crf 23 -vf scale=2560:1440 -c:a aac -b:a 128k"
        ;;
    1080p)
        echo -e "${YELLOW}Configuration pour 1080p H.264${NC}"
        PARAMS="-c:v libx264 -preset medium -crf 23 -vf scale=1920:1080 -c:a aac -b:a 128k"
        ;;
    *)
        echo -e "${RED}Erreur: Résolution non valide. Utilisez 4k, 2k ou 1080p${NC}"
        exit 1
        ;;
esac

# Conversion
echo -e "${GREEN}Début de la conversion...${NC}"
echo "De: $INPUT"
echo "Vers: $OUTPUT"
echo "Avec les paramètres: $PARAMS"

ffmpeg -i "$INPUT" $PARAMS "$OUTPUT"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Conversion terminée avec succès!${NC}"
    echo "Fichier de sortie: $OUTPUT"
else
    echo -e "${RED}Erreur lors de la conversion${NC}"
    exit 1
fi 