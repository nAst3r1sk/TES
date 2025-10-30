#pragma once

#include "types.h"
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <map>
#include <vector>

// 前向声明gateway类型
namespace trading {
    class IExchangeWebSocket;
    class BinanceWebSocket;
    struct ExchangeConfig;
    struct AccountUpdate;
    struct PositionUpdate;
    struct OrderUpdate;
    struct AccountBalanceResponse;
    struct AccountInfoResponse;
    struct OrderResponse;
    enum class ConnectionStatus;
    struct OrderRequest;
    struct CancelOrderRequest;
    struct Position;
}

namespace tes {
namespace execution {

/**
 * @brief Gateway适配器类
 * 
 * 将新的gateway接口适配到现有的TES系统中，替换原有的BinanceTradingInterface
 */
class GatewayAdapter {
public:
    /**
     * @brief 配置结构
     */
    struct Config {
        std::string api_key;
        std::string api_secret;
        bool testnet = true;
        std::string trading_type = "futures";
        bool enable_websocket = true;
        uint32_t sync_interval_ms = 1000;
        uint32_t timeout_ms = 5000;
        
        Config() = default;
    };

    // 回调函数类型
    using OrderUpdateCallback = std::function<void(const Order&)>;
    using PositionUpdateCallback = std::function<void(const Position&)>;
    using TradeExecutionCallback = std::function<void(const Trade&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    // 单例模式
    static GatewayAdapter& getInstance();
    
    ~GatewayAdapter();

    // 生命周期管理
    bool initialize(const Config& config);
    bool start();
    void stop();
    void cleanup();
    bool is_running() const;
    bool is_connected() const;

    // 订单操作
    std::string submit_order(const Order& order);
    bool cancel_order(const std::string& order_id);
    std::shared_ptr<Order> get_order(const std::string& order_id) const;
    std::vector<std::shared_ptr<Order>> get_active_orders() const;

    // 持仓查询
    std::vector<Position> get_positions(const std::string& symbol = "") const;
    std::shared_ptr<Position> get_position(const std::string& symbol) const;

    // 账户信息
    double get_account_balance(const std::string& asset = "USDT") const;
    double get_available_balance(const std::string& asset = "USDT") const;

    // WebSocket查询
    bool query_account_balance_ws();
    bool query_account_status_ws();

    // 回调设置
    void set_order_update_callback(OrderUpdateCallback callback);
    void set_position_update_callback(PositionUpdateCallback callback);
    void set_trade_execution_callback(TradeExecutionCallback callback);
    void set_error_callback(ErrorCallback callback);

    // 错误处理
    std::string get_last_error() const;

private:
    GatewayAdapter();
    GatewayAdapter(const GatewayAdapter&) = delete;
    GatewayAdapter& operator=(const GatewayAdapter&) = delete;

    // 内部方法
    void setup_callbacks();
    void on_account_update(const trading::AccountUpdate& update);
    void on_position_update(const trading::PositionUpdate& update);
    void on_order_update(const trading::OrderUpdate& update);
    void on_connection_status(trading::ConnectionStatus status);
    void on_error(const std::string& error);

    // 数据转换
    Order convert_gateway_order_to_tes(const trading::OrderUpdate& gateway_order) const;
    tes::execution::Position convert_gateway_position_to_tes(const trading::Position& gateway_position) const;
    // trading::OrderRequest convert_tes_order_to_gateway(const Order& tes_order) const; // 移除此方法

    // 成员变量
    mutable std::mutex mutex_;
    Config config_;
    std::string last_error_;
    std::atomic<bool> initialized_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;

    // Gateway组件
    std::unique_ptr<trading::IExchangeWebSocket> websocket_client_;

    // 回调函数
    OrderUpdateCallback order_update_callback_;
    PositionUpdateCallback position_update_callback_;
    TradeExecutionCallback trade_execution_callback_;
    ErrorCallback error_callback_;

    // 缓存数据
    mutable std::mutex cache_mutex_;
    std::vector<Position> cached_positions_;
    std::map<std::string, double> cached_balances_;
};

} // namespace execution
} // namespace tes