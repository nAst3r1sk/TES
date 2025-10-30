#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <cstdint>

namespace tes {
namespace execution {

// 订单状态
enum class OrderStatus {
    PENDING,        // 待处理
    SUBMITTED,      // 已提交
    PARTIALLY_FILLED, // 部分成交
    FILLED,         // 完全成交
    CANCELLED,      // 已取消
    REJECTED,       // 已拒绝
    ERROR           // 错误状态
};

// 订单类型
enum class OrderType {
    MARKET,         // 市价单
    LIMIT,          // 限价单
    STOP,           // 止损单
    STOP_LIMIT      // 止损限价单
};

// 订单方向
enum class OrderSide {
    BUY,            // 买入
    SELL            // 卖出
};

// 时间有效性
enum class TimeInForce {
    DAY,            // 当日有效
    GTC,            // 撤销前有效
    IOC,            // 立即成交或取消
    FOK             // 全部成交或取消
};

// 订单信息
struct Order {
    std::string order_id;
    std::string client_order_id;
    std::string instrument_id;
    std::string strategy_id;
    OrderType type;
    OrderSide side;
    TimeInForce time_in_force;
    double quantity;
    double price;
    double filled_quantity;
    double average_price;
    OrderStatus status;
    std::chrono::high_resolution_clock::time_point create_time;
    std::chrono::high_resolution_clock::time_point update_time;
    std::chrono::high_resolution_clock::time_point timestamp;
    std::string error_message;
    
    Order() : type(OrderType::MARKET), side(OrderSide::BUY), time_in_force(TimeInForce::GTC),
              quantity(0.0), price(0.0), filled_quantity(0.0), average_price(0.0),
              status(OrderStatus::PENDING) {
        create_time = std::chrono::high_resolution_clock::now();
        update_time = create_time;
        timestamp = create_time;
    }
};

// 成交信息
struct Trade {
    std::string trade_id;
    std::string order_id;
    std::string instrument_id;
    OrderSide side;
    double quantity;
    double price;
    std::chrono::high_resolution_clock::time_point trade_time;
    double commission;
    
    Trade() : side(OrderSide::BUY), quantity(0.0), price(0.0), commission(0.0) {
        trade_time = std::chrono::high_resolution_clock::now();
    }
};

// 持仓信息
struct Position {
    std::string instrument_id;
    std::string strategy_id;
    double long_quantity;
    double short_quantity;
    double net_quantity;
    double average_cost;
    double unrealized_pnl;
    double realized_pnl;
    std::chrono::high_resolution_clock::time_point update_time;
    
    Position() : long_quantity(0.0), short_quantity(0.0), net_quantity(0.0),
                 average_cost(0.0), unrealized_pnl(0.0), realized_pnl(0.0) {
        update_time = std::chrono::high_resolution_clock::now();
    }
};

// 风险限制
struct RiskLimits {
    double max_position_size;          // 最大持仓量
    double max_order_size;             // 最大单笔订单量
    double max_daily_loss;             // 最大日损失
    double max_total_exposure;         // 最大总敞口
    uint32_t max_orders_per_second;    // 每秒最大订单数
    uint32_t max_orders_per_minute;    // 每分钟最大订单数
    
    RiskLimits() : max_position_size(1000000.0), max_order_size(100000.0),
                   max_daily_loss(50000.0), max_total_exposure(500000.0),
                   max_orders_per_second(10), max_orders_per_minute(100) {}
};

// TWAP算法参数
struct TWAPParams {
    double total_quantity;             // 总数量
    uint32_t duration_minutes;         // 执行时长（分钟）
    uint32_t slice_count;              // 切片数量
    double participation_rate;         // 参与率
    double price_tolerance;            // 价格容忍度
    bool allow_partial_fill;           // 允许部分成交
    
    TWAPParams() : total_quantity(0.0), duration_minutes(60), slice_count(10),
                   participation_rate(0.1), price_tolerance(0.01),
                   allow_partial_fill(true) {}
};

// 算法状态
enum class AlgorithmStatus {
    IDLE,           // 空闲
    RUNNING,        // 运行中
    PAUSED,         // 暂停
    COMPLETED,      // 完成
    CANCELLED,      // 取消
    ERROR           // 错误
};

// 算法执行信息
struct AlgorithmExecution {
    std::string execution_id;
    std::string strategy_id;
    std::string instrument_id;
    OrderSide side;
    TWAPParams params;
    AlgorithmStatus status;
    double executed_quantity;
    double remaining_quantity;
    double average_price;
    std::vector<std::string> child_orders;
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    std::string error_message;
    
    AlgorithmExecution() : side(OrderSide::BUY), status(AlgorithmStatus::IDLE),
                          executed_quantity(0.0), remaining_quantity(0.0),
                          average_price(0.0) {
        start_time = std::chrono::high_resolution_clock::now();
        end_time = start_time;
    }
};

// 市场数据
struct MarketData {
    std::string instrument_id;
    double bid_price;
    double ask_price;
    double bid_size;
    double ask_size;
    double last_price;
    double volume;
    std::chrono::high_resolution_clock::time_point timestamp;
    
    MarketData() : bid_price(0.0), ask_price(0.0), bid_size(0.0),
                   ask_size(0.0), last_price(0.0), volume(0.0) {
        timestamp = std::chrono::high_resolution_clock::now();
    }
};

// 账户信息
struct AccountInfo {
    std::string account_id;
    double available_cash;
    double total_value;
    double margin_used;
    double margin_available;
    double unrealized_pnl;
    double realized_pnl;
    std::chrono::high_resolution_clock::time_point update_time;
    
    AccountInfo() : available_cash(0.0), total_value(0.0), margin_used(0.0),
                    margin_available(0.0), unrealized_pnl(0.0), realized_pnl(0.0) {
        update_time = std::chrono::high_resolution_clock::now();
    }
};



} // namespace execution
} // namespace tes