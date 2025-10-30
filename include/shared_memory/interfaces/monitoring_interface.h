#pragma once

#include "base_interface.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>
#include <memory>

namespace tes {
namespace shared_memory {
namespace interfaces {

/**
 * @brief 监控层共享内存接口
 * 
 * 专门处理系统监控、性能统计、健康检查等功能
 */
class MonitoringInterface : public BaseSharedMemoryInterface {
public:
    /**
     * @brief 监控配置结构
     */
    struct MonitoringConfig {
        bool enable_buffer_monitoring = true;      ///< 启用缓冲区监控
        bool enable_performance_monitoring = true; ///< 启用性能监控
        bool enable_system_monitoring = true;      ///< 启用系统监控
        bool enable_auto_monitoring = false;       ///< 启用自动监控线程
        uint32_t monitoring_interval_ms = 1000;    ///< 监控间隔（毫秒）
        uint32_t alert_threshold_percent = 80;     ///< 告警阈值（百分比）
    };

    /**
     * @brief 系统监控状态
     */
    struct SystemMonitoringStatus {
        SystemState system_state = SystemState::INITIALIZING;
        TradingMode trading_mode = TradingMode::SIMULATION;
        bool is_trading_enabled = false;
        bool is_emergency_stop = true;
        Timestamp last_update_time = 0;
        uint64_t total_signals_processed = 0;
        uint64_t total_orders_processed = 0;
        double system_load_percent = 0.0;
    };

    /**
     * @brief 缓冲区监控状态
     */
    struct BufferMonitoringStatus {
        struct BufferStatus {
            std::string name;
            size_t capacity = 0;
            size_t current_size = 0;
            double usage_percent = 0.0;
            uint64_t total_reads = 0;
            uint64_t total_writes = 0;
            uint64_t read_failures = 0;
            uint64_t write_failures = 0;
            bool is_healthy = false;
        };
        
        std::vector<BufferStatus> buffer_statuses;
        Timestamp last_update_time = 0;
    };

    /**
     * @brief 性能监控状态
     */
    struct PerformanceMonitoringStatus {
        double avg_signal_latency_us = 0.0;        ///< 平均信号延迟（微秒）
        double avg_order_latency_us = 0.0;         ///< 平均订单延迟（微秒）
        double max_signal_latency_us = 0.0;        ///< 最大信号延迟（微秒）
        double max_order_latency_us = 0.0;         ///< 最大订单延迟（微秒）
        uint64_t signals_per_second = 0;           ///< 每秒信号数
        uint64_t orders_per_second = 0;            ///< 每秒订单数
        double cpu_usage_percent = 0.0;            ///< CPU使用率
        double memory_usage_percent = 0.0;         ///< 内存使用率
        Timestamp last_update_time = 0;
    };

public:
    MonitoringInterface() = default;
    virtual ~MonitoringInterface() {
        stop_monitoring();
    }

    /**
     * @brief 初始化监控接口
     * @param config 监控配置
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize(const MonitoringConfig& config) {
        if (!BaseSharedMemoryInterface::initialize()) {
            return false;
        }

        config_ = config;

        // 初始化各个组件
        if (config_.enable_buffer_monitoring || config_.enable_system_monitoring) {
            signal_buffer_ = std::make_unique<SignalBuffer>("tes_signal_buffer", false);
            order_feedback_buffer_ = std::make_unique<OrderFeedbackBuffer>("tes_order_feedback", false);
            control_info_ = std::make_unique<ControlInfo>("tes_control_info", false);

            // 缓冲区在构造时已经初始化，无需额外初始化操作
        }

        return true;
    }

    /**
     * @brief 连接到共享内存
     * @return true 连接成功，false 连接失败
     */
    bool connect() override {
        if (!BaseSharedMemoryInterface::connect()) {
            return false;
        }

        // 缓冲区和控制信息在构造时已经连接，无需额外连接操作

        // 启动自动监控线程
        if (config_.enable_auto_monitoring) {
            start_monitoring();
        }

        return true;
    }

    /**
     * @brief 断开连接
     */
    void disconnect() override {
        stop_monitoring();
        
        // 缓冲区和控制信息在析构时自动断开连接
        
        BaseSharedMemoryInterface::disconnect();
    }

    /**
     * @brief 获取系统监控状态
     * @return 系统监控状态
     */
    SystemMonitoringStatus get_system_status() {
        SystemMonitoringStatus status;
        
        if (control_info_) {
            // 类型转换：从ControlInfo的枚举转换为SystemMonitoringStatus的枚举
        auto ctrl_state = control_info_->get_system_state();
        status.system_state = static_cast<SystemState>(ctrl_state);
        
        auto ctrl_mode = control_info_->get_trading_mode();
        status.trading_mode = static_cast<TradingMode>(ctrl_mode);
        
        // 使用order_routing_enabled作为trading_enabled的替代
        status.is_trading_enabled = control_info_->is_order_routing_enabled();
            status.is_emergency_stop = control_info_->is_emergency_stop();
        }
        
        status.last_update_time = get_current_timestamp();
        status.total_signals_processed = total_signals_monitored_;
        status.total_orders_processed = total_orders_monitored_;
        
        return status;
    }

    /**
     * @brief 获取缓冲区监控状态
     * @return 缓冲区监控状态
     */
    BufferMonitoringStatus get_buffer_status() {
        BufferMonitoringStatus status;
        status.last_update_time = get_current_timestamp();
        
        // 监控信号缓冲区
        if (signal_buffer_) {
            BufferMonitoringStatus::BufferStatus signal_status;
            signal_status.name = "SignalBuffer";
            auto stats = signal_buffer_->get_statistics();
            size_t available_signals = signal_buffer_->available_signals();
            signal_status.capacity = SignalBuffer::BUFFER_SIZE;
            signal_status.current_size = available_signals;
            signal_status.usage_percent = (double)available_signals / SignalBuffer::BUFFER_SIZE * 100.0;
            signal_status.total_reads = stats.total_reads;
            signal_status.total_writes = stats.total_writes;
            signal_status.read_failures = stats.read_failures;
            signal_status.write_failures = stats.write_failures;
            signal_status.is_healthy = true; // 简化健康检查
            status.buffer_statuses.push_back(signal_status);
        }
        
        // 监控订单反馈缓冲区
        if (order_feedback_buffer_) {
            BufferMonitoringStatus::BufferStatus feedback_status;
            feedback_status.name = "OrderFeedbackBuffer";
            auto stats = order_feedback_buffer_->get_statistics();
            size_t available_feedbacks = order_feedback_buffer_->available_feedbacks();
            feedback_status.capacity = OrderFeedbackBuffer::BUFFER_SIZE;
            feedback_status.current_size = available_feedbacks;
            feedback_status.usage_percent = (double)available_feedbacks / OrderFeedbackBuffer::BUFFER_SIZE * 100.0;
            feedback_status.total_reads = stats.total_reads;
            feedback_status.total_writes = stats.total_writes;
            feedback_status.read_failures = stats.read_failures;
            feedback_status.write_failures = stats.write_failures;
            feedback_status.is_healthy = true; // 简化健康检查
            status.buffer_statuses.push_back(feedback_status);
        }
        
        return status;
    }

    /**
     * @brief 获取性能监控状态
     * @return 性能监控状态
     */
    PerformanceMonitoringStatus get_performance_status() {
        PerformanceMonitoringStatus status;
        status.last_update_time = get_current_timestamp();
        
        // 计算性能指标
        if (performance_samples_.size() > 0) {
            double total_latency = 0.0;
            double max_latency = 0.0;
            
            for (const auto& sample : performance_samples_) {
                total_latency += sample.latency_us;
                max_latency = std::max(max_latency, sample.latency_us);
            }
            
            status.avg_signal_latency_us = total_latency / performance_samples_.size();
            status.max_signal_latency_us = max_latency;
        }
        
        // 计算吞吐量
        auto current_time = get_current_timestamp();
        if (last_throughput_calculation_ > 0) {
            auto time_diff_seconds = (current_time - last_throughput_calculation_) / 1000000.0;
            if (time_diff_seconds > 0) {
                status.signals_per_second = (uint64_t)(signals_in_period_ / time_diff_seconds);
                status.orders_per_second = (uint64_t)(orders_in_period_ / time_diff_seconds);
            }
        }
        
        return status;
    }

    /**
     * @brief 启动自动监控
     */
    void start_monitoring() {
        if (monitoring_thread_running_.load()) {
            return;
        }
        
        monitoring_thread_running_.store(true);
        monitoring_thread_ = std::thread(&MonitoringInterface::monitoring_loop, this);
    }

    /**
     * @brief 停止自动监控
     */
    void stop_monitoring() {
        monitoring_thread_running_.store(false);
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
    }

    /**
     * @brief 记录性能样本
     * @param latency_us 延迟（微秒）
     */
    void record_performance_sample(double latency_us) {
        PerformanceSample sample;
        sample.timestamp = get_current_timestamp();
        sample.latency_us = latency_us;
        
        performance_samples_.push_back(sample);
        
        // 限制样本数量
        if (performance_samples_.size() > 1000) {
            performance_samples_.erase(performance_samples_.begin());
        }
        
        signals_in_period_++;
    }

    /**
     * @brief 获取接口类型名称
     * @return 接口类型字符串
     */
    std::string get_interface_type() const override {
        return "MonitoringInterface";
    }

    /**
     * @brief 执行健康检查
     * @return true 健康，false 不健康
     */
    bool health_check() const override {
        if (!BaseSharedMemoryInterface::health_check()) {
            return false;
        }
        
        // 检查各个组件状态
        // 简化健康检查，缓冲区存在即认为健康
        if (!signal_buffer_ || !order_feedback_buffer_) {
            return false;
        }
        
        if (control_info_ && !control_info_->is_system_healthy()) {
            return false;
        }
        
        return true;
    }

    /**
     * @brief 重置统计信息
     */
    void reset_stats() override {
        BaseSharedMemoryInterface::reset_stats();
        total_signals_monitored_ = 0;
        total_orders_monitored_ = 0;
        performance_samples_.clear();
        signals_in_period_ = 0;
        orders_in_period_ = 0;
        last_throughput_calculation_ = get_current_timestamp();
    }

private:
    /**
     * @brief 性能样本结构
     */
    struct PerformanceSample {
        Timestamp timestamp;
        double latency_us;
    };

    /**
     * @brief 监控循环
     */
    void monitoring_loop() {
        while (monitoring_thread_running_.load()) {
            // 执行监控任务
            if (config_.enable_system_monitoring) {
                monitor_system();
            }
            
            if (config_.enable_buffer_monitoring) {
                monitor_buffers();
            }
            
            if (config_.enable_performance_monitoring) {
                monitor_performance();
            }
            
            // 等待下一个监控周期
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.monitoring_interval_ms));
        }
    }

    /**
     * @brief 监控系统状态
     */
    void monitor_system() {
        total_signals_monitored_++;
        update_heartbeat();
    }

    /**
     * @brief 监控缓冲区状态
     */
    void monitor_buffers() {
        // 检查缓冲区使用率
        auto buffer_status = get_buffer_status();
        for (const auto& status : buffer_status.buffer_statuses) {
            if (status.usage_percent > config_.alert_threshold_percent) {
                log_error("Buffer " + status.name + " usage high: " + std::to_string(status.usage_percent) + "%");
            }
        }
    }

    /**
     * @brief 监控性能指标
     */
    void monitor_performance() {
        // 计算吞吐量
        auto current_time = get_current_timestamp();
        if (last_throughput_calculation_ > 0) {
            auto time_diff = current_time - last_throughput_calculation_;
            if (time_diff >= 1000000) { // 1秒
                // 重置计数器
                signals_in_period_ = 0;
                orders_in_period_ = 0;
                last_throughput_calculation_ = current_time;
            }
        } else {
            last_throughput_calculation_ = current_time;
        }
    }

private:
    MonitoringConfig config_;                                        ///< 监控配置
    
    std::unique_ptr<SignalBuffer> signal_buffer_;                    ///< 信号缓冲区
    std::unique_ptr<OrderFeedbackBuffer> order_feedback_buffer_;     ///< 订单反馈缓冲区
    std::unique_ptr<ControlInfo> control_info_;                      ///< 控制信息
    
    std::atomic<bool> monitoring_thread_running_{false};            ///< 监控线程运行状态
    std::thread monitoring_thread_;                                  ///< 监控线程
    
    std::atomic<uint64_t> total_signals_monitored_{0};              ///< 监控信号总数
    std::atomic<uint64_t> total_orders_monitored_{0};               ///< 监控订单总数
    
    std::vector<PerformanceSample> performance_samples_;             ///< 性能样本
    std::atomic<uint64_t> signals_in_period_{0};                    ///< 周期内信号数
    std::atomic<uint64_t> orders_in_period_{0};                     ///< 周期内订单数
    Timestamp last_throughput_calculation_{0};                       ///< 上次吞吐量计算时间
};

/**
 * @brief 全局共享内存监控器（单例模式）
 */
class GlobalSharedMemoryMonitor {
public:
    static GlobalSharedMemoryMonitor& getInstance() {
        static GlobalSharedMemoryMonitor instance;
        return instance;
    }

    bool initialize(const MonitoringInterface::MonitoringConfig& config) {
        return monitoring_interface_.initialize(config);
    }

    bool connect() {
        return monitoring_interface_.connect();
    }

    void disconnect() {
        monitoring_interface_.disconnect();
    }

    MonitoringInterface::SystemMonitoringStatus get_system_status() {
        return monitoring_interface_.get_system_status();
    }

    MonitoringInterface::BufferMonitoringStatus get_buffer_status() {
        return monitoring_interface_.get_buffer_status();
    }

    MonitoringInterface::PerformanceMonitoringStatus get_performance_status() {
        return monitoring_interface_.get_performance_status();
    }

    void start_monitoring() {
        monitoring_interface_.start_monitoring();
    }

    void stop_monitoring() {
        monitoring_interface_.stop_monitoring();
    }

private:
    GlobalSharedMemoryMonitor() = default;
    ~GlobalSharedMemoryMonitor() = default;
    GlobalSharedMemoryMonitor(const GlobalSharedMemoryMonitor&) = delete;
    GlobalSharedMemoryMonitor& operator=(const GlobalSharedMemoryMonitor&) = delete;

    MonitoringInterface monitoring_interface_;
};

} // namespace interfaces
} // namespace shared_memory
} // namespace tes