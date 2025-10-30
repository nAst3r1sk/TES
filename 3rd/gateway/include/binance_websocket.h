#pragma once

#include "exchange_interface.h"
#include "config_manager.h"
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXHttpClient.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <queue>

namespace trading {

/**
 * @brief 币安WebSocket API客户端实现
 */
class BinanceWebSocket : public IExchangeWebSocket {
public:
    explicit BinanceWebSocket(const ExchangeConfig& config);
    ~BinanceWebSocket() override;

    // IExchangeWebSocket接口实现
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    ConnectionStatus getStatus() const override;

    bool startUserDataStream() override;
    bool keepaliveUserDataStream() override;
    bool closeUserDataStream() override;

    void setAccountUpdateCallback(std::function<void(const AccountUpdate&)> callback) override;
    void setPositionUpdateCallback(std::function<void(const PositionUpdate&)> callback) override;
    void setOrderUpdateCallback(std::function<void(const OrderUpdate&)> callback) override;
    void setConnectionStatusCallback(std::function<void(ConnectionStatus)> callback) override;
    void setErrorCallback(std::function<void(const std::string&)> callback) override;

    void setApiCredentials(const std::string& apiKey, const std::string& apiSecret) override;
    void setTimeout(int timeoutMs) override;
    void setReconnectInterval(int intervalMs) override;

    std::string getExchangeName() const override { return "binance"; }

    // WebSocket API账户查询方法
    void requestAccountBalance(const std::string& requestId = "") override;
    void requestAccountInfo(const std::string& requestId = "") override;
    void requestPositionInfo(const std::string& requestId = "") override;
    void setAccountBalanceCallback(std::function<void(const AccountBalanceResponse&)> callback) override;
    void setAccountInfoCallback(std::function<void(const AccountInfoResponse&)> callback) override;
    void setOrderResponseCallback(std::function<void(const OrderResponse&)> callback) override;
    void setDepthUpdateCallback(std::function<void(const DepthUpdate&)> callback) override;
    void setTradeLiteCallback(std::function<void(const TradeLite&)> callback) override;
    
    // 订单操作方法
    void placeOrder(const OrderRequest& orderRequest, const std::string& requestId = "") override;
    void cancelOrder(const CancelOrderRequest& cancelRequest, const std::string& requestId = "") override;
    
    // 市场数据订阅方法
    bool subscribeDepthUpdate(const std::string& symbol, int levels = 20, int updateSpeed = 100) override;
    bool unsubscribeDepthUpdate(const std::string& symbol) override;
    bool subscribeTradeLite() override;
    bool unsubscribeTradeLite() override;
    
    // WebSocket API会话管理
    bool sessionLogon();
    bool sessionLogout();
    bool performSessionLogon();  // 执行实际的session.logon请求
    bool sessionStatus();        // 检查会话状态

private:
    // WebSocket事件处理
    void onWebSocketMessage(const ix::WebSocketMessagePtr& msg);
    void onWebSocketApiMessage(const ix::WebSocketMessagePtr& msg);  // WebSocket API消息处理
    void onWebSocketOpen();
    void onWebSocketClose();
    void onWebSocketError(const std::string& error);

    // 消息解析
    void parseMessage(const std::string& message);
    void parseWebSocketApiMessage(const std::string& message);  // WebSocket API消息解析
    void parseAccountUpdate(yyjson_val* root);
    void parseOrderUpdate(yyjson_val* root);
    void parseAccountBalanceResponse(yyjson_val* root);  // 新增：解析账户余额响应
    void parseAccountInfoResponse(yyjson_val* root);     // 新增：解析账户信息响应
    void parsePositionInfoResponse(yyjson_val* root);    // 新增：解析持仓信息响应
    void parseDepthUpdate(yyjson_val* root);             // 新增：解析深度更新
    void parseTradeLite(yyjson_val* root);               // 新增：解析交易数据
    void parseOrderResponse(yyjson_val* root);           // 新增：解析订单响应
    
    // HTTP API调用 (旧方法，已弃用)
    bool createListenKey();
    bool extendListenKey();
    bool deleteListenKey();
    std::string makeHttpRequest(const std::string& method, const std::string& endpoint, 
                               const std::string& params = "");
    std::string generateSignature(const std::string& queryString) const;
    std::string generateHmacSha256Signature(const std::string& queryString) const;  // HMAC-SHA256签名函数
    std::string generateEd25519Signature(const std::string& message) const;  // Ed25519签名函数
    
    // 新的WebSocket API方法
    bool createListenKeyViaWebSocket();  // 通过WebSocket API创建listenKey
    bool keepaliveListenKeyViaWebSocket();  // 通过WebSocket API保持listenKey活跃
    bool closeListenKeyViaWebSocket();  // 通过WebSocket API关闭listenKey
    bool subscribeUserDataStream();
    bool unsubscribeUserDataStream();
    void sendWebSocketRequest(const std::string& method, const std::string& params = "");
    void sendWebSocketApiRequest(const std::string& method, const std::string& params = "");
    std::string generateWebSocketSignature(const std::string& params) const;
    
    // 动态构建市场数据流
    std::string buildMarketDataStreams() const;
    
    // 心跳和重连管理
    void startHeartbeat();
    void stopHeartbeat();
    void heartbeatLoop();
    void attemptReconnect();
    
    // 状态管理
    void setConnectionStatus(ConnectionStatus status);
    
    // 配置
    ExchangeConfig config_;
    std::string apiKey_;
    std::string apiSecret_;
    int timeoutMs_;
    int reconnectIntervalMs_;
    
    // WebSocket连接
    std::unique_ptr<ix::WebSocket> webSocket_;
    std::unique_ptr<ix::WebSocket> wsApiSocket_;  // 专用于WebSocket API的连接
    std::string listenKey_;  // 旧方法使用，已弃用
    std::string wsUrl_;
    std::string wsApiUrl_;   // 新的WebSocket API URL
    std::string userDataStreamBaseUrl_;  // 用户数据流基础URL
    
    // 新的WebSocket API状态
    std::atomic<bool> sessionAuthenticated_;
    std::atomic<bool> wsApiConnected_;  // WebSocket API连接状态
    std::string sessionId_;
    int subscriptionId_;
    int requestId_;  // WebSocket API请求ID计数器
    
    // 状态
    std::atomic<ConnectionStatus> status_;
    std::atomic<bool> running_;
    std::atomic<bool> userDataStreamActive_;
    
    // 线程管理
    std::thread heartbeatThread_;
    std::mutex mutex_;
    
    // 回调函数
    std::function<void(const AccountUpdate&)> accountUpdateCallback_;
    std::function<void(const PositionUpdate&)> positionUpdateCallback_;
    std::function<void(const OrderUpdate&)> orderUpdateCallback_;
    std::function<void(ConnectionStatus)> connectionStatusCallback_;
    std::function<void(const std::string&)> errorCallback_;
    std::function<void(const AccountBalanceResponse&)> accountBalanceCallback_;  // 新增
    std::function<void(const AccountInfoResponse&)> accountInfoCallback_;        // 新增
    std::function<void(const OrderResponse&)> orderResponseCallback_;           // 新增
    std::function<void(const DepthUpdate&)> depthUpdateCallback_;               // 新增
    std::function<void(const TradeLite&)> tradeLiteCallback_;                   // 新增
    
    // 心跳管理
    std::chrono::steady_clock::time_point lastHeartbeat_;
    static constexpr int HEARTBEAT_INTERVAL_MS = 30000; // 30秒
    static constexpr int LISTEN_KEY_REFRESH_INTERVAL_MS = 1800000; // 30分钟
    
    // HTTP客户端
    std::unique_ptr<ix::HttpClient> httpClient_;
    std::string baseApiUrl_;
};

/**
 * @brief 币安交易所工厂
 */
class BinanceFactory : public IExchangeFactory {
public:
    std::unique_ptr<IExchangeWebSocket> createWebSocketClient(const std::string& exchangeName) override;
    std::vector<std::string> getSupportedExchanges() const override;
};

} // namespace trading