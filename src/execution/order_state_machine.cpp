#include "execution/order_state_machine.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace tes {
namespace execution {

OrderStateMachine::OrderStateMachine()
    : running_(false)
    , initialized_(false)
    , order_sequence_(0)
    , cleanup_running_(false)
{
    // 初始化统计信息
    memset(&statistics_, 0, sizeof(statistics_));
    statistics_.last_activity_time = std::chrono::high_resolution_clock::now();
}

OrderStateMachine::~OrderStateMachine()
{
    stop();
    cleanup();
}

bool OrderStateMachine::initialize(const Config& config)
{
    if (initialized_.load()) {
        return true;
    }
    
    try {
        set_config(config);
        
        // 清理现有数据
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            orders_.clear();
        }
        
        reset_statistics();
        initialized_.store(true);
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

bool OrderStateMachine::start()
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
        if (config_.enable_auto_cleanup) {
            cleanup_running_.store(true);
            cleanup_thread_ = std::thread(&OrderStateMachine::cleanup_worker, this);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        running_.store(false);
        return false;
    }
}

void OrderStateMachine::stop()
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

void OrderStateMachine::cleanup()
{
    stop();
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        orders_.clear();
    }
    
    initialized_.store(false);
}

bool OrderStateMachine::is_running() const
{
    return running_.load();
}

std::string OrderStateMachine::create_order(const Order& order)
{
    if (!running_.load()) {
        return "";
    }
    
    // 生成订单ID
    std::string order_id = generate_order_id();
    
    // 创建订单状态信息
    auto state_info = std::make_shared<OrderStateInfo>();
    state_info->order_id = order_id;
    state_info->client_order_id = order.client_order_id;
    state_info->instrument_id = order.instrument_id;
    state_info->strategy_id = order.strategy_id;
    state_info->side = order.side;
    state_info->quantity = order.quantity;
    state_info->price = order.price;
    state_info->filled_quantity = 0.0;
    state_info->average_price = 0.0;
    state_info->current_state = OrderState::CREATED;
    state_info->previous_state = OrderState::CREATED;
    state_info->submit_timeout = config_.default_submit_timeout;
    state_info->cancel_timeout = config_.default_cancel_timeout;
    
    auto now = std::chrono::high_resolution_clock::now();
    state_info->create_time = now;
    state_info->state_change_time = now;
    state_info->last_update_time = now;
    
    // 存储订单状态
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        orders_[order_id] = state_info;
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.total_orders_created++;
        statistics_.orders_by_state[static_cast<int>(OrderState::CREATED)]++;
        statistics_.last_activity_time = now;
    }
    
    // 通知状态变化
    if (state_change_callback_) {
        try {
            state_change_callback_(*state_info, OrderState::CREATED, OrderState::CREATED);
        } catch (const std::exception& e) {
            // 忽略回调异常
        }
    }
    
    return order_id;
}

bool OrderStateMachine::process_event(const std::string& order_id, OrderEvent event, const std::string& exchange_order_id)
{
    std::shared_ptr<OrderStateInfo> state_info;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        state_info = it->second;
    }
    
    OrderState new_state = state_info->current_state;
    bool state_changed = false;
    
    // 根据事件和当前状态确定新状态
    switch (event) {
        case OrderEvent::CREATE:
            // 订单已经在create_order中创建
            break;
            
        case OrderEvent::SUBMIT:
            if (state_info->current_state == OrderState::CREATED) {
                new_state = OrderState::PENDING_SUBMIT;
                state_changed = true;
            }
            break;
            
        case OrderEvent::ACKNOWLEDGE:
            if (state_info->current_state == OrderState::PENDING_SUBMIT) {
                new_state = OrderState::SUBMITTED;
                state_changed = true;
                if (!exchange_order_id.empty()) {
                    state_info->exchange_order_id = exchange_order_id;
                }
            }
            break;
            
        case OrderEvent::PARTIAL_FILL:
            if (state_info->current_state == OrderState::SUBMITTED || 
                state_info->current_state == OrderState::PARTIALLY_FILLED) {
                new_state = OrderState::PARTIALLY_FILLED;
                state_changed = true;
            }
            break;
            
        case OrderEvent::FILL:
            if (is_active_state(state_info->current_state)) {
                new_state = OrderState::FILLED;
                state_changed = true;
            }
            break;
            
        case OrderEvent::CANCEL_REQUEST:
            if (is_active_state(state_info->current_state)) {
                new_state = OrderState::PENDING_CANCEL;
                state_changed = true;
            }
            break;
            
        case OrderEvent::CANCEL_CONFIRM:
            if (state_info->current_state == OrderState::PENDING_CANCEL) {
                new_state = OrderState::CANCELLED;
                state_changed = true;
            }
            break;
            
        case OrderEvent::REJECT:
            if (!is_terminal_state(state_info->current_state)) {
                new_state = OrderState::REJECTED;
                state_changed = true;
            }
            break;
            
        case OrderEvent::EXPIRE:
            if (is_active_state(state_info->current_state)) {
                new_state = OrderState::EXPIRED;
                state_changed = true;
            }
            break;
            
        case OrderEvent::ERROR_OCCURRED:
            if (!is_terminal_state(state_info->current_state)) {
                new_state = OrderState::ERROR;
                state_changed = true;
            }
            break;
    }
    
    // 执行状态变化
    if (state_changed && is_valid_transition(state_info->current_state, new_state)) {
        return change_state(order_id, new_state, order_event_to_string(event));
    }
    
    return !state_changed; // 如果没有状态变化，返回true表示处理成功
}

bool OrderStateMachine::update_fill_info(const std::string& order_id, double filled_qty, double avg_price)
{
    std::shared_ptr<OrderStateInfo> state_info;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        state_info = it->second;
    }
    
    // 更新成交信息
    state_info->filled_quantity = filled_qty;
    state_info->average_price = avg_price;
    state_info->last_update_time = std::chrono::high_resolution_clock::now();
    
    // 根据成交情况更新状态
    if (filled_qty >= state_info->quantity) {
        return process_event(order_id, OrderEvent::FILL);
    } else if (filled_qty > 0) {
        return process_event(order_id, OrderEvent::PARTIAL_FILL);
    }
    
    return true;
}

bool OrderStateMachine::set_error(const std::string& order_id, const std::string& error_message)
{
    std::shared_ptr<OrderStateInfo> state_info;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        state_info = it->second;
    }
    
    state_info->last_error_message = error_message;
    state_info->last_update_time = std::chrono::high_resolution_clock::now();
    
    return process_event(order_id, OrderEvent::ERROR_OCCURRED);
}

std::shared_ptr<OrderStateInfo> OrderStateMachine::get_order_state(const std::string& order_id) const
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    auto it = orders_.find(order_id);
    return (it != orders_.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<OrderStateInfo>> OrderStateMachine::get_orders_by_state(OrderState state) const
{
    std::vector<std::shared_ptr<OrderStateInfo>> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        if (pair.second->current_state == state) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<OrderStateInfo>> OrderStateMachine::get_active_orders() const
{
    std::vector<std::shared_ptr<OrderStateInfo>> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        if (is_active_state(pair.second->current_state)) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<OrderStateInfo>> OrderStateMachine::get_orders_by_instrument(const std::string& instrument_id) const
{
    std::vector<std::shared_ptr<OrderStateInfo>> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        if (pair.second->instrument_id == instrument_id) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

bool OrderStateMachine::has_pending_order(const std::string& instrument_id, OrderSide side, double quantity, double price, double tolerance) const
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    for (const auto& pair : orders_) {
        const auto& state_info = pair.second;
        
        if (state_info->instrument_id == instrument_id &&
            state_info->side == side &&
            std::abs(state_info->quantity - quantity) < tolerance &&
            std::abs(state_info->price - price) < tolerance &&
            is_active_state(state_info->current_state)) {
            return true;
        }
    }
    
    return false;
}

bool OrderStateMachine::has_recent_executed_order(const std::string& instrument_id, OrderSide side, double quantity, double price, std::chrono::milliseconds time_window, double tolerance) const
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    auto now = std::chrono::high_resolution_clock::now();
    
    for (const auto& pair : orders_) {
        const auto& state_info = pair.second;
        
        // 检查是否是相同的订单参数
        if (state_info->instrument_id == instrument_id &&
            state_info->side == side &&
            std::abs(state_info->quantity - quantity) < tolerance &&
            std::abs(state_info->price - price) < tolerance) {
            
            // 检查是否是已执行的订单（完全成交或部分成交）
            if (state_info->current_state == OrderState::FILLED ||
                state_info->current_state == OrderState::PARTIALLY_FILLED) {
                
                // 检查是否在时间窗口内
                auto time_since_execution = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - state_info->state_change_time);
                if (time_since_execution <= time_window) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

std::vector<std::string> OrderStateMachine::get_pending_order_ids(const std::string& instrument_id) const
{
    std::vector<std::string> result;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        if (pair.second->instrument_id == instrument_id && 
            is_active_state(pair.second->current_state)) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

std::vector<std::string> OrderStateMachine::get_timeout_orders() const
{
    std::vector<std::string> result;
    auto now = std::chrono::high_resolution_clock::now();
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    for (const auto& pair : orders_) {
        const auto& state_info = pair.second;
        
        bool is_timeout = false;
        
        if (state_info->current_state == OrderState::PENDING_SUBMIT) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state_info->state_change_time);
            if (elapsed > state_info->submit_timeout) {
                is_timeout = true;
            }
        } else if (state_info->current_state == OrderState::PENDING_CANCEL) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state_info->state_change_time);
            if (elapsed > state_info->cancel_timeout) {
                is_timeout = true;
            }
        }
        
        if (is_timeout) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

bool OrderStateMachine::extend_timeout(const std::string& order_id, std::chrono::milliseconds additional_time)
{
    std::lock_guard<std::mutex> lock(orders_mutex_);
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;
    }
    
    auto& state_info = it->second;
    if (state_info->current_state == OrderState::PENDING_SUBMIT) {
        state_info->submit_timeout += additional_time;
    } else if (state_info->current_state == OrderState::PENDING_CANCEL) {
        state_info->cancel_timeout += additional_time;
    }
    
    return true;
}

void OrderStateMachine::set_state_change_callback(StateChangeCallback callback)
{
    state_change_callback_ = callback;
}

void OrderStateMachine::set_timeout_callback(OrderTimeoutCallback callback)
{
    timeout_callback_ = callback;
}

OrderStateMachine::Statistics OrderStateMachine::get_statistics() const
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    return statistics_;
}

void OrderStateMachine::reset_statistics()
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    memset(&statistics_, 0, sizeof(statistics_));
    statistics_.last_activity_time = std::chrono::high_resolution_clock::now();
}

void OrderStateMachine::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

OrderStateMachine::Config OrderStateMachine::get_config() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

bool OrderStateMachine::is_valid_transition(OrderState from, OrderState to) const
{
    // 定义有效的状态转换
    switch (from) {
        case OrderState::CREATED:
            return to == OrderState::PENDING_SUBMIT || to == OrderState::ERROR;
            
        case OrderState::PENDING_SUBMIT:
            return to == OrderState::SUBMITTED || to == OrderState::REJECTED || 
                   to == OrderState::ERROR || to == OrderState::EXPIRED;
                   
        case OrderState::SUBMITTED:
            return to == OrderState::ACKNOWLEDGED || to == OrderState::PARTIALLY_FILLED ||
                   to == OrderState::FILLED || to == OrderState::PENDING_CANCEL ||
                   to == OrderState::CANCELLED || to == OrderState::REJECTED ||
                   to == OrderState::ERROR || to == OrderState::EXPIRED;
                   
        case OrderState::ACKNOWLEDGED:
            return to == OrderState::PARTIALLY_FILLED || to == OrderState::FILLED ||
                   to == OrderState::PENDING_CANCEL || to == OrderState::CANCELLED ||
                   to == OrderState::ERROR || to == OrderState::EXPIRED;
                   
        case OrderState::PARTIALLY_FILLED:
            return to == OrderState::FILLED || to == OrderState::PENDING_CANCEL ||
                   to == OrderState::CANCELLED || to == OrderState::ERROR ||
                   to == OrderState::EXPIRED;
                   
        case OrderState::PENDING_CANCEL:
            return to == OrderState::CANCELLED || to == OrderState::FILLED ||
                   to == OrderState::ERROR;
                   
        case OrderState::FILLED:
        case OrderState::CANCELLED:
        case OrderState::REJECTED:
        case OrderState::EXPIRED:
        case OrderState::ERROR:
            return false; // 终态不能转换
    }
    
    return false;
}

bool OrderStateMachine::change_state(const std::string& order_id, OrderState new_state, const std::string& reason)
{
    std::shared_ptr<OrderStateInfo> state_info;
    OrderState old_state;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        
        state_info = it->second;
        old_state = state_info->current_state;
        
        // 更新状态
        state_info->previous_state = old_state;
        state_info->current_state = new_state;
        state_info->state_change_time = std::chrono::high_resolution_clock::now();
        state_info->last_update_time = state_info->state_change_time;
        state_info->state_change_count++;
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.orders_by_state[static_cast<int>(old_state)]--;
        statistics_.orders_by_state[static_cast<int>(new_state)]++;
        statistics_.state_transitions++;
        statistics_.last_activity_time = state_info->state_change_time;
        
        // 计算平均成交时间
        if (new_state == OrderState::FILLED) {
            auto fill_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                state_info->state_change_time - state_info->create_time).count();
            statistics_.average_fill_time_ms = (statistics_.average_fill_time_ms * 0.9) + (fill_time * 0.1);
        }
    }
    
    // 通知状态变化
    if (state_change_callback_) {
        try {
            state_change_callback_(*state_info, old_state, new_state);
        } catch (const std::exception& e) {
            // 忽略回调异常
        }
    }
    
    return true;
}

std::string OrderStateMachine::generate_order_id()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    auto sequence = order_sequence_.fetch_add(1);
    
    std::ostringstream oss;
    oss << "OSM_" << timestamp << "_" << std::setfill('0') << std::setw(6) << sequence;
    return oss.str();
}

void OrderStateMachine::cleanup_expired_orders()
{
    auto now = std::chrono::high_resolution_clock::now();
    std::vector<std::string> expired_orders;
    
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        for (const auto& pair : orders_) {
            const auto& state_info = pair.second;
            
            // 清理过期的终态订单
            if (is_terminal_state(state_info->current_state)) {
                auto age = std::chrono::duration_cast<std::chrono::hours>(
                    now - state_info->state_change_time);
                if (age > config_.order_retention_time) {
                    expired_orders.push_back(pair.first);
                }
            }
        }
        
        // 删除过期订单
        for (const auto& order_id : expired_orders) {
            orders_.erase(order_id);
        }
    }
}

void OrderStateMachine::check_timeouts()
{
    auto timeout_orders = get_timeout_orders();
    
    for (const auto& order_id : timeout_orders) {
        // 处理超时订单
        auto state_info = get_order_state(order_id);
        if (state_info) {
            if (state_info->current_state == OrderState::PENDING_SUBMIT) {
                process_event(order_id, OrderEvent::EXPIRE);
            } else if (state_info->current_state == OrderState::PENDING_CANCEL) {
                // 取消超时，可能需要强制设为错误状态
                set_error(order_id, "Cancel timeout");
            }
            
            // 通知超时回调
            if (timeout_callback_) {
                try {
                    timeout_callback_(*state_info);
                } catch (const std::exception& e) {
                    // 忽略回调异常
                }
            }
            
            // 更新统计
            {
                std::lock_guard<std::mutex> lock(statistics_mutex_);
                statistics_.timeout_events++;
            }
        }
    }
}

void OrderStateMachine::update_statistics()
{
    std::lock_guard<std::mutex> orders_lock(orders_mutex_);
    std::lock_guard<std::mutex> statistics_lock(statistics_mutex_);
    
    // 重新计算各状态的订单数量
    memset(statistics_.orders_by_state, 0, sizeof(statistics_.orders_by_state));
    
    for (const auto& pair : orders_) {
        int state_index = static_cast<int>(pair.second->current_state);
        statistics_.orders_by_state[state_index]++;
    }
}

void OrderStateMachine::cleanup_worker()
{
    while (cleanup_running_.load()) {
        try {
            cleanup_expired_orders();
            check_timeouts();
            update_statistics();
            
            // 休眠
            std::this_thread::sleep_for(config_.cleanup_interval);
        } catch (const std::exception& e) {
            // 记录错误但继续运行
        }
    }
}

// 辅助函数实现
std::string order_state_to_string(OrderState state)
{
    switch (state) {
        case OrderState::CREATED: return "CREATED";
        case OrderState::PENDING_SUBMIT: return "PENDING_SUBMIT";
        case OrderState::SUBMITTED: return "SUBMITTED";
        case OrderState::ACKNOWLEDGED: return "ACKNOWLEDGED";
        case OrderState::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderState::FILLED: return "FILLED";
        case OrderState::PENDING_CANCEL: return "PENDING_CANCEL";
        case OrderState::CANCELLED: return "CANCELLED";
        case OrderState::REJECTED: return "REJECTED";
        case OrderState::EXPIRED: return "EXPIRED";
        case OrderState::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string order_event_to_string(OrderEvent event)
{
    switch (event) {
        case OrderEvent::CREATE: return "CREATE";
        case OrderEvent::SUBMIT: return "SUBMIT";
        case OrderEvent::ACKNOWLEDGE: return "ACKNOWLEDGE";
        case OrderEvent::PARTIAL_FILL: return "PARTIAL_FILL";
        case OrderEvent::FILL: return "FILL";
        case OrderEvent::CANCEL_REQUEST: return "CANCEL_REQUEST";
        case OrderEvent::CANCEL_CONFIRM: return "CANCEL_CONFIRM";
        case OrderEvent::REJECT: return "REJECT";
        case OrderEvent::EXPIRE: return "EXPIRE";
        case OrderEvent::ERROR_OCCURRED: return "ERROR_OCCURRED";
        default: return "UNKNOWN";
    }
}

bool is_terminal_state(OrderState state)
{
    return state == OrderState::FILLED ||
           state == OrderState::CANCELLED ||
           state == OrderState::REJECTED ||
           state == OrderState::EXPIRED ||
           state == OrderState::ERROR;
}

bool is_active_state(OrderState state)
{
    return state == OrderState::PENDING_SUBMIT ||
           state == OrderState::SUBMITTED ||
           state == OrderState::ACKNOWLEDGED ||
           state == OrderState::PARTIALLY_FILLED ||
           state == OrderState::PENDING_CANCEL;
}

} // namespace execution
} // namespace tes