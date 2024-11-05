#pragma once
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio.hpp>
#include <json/json.h>
#include <functional>
#include <string>
#include <map>

class VideoPlayer;

class WebSocketController {
public:
    WebSocketController(VideoPlayer* player);
    virtual ~WebSocketController() = default;

    bool initialize(const std::string& address = "0.0.0.0", uint16_t port = 9002);
    void start();
    void stop();

private:
    using Server = websocketpp::server<websocketpp::config::asio>;
    using ConnectionHdl = websocketpp::connection_hdl;
    using MessagePtr = Server::message_ptr;

    void onOpen(ConnectionHdl hdl);
    void onClose(ConnectionHdl hdl);
    void onMessage(ConnectionHdl hdl, MessagePtr msg);
    bool validateAuth(const std::string& token);

    void handlePlayCommand();
    void handlePauseCommand();
    void handleStopCommand();
    void handleResetCommand();
    void handleVolumeCommand(int volume);

    Server server;
    VideoPlayer* player;
    std::string authToken;
    std::map<void*, bool> connections;
    bool isRunning;
}; 