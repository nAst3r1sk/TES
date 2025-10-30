#include "execution/trading_rule_checker.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>

namespace tes {
namespace execution {

TradingRuleChecker::TradingRuleChecker() 
    : initialized_(false)
    , event_sequence_(0)
{
    statistics_ = {};
}

TradingRuleChecker::~TradingRuleChecker()
{
    cleanup();
}

bool TradingRuleChecker::initialize()
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    
    if (initialized_.load()) {
        return true;
    }
    
    // 初始化统计信息
    statistics_ = {};
    statistics_.last_check_time = std::chrono::high_resolution_clock::now();
    
    initialized_.store(true);
    return true;
}

void TradingRuleChecker::cleanup()
{
    if (!initialized_.load()) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        recent_events_.clear();
    }
    
    initialized_.store(false);
}

TradingRuleCheckResult TradingRuleChecker::check_order(const Order& order, bool is_futures)
{
    if (!initialized_.load()) {
        return TradingRuleCheckResult::REJECT_SYSTEM_ERROR;
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.total_checks++;
        statistics_.last_check_time = std::chrono::high_resolution_clock::now();
    }
    
    // 检查交易对状态
    if (config_.enable_symbol_status_check) {
        auto result = check_symbol_status(order.instrument_id, is_futures);
        if (result != TradingRuleCheckResult::PASS) {
            return result;
        }
    }
    
    // 检查数量规则
    if (config_.enable_quantity_check) {
        auto result = check_quantity_rules(order.instrument_id, order.quantity, is_futures);
        if (result != TradingRuleCheckResult::PASS) {
            return result;
        }
    }
    
    // 检查价格规则
    if (config_.enable_price_check && order.price > 0) {
        auto result = check_price_rules(order.instrument_id, order.price, is_futures);
        if (result != TradingRuleCheckResult::PASS) {
            return result;
        }
    }
    
    // 检查最小名义价值
    if (config_.enable_min_notional_check && order.price > 0) {
        auto result = check_min_notional(order.instrument_id, order.quantity, order.price, is_futures);
        if (result != TradingRuleCheckResult::PASS) {
            return result;
        }
    }
    
    // 更新通过统计
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.passed_checks++;
    }
    
    return TradingRuleCheckResult::PASS;
}

TradingRuleCheckResult TradingRuleChecker::check_symbol_status(const std::string& symbol, bool is_futures)
{
    // 由于get_symbol_info返回nullptr，暂时跳过符号状态检查
    // 在实际部署时需要实现符号信息获取功能
    (void)symbol;      // 避免未使用参数警告
    (void)is_futures;  // 避免未使用参数警告
    
    if (!config_.enable_symbol_status_check) {
        return TradingRuleCheckResult::PASS;
    }
    
    // 暂时返回PASS，实际应用中需要实现符号状态检查
    return TradingRuleCheckResult::PASS;
}

TradingRuleCheckResult TradingRuleChecker::check_quantity_rules(const std::string& symbol, double quantity, bool is_futures)
{
    // 由于get_symbol_info返回nullptr，暂时跳过数量规则检查
    // 在实际部署时需要实现符号信息获取功能
    (void)symbol;      // 避免未使用参数警告
    (void)is_futures;  // 避免未使用参数警告
    
    if (!config_.enable_quantity_check) {
        return TradingRuleCheckResult::PASS;
    }
    
    // 基本的数量检查
    if (quantity <= 0) {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.rejected_checks++;
        statistics_.quantity_violations++;
        statistics_.last_violation_time = std::chrono::high_resolution_clock::now();
        
        if (config_.log_violations) {
            TradingRuleEvent event;
            event.event_id = generate_event_id();
            event.instrument_id = symbol;
            event.result = TradingRuleCheckResult::REJECT_QUANTITY_TOO_SMALL;
            event.description = "Quantity " + std::to_string(quantity) + " must be positive";
            event.timestamp = std::chrono::high_resolution_clock::now();
            log_trading_rule_event(event);
        }
        
        return TradingRuleCheckResult::REJECT_QUANTITY_TOO_SMALL;
    }
    
    // 暂时返回PASS，实际应用中需要实现完整的数量规则检查
    return TradingRuleCheckResult::PASS;
}

TradingRuleCheckResult TradingRuleChecker::check_price_rules(const std::string& symbol, double price, bool is_futures)
{
    // 由于get_symbol_info返回nullptr，暂时跳过价格规则检查
    // 在实际部署时需要实现符号信息获取功能
    (void)symbol;      // 避免未使用参数警告
    (void)is_futures;  // 避免未使用参数警告
    
    if (!config_.enable_price_check) {
        return TradingRuleCheckResult::PASS;
    }
    
    // 基本的价格检查
    if (price <= 0) {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.rejected_checks++;
        statistics_.price_violations++;
        statistics_.last_violation_time = std::chrono::high_resolution_clock::now();
        
        if (config_.log_violations) {
            TradingRuleEvent event;
            event.event_id = generate_event_id();
            event.instrument_id = symbol;
            event.result = TradingRuleCheckResult::REJECT_PRICE_TOO_LOW;
            event.description = "Price " + std::to_string(price) + " must be positive";
            event.timestamp = std::chrono::high_resolution_clock::now();
            log_trading_rule_event(event);
        }
        
        return TradingRuleCheckResult::REJECT_PRICE_TOO_LOW;
    }
    
    // 暂时返回PASS，实际应用中需要实现完整的价格规则检查
    return TradingRuleCheckResult::PASS;
}

TradingRuleCheckResult TradingRuleChecker::check_min_notional(const std::string& symbol, double quantity, double price, bool is_futures)
{
    // 由于get_symbol_info返回nullptr，暂时跳过最小名义价值检查
    // 在实际部署时需要实现符号信息获取功能
    (void)symbol;      // 避免未使用参数警告
    (void)is_futures;  // 避免未使用参数警告
    
    if (!config_.enable_min_notional_check) {
        return TradingRuleCheckResult::PASS;
    }
    
    // 基本的名义价值检查
    double notional = quantity * price;
    if (notional <= 0) {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.rejected_checks++;
        statistics_.min_notional_violations++;
        statistics_.last_violation_time = std::chrono::high_resolution_clock::now();
        
        if (config_.log_violations) {
            TradingRuleEvent event;
            event.event_id = generate_event_id();
            event.instrument_id = symbol;
            event.result = TradingRuleCheckResult::REJECT_MIN_NOTIONAL;
            event.description = "Notional value " + std::to_string(notional) + " must be positive";
            event.timestamp = std::chrono::high_resolution_clock::now();
            log_trading_rule_event(event);
        }
        
        return TradingRuleCheckResult::REJECT_MIN_NOTIONAL;
    }
    
    // 暂时返回PASS，实际应用中需要实现完整的最小名义价值检查
    return TradingRuleCheckResult::PASS;
}

TradingRuleCheckResult TradingRuleChecker::check_twap_slice(const std::string& symbol, double slice_quantity, double price, bool is_futures)
{
    // 由于get_symbol_info返回nullptr，暂时跳过TWAP切片检查
    // 在实际部署时需要实现符号信息获取功能
    (void)symbol;      // 避免未使用参数警告
    (void)is_futures;  // 避免未使用参数警告
    
    // 基本的切片检查
    if (slice_quantity <= 0) {
        return TradingRuleCheckResult::REJECT_QUANTITY_TOO_SMALL;
    }
    
    if (price <= 0) {
        return TradingRuleCheckResult::REJECT_PRICE_TOO_LOW;
    }
    
    // 暂时返回PASS，实际应用中需要实现完整的TWAP切片检查
    return TradingRuleCheckResult::PASS;
}

double TradingRuleChecker::fix_quantity_precision(const std::string& symbol, double quantity, bool is_futures)
{
    // 由于get_symbol_info返回nullptr，暂时返回原始数量
    // 在实际部署时需要实现符号信息获取功能
    (void)symbol;      // 避免未使用参数警告
    (void)is_futures;  // 避免未使用参数警告
    
    return quantity;
}

double TradingRuleChecker::fix_price_precision(const std::string& symbol, double price, bool is_futures)
{
    // 由于get_symbol_info返回nullptr，暂时返回原始价格
    // 在实际部署时需要实现符号信息获取功能
    (void)symbol;      // 避免未使用参数警告
    (void)is_futures;  // 避免未使用参数警告
    
    return price;
}

void TradingRuleChecker::set_binance_client(std::shared_ptr<trading::BinanceWebSocket> client)
{
    std::lock_guard<std::mutex> lock(binance_client_mutex_);
    binance_client_ = client;
}

std::shared_ptr<trading::BinanceWebSocket> TradingRuleChecker::get_binance_client() const
{
    std::lock_guard<std::mutex> lock(binance_client_mutex_);
    return binance_client_;
}

double TradingRuleChecker::get_daily_pnl(const std::string& strategy_id) const
{
    // 这个方法保留用于兼容性，但在交易规则检查器中不实现具体逻辑
    return 0.0;
}

double TradingRuleChecker::get_total_exposure(const std::string& strategy_id) const
{
    // 这个方法保留用于兼容性，但在交易规则检查器中不实现具体逻辑
    return 0.0;
}

void TradingRuleChecker::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

TradingRuleChecker::Config TradingRuleChecker::get_config() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

void TradingRuleChecker::set_limits(const TradingRuleLimits& limits)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    limits_ = limits;
}

TradingRuleLimits TradingRuleChecker::get_limits() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return limits_;
}

TradingRuleStatistics TradingRuleChecker::get_statistics() const
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    return statistics_;
}

std::vector<TradingRuleEvent> TradingRuleChecker::get_recent_events(uint32_t count) const
{
    std::lock_guard<std::mutex> lock(events_mutex_);
    
    if (recent_events_.size() <= count) {
        return recent_events_;
    }
    
    // 返回最近的事件
    std::vector<TradingRuleEvent> result;
    result.reserve(count);
    auto start_it = recent_events_.end() - count;
    result.assign(start_it, recent_events_.end());
    
    return result;
}

std::string TradingRuleChecker::get_trading_rule_result_description(TradingRuleCheckResult result) const
{
    switch (result) {
        case TradingRuleCheckResult::PASS:
            return "通过";
        case TradingRuleCheckResult::REJECT_SYMBOL_NOT_TRADING:
            return "拒绝：交易对未开放交易";
        case TradingRuleCheckResult::REJECT_QUANTITY_TOO_SMALL:
            return "拒绝：数量过小";
        case TradingRuleCheckResult::REJECT_QUANTITY_TOO_LARGE:
            return "拒绝：数量过大";
        case TradingRuleCheckResult::REJECT_QUANTITY_PRECISION:
            return "拒绝：数量精度不符";
        case TradingRuleCheckResult::REJECT_PRICE_TOO_LOW:
            return "拒绝：价格过低";
        case TradingRuleCheckResult::REJECT_PRICE_TOO_HIGH:
            return "拒绝：价格过高";
        case TradingRuleCheckResult::REJECT_PRICE_PRECISION:
            return "拒绝：价格精度不符";
        case TradingRuleCheckResult::REJECT_MIN_NOTIONAL:
            return "拒绝：最小名义价值不足";
        case TradingRuleCheckResult::REJECT_INVALID_PARAMS:
            return "拒绝：无效参数";
        case TradingRuleCheckResult::REJECT_SYMBOL_NOT_FOUND:
            return "拒绝：交易对未找到";
        case TradingRuleCheckResult::REJECT_SYSTEM_ERROR:
            return "拒绝：系统错误";
        default:
            return "未知结果";
    }
}

std::string TradingRuleChecker::generate_event_id()
{
    uint64_t seq = event_sequence_.fetch_add(1);
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << "TR" << std::setfill('0') << std::setw(16) << std::hex << timestamp
        << std::setw(8) << seq;
    return oss.str();
}

void TradingRuleChecker::log_trading_rule_event(const TradingRuleEvent& event)
{
    std::lock_guard<std::mutex> lock(events_mutex_);
    
    recent_events_.push_back(event);
    
    // 保持事件列表大小在合理范围内
    const size_t max_events = 1000;
    if (recent_events_.size() > max_events) {
        recent_events_.erase(recent_events_.begin(), 
                           recent_events_.begin() + (recent_events_.size() - max_events));
    }
    
    // 可选：输出到日志
    if (config_.log_violations) {
        std::cout << "[TradingRule] " << event.event_id << " - " 
                  << event.instrument_id << ": " << event.description << std::endl;
    }
}

const void* TradingRuleChecker::get_symbol_info(const std::string& symbol, bool is_futures) const
{
    std::lock_guard<std::mutex> lock(binance_client_mutex_);
    if (!binance_client_) {
        return nullptr;
    }
    
    // BinanceWebSocket doesn't have getSymbolInfo method, return nullptr for now
    // This functionality needs to be implemented in the gateway
    return nullptr;
}

} // namespace execution
} // namespace tes