#include "VideoPlayer.h"
//#include "WebSocketController.h"  // Commenté temporairement
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>" << std::endl;
        return 1;
    }

    VideoPlayer player;
    //WebSocketController wsController(&player);  // Commenté temporairement

    if (!player.initialize(argv[1])) {
        return 1;
    }

    /*if (!wsController.initialize()) {  // Commenté temporairement
        Logger::logError("Failed to initialize WebSocket server");
        return 1;
    }*/

    player.run();

    return 0;
} 