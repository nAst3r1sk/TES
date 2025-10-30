#pragma once

#include "types.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace tes {
namespace execution {

// 订单状态机状态
enum class OrderState {
    CREATED,            // 已创建（本地）
    PENDING_SUBMIT,     // 等待提交
    SUBMITTED,          // 已提交到交易所
    ACKNOWLEDGED,       // 交易所确认
    PARTIALLY_FILLED,   // 部分成交
    FILLED,             // 完全成交
    PENDING_CANCEL,     // 等待取消
    CANCELLED,          // 已取消
    REJECTED,           // 被拒绝
    EXPIRED,            // 已过期
    ERROR               // 错误状态
};

// 订单事件类型
enum class OrderEvent {
    CREATE,             // 创建订单
    SUBMIT,             // 提交订单
    ACKNOWLEDGE,        // 交易所确认
    PARTIAL_FILL,       // 部分成交
    FILL,               // 完全成交
    CANCEL_REQUEST,     // 取消请求
    CANCEL_CONFIRM,     // 取消确认
    REJECT,             // 拒绝
    EXPIRE,             // 过期
    ERROR_OCCURRED      // 发生错误
};

// 订单状态信息
struct OrderStateInfo {
    std::string order_id;
    std::string client_order_id;
    std::string exchange_order_id;
    OrderState current_state;
    OrderState previous_state;
    std::chrono::high_resolution_clock::time_point state_change_time;
    std::chrono::high_resolution_clock::time_point create_time;
    std::chrono::high_resolution_clock::time_point last_update_time;
    
    // 订单基本信息
    std::string instrument_id;
    std::string strategy_id;
    OrderSide side;
    double quantity;
    double price;
    double filled_quantity;
    double average_price;
    
    // 状态统计
    uint32_t retry_count;
    uint32_t state_change_count;
    std::string last_error_message;
    
    // 超时设置
    std::chrono::milliseconds submit_timeout;
    std::chrono::milliseconds cancel_timeout;
    
    OrderStateInfo() : current_state(OrderState::CREATED), 
                      previous_state(OrderState::CREATED),
                      side(OrderSide::BUY), quantity(0.0), price(0.0),
                      filled_quantity(0.0), average_price(0.0),
                      retry_count(0), state_change_count(0),
                      submit_timeout(std::chrono::milliseconds(5000)),
                      cancel_timeout(std::chrono::milliseconds(3000)) {
        auto now = std::chrono::high_resolution_clock::now();
        state_change_time = now;
        create_time = now;
        last_update_time = now;
    }
};

// 状态变化回调
using StateChangeCallback = std::function<void(const OrderStateInfo&, OrderState old_state, OrderState new_state)>;
using OrderTimeoutCallback = std::function<void(const OrderStateInfo&)>;

// 订单状态机
class OrderStateMachine {
public:
    struct Config {
        std::chrono::milliseconds default_submit_timeout;
        std::chrono::milliseconds default_cancel_timeout;
        std::chrono::milliseconds cleanup_interval;
        uint32_t max_retry_count;
        bool enable_auto_cleanup;
        std::chrono::hours order_retention_time;
        
        Config() : default_submit_timeout(std::chrono::milliseconds(5000)),
                  default_cancel_timeout(std::chrono::milliseconds(3000)),
                  cleanup_interval(std::chrono::milliseconds(1000)),
                  max_retry_count(3),
                  enable_auto_cleanup(true),
                  order_retention_time(std::chrono::hours(24)) {}
    };
    
    OrderStateMachine();
    ~OrderStateMachine();
    
    // 生命周期管理
    bool initialize(const Config& config = Config{});
    bool start();
    void stop();
    void cleanup();
    bool is_running() const;
    
    // 订单状态管理
    std::string create_order(const Order& order);
    bool process_event(const std::string& order_id, OrderEvent event, const std::string& exchange_order_id = "");
    bool update_fill_info(const std::string& order_id, double filled_qty, double avg_price);
    bool set_error(const std::string& order_id, const std::string& error_message);
    
    // 查询接口
    std::shared_ptr<OrderStateInfo> get_order_state(const std::string& order_id) const;
    std::vector<std::shared_ptr<OrderStateInfo>> get_orders_by_state(OrderState state) const;
    std::vector<std::shared_ptr<OrderStateInfo>> get_active_orders() const;
    std::vector<std::shared_ptr<OrderStateInfo>> get_orders_by_instrument(const std::string& instrument_id) const;
    
    // 重复检查
    bool has_pending_order(const std::string& instrument_id, OrderSide side, double quantity, double price, double tolerance = 1e-6) const;
    bool has_recent_executed_order(const std::string& instrument_id, OrderSide side, double quantity, double price, std::chrono::milliseconds time_window = std::chrono::milliseconds(30000), double tolerance = 1e-6) const;
    std::vector<std::string> get_pending_order_ids(const std::string& instrument_id) const;
    
    // 超时管理
    std::vector<std::string> get_timeout_orders() const;
    bool extend_timeout(const std::string& order_id, std::chrono::milliseconds additional_time);
    
    // 回调设置
    void set_state_change_callback(StateChangeCallback callback);
    void set_timeout_callback(OrderTimeoutCallback callback);
    
    // 统计信息
    struct Statistics {
        uint64_t total_orders_created;
        uint64_t orders_by_state[static_cast<int>(OrderState::ERROR) + 1];
        uint64_t state_transitions;
        uint64_t timeout_events;
        uint64_t error_events;
        double average_fill_time_ms;
        std::chrono::high_resolution_clock::time_point last_activity_time;
    };
    
    Statistics get_statistics() const;
    void reset_statistics();
    
    // 配置管理
    void set_config(const Config& config);
    Config get_config() const;
    
private:
    // 内部方法
    bool is_valid_transition(OrderState from, OrderState to) const;
    bool change_state(const std::string& order_id, OrderState new_state, const std::string& reason = "");
    std::string generate_order_id();
    void cleanup_expired_orders();
    void check_timeouts();
    void update_statistics();
    void cleanup_worker();
    
    // 成员变量
    mutable std::mutex orders_mutex_;
    mutable std::mutex config_mutex_;
    mutable std::mutex statistics_mutex_;
    
    std::unordered_map<std::string, std::shared_ptr<OrderStateInfo>> orders_;
    
    StateChangeCallback state_change_callback_;
    OrderTimeoutCallback timeout_callback_;
    
    Config config_;
    Statistics statistics_;
    
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    std::atomic<uint64_t> order_sequence_;
    
    // 清理线程
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_;
};

// 辅助函数
std::string order_state_to_string(OrderState state);
std::string order_event_to_string(OrderEvent event);
bool is_terminal_state(OrderState state);
bool is_active_state(OrderState state);

} // namespace execution
} // namespace tes