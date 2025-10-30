#include "execution/position_manager.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <thread>

namespace tes {
namespace execution {

PositionManager::PositionManager()
    : initialized_(false)
    , running_(false)
    , event_sequence_(0)
{
    statistics_ = {};
}

PositionManager::~PositionManager()
{
    stop();
    cleanup();
}

bool PositionManager::initialize()
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    
    if (initialized_.load()) {
        return true;
    }
    
    // 初始化统计信息
    statistics_ = {};
    statistics_.last_update_time = std::chrono::high_resolution_clock::now();
    
    initialized_.store(true);
    return true;
}

bool PositionManager::start()
{
    if (!initialized_.load()) {
        if (!initialize()) {
            return false;
        }
    }
    
    if (running_.load()) {
        return true;
    }
    
    running_.store(true);
    
    // 启动工作线程
    worker_thread_ = std::make_unique<std::thread>(&PositionManager::worker_thread, this);
    
    return true;
}

void PositionManager::stop()
{
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 等待工作线程结束
    if (worker_thread_ && (*worker_thread_).joinable()) {
        (*worker_thread_).join();
    }
}

void PositionManager::cleanup()
{
    if (!initialized_.load()) {
        return;
    }
    
    stop();
    
    {
        std::lock_guard<std::mutex> lock(positions_mutex_);
        positions_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(market_data_mutex_);
        current_prices_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        recent_events_.clear();
    }
    
    initialized_.store(false);
}

void PositionManager::process_trade(const Trade& trade, const std::string& strategy_id)
{
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
    
    statistics_.total_trades_processed++;
    statistics_.last_trade_time = std::chrono::high_resolution_clock::now();
    
    std::string position_key = get_position_key(strategy_id, trade.instrument_id);
    
    auto it = positions_.find(position_key);
    if (it == positions_.end()) {
        // 创建新持仓
        PositionData position_data;
        position_data.position.strategy_id = strategy_id;
        position_data.position.instrument_id = trade.instrument_id;
        position_data.position.net_quantity = (trade.side == OrderSide::BUY) ? trade.quantity : -trade.quantity;
        position_data.position.average_cost = trade.price;
        position_data.position.realized_pnl = 0.0;
        position_data.position.unrealized_pnl = 0.0;
        position_data.position.update_time = trade.trade_time;
        position_data.last_update_time = std::chrono::high_resolution_clock::now();
        
        positions_[position_key] = position_data;
        statistics_.total_positions++;
        statistics_.active_positions++;
        
        // 记录开仓事件
        if (config_.enable_event_logging) {
            PositionEvent event;
            event.event_id = generate_event_id();
            event.type = PositionEventType::POSITION_OPENED;
            event.position = position_data.position;
            event.description = "Position opened for " + trade.instrument_id;
            log_position_event(event);
        }
    } else {
        // 更新现有持仓
        auto& position_data = it->second;
        auto& position = position_data.position;
        
        double old_quantity = position.net_quantity;
        double trade_quantity = (trade.side == OrderSide::BUY) ? trade.quantity : -trade.quantity;
        double new_quantity = old_quantity + trade_quantity;
        
        // 计算已实现盈亏
        if ((old_quantity > 0 && trade_quantity < 0) || (old_quantity < 0 && trade_quantity > 0)) {
            // 平仓交易
            double close_quantity = std::min(std::abs(old_quantity), std::abs(trade_quantity));
            double realized_pnl = 0.0;
            
            if (old_quantity > 0) {
                // 平多仓
                realized_pnl = close_quantity * (trade.price - position.average_cost);
            } else {
                // 平空仓
                realized_pnl = close_quantity * (position.average_cost - trade.price);
            }
            
            position.realized_pnl += realized_pnl;
            statistics_.total_realized_pnl += realized_pnl;
        }
        
        // 更新持仓数量和平均价格
        if (new_quantity == 0) {
            // 持仓完全平仓
            if (config_.auto_close_zero_positions) {
                positions_.erase(it);
                statistics_.active_positions--;
                statistics_.closed_positions++;
                
                // 记录平仓事件
                if (config_.enable_event_logging) {
                    PositionEvent event;
                    event.event_id = generate_event_id();
                    event.type = PositionEventType::POSITION_CLOSED;
                    event.position = position;
                    event.description = "Position closed for " + trade.instrument_id;
                    log_position_event(event);
                }
                return;
            } else {
                position.net_quantity = 0;
                position.average_cost = 0;
            }
        } else if ((old_quantity >= 0 && new_quantity >= 0) || (old_quantity <= 0 && new_quantity <= 0)) {
            // 同向加仓
            double total_cost = old_quantity * position.average_cost + trade_quantity * trade.price;
            position.net_quantity = new_quantity;
            position.average_cost = total_cost / new_quantity;
        } else {
            // 反向开仓
            position.net_quantity = new_quantity;
            position.average_cost = trade.price;
        }
        
        position.update_time = trade.trade_time;
        position_data.last_update_time = std::chrono::high_resolution_clock::now();
        
        // 记录更新事件
        if (config_.enable_event_logging) {
            PositionEvent event;
            event.event_id = generate_event_id();
            event.type = PositionEventType::POSITION_UPDATED;
            event.position = position;
            event.description = "Position updated for " + trade.instrument_id;
            log_position_event(event);
        }
    }
    
    statistics_.last_update_time = std::chrono::high_resolution_clock::now();
}

void PositionManager::process_trades(const std::vector<Trade>& trades)
{
    // 方法需要strategy_id，但Trade结构体中没有该字段
    // 实际使用时应该通过其他方式获取strategy_id或重新设计接口
    for (const auto& trade : trades) {
        // 临时使用空字符串作为strategy_id，实际应用中需要修改
        process_trade(trade, "");
    }
}

Position PositionManager::get_position(const std::string& strategy_id, const std::string& instrument_id) const
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    std::string position_key = get_position_key(strategy_id, instrument_id);
    auto it = positions_.find(position_key);
    if (it != positions_.end()) {
        return it->second.position;
    }
    
    return Position{}; // 返回空持仓
}

std::vector<Position> PositionManager::get_positions_by_strategy(const std::string& strategy_id) const
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    std::vector<Position> positions;
    for (const auto& pair : positions_) {
        if (pair.second.position.strategy_id == strategy_id) {
            positions.push_back(pair.second.position);
        }
    }
    
    return positions;
}

std::vector<Position> PositionManager::get_positions_by_instrument(const std::string& instrument_id) const
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    std::vector<Position> positions;
    for (const auto& pair : positions_) {
        if (pair.second.position.instrument_id == instrument_id) {
            positions.push_back(pair.second.position);
        }
    }
    
    return positions;
}

std::vector<Position> PositionManager::get_all_positions() const
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    std::vector<Position> positions;
    for (const auto& pair : positions_) {
        positions.push_back(pair.second.position);
    }
    
    return positions;
}

void PositionManager::update_position(const Position& position)
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    update_position_internal(position);
}

void PositionManager::close_position(const std::string& strategy_id, const std::string& instrument_id)
{
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
    
    std::string position_key = get_position_key(strategy_id, instrument_id);
    auto it = positions_.find(position_key);
    if (it != positions_.end()) {
        Position position = it->second.position;
        positions_.erase(it);
        statistics_.active_positions--;
        statistics_.closed_positions++;
        
        // 记录平仓事件
        if (config_.enable_event_logging) {
            PositionEvent event;
            event.event_id = generate_event_id();
            event.type = PositionEventType::POSITION_CLOSED;
            event.position = position;
            event.description = "Position manually closed for " + strategy_id + ":" + instrument_id;
            log_position_event(event);
        }
    }
}

void PositionManager::close_all_positions(const std::string& strategy_id)
{
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
    
    auto it = positions_.begin();
    while (it != positions_.end()) {
        if (it->second.position.strategy_id == strategy_id) {
            Position position = it->second.position;
            it = positions_.erase(it);
            statistics_.active_positions--;
            statistics_.closed_positions++;
            
            // 记录平仓事件
            if (config_.enable_event_logging) {
                PositionEvent event;
                event.event_id = generate_event_id();
                event.type = PositionEventType::POSITION_CLOSED;
                event.position = position;
                event.description = "Position closed for strategy " + strategy_id;
                log_position_event(event);
            }
        } else {
            ++it;
        }
    }
}

void PositionManager::update_market_data(const MarketData& market_data)
{
    {
        std::lock_guard<std::mutex> lock(market_data_mutex_);
        current_prices_[market_data.instrument_id] = market_data.last_price;
    }
    
    // 更新未实现盈亏
    if (config_.enable_pnl_calculation) {
        std::lock_guard<std::mutex> pos_lock(positions_mutex_);
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        
        double total_unrealized_pnl = 0.0;
        for (auto& pair : positions_) {
            auto& position_data = pair.second;
            if (position_data.position.instrument_id == market_data.instrument_id) {
                position_data.current_price = market_data.last_price;
                position_data.position.unrealized_pnl = calculate_unrealized_pnl(
                    position_data.position, market_data.last_price);
                total_unrealized_pnl += position_data.position.unrealized_pnl;
            }
        }
        
        statistics_.total_unrealized_pnl = total_unrealized_pnl;
        statistics_.last_update_time = std::chrono::high_resolution_clock::now();
    }
}

void PositionManager::update_market_data_batch(const std::vector<MarketData>& market_data_list)
{
    for (const auto& market_data : market_data_list) {
        update_market_data(market_data);
    }
}

double PositionManager::calculate_unrealized_pnl(const Position& position, double current_price) const
{
    if (position.net_quantity == 0 || current_price <= 0) {
        return 0.0;
    }
    
    return position.net_quantity * (current_price - position.average_cost);
}

double PositionManager::calculate_realized_pnl(const std::string& strategy_id) const
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    double total_realized_pnl = 0.0;
    for (const auto& pair : positions_) {
        if (pair.second.position.strategy_id == strategy_id) {
            total_realized_pnl += pair.second.position.realized_pnl;
        }
    }
    
    return total_realized_pnl;
}

double PositionManager::calculate_unrealized_pnl(const std::string& strategy_id) const
{
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> market_lock(market_data_mutex_);
    
    double total_unrealized_pnl = 0.0;
    for (const auto& pair : positions_) {
        const auto& position = pair.second.position;
        if (position.strategy_id == strategy_id) {
            double current_price = position.average_cost;
            auto price_it = current_prices_.find(position.instrument_id);
            if (price_it != current_prices_.end()) {
                current_price = price_it->second;
            }
            total_unrealized_pnl += calculate_unrealized_pnl(position, current_price);
        }
    }
    
    return total_unrealized_pnl;
}

double PositionManager::calculate_total_pnl(const std::string& strategy_id) const
{
    return calculate_realized_pnl(strategy_id) + 
           calculate_unrealized_pnl(strategy_id);
}

double PositionManager::calculate_total_exposure(const std::string& strategy_id) const
{
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> market_lock(market_data_mutex_);
    
    double total_exposure = 0.0;
    for (const auto& pair : positions_) {
        const auto& position = pair.second.position;
        if (position.strategy_id == strategy_id) {
            double current_price = position.average_cost;
            auto price_it = current_prices_.find(position.instrument_id);
            if (price_it != current_prices_.end()) {
                current_price = price_it->second;
            }
            total_exposure += std::abs(position.net_quantity) * current_price;
        }
    }
    
    return total_exposure;
}

double PositionManager::calculate_net_position(const std::string& strategy_id, const std::string& instrument_id) const
{
    Position position = get_position(strategy_id, instrument_id);
    return position.net_quantity;
}

std::unordered_map<std::string, double> PositionManager::get_exposure_by_instrument() const
{
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> market_lock(market_data_mutex_);
    
    std::unordered_map<std::string, double> exposure_map;
    
    for (const auto& pair : positions_) {
        const auto& position = pair.second.position;
        double current_price = position.average_cost;
        auto price_it = current_prices_.find(position.instrument_id);
        if (price_it != current_prices_.end()) {
            current_price = price_it->second;
        }
        
        double exposure = std::abs(position.net_quantity) * current_price;
        exposure_map[position.instrument_id] += exposure;
    }
    
    return exposure_map;
}

void PositionManager::set_position_event_callback(PositionEventCallback callback)
{
    event_callback_ = callback;
}

std::vector<PositionEvent> PositionManager::get_recent_events(uint32_t count) const
{
    std::lock_guard<std::mutex> lock(events_mutex_);
    
    std::vector<PositionEvent> events;
    uint32_t start_index = 0;
    if (recent_events_.size() > count) {
        start_index = recent_events_.size() - count;
    }
    
    for (uint32_t i = start_index; i < recent_events_.size(); ++i) {
        events.push_back(recent_events_[i]);
    }
    
    return events;
}

void PositionManager::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

PositionManager::Config PositionManager::get_config() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

PositionStatistics PositionManager::get_statistics() const
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    return statistics_;
}

bool PositionManager::is_position_exists(const std::string& strategy_id, const std::string& instrument_id) const
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    std::string position_key = get_position_key(strategy_id, instrument_id);
    return positions_.find(position_key) != positions_.end();
}

std::string PositionManager::get_position_key(const std::string& strategy_id, const std::string& instrument_id) const
{
    return strategy_id + ":" + instrument_id;
}

std::string PositionManager::generate_event_id()
{
    auto seq = event_sequence_.fetch_add(1);
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << "POS_" << timestamp << "_" << seq;
    return oss.str();
}

void PositionManager::log_position_event(const PositionEvent& event)
{
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        recent_events_.push_back(event);
        
        // 保持事件列表大小
        if (recent_events_.size() > 1000) {
            recent_events_.erase(recent_events_.begin(), recent_events_.begin() + 100);
        }
    }
    
    // 调用回调函数
    if (event_callback_) {
        event_callback_(event);
    }
    
    // 输出日志
    auto time_t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::cout << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
              << "] POSITION_EVENT: " << event.event_id
              << ", Type: " << static_cast<int>(event.type)
              << ", Strategy: " << event.position.strategy_id
              << ", Instrument: " << event.position.instrument_id
              << ", Quantity: " << event.position.net_quantity
              << ", Description: " << event.description << std::endl;
}

void PositionManager::update_position_internal(const Position& position)
{
    std::string position_key = get_position_key(position.strategy_id, position.instrument_id);
    
    auto it = positions_.find(position_key);
    if (it != positions_.end()) {
        it->second.position = position;
        it->second.last_update_time = std::chrono::high_resolution_clock::now();
    } else {
        PositionData position_data;
        position_data.position = position;
        position_data.last_update_time = std::chrono::high_resolution_clock::now();
        positions_[position_key] = position_data;
        
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        statistics_.total_positions++;
        statistics_.active_positions++;
    }
}

void PositionManager::cleanup_zero_positions()
{
    if (!config_.auto_close_zero_positions) {
        return;
    }
    
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
    
    auto it = positions_.begin();
    while (it != positions_.end()) {
        if (it->second.position.net_quantity == 0) {
            Position position = it->second.position;
            it = positions_.erase(it);
            statistics_.active_positions--;
            statistics_.closed_positions++;
            
            // 记录清理事件
            if (config_.enable_event_logging) {
                PositionEvent event;
                event.event_id = generate_event_id();
                event.type = PositionEventType::POSITION_CLOSED;
                event.position = position;
                event.description = "Zero position cleaned up";
                log_position_event(event);
            }
        } else {
            ++it;
        }
    }
}

void PositionManager::update_statistics()
{
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
    
    statistics_.active_positions = positions_.size();
    
    double total_unrealized_pnl = 0.0;
    for (const auto& pair : positions_) {
        total_unrealized_pnl += pair.second.position.unrealized_pnl;
    }
    statistics_.total_unrealized_pnl = total_unrealized_pnl;
    
    statistics_.last_update_time = std::chrono::high_resolution_clock::now();
}

void PositionManager::worker_thread()
{
    while (running_.load()) {
        try {
            // 清理零持仓
            cleanup_zero_positions();
            
            // 更新统计信息
            update_statistics();
            
            // 休眠
            std::this_thread::sleep_for(std::chrono::seconds(config_.position_cleanup_interval_seconds));
        } catch (const std::exception& e) {
            std::cerr << "PositionManager worker thread error: " << e.what() << std::endl;
        }
    }
}

// 外部持仓同步方法实现
void PositionManager::sync_position_from_exchange(const Position& position)
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    std::string position_key = get_position_key(position.strategy_id, position.instrument_id);
    auto it = positions_.find(position_key);
    
    if (it != positions_.end()) {
        // 更新现有持仓
        it->second.position = position;
        it->second.last_update_time = std::chrono::high_resolution_clock::now();
    } else {
        // 添加新持仓
        PositionData position_data;
        position_data.position = position;
        position_data.last_update_time = std::chrono::high_resolution_clock::now();
        positions_[position_key] = position_data;
        
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        statistics_.total_positions++;
        if (position.net_quantity != 0.0) {
            statistics_.active_positions++;
        }
    }
    
    // 记录持仓事件
    PositionEvent event;
    event.event_id = generate_event_id();
    event.type = PositionEventType::POSITION_UPDATED;
    event.position = position;
    event.description = "Position synced from exchange";
    log_position_event(event);
}

void PositionManager::sync_positions_from_exchange(const std::vector<Position>& positions)
{
    for (const auto& position : positions) {
        sync_position_from_exchange(position);
    }
}

bool PositionManager::remove_position(const std::string& strategy_id, const std::string& instrument_id)
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    std::string position_key = get_position_key(strategy_id, instrument_id);
    auto it = positions_.find(position_key);
    
    if (it != positions_.end()) {
        positions_.erase(it);
        
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        if (statistics_.active_positions > 0) {
            statistics_.active_positions--;
        }
        return true;
    }
    return false;
}

// 交易所适配器管理方法实现
void PositionManager::set_exchange_adapter(std::shared_ptr<PositionExchangeAdapter> adapter)
{
    std::lock_guard<std::mutex> lock(adapter_mutex_);
    exchange_adapter_ = adapter;
}

std::shared_ptr<PositionExchangeAdapter> PositionManager::get_exchange_adapter() const
{
    std::lock_guard<std::mutex> lock(adapter_mutex_);
    return exchange_adapter_;
}

bool PositionManager::has_exchange_adapter() const
{
    std::lock_guard<std::mutex> lock(adapter_mutex_);
    return exchange_adapter_ != nullptr;
}

// 持仓同步和状态管理方法实现
void PositionManager::force_update_position(const Position& position)
{
    update_position_internal(position);
}

void PositionManager::batch_update_positions(const std::vector<Position>& positions)
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    for (const auto& position : positions) {
        std::string position_key = get_position_key(position.strategy_id, position.instrument_id);
        auto it = positions_.find(position_key);
        
        if (it != positions_.end()) {
            it->second.position = position;
            it->second.last_update_time = std::chrono::high_resolution_clock::now();
        } else {
            PositionData position_data;
            position_data.position = position;
            position_data.last_update_time = std::chrono::high_resolution_clock::now();
            positions_[position_key] = position_data;
        }
        
        // 记录持仓事件
        PositionEvent event;
        event.event_id = generate_event_id();
        event.type = PositionEventType::POSITION_UPDATED;
        event.position = position;
        event.description = "Position batch updated";
        log_position_event(event);
    }
}

void PositionManager::refresh_positions_from_exchange()
{
    std::lock_guard<std::mutex> lock(adapter_mutex_);
    
    if (exchange_adapter_) {
        try {
            auto positions = exchange_adapter_->fetch_positions();
            sync_positions_from_exchange(positions);
        } catch (const std::exception& e) {
            std::cerr << "Failed to refresh positions from exchange: " << e.what() << std::endl;
        }
    }
}

void PositionManager::clear_stale_positions(std::chrono::seconds max_age)
{
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    auto now = std::chrono::high_resolution_clock::now();
    auto it = positions_.begin();
    
    while (it != positions_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_update_time);
        if (age > max_age && it->second.position.net_quantity == 0.0) {
            it = positions_.erase(it);
        } else {
            ++it;
        }
    }
}

// 策略映射功能实现
void PositionManager::set_strategy_mapping_callback(StrategyMappingCallback callback)
{
    std::lock_guard<std::mutex> lock(strategy_mapping_mutex_);
    strategy_mapping_callback_ = callback;
}

std::string PositionManager::map_exchange_strategy_id(const std::string& exchange_strategy_id) const
{
    std::lock_guard<std::mutex> lock(strategy_mapping_mutex_);
    
    // 首先尝试使用映射表
    auto it = strategy_mappings_.find(exchange_strategy_id);
    if (it != strategy_mappings_.end()) {
        return it->second;
    }
    
    // 如果映射表中没有，尝试使用回调函数
    if (strategy_mapping_callback_) {
        return strategy_mapping_callback_(exchange_strategy_id);
    }
    
    // 如果都没有，返回原始ID
    return exchange_strategy_id;
}

void PositionManager::add_strategy_mapping(const std::string& exchange_strategy_id, const std::string& internal_strategy_id)
{
    std::lock_guard<std::mutex> lock(strategy_mapping_mutex_);
    strategy_mappings_[exchange_strategy_id] = internal_strategy_id;
}

void PositionManager::remove_strategy_mapping(const std::string& exchange_strategy_id)
{
    std::lock_guard<std::mutex> lock(strategy_mapping_mutex_);
    strategy_mappings_.erase(exchange_strategy_id);
}

std::unordered_map<std::string, std::string> PositionManager::get_strategy_mappings() const
{
    std::lock_guard<std::mutex> lock(strategy_mapping_mutex_);
    return strategy_mappings_;
}

} // namespace execution
} // namespace tes