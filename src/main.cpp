#include "VideoPlayer.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>" << std::endl;
        return 1;
    }

    VideoPlayer player;
    if (!player.initialize(argv[1])) {
        std::cerr << "Failed to initialize video player" << std::endl;
        return 1;
    }

    player.run();
    return 0;
} 