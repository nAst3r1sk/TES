#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <cstring>

namespace tes {
namespace shared_memory {

// 时间戳类型定义
using Timestamp = uint64_t;

// 订单ID类型定义
using OrderId = std::string;

// 系统状态
enum class SystemState {
    INITIALIZING = 0,
    RUNNING = 1,
    PAUSED = 2,
    STOPPING = 3,
    STOPPED = 4,
    ERROR = 5
};

// 交易模式
enum class TradingMode {
    SIMULATION = 0,
    PAPER_TRADING = 1,
    LIVE_TRADING = 2
};

// 订单方向
enum class OrderSide {
    BUY = 0,
    SELL = 1
};

// 订单类型
enum class OrderType {
    MARKET = 0,           // 市价单
    LIMIT = 1,            // 限价单
    STOP = 2,             // 止损单
    STOP_LIMIT = 3,       // 止损限价单
    TAKE_PROFIT = 4,      // 止盈单
    TAKE_PROFIT_LIMIT = 5, // 止盈限价单
    ICEBERG = 6,          // 冰山订单
    TWAP = 7,             // 时间加权平均价格订单
    VWAP = 8,             // 成交量加权平均价格订单
    TRAILING_STOP = 9,    // 跟踪止损单
    OCO = 10              // One-Cancels-Other订单
};

// 订单状态
enum class OrderStatus {
    PENDING = 0,
    SUBMITTED = 1,
    PARTIAL_FILLED = 2,
    FILLED = 3,
    CANCELLED = 4,
    REJECTED = 5,
    EXPIRED = 6
};

// 信号类型
enum class SignalType {
    BUY = 0,
    SELL = 1,
    HOLD = 2,
    CLOSE = 3
};

// 信号紧急程度
enum class SignalUrgency {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    URGENT = 3
};

// 时间有效性
enum class TimeInForce {
    DAY = 0,              // 当日有效（加密货币市场不常用）
    GTC = 1,              // Good Till Cancelled - 撤销前有效
    IOC = 2,              // Immediate Or Cancel - 立即成交或撤销
    FOK = 3,              // Fill Or Kill - 全部成交或撤销
    GTD = 4,              // Good Till Date - 指定日期前有效
    POST_ONLY = 5,        // 只做Maker订单
    REDUCE_ONLY = 6       // 只减仓订单（期货合约）
};

// 加密货币市场数据结构
struct MarketData {
    std::string symbol;          // 交易对，如 "BTC/USDT", "ETH/BTC"
    std::string base_asset;      // 基础资产，如 "BTC", "ETH"
    std::string quote_asset;     // 计价资产，如 "USDT", "BTC"
    double price;                // 最新价格
    double volume_24h;           // 24小时成交量
    double bid_price;            // 买一价
    double ask_price;            // 卖一价
    double bid_volume;           // 买一量
    double ask_volume;           // 卖一量
    double high_24h;             // 24小时最高价
    double low_24h;              // 24小时最低价
    double open_24h;             // 24小时开盘价
    double price_change_24h;     // 24小时价格变化
    double price_change_percent_24h; // 24小时价格变化百分比
    double funding_rate;         // 资金费率（期货合约）
    uint64_t timestamp;          // 时间戳
    
    MarketData() : price(0.0), volume_24h(0.0), bid_price(0.0), ask_price(0.0)
                  , bid_volume(0.0), ask_volume(0.0), high_24h(0.0), low_24h(0.0)
                  , open_24h(0.0), price_change_24h(0.0), price_change_percent_24h(0.0)
                  , funding_rate(0.0), timestamp(0) {}
};

// 交易信号结构 - 使用固定长度字符数组以支持共享内存
struct alignas(8) TradingSignal {
    uint64_t signal_id;
    uint64_t sequence_id;
    char symbol[32];  // 固定长度字符数组替代std::string
    char instrument_id[32];  // 固定长度字符数组替代std::string
    char strategy_id[64];  // 固定长度字符数组替代std::string
    SignalType type;
    SignalUrgency urgency;
    OrderSide side;
    OrderType order_type;
    TimeInForce time_in_force;
    double target_price;
    double price;
    double target_quantity;
    double quantity;
    double stop_loss;
    double take_profit;
    uint64_t timestamp;
    uint64_t expiry_time;
    char metadata[512];  // 固定长度字符数组替代std::string
    
    TradingSignal() : signal_id(0), sequence_id(0), type(SignalType::HOLD), urgency(SignalUrgency::NORMAL)
                    , side(OrderSide::BUY), order_type(OrderType::LIMIT), time_in_force(TimeInForce::GTC)
                    , target_price(0.0), price(0.0), target_quantity(0.0), quantity(0.0)
                    , stop_loss(0.0), take_profit(0.0), timestamp(0), expiry_time(0) {
        symbol[0] = '\0';
        instrument_id[0] = '\0';
        strategy_id[0] = '\0';
        metadata[0] = '\0';
    }
    
    // 辅助方法设置字符串字段
    void set_symbol(const std::string& sym) {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }
    
    void set_instrument_id(const std::string& id) {
        strncpy(instrument_id, id.c_str(), sizeof(instrument_id) - 1);
        instrument_id[sizeof(instrument_id) - 1] = '\0';
    }
    
    void set_strategy_id(const std::string& id) {
        strncpy(strategy_id, id.c_str(), sizeof(strategy_id) - 1);
        strategy_id[sizeof(strategy_id) - 1] = '\0';
    }
    
    void set_metadata(const std::string& meta) {
        strncpy(metadata, meta.c_str(), sizeof(metadata) - 1);
        metadata[sizeof(metadata) - 1] = '\0';
    }
    
    // 辅助方法获取字符串字段
    std::string get_symbol() const {
        return std::string(symbol);
    }
    
    std::string get_instrument_id() const {
        return std::string(instrument_id);
    }
    
    std::string get_strategy_id() const {
        return std::string(strategy_id);
    }
    
    std::string get_metadata() const {
        return std::string(metadata);
    }
};

// 系统控制信息
struct SystemControlInfo {
    bool trading_enabled;
    bool risk_check_enabled;
    bool emergency_stop;
    double max_position_limit;
    double max_daily_loss;
    double max_order_value;
    uint32_t max_orders_per_second;
    uint64_t last_update_time;
    
    SystemControlInfo() : trading_enabled(true), risk_check_enabled(true)
                        , emergency_stop(false), max_position_limit(1000000.0)
                        , max_daily_loss(50000.0), max_order_value(100000.0)
                        , max_orders_per_second(100), last_update_time(0) {}
};

// 订单回报结构 - 使用固定长度字符数组以支持共享内存
struct alignas(8) OrderFeedback {
    char order_id[64];  // 固定长度字符数组替代std::string
    OrderStatus status;
    double filled_volume;
    double filled_price;
    char error_message[256];  // 固定长度字符数组替代std::string
    Timestamp timestamp;
    uint64_t sequence_id;
    
    OrderFeedback() : status(OrderStatus::PENDING), filled_volume(0.0), filled_price(0.0),
                     timestamp(0), sequence_id(0) {
        order_id[0] = '\0';
        error_message[0] = '\0';
    }
    
    // 辅助方法设置order_id
    void set_order_id(const std::string& id) {
        strncpy(order_id, id.c_str(), sizeof(order_id) - 1);
        order_id[sizeof(order_id) - 1] = '\0';
    }
    
    // 辅助方法设置error_message
    void set_error_message(const std::string& msg) {
        strncpy(error_message, msg.c_str(), sizeof(error_message) - 1);
        error_message[sizeof(error_message) - 1] = '\0';
    }
    
    // 辅助方法获取order_id
    std::string get_order_id() const {
        return std::string(order_id);
    }
    
    // 辅助方法获取error_message
    std::string get_error_message() const {
        return std::string(error_message);
    }
};

// 性能统计信息
struct PerformanceStats {
    uint64_t total_signals_processed;
    uint64_t total_orders_processed;
    uint64_t total_trades_executed;
    double total_pnl;
    double max_drawdown;
    double sharpe_ratio;
    uint64_t avg_signal_latency_ns;
    uint64_t avg_order_latency_ns;
    uint64_t last_update_time;
    Timestamp connection_time;
    Timestamp last_heartbeat;
    bool is_connected;
    
    PerformanceStats() : total_signals_processed(0), total_orders_processed(0)
                       , total_trades_executed(0), total_pnl(0.0), max_drawdown(0.0)
                       , sharpe_ratio(0.0), avg_signal_latency_ns(0)
                       , avg_order_latency_ns(0), last_update_time(0)
                       , connection_time(0), last_heartbeat(0), is_connected(false) {}
};

// 时间戳辅助函数
inline uint64_t get_current_timestamp_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

inline uint64_t get_current_timestamp_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

inline uint64_t get_current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// 字符串转换函数
inline std::string order_side_to_string(OrderSide side) {
    switch (side) {
        case OrderSide::BUY: return "BUY";
        case OrderSide::SELL: return "SELL";
        default: return "UNKNOWN";
    }
}

inline std::string order_type_to_string(OrderType type) {
    switch (type) {
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::STOP: return "STOP";
        case OrderType::STOP_LIMIT: return "STOP_LIMIT";
        case OrderType::TAKE_PROFIT: return "TAKE_PROFIT";
        case OrderType::TAKE_PROFIT_LIMIT: return "TAKE_PROFIT_LIMIT";
        case OrderType::ICEBERG: return "ICEBERG";
        case OrderType::TWAP: return "TWAP";
        case OrderType::VWAP: return "VWAP";
        case OrderType::TRAILING_STOP: return "TRAILING_STOP";
        case OrderType::OCO: return "OCO";
        default: return "UNKNOWN";
    }
}

inline std::string order_status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::PENDING: return "PENDING";
        case OrderStatus::SUBMITTED: return "SUBMITTED";
        case OrderStatus::PARTIAL_FILLED: return "PARTIAL_FILLED";
        case OrderStatus::FILLED: return "FILLED";
        case OrderStatus::CANCELLED: return "CANCELLED";
        case OrderStatus::REJECTED: return "REJECTED";
        case OrderStatus::EXPIRED: return "EXPIRED";
        default: return "UNKNOWN";
    }
}

inline std::string signal_type_to_string(SignalType type) {
    switch (type) {
        case SignalType::BUY: return "BUY";
        case SignalType::SELL: return "SELL";
        case SignalType::HOLD: return "HOLD";
        case SignalType::CLOSE: return "CLOSE";
        default: return "UNKNOWN";
    }
}

inline std::string signal_urgency_to_string(SignalUrgency urgency) {
    switch (urgency) {
        case SignalUrgency::LOW: return "LOW";
        case SignalUrgency::NORMAL: return "NORMAL";
        case SignalUrgency::HIGH: return "HIGH";
        case SignalUrgency::URGENT: return "URGENT";
        default: return "UNKNOWN";
    }
}

inline std::string time_in_force_to_string(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::DAY: return "DAY";
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
        case TimeInForce::GTD: return "GTD";
        case TimeInForce::POST_ONLY: return "POST_ONLY";
        case TimeInForce::REDUCE_ONLY: return "REDUCE_ONLY";
        default: return "UNKNOWN";
    }
}

} // namespace shared_memory
} // namespace tes