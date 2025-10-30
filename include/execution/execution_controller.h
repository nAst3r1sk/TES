#pragma once

#include "../common/common_types.h"
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <shared_mutex>
#include <filesystem>

// 前向声明
namespace md {
    class IMarketDataManager;
}

// 首先包含需要在Config中使用的头文件
#include "signal_transmission_manager.h"
#include "json_feedback_writer.h"
#include "gateway_adapter.h"

// 然后包含其他头文件
#include "order_manager.h"
#include "twap_algorithm.h"
#include "shared_memory_interface.h"
#include "trading_rule_checker.h"
#include "position_manager.h"
#include "thread_pool.h"
#include "lockfree_queue.h"
#include "async_callback_manager.h"
#include "performance_monitor.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <shared_mutex>

namespace tes {
namespace execution {

// 执行控制器统计
struct ExecutionStatistics {
    uint64_t signals_processed;
    uint64_t orders_created;
    uint64_t orders_executed;
    uint64_t trades_processed;
    uint64_t risk_violations;
    uint64_t algorithm_executions;
    uint64_t twap_executions_started;      // TWAP执行启动次数
    uint64_t twap_execution_failures;      // TWAP执行失败次数
    uint64_t direct_orders_executed;       // 直接订单执行次数
    std::chrono::high_resolution_clock::time_point last_signal_time;
    std::chrono::high_resolution_clock::time_point last_order_time;
    std::chrono::high_resolution_clock::time_point last_trade_time;
    
    ExecutionStatistics() : signals_processed(0), orders_created(0), orders_executed(0),
                           trades_processed(0), risk_violations(0), algorithm_executions(0),
                           twap_executions_started(0), twap_execution_failures(0),
                           direct_orders_executed(0) {}
};

class ExecutionController {
public:
    // 配置结构
    struct Config {
        uint32_t worker_thread_count;            // 工作线程数量
        uint32_t signal_processing_interval_ms;  // 信号处理间隔（毫秒）
        uint32_t heartbeat_interval_ms;          // 心跳间隔（毫秒）
        uint32_t statistics_update_interval_ms;  // 统计更新间隔（毫秒）
        bool enable_risk_checking;               // 启用风险检查
        bool enable_position_tracking;           // 启用持仓跟踪
        bool enable_algorithm_execution;         // 启用算法执行
        bool enable_order_feedback;              // 启用订单回报
        std::vector<std::string> trading_exchanges;  // 启用的交易所列表
        
        // 信号传递配置
        SignalTransmissionMode signal_transmission_mode;  // 信号传递模式
        std::string system_config_file;                   // 系统配置文件路径
        
        // JSON反馈写入器配置
        JsonFeedbackWriter::Config json_feedback_config;  // JSON反馈写入器配置
        
        // Gateway适配器配置
        GatewayAdapter::Config gateway_config;  // Gateway配置
        
        // TWAP算法配置参数
        double twap_quantity_threshold;          // TWAP数量阈值
        double twap_value_threshold;             // TWAP价值阈值
        double twap_market_impact_threshold;     // 市场冲击阈值
        uint32_t default_twap_duration_minutes;  // 默认TWAP执行时长（分钟）
        double twap_min_slice_size;              // 最小切片大小
        uint32_t max_twap_slices;                // 最大切片数
        double default_participation_rate;       // 默认参与率
        uint32_t max_price_deviation_bps;        // 最大价格偏离（基点）
        
        Config() : worker_thread_count(std::thread::hardware_concurrency()),
                   signal_processing_interval_ms(1),
                   heartbeat_interval_ms(1000),
                   statistics_update_interval_ms(5000),
                   enable_risk_checking(true),
                   enable_position_tracking(true),
                   enable_algorithm_execution(true),
                   enable_order_feedback(true),
                   trading_exchanges({}),
                   signal_transmission_mode(SignalTransmissionMode::SHARED_MEMORY),
                   system_config_file("config/system_config.json"),
                   twap_quantity_threshold(10000.0),
                   twap_value_threshold(1000000.0),
                   twap_market_impact_threshold(0.05),
                   default_twap_duration_minutes(30),
                   twap_min_slice_size(100.0),
                   max_twap_slices(200),
                   default_participation_rate(0.2),
                   max_price_deviation_bps(50) {}
    };
    
    // 事件回调函数类型
    using OrderEventCallback = std::function<void(const Order&)>;
    using TradeEventCallback = std::function<void(const Trade&)>;
    using TradingRuleEventCallback = std::function<void(const TradingRuleEvent&)>;
    using PositionEventCallback = std::function<void(const PositionEvent&)>;
    
    ExecutionController();
    ~ExecutionController();
    
    // 生命周期管理
    bool initialize();
    bool start();
    void stop();
    void cleanup();
    
    // 组件访问
    OrderManager* get_order_manager() const;
    TWAPAlgorithm* get_twap_algorithm() const;
    SharedMemoryInterface* get_shared_memory_interface() const;
    TradingRuleChecker* get_trading_rule_checker() const;
    PositionManager* get_position_manager() const;
    SignalTransmissionManager* get_signal_transmission_manager() const;
    GatewayAdapter* get_gateway_adapter() const;
    
    // 信号处理
    void process_trading_signal(const shared_memory::TradingSignal& signal);
    void process_trading_signals(const std::vector<shared_memory::TradingSignal>& signals);
    
    // 订单管理
    std::string create_order(const Order& order);
    bool submit_order(const std::string& order_id);
    bool cancel_order(const std::string& order_id);
    bool modify_order(const std::string& order_id, const Order& new_order);
    
    // 算法执行
    std::string start_twap_execution(const std::string& strategy_id,
                                    const std::string& instrument_id,
                                    OrderSide side,
                                    const TWAPParams& params);
    bool pause_twap_execution(const std::string& execution_id);
    bool resume_twap_execution(const std::string& execution_id);
    bool cancel_twap_execution(const std::string& execution_id);
    
    // 风险管理
    void set_trading_rule_limits(const std::string& strategy_id, const TradingRuleLimits& limits);
    TradingRuleLimits get_trading_rule_limits(const std::string& strategy_id) const;
    TradingRuleCheckResult check_order_trading_rules(const Order& order);
    
    // 持仓管理
    Position get_position(const std::string& strategy_id, const std::string& instrument_id) const;
    std::vector<Position> get_positions_by_strategy(const std::string& strategy_id) const;
    double calculate_total_pnl(const std::string& strategy_id) const;
    
    // 市场数据更新
    void update_market_data(const MarketData& market_data);
    void update_account_info(const AccountInfo& account_info);
    
    // 事件回调设置
    void set_order_event_callback(OrderEventCallback callback);
    void set_trade_event_callback(TradeEventCallback callback);
    void set_trading_rule_event_callback(TradingRuleEventCallback callback);
    void set_position_event_callback(PositionEventCallback callback);
    
    // 配置和统计
    void set_config(const Config& config);
    Config get_config() const;
    ExecutionStatistics get_statistics() const;
    
    // 状态查询
    bool is_running() const;
    bool is_connected() const;
    std::string get_last_error() const;
    
    // 辅助函数
    bool is_exchange_enabled(const std::string& exchange) const;
    
private:
    void signal_processing_worker();
    void heartbeat_worker();
    void statistics_worker();
    void setup_event_callbacks();
    void handle_order_event(const Order& order);
    void handle_trade_event(const Trade& trade);
    void handle_trading_rule_event(const TradingRuleEvent& event);
    void handle_position_event(const PositionEvent& event);
    void send_order_feedback(const Order& order);
    void update_statistics();
    void set_error(const std::string& error);

    bool should_use_twap_execution(const shared_memory::TradingSignal& signal);
    void execute_with_twap(const shared_memory::TradingSignal& signal);
    void execute_direct_order(const Order& order);

    
    // 成员变量
    mutable std::mutex config_mutex_;
    mutable std::mutex statistics_mutex_;
    mutable std::mutex error_mutex_;
    mutable std::mutex callbacks_mutex_;
    
    Config config_;
    ExecutionStatistics statistics_;
    std::string last_error_;
    
    std::atomic<bool> initialized_;
    std::atomic<bool> running_;
    
    // 核心组件
    std::unique_ptr<SignalTransmissionManager> signal_transmission_manager_;
    std::unique_ptr<TWAPAlgorithm> twap_algorithm_;
    std::unique_ptr<SharedMemoryInterface> shared_memory_interface_;
    std::unique_ptr<TradingRuleChecker> trading_rule_checker_;
    std::unique_ptr<PositionManager> position_manager_;
    std::unique_ptr<OrderManager> order_manager_;
    GatewayAdapter* gateway_adapter_;  // 使用单例模式，不需要unique_ptr
    
    // JSON反馈写入器（用于JSON模式下的订单反馈）
    std::unique_ptr<JsonFeedbackWriter> json_feedback_writer_;
    
    // 线程池用于并发信号处理
    std::unique_ptr<ThreadPool> signal_thread_pool_;
    
    // 无锁信号队列用于信号处理
    std::unique_ptr<MPMCLockFreeQueue<shared_memory::TradingSignal>> signal_queue_;
    
    // 异步回调管理器
    std::unique_ptr<AsyncCallbackManager> async_callback_manager_;
        
    // 性能监控器
    std::unique_ptr<PerformanceMonitor> performance_monitor_;
    
    // 工作线程
    std::vector<std::unique_ptr<std::thread>> worker_threads_;
    
    // 事件回调
    OrderEventCallback order_event_callback_;
    TradeEventCallback trade_event_callback_;
    TradingRuleEventCallback trading_rule_event_callback_;
    PositionEventCallback position_event_callback_;

};

} // namespace execution
} // namespace tes