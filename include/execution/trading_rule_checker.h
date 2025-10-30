#pragma once

#include "types.h"
#include "../../../3rd/gateway/include/binance_websocket.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace tes {
namespace execution {

// 交易规则检查结果
enum class TradingRuleCheckResult {
    PASS,                        // 通过
    REJECT_SYMBOL_NOT_TRADING,   // 拒绝：交易对未开放交易
    REJECT_QUANTITY_TOO_SMALL,   // 拒绝：数量过小
    REJECT_QUANTITY_TOO_LARGE,   // 拒绝：数量过大
    REJECT_QUANTITY_PRECISION,   // 拒绝：数量精度不符
    REJECT_PRICE_TOO_LOW,        // 拒绝：价格过低
    REJECT_PRICE_TOO_HIGH,       // 拒绝：价格过高
    REJECT_PRICE_PRECISION,      // 拒绝：价格精度不符
    REJECT_MIN_NOTIONAL,         // 拒绝：最小名义价值不足
    REJECT_INVALID_PARAMS,       // 拒绝：无效参数
    REJECT_SYMBOL_NOT_FOUND,     // 拒绝：交易对未找到
    REJECT_SYSTEM_ERROR          // 拒绝：系统错误
};

// 交易规则检查事件
struct TradingRuleEvent {
    std::string event_id;
    std::string strategy_id;
    std::string instrument_id;
    TradingRuleCheckResult result;
    std::string description;
    std::chrono::high_resolution_clock::time_point timestamp;
    
    TradingRuleEvent() : result(TradingRuleCheckResult::PASS) {
        timestamp = std::chrono::high_resolution_clock::now();
    }
};

// 交易规则限制配置
struct TradingRuleLimits {
    double max_order_quantity;           // 最大订单数量
    double max_daily_quantity;           // 每日最大数量
    double max_order_value;              // 最大订单价值
    double max_daily_value;              // 每日最大价值
    double min_price_increment;          // 最小价格增量
    double max_price_deviation_pct;      // 最大价格偏离百分比
    bool enable_after_hours_trading;     // 启用盘后交易
    
    TradingRuleLimits() : max_order_quantity(1000000.0), max_daily_quantity(10000000.0),
                         max_order_value(10000000.0), max_daily_value(100000000.0),
                         min_price_increment(0.01), max_price_deviation_pct(0.05),
                         enable_after_hours_trading(false) {}
};

// 交易规则检查统计
struct TradingRuleStatistics {
    uint64_t total_checks;
    uint64_t passed_checks;
    uint64_t rejected_checks;
    uint64_t symbol_not_trading_violations;
    uint64_t quantity_violations;
    uint64_t price_violations;
    uint64_t precision_violations;
    uint64_t min_notional_violations;
    std::chrono::high_resolution_clock::time_point last_check_time;
    std::chrono::high_resolution_clock::time_point last_violation_time;
};

class TradingRuleChecker {
public:
    // 配置结构
    struct Config {
        bool enable_symbol_status_check;    // 启用交易对状态检查
        bool enable_quantity_check;         // 启用数量检查
        bool enable_price_check;            // 启用价格检查
        bool enable_precision_check;        // 启用精度检查
        bool enable_min_notional_check;     // 启用最小名义价值检查
        bool log_violations;                // 记录违规事件
        bool auto_fix_precision;            // 自动修正精度
        
        Config() : enable_symbol_status_check(true), enable_quantity_check(true),
                   enable_price_check(true), enable_precision_check(true),
                   enable_min_notional_check(true), log_violations(true),
                   auto_fix_precision(true) {}
    };
    
    TradingRuleChecker();
    ~TradingRuleChecker();
    
    // 生命周期管理
    bool initialize();
    void cleanup();
    
    // 交易规则检查
    TradingRuleCheckResult check_order(const Order& order, bool is_futures = false);
    TradingRuleCheckResult check_symbol_status(const std::string& symbol, bool is_futures = false);
    TradingRuleCheckResult check_quantity_rules(const std::string& symbol, double quantity, bool is_futures = false);
    TradingRuleCheckResult check_price_rules(const std::string& symbol, double price, bool is_futures = false);
    TradingRuleCheckResult check_min_notional(const std::string& symbol, double quantity, double price, bool is_futures = false);
    
    // TWAP拆单专用检查和修正
    TradingRuleCheckResult check_twap_slice(const std::string& symbol, double slice_quantity, double price, bool is_futures = false);
    double fix_quantity_precision(const std::string& symbol, double quantity, bool is_futures = false);
    double fix_price_precision(const std::string& symbol, double price, bool is_futures = false);
    
    // BinanceClient管理
    void set_binance_client(std::shared_ptr<trading::BinanceWebSocket> client);
    std::shared_ptr<trading::BinanceWebSocket> get_binance_client() const;
    double get_daily_pnl(const std::string& strategy_id) const;
    double get_total_exposure(const std::string& strategy_id) const;
    
    // 配置和统计
    void set_config(const Config& config);
    Config get_config() const;
    void set_limits(const TradingRuleLimits& limits);
    TradingRuleLimits get_limits() const;
    TradingRuleStatistics get_statistics() const;
    std::vector<TradingRuleEvent> get_recent_events(uint32_t count = 100) const;
    
    // 实用方法
    std::string get_trading_rule_result_description(TradingRuleCheckResult result) const;
    
private:
    // 内部方法
    std::string generate_event_id();
    void log_trading_rule_event(const TradingRuleEvent& event);
    const void* get_symbol_info(const std::string& symbol, bool is_futures = false) const;
    
    // 成员变量
    mutable std::mutex events_mutex_;
    mutable std::mutex config_mutex_;
    mutable std::mutex statistics_mutex_;
    mutable std::mutex binance_client_mutex_;
    
    std::vector<TradingRuleEvent> recent_events_;
    Config config_;
    TradingRuleLimits limits_;
    TradingRuleStatistics statistics_;
    std::shared_ptr<trading::BinanceWebSocket> binance_client_;
    
    std::atomic<bool> initialized_;
    std::atomic<uint64_t> event_sequence_;
};

} // namespace execution
} // namespace tes