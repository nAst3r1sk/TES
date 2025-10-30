#pragma once

#include "../common/common_types.h"
#include "types.h"
#include "order_manager.h"
#include "thread_pool.h"
#include "gateway_adapter.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <shared_mutex>

namespace tes {
namespace execution {

class OrderManager;

// TWAP算法事件回调
using TWAPEventCallback = std::function<void(const execution::AlgorithmExecution&)>;
using TWAPOrderCallback = std::function<void(const std::string& execution_id, const Order&)>;

class TWAPAlgorithm {
public:
    // 配置结构
    struct Config {
        double min_slice_size;              // 最小切片大小
        double max_slice_size;              // 最大切片大小
        uint32_t min_interval_seconds;      // 最小间隔时间（秒）
        uint32_t max_interval_seconds;      // 最大间隔时间（秒）
        double price_improvement_threshold; // 价格改善阈值
        bool enable_adaptive_sizing;        // 启用自适应切片大小
        bool enable_market_impact_control;  // 启用市场冲击控制
        double max_participation_rate;      // 最大参与率
        
        Config() : min_slice_size(100.0), max_slice_size(10000.0),
                   min_interval_seconds(30), max_interval_seconds(300),
                   price_improvement_threshold(0.001), enable_adaptive_sizing(true),
                   enable_market_impact_control(true), max_participation_rate(0.2) {}
    };
    
    // 统计信息
    struct Statistics {
        uint32_t total_executions;
        uint32_t completed_executions;
        uint32_t cancelled_executions;
        uint32_t error_executions;
        double average_execution_time;
        double average_slippage;
        double total_volume_executed;
        std::chrono::high_resolution_clock::time_point last_execution_time;
    };
    
    TWAPAlgorithm(std::shared_ptr<OrderManager> order_manager, 
                   std::shared_ptr<GatewayAdapter> gateway_adapter = nullptr);
    ~TWAPAlgorithm();
    
    // 生命周期管理
    bool initialize();
    bool start();
    void stop();
    void cleanup();
    bool is_running() const;
    
    // 算法执行
    std::string start_execution(const std::string& strategy_id,
                               const std::string& instrument_id,
                               execution::OrderSide side,
                               const execution::TWAPParams& params);
    bool pause_execution(const std::string& execution_id);
    bool resume_execution(const std::string& execution_id);
    bool cancel_execution(const std::string& execution_id);
    
    // 查询接口
    std::shared_ptr<execution::AlgorithmExecution> get_execution(const std::string& execution_id) const;
    std::vector<std::shared_ptr<execution::AlgorithmExecution>> get_executions_by_strategy(const std::string& strategy_id) const;
    std::vector<std::shared_ptr<execution::AlgorithmExecution>> get_active_executions() const;
    std::vector<std::shared_ptr<execution::AlgorithmExecution>> get_all_executions() const;
    
    // 市场数据更新
    void update_market_data(const execution::MarketData& market_data);
    
    // 事件回调
    void set_event_callback(TWAPEventCallback callback);
    void set_order_callback(TWAPOrderCallback callback);
    
    // 配置和统计
    void set_config(const Config& config);
    Config get_config() const;
    Statistics get_statistics() const;
    
private:
    // 内部结构
    struct ExecutionSlice {
        std::string slice_id;
        std::string execution_id;
        double quantity;
        double target_price;
        std::chrono::high_resolution_clock::time_point scheduled_time;
        std::string order_id;
        bool executed;
        
        ExecutionSlice() : quantity(0.0), target_price(0.0), executed(false) {}
    };
    
    // 内部方法
    std::string generate_execution_id();
    std::string generate_slice_id();
    void calculate_slices(std::shared_ptr<execution::AlgorithmExecution> execution);
    void schedule_next_slice(const std::string& execution_id);
    void execute_slice(const std::string& execution_id, const ExecutionSlice& slice);
    void update_execution_progress(const std::string& execution_id, const Order& order);
    void complete_execution(const std::string& execution_id);
    void notify_execution_event(const execution::AlgorithmExecution& execution);
    void notify_order_event(const std::string& execution_id, const Order& order);
    
    // 算法计算方法
    double calculate_slice_size(const std::shared_ptr<execution::AlgorithmExecution>& execution, 
                               const execution::MarketData& market_data) const;
    double calculate_target_price(const std::shared_ptr<execution::AlgorithmExecution>& execution,
                                 const execution::MarketData& market_data) const;
    uint32_t calculate_slice_interval(const std::shared_ptr<execution::AlgorithmExecution>& execution) const;
    bool should_adjust_execution(const std::shared_ptr<execution::AlgorithmExecution>& execution,
                                const execution::MarketData& market_data) const;
    
    // 工作线程
    void execution_worker();
    void process_scheduled_slices();
    void monitor_executions();
    
    // 多线程处理方法
    void process_instrument_executions(const std::string& instrument_id);
    void execute_slice_async(const std::string& execution_id, const ExecutionSlice& slice);
    void distribute_executions_to_threads();
    
    // 成员变量
    std::shared_ptr<OrderManager> order_manager_;
    std::shared_ptr<GatewayAdapter> gateway_adapter_;
    std::unique_ptr<ThreadPool> execution_thread_pool_;
    
    mutable std::shared_mutex executions_mutex_;
    mutable std::mutex slices_mutex_;
    mutable std::shared_mutex market_data_mutex_;
    mutable std::mutex config_mutex_;
    mutable std::mutex statistics_mutex_;
    
    // 按标的分组的执行队列
    std::unordered_map<std::string, std::queue<std::string>> instrument_execution_queues_;
    mutable std::mutex instrument_queues_mutex_;
    
    std::unordered_map<std::string, std::shared_ptr<execution::AlgorithmExecution>> executions_;
    std::unordered_map<std::string, std::vector<ExecutionSlice>> execution_slices_;
    std::unordered_map<std::string, execution::MarketData> market_data_cache_;
    
    std::priority_queue<std::pair<std::chrono::high_resolution_clock::time_point, std::string>,
                       std::vector<std::pair<std::chrono::high_resolution_clock::time_point, std::string>>,
                       std::greater<std::pair<std::chrono::high_resolution_clock::time_point, std::string>>> scheduled_slices_;
    
    TWAPEventCallback event_callback_;
    TWAPOrderCallback order_callback_;
    
    Config config_;
    Statistics statistics_;
    
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    std::atomic<uint64_t> execution_sequence_;
    std::atomic<uint64_t> slice_sequence_;
    
    std::thread execution_thread_;
};

} // namespace execution
} // namespace tes