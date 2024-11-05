#include "WebSocketController.h"
#include "../VideoPlayer.h"
#include "../utils/Logger.h"
#include <openssl/sha.h>
#include <iomanip>

WebSocketController::WebSocketController(VideoPlayer* p) 
    : player(p), isRunning(false) {
    // Générer un token aléatoire au démarrage
    unsigned char hash[SHA256_DIGEST_LENGTH];
    std::string baseToken = std::to_string(std::time(nullptr));
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, baseToken.c_str(), baseToken.length());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    authToken = ss.str();
    
    Logger::logInfo("WebSocket auth token: " + authToken);
}

bool WebSocketController::initialize(const std::string& address, uint16_t port) {
    try {
        server.set_access_channels(websocketpp::log::alevel::none);
        server.clear_access_channels(websocketpp::log::alevel::all);
        
        server.init_asio();

        server.set_open_handler(std::bind(&WebSocketController::onOpen, this, std::placeholders::_1));
        server.set_close_handler(std::bind(&WebSocketController::onClose, this, std::placeholders::_1));
        server.set_message_handler(std::bind(&WebSocketController::onMessage, this, 
            std::placeholders::_1, std::placeholders::_2));

        server.listen(port);
        return true;
    } catch (const std::exception& e) {
        Logger::logError("WebSocket initialization failed: " + std::string(e.what()));
        return false;
    }
}

void WebSocketController::onMessage(ConnectionHdl hdl, MessagePtr msg) {
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(msg->get_payload(), root)) {
            return;
        }

        // Vérifier l'authentification
        if (!root.isMember("token") || !validateAuth(root["token"].asString())) {
            server.close(hdl, websocketpp::close::status::policy_violation, 
                        "Invalid authentication");
            return;
        }

        std::string command = root["command"].asString();
        if (command == "play") handlePlayCommand();
        else if (command == "pause") handlePauseCommand();
        else if (command == "stop") handleStopCommand();
        else if (command == "reset") handleResetCommand();
        else if (command == "volume" && root.isMember("value")) {
            int volume = std::clamp(root["value"].asInt(), 0, 100);
            handleVolumeCommand(volume);
        }
    } catch (const std::exception& e) {
        Logger::logError("WebSocket message handling error: " + std::string(e.what()));
    }
}

void WebSocketController::handlePlayCommand() {
    player->play();
}

void WebSocketController::handlePauseCommand() {
    player->pause();
}

void WebSocketController::handleStopCommand() {
    player->stop();
}

void WebSocketController::handleResetCommand() {
    player->reset();
}

void WebSocketController::handleVolumeCommand(int volume) {
    player->setVolume(volume);
}

bool WebSocketController::validateAuth(const std::string& token) {
    return token == authToken;
} 