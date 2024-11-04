# Lecteur Vidéo 4K pour Raspberry Pi 5

Un lecteur vidéo haute performance optimisé pour le Raspberry Pi 5, capable de lire des vidéos 4K avec un contrôle via WebSocket.

## Caractéristiques

- Lecture vidéo 4K optimisée avec accélération matérielle
- Interface de contrôle via WebSocket
- Support du H.265/HEVC avec accélération matérielle sur RPi 5
- Support multi-plateforme (RPi 5 et macOS)
- Métriques de performance en temps réel
- Synchronisation audio/vidéo optimisée
- Décodage multi-thread
- Gestion intelligente des buffers

## Prérequis

### Matériel
- Raspberry Pi 5
- Minimum 4GB RAM (8GB recommandé pour la 4K)
- Carte SD classe 10 ou supérieure
- Écran compatible 4K (pour la résolution maximale)

### Format Vidéo Supportés
- Conteneurs : MP4, MKV
- Codecs : 
  - H.265/HEVC (jusqu'à 4K@60fps) - **Recommandé pour RPi 5**
  - H.264/AVC (jusqu'à 4K@30fps) - *Décodage logiciel uniquement sur RPi 5*
  - VP9 (jusqu'à 4K@30fps)
- Résolutions recommandées :
  - 4K (3840x2160)
  - 2K (2560x1440)
  - 1080p (1920x1080)
- Débit binaire maximum recommandé :
  - 4K : 60 Mbps
  - 2K : 40 Mbps
  - 1080p : 20 Mbps

## Installation

### Sur Raspberry Pi 5