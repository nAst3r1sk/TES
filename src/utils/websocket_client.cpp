#include "utils/websocket_client.h"
#include <iostream>
#include <thread>
#include <chrono>
#include "ixwebsocket/IXWebSocket.h"

namespace tes {
namespace utils {

WebSocketClient::WebSocketClient() 
    : websocket_(std::make_unique<ix::WebSocket>()), connected_(false) {
    setupCallbacks();
}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

bool WebSocketClient::connect(const std::string& url, const std::map<std::string, std::string>& headers) {
    if (connected_) {
        return true;
    }

    // 设置URL
    websocket_->setUrl(url);
    
    // 设置额外的HTTP头
    ix::WebSocketHttpHeaders wsHeaders;
    for (const auto& header : headers) {
        wsHeaders[header.first] = header.second;
    }
    websocket_->setExtraHeaders(wsHeaders);
    
    // 启动连接
    websocket_->start();
    
    // 等待连接建立（最多等待5秒）
    int timeout = 50; // 5秒，每次等待100ms
    while (!connected_ && timeout > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        timeout--;
    }
    
    return connected_;
}

void WebSocketClient::disconnect() {
    if (websocket_) {
        connected_ = false;
        websocket_->stop();
    }
}

bool WebSocketClient::send(const std::string& message) {
    if (!connected_ || !websocket_) {
        last_error_ = "Not connected";
        return false;
    }
    
    auto result = websocket_->send(message);
    if (!result.success) {
        if (result.compressionError) {
            last_error_ = "Failed to send message: compression error";
        } else {
            last_error_ = "Failed to send message: unknown error";
        }
        return false;
    }
    
    return true;
}

bool WebSocketClient::isConnected() const {
    return connected_;
}

void WebSocketClient::setMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    message_callback_ = callback;
}

void WebSocketClient::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = callback;
}

void WebSocketClient::setConnectCallback(ConnectCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    connect_callback_ = callback;
}

void WebSocketClient::setDisconnectCallback(DisconnectCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnect_callback_ = callback;
}

std::string WebSocketClient::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void WebSocketClient::setupCallbacks() {
    websocket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Open:
            {
                std::cout << "WebSocket connection opened" << std::endl;
                connected_ = true;
                std::lock_guard<std::mutex> lock(mutex_);
                if (connect_callback_) {
                    connect_callback_();
                }
                break;
            }
            case ix::WebSocketMessageType::Close:
            {
                std::cout << "WebSocket connection closed: " << msg->closeInfo.code 
                         << " " << msg->closeInfo.reason << std::endl;
                connected_ = false;
                std::lock_guard<std::mutex> lock(mutex_);
                if (disconnect_callback_) {
                    disconnect_callback_();
                }
                break;
            }
            case ix::WebSocketMessageType::Message:
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (message_callback_) {
                    message_callback_(msg->str);
                }
                break;
            }
            case ix::WebSocketMessageType::Error:
            {
                std::cout << "WebSocket error: " << msg->errorInfo.reason << std::endl;
                last_error_ = msg->errorInfo.reason;
                std::lock_guard<std::mutex> lock(mutex_);
                if (error_callback_) {
                    error_callback_(msg->errorInfo.reason);
                }
                break;
            }
            case ix::WebSocketMessageType::Ping:
            {
                std::cout << "WebSocket ping received" << std::endl;
                break;
            }
            case ix::WebSocketMessageType::Pong:
            {
                std::cout << "WebSocket pong received" << std::endl;
                break;
            }
            case ix::WebSocketMessageType::Fragment:
            {
                // 处理分片消息
                break;
            }
        }
    });
}

} // namespace utils
} // namespace tes