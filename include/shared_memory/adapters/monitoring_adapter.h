#pragma once

#include "../interfaces/monitoring_interface.h"
#include <memory>

namespace tes {
namespace shared_memory {
namespace adapters {

/**
 * @brief 监控层适配器
 */
class MonitoringAdapter {
public:
    MonitoringAdapter() : impl_(std::make_unique<interfaces::MonitoringInterface>()) {}
    
    virtual ~MonitoringAdapter() = default;

    // 禁用拷贝构造和赋值
    MonitoringAdapter(const MonitoringAdapter&) = delete;
    MonitoringAdapter& operator=(const MonitoringAdapter&) = delete;

    /**
     * @brief 配置监控参数
     * @param config 监控配置
     * @return true 配置成功，false 配置失败
     */
    bool configure(const interfaces::MonitoringInterface::MonitoringConfig& config) {
        return impl_->configure(config);
    }

    /**
     * @brief 初始化接口
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize() {
        return impl_->initialize();
    }

    /**
     * @brief 连接到共享内存
     * @return true 连接成功，false 连接失败
     */
    bool connect() {
        return impl_->connect();
    }

    /**
     * @brief 启动监控
     * @return true 启动成功，false 启动失败
     */
    bool start() {
        return impl_->initialize() && impl_->connect() && impl_->start_monitoring();
    }

    /**
     * @brief 停止监控
     */
    void stop() {
        impl_->stop_monitoring();
        impl_->disconnect();
    }

    /**
     * @brief 清理资源
     */
    void cleanup() {
        stop();
        impl_.reset();
    }

    /**
     * @brief 断开连接
     */
    void disconnect() {
        impl_->disconnect();
    }

    /**
     * @brief 检查是否已连接
     * @return true 已连接，false 未连接
     */
    bool is_connected() const {
        return impl_->is_connected();
    }

    /**
     * @brief 启动监控
     * @return true 启动成功，false 启动失败
     */
    bool start_monitoring() {
        return impl_->start_monitoring();
    }

    /**
     * @brief 停止监控
     */
    void stop_monitoring() {
        impl_->stop_monitoring();
    }

    /**
     * @brief 检查监控是否运行
     * @return true 运行中，false 已停止
     */
    bool is_monitoring_active() const {
        return impl_->is_monitoring_active();
    }

    /**
     * @brief 获取系统监控状态
     * @return 系统监控状态
     */
    interfaces::MonitoringInterface::SystemMonitoringStatus get_system_status() const {
        return impl_->get_system_status();
    }

    /**
     * @brief 获取缓冲区监控状态
     * @return 缓冲区监控状态
     */
    interfaces::MonitoringInterface::BufferMonitoringStatus get_buffer_status() const {
        return impl_->get_buffer_status();
    }

    /**
     * @brief 获取性能监控状态
     * @return 性能监控状态
     */
    interfaces::MonitoringInterface::PerformanceMonitoringStatus get_performance_status() const {
        return impl_->get_performance_status();
    }

    /**
     * @brief 获取完整监控报告
     * @return 监控报告
     */
    interfaces::MonitoringInterface::MonitoringReport get_monitoring_report() const {
        return impl_->get_monitoring_report();
    }

    /**
     * @brief 执行健康检查
     * @return true 健康，false 不健康
     */
    bool health_check() const {
        return impl_->health_check();
    }

    /**
     * @brief 重置统计信息
     */
    void reset_stats() {
        impl_->reset_stats();
    }

    /**
     * @brief 更新心跳时间戳
     */
    void update_heartbeat() {
        impl_->update_heartbeat();
    }

    /**
     * @brief 获取最后心跳时间
     * @return 最后心跳时间戳
     */
    Timestamp get_last_heartbeat() const {
        return impl_->get_last_heartbeat();
    }

    /**
     * @brief 获取连接时间
     * @return 连接时间戳
     */
    Timestamp get_connection_time() const {
        return impl_->get_connection_time();
    }

    /**
     * @brief 获取接口类型名称
     * @return 接口类型字符串
     */
    std::string get_interface_type() const {
        return "MonitoringAdapter(" + impl_->get_interface_type() + ")";
    }

    /**
     * @brief 获取全局监控器实例
     * @return 全局监控器引用
     */
    static interfaces::GlobalSharedMemoryMonitor& get_global_monitor() {
        return interfaces::GlobalSharedMemoryMonitor::get_instance();
    }

    // 为了兼容旧代码，提供一些旧接口的别名方法
    
    /**
     * @brief 旧接口兼容：检查连接状态
     * @return true 已连接，false 未连接
     */
    bool connected() const {
        return is_connected();
    }

    /**
     * @brief 旧接口兼容：获取系统状态
     * @return 系统状态信息
     */
    SystemState get_system_state() const {
        auto status = get_system_status();
        return status.current_state;
    }

    /**
     * @brief 旧接口兼容：获取交易模式
     * @return 交易模式
     */
    TradingMode get_trading_mode() const {
        auto status = get_system_status();
        return status.trading_mode;
    }

    /**
     * @brief 旧接口兼容：检查紧急停止状态
     * @return true 紧急停止，false 正常运行
     */
    bool is_emergency_stop() const {
        auto status = get_system_status();
        return status.emergency_stop;
    }

    /**
     * @brief 旧接口兼容：获取信号缓冲区使用率
     * @return 使用率百分比 (0.0-1.0)
     */
    double get_signal_buffer_usage() const {
        auto status = get_buffer_status();
        return status.signal_buffer_usage;
    }

    /**
     * @brief 旧接口兼容：获取订单反馈缓冲区使用率
     * @return 使用率百分比 (0.0-1.0)
     */
    double get_feedback_buffer_usage() const {
        auto status = get_buffer_status();
        return status.feedback_buffer_usage;
    }

    /**
     * @brief 旧接口兼容：获取订单报告缓冲区使用率
     * @return 使用率百分比 (0.0-1.0)
     */
    double get_report_buffer_usage() const {
        auto status = get_buffer_status();
        return status.report_buffer_usage;
    }

    /**
     * @brief 旧接口兼容：获取平均延迟
     * @return 平均延迟（微秒）
     */
    double get_average_latency() const {
        auto status = get_performance_status();
        return status.average_latency_us;
    }

    /**
     * @brief 旧接口兼容：获取最大延迟
     * @return 最大延迟（微秒）
     */
    double get_max_latency() const {
        auto status = get_performance_status();
        return status.max_latency_us;
    }

    /**
     * @brief 旧接口兼容：获取吞吐量
     * @return 每秒处理的消息数
     */
    double get_throughput() const {
        auto status = get_performance_status();
        return status.throughput_per_second;
    }

    /**
     * @brief 旧接口兼容：检查是否有告警
     * @return true 有告警，false 无告警
     */
    bool has_alerts() const {
        auto report = get_monitoring_report();
        return !report.alerts.empty();
    }

    /**
     * @brief 旧接口兼容：获取告警数量
     * @return 告警数量
     */
    size_t get_alert_count() const {
        auto report = get_monitoring_report();
        return report.alerts.size();
    }

private:
    std::unique_ptr<interfaces::MonitoringInterface> impl_;  ///< 新接口实现
};

} // namespace adapters
} // namespace shared_memory
} // namespace tes