#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>
#include <memory>
#include "ixwebsocket/IXWebSocket.h"

namespace tes {
namespace utils {

/**
 * WebSocket客户端实现，基于IXWebSocket库
 * 用于连接到Binance WebSocket API
 */
class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ConnectCallback = std::function<void()>;
    using DisconnectCallback = std::function<void()>;

    WebSocketClient();
    ~WebSocketClient();

    // 禁止拷贝
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    /**
     * 连接到WebSocket服务器
     * @param url WebSocket URL (ws://或wss://)
     * @param headers 额外的HTTP头
     * @return 是否成功启动连接
     */
    bool connect(const std::string& url, const std::map<std::string, std::string>& headers = {});

    /**
     * 断开连接
     */
    void disconnect();

    /**
     * 发送消息
     * @param message 要发送的消息
     * @return 是否成功发送
     */
    bool send(const std::string& message);

    /**
     * 检查连接状态
     */
    bool isConnected() const;

    /**
     * 设置回调函数
     */
    void setMessageCallback(MessageCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setConnectCallback(ConnectCallback callback);
    void setDisconnectCallback(DisconnectCallback callback);

    /**
     * 获取最后的错误信息
     */
    std::string getLastError() const;

private:
    /**
     * 设置IXWebSocket回调函数
     */
    void setupCallbacks();

private:
    std::unique_ptr<ix::WebSocket> websocket_;
    std::atomic<bool> connected_;
    std::string last_error_;
    
    // 回调函数
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    ConnectCallback connect_callback_;
    DisconnectCallback disconnect_callback_;
    
    // 线程同步
    mutable std::mutex mutex_;
};

} // namespace utils
} // namespace tes