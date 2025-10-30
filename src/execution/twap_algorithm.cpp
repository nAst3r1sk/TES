#include "execution/twap_algorithm.h"
#include "execution/order_manager.h"
#include "common/common_types.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <map>

namespace tes {
namespace execution {

TWAPAlgorithm::TWAPAlgorithm(std::shared_ptr<OrderManager> order_manager,
                               std::shared_ptr<GatewayAdapter> gateway_adapter)
    : order_manager_(order_manager)
    , gateway_adapter_(gateway_adapter)
    , execution_thread_pool_(nullptr)
    , running_(false)
    , initialized_(false)
    , execution_sequence_(0)
    , slice_sequence_(0) {
    // 初始化统计信息
    statistics_.total_executions = 0;
    statistics_.completed_executions = 0;
    statistics_.cancelled_executions = 0;
    statistics_.error_executions = 0;
    statistics_.average_execution_time = 0.0;
    statistics_.average_slippage = 0.0;
    statistics_.total_volume_executed = 0.0;
    statistics_.last_execution_time = std::chrono::high_resolution_clock::time_point{};
}

TWAPAlgorithm::~TWAPAlgorithm()
{
    stop();
    cleanup();
}

bool TWAPAlgorithm::initialize()
{
    if (initialized_.load()) {
        return true;
    }
    
    if (!order_manager_) {
        return false;
    }
    
    try {
        // 初始化线程池
        execution_thread_pool_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
        
        // 清理现有数据
        {
            std::unique_lock<std::shared_mutex> executions_lock(executions_mutex_);
            std::lock_guard<std::mutex> slices_lock(slices_mutex_);
            executions_.clear();
            execution_slices_.clear();
            while (!scheduled_slices_.empty()) {
                scheduled_slices_.pop();
            }
        }
        
        initialized_.store(true);
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

bool TWAPAlgorithm::start()
{
    if (!initialize()) {
        return false;
    }
    
    if (running_.load()) {
        return true;
    }
    
    try {
        running_.store(true);
        
        // 启动执行线程
        execution_thread_ = std::thread(&TWAPAlgorithm::execution_worker, this);
        
        return true;
        
    } catch (const std::exception& e) {
        running_.store(false);
        return false;
    }
}

void TWAPAlgorithm::stop()
{
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 等待执行线程结束
    if (execution_thread_.joinable()) {
        execution_thread_.join();
    }
}

void TWAPAlgorithm::cleanup()
{
    stop();
    
    {
        std::unique_lock<std::shared_mutex> executions_lock(executions_mutex_);
        std::lock_guard<std::mutex> slices_lock(slices_mutex_);
        executions_.clear();
        execution_slices_.clear();
        while (!scheduled_slices_.empty()) {
            scheduled_slices_.pop();
        }
    }
    
    initialized_.store(false);
}

bool TWAPAlgorithm::is_running() const
{
    return running_.load();
}

std::string TWAPAlgorithm::start_execution(const std::string& strategy_id,
                                          const std::string& instrument_id,
                                          OrderSide side,
                                          const execution::TWAPParams& params)
{
    if (!running_.load()) {
        return "";
    }
    
    // 验证参数
    if (strategy_id.empty() || instrument_id.empty() || 
        params.total_quantity <= 0 || params.duration_minutes == 0 || params.slice_count == 0) {
        return "";
    }
    
    // 生成执行ID
    std::string execution_id = generate_execution_id();
    
    // 创建算法执行对象
    auto execution = std::make_shared<execution::AlgorithmExecution>();
    execution->execution_id = execution_id;
    execution->strategy_id = strategy_id;
    execution->instrument_id = instrument_id;
    execution->side = side;
    execution->params = params;
    execution->status = execution::AlgorithmStatus::RUNNING;
    execution->executed_quantity = 0.0;
    execution->remaining_quantity = params.total_quantity;
    execution->average_price = 0.0;
    execution->start_time = std::chrono::high_resolution_clock::now();
    
    // 存储执行对象
    {
        std::unique_lock<std::shared_mutex> lock(executions_mutex_);
        executions_[execution_id] = execution;
    }
    
    // 计算切片
    calculate_slices(execution);
    
    // 调度第一个切片
    schedule_next_slice(execution_id);
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.total_executions++;
        statistics_.last_execution_time = execution->start_time;
    }
    
    // 通知事件
    notify_execution_event(*execution);
    
    return execution_id;
}

bool TWAPAlgorithm::pause_execution(const std::string& execution_id)
{
    std::shared_ptr<AlgorithmExecution> execution;
    
    {
        std::shared_lock<std::shared_mutex> lock(executions_mutex_);
        auto it = executions_.find(execution_id);
        if (it == executions_.end()) {
            return false;
        }
        execution = it->second;
    }
    
    if (execution->status != AlgorithmStatus::RUNNING) {
        return false;
    }
    
    execution->status = AlgorithmStatus::PAUSED;
    notify_execution_event(*execution);
    
    return true;
}

bool TWAPAlgorithm::resume_execution(const std::string& execution_id)
{
    std::shared_ptr<AlgorithmExecution> execution;
    
    {
        std::shared_lock<std::shared_mutex> lock(executions_mutex_);
        auto it = executions_.find(execution_id);
        if (it == executions_.end()) {
            return false;
        }
        execution = it->second;
    }
    
    if (execution->status != AlgorithmStatus::PAUSED) {
        return false;
    }
    
    execution->status = AlgorithmStatus::RUNNING;
    schedule_next_slice(execution_id);
    notify_execution_event(*execution);
    
    return true;
}

bool TWAPAlgorithm::cancel_execution(const std::string& execution_id)
{
    std::shared_ptr<AlgorithmExecution> execution;
    
    {
        std::shared_lock<std::shared_mutex> lock(executions_mutex_);
        auto it = executions_.find(execution_id);
        if (it == executions_.end()) {
            return false;
        }
        execution = it->second;
    }
    
    if (execution->status == AlgorithmStatus::COMPLETED || 
        execution->status == AlgorithmStatus::CANCELLED) {
        return false;
    }
    
    // 取消所有未执行的子订单
    for (const auto& order_id : execution->child_orders) {
        order_manager_->cancel_order(order_id);
    }
    
    execution->status = AlgorithmStatus::CANCELLED;
    execution->end_time = std::chrono::high_resolution_clock::now();
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.cancelled_executions++;
    }
    
    notify_execution_event(*execution);
    
    return true;
}

std::shared_ptr<AlgorithmExecution> TWAPAlgorithm::get_execution(const std::string& execution_id) const
{
    std::shared_lock<std::shared_mutex> lock(executions_mutex_);
    auto it = executions_.find(execution_id);
    return (it != executions_.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<AlgorithmExecution>> TWAPAlgorithm::get_executions_by_strategy(
    const std::string& strategy_id) const
{
    std::vector<std::shared_ptr<AlgorithmExecution>> result;
    
    std::shared_lock<std::shared_mutex> lock(executions_mutex_);
    for (const auto& pair : executions_) {
        if (pair.second->strategy_id == strategy_id) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<AlgorithmExecution>> TWAPAlgorithm::get_active_executions() const
{
    std::vector<std::shared_ptr<AlgorithmExecution>> result;
    
    std::shared_lock<std::shared_mutex> lock(executions_mutex_);
    for (const auto& pair : executions_) {
        if (pair.second->status == AlgorithmStatus::RUNNING ||
            pair.second->status == AlgorithmStatus::PAUSED) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<AlgorithmExecution>> TWAPAlgorithm::get_all_executions() const
{
    std::vector<std::shared_ptr<AlgorithmExecution>> result;
    
    std::shared_lock<std::shared_mutex> lock(executions_mutex_);
    for (const auto& pair : executions_) {
        result.push_back(pair.second);
    }
    
    return result;
}

void TWAPAlgorithm::update_market_data(const execution::MarketData& market_data)
{
    std::unique_lock<std::shared_mutex> lock(market_data_mutex_);
    market_data_cache_[market_data.instrument_id] = market_data;
}

void TWAPAlgorithm::set_event_callback(TWAPEventCallback callback)
{
    event_callback_ = callback;
}

void TWAPAlgorithm::set_order_callback(TWAPOrderCallback callback)
{
    order_callback_ = callback;
}

void TWAPAlgorithm::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

TWAPAlgorithm::Config TWAPAlgorithm::get_config() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

TWAPAlgorithm::Statistics TWAPAlgorithm::get_statistics() const
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    return statistics_;
}

std::string TWAPAlgorithm::generate_execution_id()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    auto sequence = execution_sequence_.fetch_add(1);
    
    std::ostringstream oss;
    oss << "TWAP_" << timestamp << "_" << std::setfill('0') << std::setw(6) << sequence;
    return oss.str();
}

std::string TWAPAlgorithm::generate_slice_id()
{
    auto sequence = slice_sequence_.fetch_add(1);
    
    std::ostringstream oss;
    oss << "SLICE_" << std::setfill('0') << std::setw(8) << sequence;
    return oss.str();
}

void TWAPAlgorithm::calculate_slices(std::shared_ptr<AlgorithmExecution> execution)
{
    std::vector<ExecutionSlice> slices;
    
    double slice_quantity = execution->params.total_quantity / execution->params.slice_count;
    uint32_t slice_interval = (execution->params.duration_minutes * 60) / execution->params.slice_count;
    
    auto current_time = std::chrono::high_resolution_clock::now();
    
    for (uint32_t i = 0; i < execution->params.slice_count; ++i) {
        ExecutionSlice slice;
        slice.slice_id = generate_slice_id();
        slice.execution_id = execution->execution_id;
        slice.quantity = slice_quantity;
        slice.scheduled_time = current_time + std::chrono::seconds(i * slice_interval);
        slice.executed = false;
        
        slices.push_back(slice);
    }
    
    // 调整最后一个切片的数量以确保总量准确
    if (!slices.empty()) {
        double total_slice_quantity = slice_quantity * (execution->params.slice_count - 1);
        slices.back().quantity = execution->params.total_quantity - total_slice_quantity;
    }
    
    {
        std::lock_guard<std::mutex> lock(slices_mutex_);
        execution_slices_[execution->execution_id] = slices;
    }
}

void TWAPAlgorithm::schedule_next_slice(const std::string& execution_id)
{
    std::lock_guard<std::mutex> lock(slices_mutex_);
    
    auto slices_it = execution_slices_.find(execution_id);
    if (slices_it == execution_slices_.end()) {
        return;
    }
    
    // 找到下一个未执行的切片
    for (auto& slice : slices_it->second) {
        if (!slice.executed) {
            scheduled_slices_.push({slice.scheduled_time, execution_id});
            break;
        }
    }
}

void TWAPAlgorithm::execute_slice(const std::string& execution_id, const ExecutionSlice& slice)
{
    auto execution = get_execution(execution_id);
    if (!execution || execution->status != AlgorithmStatus::RUNNING) {
        return;
    }
    
    // 从缓存获取市场数据，而不是从MarketDataManager
    execution::MarketData market_data;
    {
        std::shared_lock<std::shared_mutex> lock(market_data_mutex_);
        auto it = market_data_cache_.find(execution->instrument_id);
        if (it != market_data_cache_.end()) {
            market_data = it->second;
        } else {
            // 如果缓存中没有数据，使用默认值
            market_data.instrument_id = execution->instrument_id;
            market_data.bid_price = 0.0;
            market_data.ask_price = 0.0;
            market_data.last_price = 0.0;
            market_data.volume = 0.0;
        }
    }
    
    // 计算切片大小和目标价格
    double actual_slice_size = calculate_slice_size(execution, market_data);
    double target_price = calculate_target_price(execution, market_data);
    
    // 提交订单 - 使用不同的Order类型
    std::string order_id;
    if (gateway_adapter_) {
        // 为Gateway接口创建execution::Order
        execution::Order binance_order;
        binance_order.client_order_id = slice.slice_id;
        binance_order.instrument_id = execution->instrument_id;
        binance_order.strategy_id = execution->strategy_id;
        binance_order.type = execution::OrderType::LIMIT;
        binance_order.side = (execution->side == execution::OrderSide::BUY) ? execution::OrderSide::BUY : execution::OrderSide::SELL;
        binance_order.time_in_force = execution::TimeInForce::IOC;
        binance_order.quantity = actual_slice_size;
        binance_order.price = target_price;
        order_id = gateway_adapter_->submit_order(binance_order);
    } else if (order_manager_) {
        // 为OrderManager创建Order
        Order order;
        order.client_order_id = slice.slice_id;
        order.instrument_id = execution->instrument_id;
        order.strategy_id = execution->strategy_id;
        order.type = OrderType::LIMIT;
        order.side = (execution->side == execution::OrderSide::BUY) ? OrderSide::BUY : OrderSide::SELL;
        order.time_in_force = TimeInForce::IOC;
        order.quantity = actual_slice_size;
        order.price = target_price;
        order_id = order_manager_->create_order(order);
        if (!order_id.empty()) {
            order_manager_->submit_order(order_id);
        }
    }
    
    if (!order_id.empty()) {
        // 记录子订单
        execution->child_orders.push_back(order_id);
        
        // 标记切片为已执行
        {
            std::lock_guard<std::mutex> lock(slices_mutex_);
            auto slices_it = execution_slices_.find(execution_id);
            if (slices_it != execution_slices_.end()) {
                for (auto& s : slices_it->second) {
                    if (s.slice_id == slice.slice_id) {
                        s.executed = true;
                        s.order_id = order_id;
                        break;
                    }
                }
            }
        }
        
        // 通知订单事件 - 使用适当的Order对象
        if (gateway_adapter_) {
            // 为Gateway接口创建Order用于通知
            Order notification_order;
            notification_order.order_id = order_id;
            notification_order.client_order_id = slice.slice_id;
            notification_order.instrument_id = execution->instrument_id;
            notification_order.strategy_id = execution->strategy_id;
            notification_order.type = OrderType::LIMIT;
            notification_order.side = (execution->side == execution::OrderSide::BUY) ? OrderSide::BUY : OrderSide::SELL;
            notification_order.time_in_force = TimeInForce::IOC;
            notification_order.quantity = actual_slice_size;
            notification_order.price = target_price;
            notify_order_event(execution_id, notification_order);
        } else if (order_manager_) {
            // 使用已创建的order对象
            Order notification_order;
            notification_order.order_id = order_id;
            notification_order.client_order_id = slice.slice_id;
            notification_order.instrument_id = execution->instrument_id;
            notification_order.strategy_id = execution->strategy_id;
            notification_order.type = OrderType::LIMIT;
            notification_order.side = (execution->side == execution::OrderSide::BUY) ? OrderSide::BUY : OrderSide::SELL;
            notification_order.time_in_force = TimeInForce::IOC;
            notification_order.quantity = actual_slice_size;
            notification_order.price = target_price;
            notify_order_event(execution_id, notification_order);
        }
    }
}

void TWAPAlgorithm::update_execution_progress(const std::string& execution_id, const Order& order)
{
    auto execution = get_execution(execution_id);
    if (!execution) {
        return;
    }
    
    // 更新执行进度
    execution->executed_quantity += order.filled_quantity;
    execution->remaining_quantity = execution->params.total_quantity - execution->executed_quantity;
    
    // 计算平均价格
    if (execution->executed_quantity > 0) {
        execution->average_price = ((execution->average_price * (execution->executed_quantity - order.filled_quantity)) +
                                   (order.average_price * order.filled_quantity)) / execution->executed_quantity;
    }
    
    // 检查是否完成
    if (execution->remaining_quantity <= 0.001) { // 使用小的容差
        complete_execution(execution_id);
    } else {
        // 调度下一个切片
        schedule_next_slice(execution_id);
    }
    
    // 通知执行事件
    notify_execution_event(*execution);
}

void TWAPAlgorithm::complete_execution(const std::string& execution_id)
{
    auto execution = get_execution(execution_id);
    if (!execution) {
        return;
    }
    
    execution->status = AlgorithmStatus::COMPLETED;
    execution->end_time = std::chrono::high_resolution_clock::now();
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.completed_executions++;
        statistics_.total_volume_executed += execution->executed_quantity;
        
        // 计算平均执行时间
        auto execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            execution->end_time - execution->start_time).count();
        statistics_.average_execution_time = (statistics_.average_execution_time * 0.9) + (execution_time * 0.1);
    }
    
    notify_execution_event(*execution);
}

void TWAPAlgorithm::notify_execution_event(const AlgorithmExecution& execution)
{
    if (event_callback_) {
        try {
            event_callback_(execution);
        } catch (const std::exception& e) {
            // 忽略回调异常
        }
    }
}

void TWAPAlgorithm::notify_order_event(const std::string& execution_id, const Order& order)
{
    if (order_callback_) {
        try {
            order_callback_(execution_id, order);
        } catch (const std::exception& e) {
            // 忽略回调异常
        }
    }
}

double TWAPAlgorithm::calculate_slice_size(const std::shared_ptr<AlgorithmExecution>& execution,
                                           const execution::MarketData& market_data) const
{
    double base_slice_size = execution->remaining_quantity / 
        std::max(static_cast<size_t>(1), execution->params.slice_count - execution->child_orders.size());
    
    if (!config_.enable_adaptive_sizing) {
        return std::max(config_.min_slice_size, std::min(config_.max_slice_size, base_slice_size));
    }
    
    // 回退到传统的自适应切片大小计算
    double market_volume = market_data.volume;
    double participation_volume = market_volume * execution->params.participation_rate;
    
    double adaptive_size = std::min(base_slice_size, participation_volume);
    
    return std::max(config_.min_slice_size, std::min(config_.max_slice_size, adaptive_size));
}

double TWAPAlgorithm::calculate_target_price(const std::shared_ptr<AlgorithmExecution>& execution,
                                              const execution::MarketData& market_data) const
{
    double reference_price;
    
    // 使用基本的买卖价差进行价格计算
    if (execution->side == OrderSide::BUY) {
        // 买单：使用卖一价作为参考
        reference_price = market_data.ask_price;
    } else {
        // 卖单：使用买一价作为参考
        reference_price = market_data.bid_price;
    }
    
    // 应用价格容忍度
    double tolerance = reference_price * execution->params.price_tolerance;
    
    if (execution->side == OrderSide::BUY) {
        return reference_price + tolerance;
    } else {
        return reference_price - tolerance;
    }
}

uint32_t TWAPAlgorithm::calculate_slice_interval(const std::shared_ptr<AlgorithmExecution>& execution) const
{
    uint32_t base_interval = (execution->params.duration_minutes * 60) / execution->params.slice_count;
    
    // 添加随机性以避免可预测的交易模式
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.8, 1.2);
    
    uint32_t randomized_interval = static_cast<uint32_t>(base_interval * dis(gen));
    
    return std::max(config_.min_interval_seconds, 
                   std::min(config_.max_interval_seconds, randomized_interval));
}

bool TWAPAlgorithm::should_adjust_execution(const std::shared_ptr<AlgorithmExecution>& execution,
                                           const execution::MarketData& market_data) const
{
    // 检查市场冲击
    if (config_.enable_market_impact_control) {
        double current_participation = execution->executed_quantity / market_data.volume;
        if (current_participation > config_.max_participation_rate) {
            return true;
        }
    }
    
    // 检查价格偏离
    if (execution->average_price > 0) {
        double price_deviation = std::abs(market_data.last_price - execution->average_price) / execution->average_price;
        if (price_deviation > config_.price_improvement_threshold) {
            return true;
        }
    }
    
    return false;
}

void TWAPAlgorithm::execution_worker()
{
    while (running_.load()) {
        try {
            process_scheduled_slices();
            monitor_executions();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
        } catch (const std::exception& e) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
}

void TWAPAlgorithm::process_scheduled_slices()
{
    auto now = std::chrono::high_resolution_clock::now();
    
    // 收集需要执行的切片
    std::vector<std::pair<std::string, ExecutionSlice>> slices_to_execute;
    
    {
        std::lock_guard<std::mutex> lock(slices_mutex_);
        
        while (!scheduled_slices_.empty() && scheduled_slices_.top().first <= now) {
            std::string execution_id = scheduled_slices_.top().second;
            scheduled_slices_.pop();
            
            auto slices_it = execution_slices_.find(execution_id);
            if (slices_it != execution_slices_.end()) {
                for (const auto& slice : slices_it->second) {
                    if (!slice.executed && slice.scheduled_time <= now) {
                        slices_to_execute.emplace_back(execution_id, slice);
                        break; // 一次只执行一个切片
                    }
                }
            }
        }
    }
    
    // 按标的分组
    std::map<std::string, std::vector<std::pair<std::string, ExecutionSlice>>> instrument_slices;
    for (const auto& slice_pair : slices_to_execute) {
        const std::string& execution_id = slice_pair.first;
        
        auto execution = get_execution(execution_id);
        if (execution) {
            instrument_slices[execution->instrument_id].push_back(slice_pair);
        }
    }
    
    // 并发执行切片
    for (const auto& instrument_pair : instrument_slices) {
        const auto& slices = instrument_pair.second;
        
        if (execution_thread_pool_) {
            for (const auto& slice_pair : slices) {
                std::string execution_id = slice_pair.first;
                ExecutionSlice slice = slice_pair.second;
                execution_thread_pool_->enqueue([this, execution_id, slice]() {
                    execute_slice(execution_id, slice);
                });
            }
        } else {
            for (const auto& slice_pair : slices) {
                execute_slice(slice_pair.first, slice_pair.second);
            }
        }
    }
}

void TWAPAlgorithm::monitor_executions()
{
    std::vector<std::shared_ptr<AlgorithmExecution>> active_executions = get_active_executions();
    
    for (const auto& execution : active_executions) {
        // 检查执行超时
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - execution->start_time).count();
        
        if (duration > execution->params.duration_minutes + 5) { // 允许5分钟的缓冲时间
            // 超时，取消执行
            cancel_execution(execution->execution_id);
        }
    }
}

void TWAPAlgorithm::execute_slice_async(const std::string& execution_id, const ExecutionSlice& slice) {
    try {
        execute_slice(execution_id, slice);
    } catch (const std::exception& e) {
        // 异步执行异常处理
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_.error_executions++;
    }
}

void TWAPAlgorithm::process_instrument_executions(const std::string& instrument_id) {
    std::queue<std::string> execution_queue;
    
    {
        std::lock_guard<std::mutex> lock(instrument_queues_mutex_);
        auto it = instrument_execution_queues_.find(instrument_id);
        if (it != instrument_execution_queues_.end()) {
            execution_queue = std::move(it->second);
            instrument_execution_queues_.erase(it);
        }
    }
    
    while (!execution_queue.empty()) {
        std::string execution_id = execution_queue.front();
        execution_queue.pop();
        
        auto execution = get_execution(execution_id);
        if (execution && execution->status == AlgorithmStatus::RUNNING) {
            // 处理该执行的下一个切片
            std::lock_guard<std::mutex> lock(slices_mutex_);
            auto slices_it = execution_slices_.find(execution_id);
            if (slices_it != execution_slices_.end()) {
                for (const auto& slice : slices_it->second) {
                    if (!slice.executed) {
                        execute_slice_async(execution_id, slice);
                        break;
                    }
                }
            }
        }
    }
}

void TWAPAlgorithm::distribute_executions_to_threads() {
    std::unique_lock<std::shared_mutex> lock(executions_mutex_);
    
    for (const auto& [execution_id, execution] : executions_) {
        if (execution->status == AlgorithmStatus::RUNNING) {
            std::lock_guard<std::mutex> queue_lock(instrument_queues_mutex_);
            instrument_execution_queues_[execution->instrument_id].push(execution_id);
        }
    }
}

} // namespace execution
} // namespace tes