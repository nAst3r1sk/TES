#pragma once

#include "types.h"
#include "../shared_memory/core/signal_buffer.h"
#include "../shared_memory/core/order_feedback_buffer.h"
#include "../shared_memory/core/control_info.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>

namespace tes {
namespace execution {

// 共享内存接口统计
struct SharedMemoryStatistics {
    uint64_t signals_received;
    uint64_t signals_failed;
    uint64_t feedbacks_sent;
    uint64_t feedbacks_failed;
    uint64_t heartbeat_updates;
    std::chrono::high_resolution_clock::time_point last_signal_time;
    std::chrono::high_resolution_clock::time_point last_feedback_time;
    std::chrono::high_resolution_clock::time_point last_heartbeat_time;
};

class SharedMemoryInterfaceLegacy {
public:
    // 配置结构
    struct Config {
        std::string signal_buffer_name;        // 信号缓冲区名称
        std::string feedback_buffer_name;      // 回报缓冲区名称
        std::string control_info_name;         // 控制信息名称
        uint32_t connection_timeout_ms;        // 连接超时（毫秒）
        uint32_t operation_timeout_ms;         // 操作超时（毫秒）
        uint32_t heartbeat_interval_ms;        // 心跳间隔（毫秒）
        uint32_t retry_count;                  // 重试次数
        bool enable_heartbeat;                 // 启用心跳
        
        Config() : signal_buffer_name("tes_signal_buffer"),
                   feedback_buffer_name("tes_order_feedback"),
                   control_info_name("tes_control_info"),
                   connection_timeout_ms(5000),
                   operation_timeout_ms(1000),
                   heartbeat_interval_ms(1000),
                   retry_count(3),
                   enable_heartbeat(true) {}
    };
    
    // 信号回调函数类型
    using SignalCallback = std::function<void(const shared_memory::TradingSignal&)>;
    
    SharedMemoryInterfaceLegacy();
    ~SharedMemoryInterfaceLegacy();
    
    // 生命周期管理
    bool initialize();
    bool connect();
    void disconnect();
    void cleanup();
    
    // 信号接收
    bool receive_signal(shared_memory::TradingSignal& signal);
    bool receive_signals(std::vector<shared_memory::TradingSignal>& signals, uint32_t max_count = 100);
    void set_signal_callback(SignalCallback callback);
    
    // 订单回报发送
    bool send_order_feedback(const shared_memory::OrderFeedback& feedback);
    bool send_order_feedbacks(const std::vector<shared_memory::OrderFeedback>& feedbacks);
    
    // 控制信息访问
    bool is_system_running() const;
    bool can_process_signals() const;
    shared_memory::SystemState get_system_state() const;
    shared_memory::TradingMode get_trading_mode() const;
    
    // 系统状态更新
    bool update_execution_count(uint32_t count);
    bool update_heartbeat();
    bool set_execution_status(bool running);
    
    // 缓冲区状态查询
    uint32_t get_signal_buffer_available_space() const;
    uint32_t get_feedback_buffer_available_space() const;
    bool is_signal_buffer_empty() const;
    bool is_feedback_buffer_full() const;
    
    // 配置和统计
    void set_config(const Config& config);
    Config get_config() const;
    SharedMemoryStatistics get_statistics() const;
    
    // 错误处理
    std::string get_last_error() const;
    void clear_error();
    
private:
    // 内部方法
    bool create_or_open_shared_memory();
    bool wait_for_system_ready();
    void update_statistics_on_signal();
    void update_statistics_on_feedback();
    void update_statistics_on_heartbeat();
    void set_error(const std::string& error);
    
    // 成员变量
    mutable std::mutex config_mutex_;
    mutable std::mutex statistics_mutex_;
    mutable std::mutex error_mutex_;
    mutable std::mutex shared_memory_mutex_;
    
    Config config_;
    SharedMemoryStatistics statistics_;
    std::string last_error_;
    
    std::atomic<bool> initialized_;
    std::atomic<bool> connected_;
    
    // 共享内存组件
    std::unique_ptr<shared_memory::SignalBuffer> signal_buffer_;
    std::unique_ptr<shared_memory::OrderFeedbackBuffer> feedback_buffer_;
    std::unique_ptr<shared_memory::ControlInfo> control_info_;
    
    SignalCallback signal_callback_;
};

} // namespace execution
} // namespace tes