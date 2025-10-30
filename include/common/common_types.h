#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <cmath>
#include <vector>
#include <memory>

namespace tes {

// 基础类型定义
using Price = double;
using Volume = int64_t;
using OrderId = uint64_t;
using InstrumentId = std::string;
using Timestamp = std::chrono::high_resolution_clock::time_point;

// 订单方向
enum class Side : uint8_t {
    BUY = 1,
    SELL = 2
};

// 订单类型
enum class OrderType : uint8_t {
    MARKET = 1,
    LIMIT = 2,
    STOP = 3,
    STOP_LIMIT = 4  // 添加止损限价单类型
};

// 订单状态
enum class OrderStatus : uint8_t {
    NEW = 1,
    PARTIALLY_FILLED = 2,
    FILLED = 3,
    CANCELLED = 4,
    REJECTED = 5,
    PENDING = 6,
    SUBMITTED = 7,  // 添加已提交状态
    ERROR = 8       // 添加错误状态
};

// 时间有效性（新增）
enum class TimeInForce : uint8_t {
    DAY = 1,        // 当日有效
    GTC = 2,        // 撤销前有效
    IOC = 3,        // 立即成交或取消
    FOK = 4         // 全部成交或取消
};

// 交易信号类型
enum class SignalType : uint8_t {
    BUY_SIGNAL = 1,
    SELL_SIGNAL = 2,
    HOLD_SIGNAL = 3,
    CLOSE_SIGNAL = 4
};

// 风险事件类型
enum class RiskEventType : uint8_t {
    POSITION_LIMIT = 1,
    FUND_LIMIT = 2,
    PRICE_LIMIT = 3,
    VOLUME_LIMIT = 4,
    EMERGENCY_STOP = 5
};

// 交易信号结构
struct TradingSignal {
    InstrumentId instrument;
    SignalType signal_type;
    Price target_price;
    Volume target_volume;
    Timestamp timestamp;
    uint64_t sequence_id;
    
    TradingSignal() = default;
    TradingSignal(const InstrumentId& inst, SignalType type, Price price, Volume volume)
        : instrument(inst), signal_type(type), target_price(price), target_volume(volume),
          timestamp(std::chrono::high_resolution_clock::now()), sequence_id(0) {}
};

// 订单结构
struct Order {
    OrderId order_id;
    InstrumentId instrument;
    Side side;
    OrderType type;
    Price price;
    Volume volume;
    Volume filled_volume;
    OrderStatus status;
    Timestamp create_time;
    Timestamp update_time;
    std::string client_id;
    
    // 新增字段以兼容execution/types.h
    std::string client_order_id;
    std::string strategy_id;
    TimeInForce time_in_force;
    double quantity;           // 兼容double类型数量
    double filled_quantity;    // 兼容double类型已成交数量
    double average_price;      // 平均成交价格
    Timestamp timestamp;       // 时间戳
    std::string error_message; // 错误信息
    
    Order() = default;
    Order(OrderId id, const InstrumentId& inst, Side s, OrderType t, Price p, Volume v)
        : order_id(id), instrument(inst), side(s), type(t), price(p), volume(v),
          filled_volume(0), status(OrderStatus::NEW),
          create_time(std::chrono::high_resolution_clock::now()),
          update_time(std::chrono::high_resolution_clock::now()),
          time_in_force(TimeInForce::GTC), quantity(static_cast<double>(v)),
          filled_quantity(0.0), average_price(0.0) {
        timestamp = create_time;
    }
};

// 成交回报结构
struct Trade {
    OrderId order_id;
    InstrumentId instrument;
    Side side;
    Price price;
    Volume volume;
    Timestamp timestamp;
    std::string trade_id;
    
    // 新增字段以兼容execution/types.h
    Timestamp trade_time;      // 成交时间
    double quantity;           // 兼容double类型数量
    double commission;         // 手续费
    
    Trade() = default;
    Trade(OrderId oid, const InstrumentId& inst, Side s, Price p, Volume v)
        : order_id(oid), instrument(inst), side(s), price(p), volume(v),
          timestamp(std::chrono::high_resolution_clock::now()),
          quantity(static_cast<double>(v)), commission(0.0) {
        trade_time = timestamp;
    }
};

// 账户信息结构
struct AccountInfo {
    std::string account_id;
    double balance;
    double available_balance;
    double margin_used;
    double margin_available;
    Timestamp update_time;
    
    // 新增字段以兼容execution/types.h
    double equity;              // 净值
    double buying_power;        // 购买力
    double maintenance_margin;  // 维持保证金
    double initial_margin;      // 初始保证金
    
    AccountInfo() = default;
    AccountInfo(const std::string& id, double bal, double avail)
        : account_id(id), balance(bal), available_balance(avail),
          margin_used(0.0), margin_available(avail),
          update_time(std::chrono::high_resolution_clock::now()),
          equity(bal), buying_power(avail), maintenance_margin(0.0),
          initial_margin(0.0) {}
};

// 持仓信息结构
struct Position {
    InstrumentId instrument;
    Volume long_position;
    Volume short_position;
    Price avg_cost;
    double unrealized_pnl;
    double realized_pnl;
    Timestamp update_time;
    
    // 新增字段以兼容execution/types.h
    std::string strategy_id;        // 策略ID
    double long_quantity;           // 多头数量（兼容double类型）
    double short_quantity;          // 空头数量（兼容double类型）
    double net_quantity;            // 净持仓数量
    double average_cost;            // 平均成本（兼容double类型）
    
    Position() = default;
    Position(const InstrumentId& inst, Volume long_pos, Volume short_pos, Price cost)
        : instrument(inst), long_position(long_pos), short_position(short_pos),
          avg_cost(cost), unrealized_pnl(0.0), realized_pnl(0.0),
          update_time(std::chrono::high_resolution_clock::now()),
          long_quantity(static_cast<double>(long_pos)),
          short_quantity(static_cast<double>(short_pos)),
          net_quantity(static_cast<double>(long_pos - short_pos)),
          average_cost(cost) {}
};

// 性能指标结构
struct PerformanceMetrics {
    double latency_us;          // 延迟（微秒）
    uint64_t throughput_tps;    // 吞吐量（每秒事务数）
    uint64_t memory_usage_mb;   // 内存使用（MB）
    double cpu_usage_percent;   // CPU使用率
    uint64_t message_count;     // 消息计数
    Timestamp timestamp;
    
    PerformanceMetrics() = default;
    PerformanceMetrics(double lat, uint64_t tps, uint64_t mem, double cpu)
        : latency_us(lat), throughput_tps(tps), memory_usage_mb(mem),
          cpu_usage_percent(cpu), message_count(0),
          timestamp(std::chrono::high_resolution_clock::now()) {}
};

// 加密货币精度配置
struct CryptoPrecisionConfig {
    std::string symbol;           // 交易对符号，如 "BTC/USDT"
    int price_precision;          // 价格小数位数
    int quantity_precision;       // 数量小数位数
    double min_price;            // 最小价格
    double max_price;            // 最大价格
    double min_quantity;         // 最小交易数量
    double max_quantity;         // 最大交易数量
    double price_tick_size;      // 价格最小变动单位
    double quantity_step_size;   // 数量最小变动单位
    
    CryptoPrecisionConfig() : price_precision(8), quantity_precision(8),
                             min_price(0.00000001), max_price(1000000.0),
                             min_quantity(0.00000001), max_quantity(1000000.0),
                             price_tick_size(0.00000001), quantity_step_size(0.00000001) {}
    
    CryptoPrecisionConfig(const std::string& sym, int price_prec, int qty_prec,
                         double min_p, double max_p, double min_q, double max_q,
                         double tick_size, double step_size)
        : symbol(sym), price_precision(price_prec), quantity_precision(qty_prec),
          min_price(min_p), max_price(max_p), min_quantity(min_q), max_quantity(max_q),
          price_tick_size(tick_size), quantity_step_size(step_size) {}
};

// 精度处理工具函数
namespace precision_utils {
    // 根据精度舍入价格
    inline double round_price(double price, int precision) {
        double multiplier = std::pow(10.0, precision);
        return std::round(price * multiplier) / multiplier;
    }
    
    // 根据精度舍入数量
    inline double round_quantity(double quantity, int precision) {
        double multiplier = std::pow(10.0, precision);
        return std::round(quantity * multiplier) / multiplier;
    }
    
    // 根据tick size调整价格
    inline double adjust_price_to_tick(double price, double tick_size) {
        if (tick_size <= 0) return price;
        return std::round(price / tick_size) * tick_size;
    }
    
    // 根据step size调整数量
    inline double adjust_quantity_to_step(double quantity, double step_size) {
        if (step_size <= 0) return quantity;
        return std::round(quantity / step_size) * step_size;
    }
    
    // 验证价格是否在有效范围内
    inline bool is_valid_price(double price, const CryptoPrecisionConfig& config) {
        return price >= config.min_price && price <= config.max_price;
    }
    
    // 验证数量是否在有效范围内
    inline bool is_valid_quantity(double quantity, const CryptoPrecisionConfig& config) {
        return quantity >= config.min_quantity && quantity <= config.max_quantity;
    }
}

// 常量定义
namespace constants {
    constexpr size_t MAX_SIGNAL_BUFFER_SIZE = 10000;
    constexpr size_t MAX_ORDER_BUFFER_SIZE = 10000;
    constexpr size_t MAX_TRADE_BUFFER_SIZE = 10000;
    constexpr size_t MAX_INSTRUMENT_NAME_LENGTH = 32;
    constexpr double PRICE_EPSILON = 1e-8;
    constexpr int64_t INVALID_ORDER_ID = 0;
}

} // namespace tes
