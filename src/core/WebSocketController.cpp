#include "WebSocketController.h"
#include "../VideoPlayer.h"
#include "../utils/Logger.h"
#include <openssl/sha.h>
#include <iomanip>
#include <random>

WebSocketController::WebSocketController(VideoPlayer* p) 
    : player(p), isRunning(false) {
    // Utiliser une méthode plus simple pour générer le token
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for(int i = 0; i < 32; i++) {
        ss << std::hex << dis(gen);
    }
    authToken = ss.str();
    
    Logger::logInfo("WebSocket auth token generated");
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
        Logger::logInfo("WebSocket server listening on port " + std::to_string(port));
        return true;
    } catch (const std::exception& e) {
        Logger::logError("WebSocket initialization failed: " + std::string(e.what()));
        return false;
    }
}

void WebSocketController::start() {
    if (!isRunning) {
        isRunning = true;
        server.start_accept();
        server.run();
    }
}

void WebSocketController::stop() {
    if (isRunning) {
        isRunning = false;
        server.stop();
    }
}

void WebSocketController::onOpen(ConnectionHdl hdl) {
    Logger::logInfo("WebSocket connection opened");
    connections[hdl.lock().get()] = true;
}

void WebSocketController::onClose(ConnectionHdl hdl) {
    Logger::logInfo("WebSocket connection closed");
    connections.erase(hdl.lock().get());
}

void WebSocketController::onMessage(ConnectionHdl hdl, MessagePtr msg) {
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(msg->get_payload(), root)) {
            Logger::logError("Failed to parse WebSocket message");
            return;
        }

        // Vérifier l'authentification
        if (!root.isMember("token") || root["token"].asString() != authToken) {
            Logger::logError("Invalid WebSocket authentication");
            server.close(hdl, websocketpp::close::status::policy_violation, 
                        "Invalid authentication");
            return;
        }

        std::string command = root["command"].asString();
        Logger::logInfo("Received command: " + command);

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