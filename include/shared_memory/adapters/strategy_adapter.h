#pragma once

#include "../interfaces/strategy_interface.h"
#include <memory>
#include <vector>

namespace tes {
namespace shared_memory {
namespace adapters {

/**
 * @brief 策略层适配器
 */
class StrategyAdapter {
public:
    StrategyAdapter() : impl_(std::make_unique<interfaces::StrategyInterface>()) {}
    StrategyAdapter(const std::string& name, bool create_mode = false) : impl_(std::make_unique<interfaces::StrategyInterface>(name, create_mode)) {}
    
    virtual ~StrategyAdapter() = default;

    // 禁用拷贝构造和赋值
    StrategyAdapter(const StrategyAdapter&) = delete;
    StrategyAdapter& operator=(const StrategyAdapter&) = delete;

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
     * @brief 发送交易信号
     * @param signal 交易信号
     * @return true 发送成功，false 发送失败
     */
    bool send_signal(const TradingSignal& signal) {
        return impl_->send_signal(signal);
    }

    /**
     * @brief 批量发送交易信号
     * @param signals 交易信号列表
     * @return 成功发送的数量
     */
    size_t send_signals_batch(const std::vector<TradingSignal>& signals) {
        return impl_->send_signals_batch(signals);
    }

    /**
     * @brief 接收订单反馈
     * @param feedback 输出参数，接收到的订单反馈
     * @return true 成功接收，false 无反馈或失败
     */
    bool receive_order_feedback(OrderFeedback& feedback) {
        return impl_->receive_order_feedback(feedback);
    }

    /// 批量接收订单反馈
    size_t receive_order_feedbacks_batch(std::vector<OrderFeedback>& feedbacks, size_t max_count) {
        return impl_->receive_order_feedbacks_batch(feedbacks, max_count);
    }

    /**
     * @brief 获取系统控制信息
     * @return 系统控制信息
     */
    SystemControlInfo get_control_info() const {
        return impl_->get_control_info();
    }

    /**
     * @brief 获取系统状态
     * @return 系统状态
     */
    SystemState get_system_state() const {
        return impl_->get_system_state();
    }

    /**
     * @brief 获取交易模式
     * @return 交易模式
     */
    TradingMode get_trading_mode() const {
        return impl_->get_trading_mode();
    }

    /**
     * @brief 检查是否允许交易
     * @return true 允许交易，false 不允许交易
     */
    bool is_trading_enabled() const {
        return impl_->is_trading_enabled();
    }

    /**
     * @brief 检查紧急停止状态
     * @return true 紧急停止，false 正常运行
     */
    bool is_emergency_stop() const {
        return impl_->is_emergency_stop();
    }

    /**
     * @brief 更新策略心跳
     * @param strategy_id 策略ID
     */
    void update_strategy_heartbeat(const std::string& strategy_id) {
        impl_->update_strategy_heartbeat(strategy_id);
    }

    /**
     * @brief 获取统计信息
     * @return 策略统计信息
     */
    interfaces::StrategyInterface::StrategyStats get_statistics() const {
        return impl_->get_strategy_stats();
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
     * @brief 检查是否可以发送信号
     * @return true 可以发送，false 不能发送
     */
    bool can_send_signal() const {
        return impl_->can_send_signal();
    }

    /**
     * @brief 检查是否有待处理的订单反馈
     * @return true 有反馈，false 无反馈
     */
    bool has_pending_feedbacks() const {
        return impl_->has_pending_feedbacks();
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
        return "StrategyAdapter(" + impl_->get_interface_type() + ")";
    }

    // 为了兼容旧代码，提供一些旧接口的别名方法
    
    /**
     * @brief 旧接口兼容：发送信号
     * @param signal 交易信号
     * @return true 成功，false 失败
     */
    bool put_signal(const TradingSignal& signal) {
        return send_signal(signal);
    }

    /**
     * @brief 旧接口兼容：获取反馈
     * @param feedback 输出参数
     * @return true 成功，false 失败
     */
    bool get_feedback(OrderFeedback& feedback) {
        return receive_order_feedback(feedback);
    }

    /**
     * @brief 旧接口兼容：检查连接状态
     * @return true 已连接，false 未连接
     */
    bool connected() const {
        return is_connected();
    }

    /**
     * @brief 旧接口兼容：获取控制信息
     * @return 控制信息
     */
    SystemControlInfo get_system_control_info() const {
        return get_control_info();
    }

    /**
     * @brief 旧接口兼容：检查交易状态
     * @return true 可交易，false 不可交易
     */
    bool trading_enabled() const {
        return is_trading_enabled();
    }

    /**
     * @brief 旧接口兼容：检查紧急状态
     * @return true 紧急停止，false 正常
     */
    bool emergency_stop() const {
        return impl_->is_emergency_stop();
    }

    /**
     * @brief 更新策略数量（兼容性方法）
     */
    void update_strategy_count(uint32_t count) {
        (void)count; // 抑制未使用参数警告
        // 兼容性方法，暂时不执行任何操作
    }

    /**
     * @brief 配置结构体（兼容性）
     */
    struct Config {
        bool enable_heartbeat = true;
        uint32_t heartbeat_interval_ms = 1000;
    };

    /**
     * @brief 设置配置（兼容性方法）
     * @param config 配置参数
     */
    void set_config(const Config& config) {
        (void)config; // 抑制未使用参数警告
        // 兼容性方法，暂时不执行任何操作
    }

private:
    std::unique_ptr<interfaces::StrategyInterface> impl_;  ///< 新接口实现
};

} // namespace adapters
} // namespace shared_memory
} // namespace tes