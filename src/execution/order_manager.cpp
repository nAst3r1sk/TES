#include "execution/order_manager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace tes {
namespace execution {

OrderManager::OrderManager() 
    : running_(false)
    , initialized_(false)
    , order_sequence_(0)
    , cleanup_running_(false)
{
    // 初始化统计信息
    statistics_.total_orders_created = 0;
    statistics_.total_orders_submitted = 0;
    statistics_.total_orders_filled = 0;
    statistics_.total_orders_cancelled = 0;
    statistics_.total_orders_rejected = 0;
    statistics_.total_trades = 0;
    statistics_.active_orders = 0;
    statistics_.average_fill_time = 0.0;
    statistics_.last_order_time = std::chrono::high_resolution_clock::time_point{};
    statistics_.last_trade_time = std::chrono::high_resolution_clock::time_point{};
}

OrderManager::~OrderManager()
{
    stop();
    cleanup();
}

bool OrderManager::initialize()
{
    if (initialized_.load()) {
        return true;
    }
    
    try {
        // 清理现有数据
        {
            std::lock_guard<std::mutex> orders_lock(orders_mutex_);
            std::lock_guard<std::mutex> trades_lock(trades_mutex_);
            orders_.clear();
            trades_by_order_.clear();
        }
        
        initialized_.store(true);
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

bool OrderManager::start()
{
    if (!initialize()) {
        return false;
    }
    
    if (running_.load()) {
        return true;
    }
    
    try {
        running_.store(true);
        
        // 启动清理线程
        cleanup_running_.store(true);
        cleanup_thread_ = std::thread(&OrderManager::cleanup_worker, this);
        
        return true;
        
    } catch (const std::exception& e) {
        running_.store(false);
        return false;
    }
}

void OrderManager::stop()
{
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 停止清理线程
    cleanup_running_.store(false);
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

void OrderManager::cleanup()
{
    stop();
    
    {
        std::lock_guard<std::mutex> orders_lock(orders_mutex_);
        std::lock_guard<std::mutex> trades_lock(trades_mutex_);
        orders_.clear();
        trades_by_order_.clear();
    }
    
    initialized_.store(false);
}

bool OrderManager::is_running() const
{
    return running_.load();
}

std::string OrderManager::create_order(const Order& order)
{
    if (!running_.load()) {
        return "";
    }
    
    // 验证订单
    if (config_.enable_order_validation && !validate_order(order)) {
        return "";
    }
    
    // 检查重复订单
    if (config_.enable_duplicate_check && check_duplicate_order(order)) {
        return "";
    }
    
    // 检查待处理订单数量限制
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        if (orders_.size() >= config_.max_pending_orders) {
            return "";
        }
    }
    
    // 生成订单ID
    std::string order_id_str = generate_order_id();
    
    // 创建订单副本
    auto new_order = std::make_shared<Order>(order);
    // 将字符串ID转换为数字ID（使用哈希或简单的数字转换）
    new_order->order_id = std::hash<std::string>{}(order_id_str);
    new_order->status = OrderStatus::PENDING;
    new_order->create_time = std::chrono::high_resolution_clock::now();
    new_order->update_time = new_order->create_time;
    
    // 存储订单（使用字符串ID作为键）
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        orders_[order_id_str] = new_order;
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.total_orders_created++;
        statistics_.last_order_time = new_order->create_time;
        statistics_.active_orders++;
    }
    
    // 通知订单事件
    notify_order_event(*new_order);
    
    return order_id_str;
}

bool OrderManager::submit_order(const std::string& order_id)
{
    std::shared_ptr<Order> order;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        order = it->second;
    }
    
    if (order->status != OrderStatus::PENDING) {
        return false;
    }
    
    // 如果有交易所适配器，通过适配器提交订单
    if (has_exchange_adapter()) {
        auto adapter = get_exchange_adapter();
        if (adapter) {
            std::string exchange_order_id = adapter->submit_order_to_exchange(*order);
            if (!exchange_order_id.empty()) {
                // 更新订单的交易所ID
                {
                    std::lock_guard<std::mutex> lock(orders_mutex_);
                    // Note: Order struct doesn't have exchange_order_id field
                    // The exchange order ID is managed internally by the exchange adapter
                }
                update_order_status(order_id, OrderStatus::SUBMITTED);
            } else {
                update_order_status(order_id, OrderStatus::REJECTED, "Failed to submit to exchange");
                return false;
            }
        }
    } else {
        // 更新订单状态
        update_order_status(order_id, OrderStatus::SUBMITTED);
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.total_orders_submitted++;
    }
    
    return true;
}

bool OrderManager::cancel_order(const std::string& order_id)
{
    std::shared_ptr<Order> order;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        order = it->second;
    }
    
    // 只能取消待处理或已提交的订单
    if (order->status != OrderStatus::PENDING && 
        order->status != OrderStatus::SUBMITTED && 
        order->status != OrderStatus::PARTIALLY_FILLED) {
        return false;
    }
    
    // 如果有交易所适配器，通过适配器取消订单
    bool exchange_cancel_success = true;
    if (has_exchange_adapter() && order->status != OrderStatus::PENDING) {
        auto adapter = get_exchange_adapter();
        if (adapter) {
            exchange_cancel_success = adapter->cancel_order_on_exchange(order_id);
        }
    }
    
    if (exchange_cancel_success) {
        // 更新订单状态
        update_order_status(order_id, OrderStatus::CANCELLED);
        
        // 更新统计信息
        {
            std::lock_guard<std::mutex> lock(statistics_mutex_);
            statistics_.total_orders_cancelled++;
            if (statistics_.active_orders > 0) {
                statistics_.active_orders--;
            }
        }
        return true;
    } else {
        update_order_status(order_id, OrderStatus::ERROR, "Failed to cancel on exchange");
        return false;
    }
}

bool OrderManager::modify_order(const std::string& order_id, double new_quantity, double new_price)
{
    std::shared_ptr<Order> order;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        order = it->second;
    }
    
    // 只能修改待处理或已提交的订单
    if (order->status != OrderStatus::PENDING && 
        order->status != OrderStatus::SUBMITTED) {
        return false;
    }
    
    // 验证新参数
    if (new_quantity <= 0 || new_price <= 0) {
        return false;
    }
    
    // 如果有交易所适配器，通过适配器修改订单
    bool exchange_modify_success = true;
    if (has_exchange_adapter() && order->status != OrderStatus::PENDING) {
        auto adapter = get_exchange_adapter();
        if (adapter) {
            exchange_modify_success = adapter->modify_order_on_exchange(order_id, new_quantity, new_price);
        }
    }
    
    if (exchange_modify_success) {
        // 更新订单
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            order->quantity = new_quantity;
            order->price = new_price;
            order->update_time = std::chrono::high_resolution_clock::now();
        }
        
        // 通知订单事件
        notify_order_event(*order);
        return true;
    } else {
        update_order_status(order_id, OrderStatus::ERROR, "Failed to modify on exchange");
        return false;
    }
}

std::shared_ptr<Order> OrderManager::get_order(const std::string& order_id) const
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    auto it = orders_.find(order_id);
    return (it != orders_.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<Order>> OrderManager::get_orders_by_strategy(const std::string& strategy_id) const
{
    std::vector<std::shared_ptr<Order>> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        if (pair.second->strategy_id == strategy_id) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<Order>> OrderManager::get_orders_by_instrument(const std::string& instrument_id) const
{
    std::vector<std::shared_ptr<Order>> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        if (pair.second->instrument_id == instrument_id) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<Order>> OrderManager::get_active_orders() const
{
    std::vector<std::shared_ptr<Order>> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        if (pair.second->status == OrderStatus::PENDING ||
            pair.second->status == OrderStatus::SUBMITTED ||
            pair.second->status == OrderStatus::PARTIALLY_FILLED) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<Order>> OrderManager::get_all_orders() const
{
    std::vector<std::shared_ptr<Order>> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        result.push_back(pair.second);
    }
    
    return result;
}

void OrderManager::process_trade(const Trade& trade)
{
    if (!running_.load()) {
        return;
    }
    
    std::shared_ptr<Order> order;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(trade.order_id);
        if (it == orders_.end()) {
            return; // 订单不存在
        }
        order = it->second;
    }
    
    // 更新订单信息
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        order->filled_quantity += trade.quantity;
        
        // 计算平均成交价格
        if (order->filled_quantity > 0) {
            order->average_price = ((order->average_price * (order->filled_quantity - trade.quantity)) + 
                                   (trade.price * trade.quantity)) / order->filled_quantity;
        }
        
        // 更新订单状态
        if (order->filled_quantity >= order->quantity) {
            order->status = OrderStatus::FILLED;
        } else {
            order->status = OrderStatus::PARTIALLY_FILLED;
        }
        
        order->update_time = std::chrono::high_resolution_clock::now();
    }
    
    // 存储成交记录
    {
        std::lock_guard<std::mutex> lock(trades_mutex_);
        trades_by_order_[trade.order_id].push_back(trade);
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.total_trades++;
        statistics_.last_trade_time = trade.trade_time;
        
        if (order->status == OrderStatus::FILLED) {
            statistics_.total_orders_filled++;
            if (statistics_.active_orders > 0) {
                statistics_.active_orders--;
            }
            
            // 计算平均成交时间
            auto fill_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                order->update_time - order->create_time).count();
            statistics_.average_fill_time = (statistics_.average_fill_time * 0.9) + (fill_time * 0.1);
        }
    }
    
    // 通知事件
    notify_order_event(*order);
    notify_trade_event(trade);
}

std::vector<Trade> OrderManager::get_trades_by_order(const std::string& order_id) const
{
    std::lock_guard<std::mutex> lock(trades_mutex_);
    auto it = trades_by_order_.find(order_id);
    return (it != trades_by_order_.end()) ? it->second : std::vector<Trade>();
}

std::vector<Trade> OrderManager::get_trades_by_strategy(const std::string& strategy_id) const
{
    std::vector<Trade> result;
    
    std::lock_guard<std::mutex> orders_lock(orders_mutex_);
    std::lock_guard<std::mutex> trades_lock(trades_mutex_);
    
    for (const auto& order_pair : orders_) {
        if (order_pair.second->strategy_id == strategy_id) {
            auto trades_it = trades_by_order_.find(order_pair.first);
            if (trades_it != trades_by_order_.end()) {
                result.insert(result.end(), trades_it->second.begin(), trades_it->second.end());
            }
        }
    }
    
    return result;
}

void OrderManager::set_order_event_callback(OrderEventCallback callback)
{
    order_callback_ = callback;
}

void OrderManager::set_trade_event_callback(TradeEventCallback callback)
{
    trade_callback_ = callback;
}

void OrderManager::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

OrderManager::Config OrderManager::get_config() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

OrderManager::Statistics OrderManager::get_statistics() const
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    return statistics_;
}

bool OrderManager::validate_order(const Order& order) const
{
    // 基本验证
    if (order.instrument_id.empty() || order.strategy_id.empty()) {
        return false;
    }
    
    if (order.quantity <= 0 || order.price <= 0) {
        return false;
    }
    
    return true;
}

bool OrderManager::check_duplicate_order(const Order& order) const
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    for (const auto& pair : orders_) {
        const auto& existing_order = pair.second;
        
        // 检查是否为重复订单（相同策略、品种、方向、数量、价格）
        if (existing_order->strategy_id == order.strategy_id &&
            existing_order->instrument_id == order.instrument_id &&
            existing_order->side == order.side &&
            std::abs(existing_order->quantity - order.quantity) < 1e-6 &&
            std::abs(existing_order->price - order.price) < 1e-6 &&
            (existing_order->status == OrderStatus::PENDING ||
             existing_order->status == OrderStatus::SUBMITTED)) {
            return true;
        }
    }
    
    return false;
}

std::string OrderManager::generate_order_id()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    auto sequence = order_sequence_.fetch_add(1);
    
    std::ostringstream oss;
    oss << "ORD_" << timestamp << "_" << std::setfill('0') << std::setw(6) << sequence;
    return oss.str();
}

void OrderManager::update_order_status(const std::string& order_id, OrderStatus status, const std::string& error_message)
{
    std::shared_ptr<Order> order;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return;
        }
        order = it->second;
        
        order->status = status;
        order->update_time = std::chrono::high_resolution_clock::now();
        if (!error_message.empty()) {
            order->error_message = error_message;
        }
    }
    
    // 更新统计信息
    if (status == OrderStatus::REJECTED) {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.total_orders_rejected++;
        if (statistics_.active_orders > 0) {
            statistics_.active_orders--;
        }
    }
    
    // 通知订单事件
    notify_order_event(*order);
}

void OrderManager::notify_order_event(const Order& order)
{
    if (order_callback_) {
        try {
            order_callback_(order);
        } catch (const std::exception& e) {
        }
    }
}

void OrderManager::notify_trade_event(const Trade& trade)
{
    if (trade_callback_) {
        try {
            trade_callback_(trade);
        } catch (const std::exception& e) {
        }
    }
}

void OrderManager::cleanup_expired_orders()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto timeout = std::chrono::seconds(config_.order_timeout_seconds);
    
    std::vector<std::string> expired_orders;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        for (const auto& pair : orders_) {
            const auto& order = pair.second;
            if ((order->status == OrderStatus::PENDING || order->status == OrderStatus::SUBMITTED) &&
                (now - order->create_time) > timeout) {
                expired_orders.push_back(pair.first);
            }
        }
    }
    
    // 取消过期订单
    for (const auto& order_id : expired_orders) {
        update_order_status(order_id, OrderStatus::CANCELLED, "Order expired");
    }
}

void OrderManager::update_statistics()
{
    std::lock_guard<std::mutex> orders_lock(orders_mutex_);
    std::lock_guard<std::mutex> statistics_lock(statistics_mutex_);
    
    uint32_t active_count = 0;
    for (const auto& pair : orders_) {
        if (pair.second->status == OrderStatus::PENDING ||
            pair.second->status == OrderStatus::SUBMITTED ||
            pair.second->status == OrderStatus::PARTIALLY_FILLED) {
            active_count++;
        }
    }
    
    statistics_.active_orders = active_count;
}

void OrderManager::cleanup_worker()
{
    while (cleanup_running_.load()) {
        try {
            cleanup_expired_orders();
            update_statistics();
            
            // 休眠
            std::this_thread::sleep_for(std::chrono::seconds(config_.cleanup_interval_seconds));
        } catch (const std::exception& e) {
            // 记录错误但继续运行
        }
    }
}

// 外部订单同步方法实现
void OrderManager::sync_order_from_exchange(const Order& order)
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    auto it = orders_.find(order.order_id);
    if (it != orders_.end()) {
        // 更新现有订单
        *(it->second) = order;
        it->second->update_time = std::chrono::high_resolution_clock::now();
    } else {
        // 添加新订单
        auto new_order = std::make_shared<Order>(order);
        new_order->update_time = std::chrono::high_resolution_clock::now();
        orders_[order.order_id] = new_order;
        
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        statistics_.total_orders_created++;
        if (order.status == OrderStatus::PENDING || order.status == OrderStatus::SUBMITTED || order.status == OrderStatus::PARTIALLY_FILLED) {
            statistics_.active_orders++;
        }
    }
    
    // 通知订单事件
    notify_order_event(order);
}

void OrderManager::sync_orders_from_exchange(const std::vector<Order>& orders)
{
    for (const auto& order : orders) {
        sync_order_from_exchange(order);
    }
}

bool OrderManager::remove_order(const std::string& order_id)
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    auto it = orders_.find(order_id);
    if (it != orders_.end()) {
        orders_.erase(it);
        
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        if (statistics_.active_orders > 0) {
            statistics_.active_orders--;
        }
        return true;
    }
    return false;
}

// 交易所适配器管理方法实现
void OrderManager::set_exchange_adapter(std::shared_ptr<ExchangeAdapter> adapter)
{
    std::lock_guard<std::mutex> lock(adapter_mutex_);
    exchange_adapter_ = adapter;
}

std::shared_ptr<ExchangeAdapter> OrderManager::get_exchange_adapter() const
{
    std::lock_guard<std::mutex> lock(adapter_mutex_);
    return exchange_adapter_;
}

bool OrderManager::has_exchange_adapter() const
{
    std::lock_guard<std::mutex> lock(adapter_mutex_);
    return exchange_adapter_ != nullptr;
}

// 订单同步和状态管理方法实现
void OrderManager::force_update_order_status(const std::string& order_id, OrderStatus status, const std::string& error_message)
{
    update_order_status(order_id, status, error_message);
}

void OrderManager::batch_update_orders(const std::vector<Order>& orders)
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    for (const auto& order : orders) {
        auto it = orders_.find(order.order_id);
        if (it != orders_.end()) {
            *(it->second) = order;
            it->second->update_time = std::chrono::high_resolution_clock::now();
            
            // 通知订单事件（在锁外进行）
            notify_order_event(order);
        }
    }
}

void OrderManager::clear_expired_orders(std::chrono::seconds max_age)
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    auto now = std::chrono::high_resolution_clock::now();
    auto it = orders_.begin();
    
    while (it != orders_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second->update_time);
        if (age > max_age && (it->second->status == OrderStatus::FILLED || 
                              it->second->status == OrderStatus::CANCELLED || 
                              it->second->status == OrderStatus::REJECTED || 
                              it->second->status == OrderStatus::ERROR)) {
            it = orders_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace execution
} // namespace tes