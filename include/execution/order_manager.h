#pragma once

#include "types.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>
#include <queue>
#include <thread>

namespace tes {
namespace execution {

// 订单事件回调类型
using OrderEventCallback = std::function<void(const Order&)>;
using TradeEventCallback = std::function<void(const Trade&)>;

// 交易所适配器接口
class ExchangeAdapter {
public:
    virtual ~ExchangeAdapter() = default;
    virtual std::string submit_order_to_exchange(const Order& order) = 0;
    virtual bool cancel_order_on_exchange(const std::string& order_id) = 0;
    virtual bool modify_order_on_exchange(const std::string& order_id, double new_quantity, double new_price) = 0;
    virtual std::shared_ptr<Order> query_order_from_exchange(const std::string& order_id) = 0;
    virtual std::vector<std::shared_ptr<Order>> query_active_orders_from_exchange() = 0;
};

class OrderManager {
public:
    // 配置结构
    struct Config {
        uint32_t max_pending_orders;       // 最大待处理订单数
        uint32_t order_timeout_seconds;    // 订单超时时间（秒）
        bool enable_order_validation;      // 启用订单验证
        bool enable_duplicate_check;       // 启用重复检查
        uint32_t cleanup_interval_seconds; // 清理间隔（秒）
        
        Config() : max_pending_orders(1000), order_timeout_seconds(300),
                   enable_order_validation(true), enable_duplicate_check(true),
                   cleanup_interval_seconds(60) {}
    };
    
    // 统计信息
    struct Statistics {
        uint64_t total_orders_created;
        uint64_t total_orders_submitted;
        uint64_t total_orders_filled;
        uint64_t total_orders_cancelled;
        uint64_t total_orders_rejected;
        uint64_t total_trades;
        uint32_t active_orders;
        double average_fill_time;
        std::chrono::high_resolution_clock::time_point last_order_time;
        std::chrono::high_resolution_clock::time_point last_trade_time;
    };
    
    OrderManager();
    ~OrderManager();
    
    // 生命周期管理
    bool initialize();
    bool start();
    void stop();
    void cleanup();
    bool is_running() const;
    
    // 订单管理
    std::string create_order(const Order& order);
    bool submit_order(const std::string& order_id);
    bool cancel_order(const std::string& order_id);
    bool modify_order(const std::string& order_id, double new_quantity, double new_price);
    
    // 外部订单同步（用于交易所适配器）
    void sync_order_from_exchange(const Order& order);
    void sync_orders_from_exchange(const std::vector<Order>& orders);
    bool remove_order(const std::string& order_id);
    
    // 交易所适配器管理
    void set_exchange_adapter(std::shared_ptr<ExchangeAdapter> adapter);
    std::shared_ptr<ExchangeAdapter> get_exchange_adapter() const;
    bool has_exchange_adapter() const;
    
    // 订单查询
    std::shared_ptr<Order> get_order(const std::string& order_id) const;
    std::vector<std::shared_ptr<Order>> get_orders_by_strategy(const std::string& strategy_id) const;
    std::vector<std::shared_ptr<Order>> get_orders_by_instrument(const std::string& instrument_id) const;
    std::vector<std::shared_ptr<Order>> get_active_orders() const;
    std::vector<std::shared_ptr<Order>> get_all_orders() const;
    
    // 成交处理
    void process_trade(const Trade& trade);
    std::vector<Trade> get_trades_by_order(const std::string& order_id) const;
    std::vector<Trade> get_trades_by_strategy(const std::string& strategy_id) const;
    
    // 事件回调
    void set_order_event_callback(OrderEventCallback callback);
    void set_trade_event_callback(TradeEventCallback callback);
    
    // 配置和统计
    void set_config(const Config& config);
    Config get_config() const;
    Statistics get_statistics() const;
    
    // 风险检查接口
    bool validate_order(const Order& order) const;
    bool check_duplicate_order(const Order& order) const;
    
    // 订单同步和状态管理
    void force_update_order_status(const std::string& order_id, OrderStatus status, const std::string& error_message = "");
    void batch_update_orders(const std::vector<Order>& orders);
    void clear_expired_orders(std::chrono::seconds max_age = std::chrono::seconds(3600));
    
private:
    // 内部方法
    std::string generate_order_id();
    void update_order_status(const std::string& order_id, OrderStatus status, const std::string& error_message = "");
    void notify_order_event(const Order& order);
    void notify_trade_event(const Trade& trade);
    void cleanup_expired_orders();
    void update_statistics();
    
    // 成员变量
    mutable std::mutex orders_mutex_;
    mutable std::mutex trades_mutex_;
    mutable std::mutex config_mutex_;
    mutable std::mutex statistics_mutex_;
    
    std::unordered_map<std::string, std::shared_ptr<Order>> orders_;
    std::unordered_map<std::string, std::vector<Trade>> trades_by_order_;
    
    OrderEventCallback order_callback_;
    TradeEventCallback trade_callback_;
    
    Config config_;
    Statistics statistics_;
    
    // 交易所适配器
    std::shared_ptr<ExchangeAdapter> exchange_adapter_;
    mutable std::mutex adapter_mutex_;
    
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    std::atomic<uint64_t> order_sequence_;
    
    // 清理线程相关
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_;
    void cleanup_worker();
};

} // namespace execution
} // namespace tes