#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include "data_structures.h"

namespace trading {

/**
 * @brief 交易所WebSocket连接状态
 */
enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR
};

/**
 * @brief 交易所WebSocket接口
 */
class IExchangeWebSocket {
public:
    virtual ~IExchangeWebSocket() = default;

    // 连接管理
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual ConnectionStatus getStatus() const = 0;

    // 用户数据流管理
    virtual bool startUserDataStream() = 0;
    virtual bool keepaliveUserDataStream() = 0;
    virtual bool closeUserDataStream() = 0;
    virtual bool subscribeUserDataStream() = 0;
    virtual bool unsubscribeUserDataStream() = 0;

    // WebSocket API会话管理
    virtual bool sessionLogon() = 0;

    // 回调函数设置
    virtual void setAccountUpdateCallback(std::function<void(const AccountUpdate&)> callback) = 0;
    virtual void setPositionUpdateCallback(std::function<void(const PositionUpdate&)> callback) = 0;
    virtual void setOrderUpdateCallback(std::function<void(const OrderUpdate&)> callback) = 0;
    virtual void setConnectionStatusCallback(std::function<void(ConnectionStatus)> callback) = 0;
    virtual void setErrorCallback(std::function<void(const std::string&)> callback) = 0;
    virtual void setAccountBalanceCallback(std::function<void(const AccountBalanceResponse&)> callback) = 0;
    virtual void setAccountInfoCallback(std::function<void(const AccountInfoResponse&)> callback) = 0;
    virtual void setOrderResponseCallback(std::function<void(const OrderResponse&)> callback) = 0;
    virtual void setDepthUpdateCallback(std::function<void(const DepthUpdate&)> callback) = 0;
    virtual void setTradeLiteCallback(std::function<void(const TradeLite&)> callback) = 0;

    // 配置管理
    virtual void setApiCredentials(const std::string& apiKey, const std::string& apiSecret) = 0;
    virtual void setTimeout(int timeoutMs) = 0;
    virtual void setReconnectInterval(int intervalMs) = 0;

    // API请求方法
    virtual void requestAccountBalance(const std::string& requestId = "") = 0;
    virtual void requestAccountInfo(const std::string& requestId = "") = 0;
    virtual void requestPositionInfo(const std::string& requestId = "") = 0;
    
    // 订单操作方法
    virtual void placeOrder(const OrderRequest& orderRequest, const std::string& requestId = "") = 0;
    virtual void cancelOrder(const CancelOrderRequest& cancelRequest, const std::string& requestId = "") = 0;
    
    // 市场数据订阅方法
    virtual bool subscribeDepthUpdate(const std::string& symbol, int levels = 20, int updateSpeed = 100) = 0;
    virtual bool unsubscribeDepthUpdate(const std::string& symbol) = 0;
    virtual bool subscribeTradeLite() = 0;
    virtual bool unsubscribeTradeLite() = 0;

    // 获取交易所名称
    virtual std::string getExchangeName() const = 0;
};

/**
 * @brief 交易所工厂接口
 */
class IExchangeFactory {
public:
    virtual ~IExchangeFactory() = default;
    virtual std::unique_ptr<IExchangeWebSocket> createWebSocketClient(const std::string& exchangeName) = 0;
    virtual std::vector<std::string> getSupportedExchanges() const = 0;
};

} // namespace trading