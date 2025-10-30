#pragma once

#include "base_interface.h"
#include <unordered_map>
#include <string>
#include <vector>

namespace tes {
namespace shared_memory {
namespace interfaces {

/**
 * @brief 策略层共享内存接口
 * 
 * 专门处理策略层的信号发送、订单反馈接收、控制信息访问等功能
 */
class StrategyInterface : public BaseSharedMemoryInterface
{
public:
    StrategyInterface() = default;
    StrategyInterface(const std::string& name, bool create_mode = false)
    {
        (void)name;
        (void)create_mode;
        // 兼容性构造函数，参数暂时忽略
    }
    virtual ~StrategyInterface() = default;

    /**
     * @brief 初始化策略接口
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize() override
    {
        if (!BaseSharedMemoryInterface::initialize()) {
            return false;
        }

        // 初始化信号缓冲区
        signal_buffer_ = std::make_unique<SignalBuffer>("tes_signal_buffer", false);

        // 初始化订单反馈缓冲区
        order_feedback_buffer_ = std::make_unique<OrderFeedbackBuffer>("tes_order_feedback", false);

        // 初始化控制信息
        control_info_ = std::make_unique<ControlInfo>("tes_control_info", false);

        return true;
    }

    /**
     * @brief 连接到共享内存
     * @return true 连接成功，false 连接失败
     */
    bool connect() override
    {
        if (!BaseSharedMemoryInterface::connect()) {
            return false;
        }

        // 缓冲区已在构造函数中初始化，无需额外连接

        return true;
    }

    /**
     * @brief 断开连接
     */
    void disconnect() override
    {
        // 缓冲区将在析构函数中自动清理
        BaseSharedMemoryInterface::disconnect();
    }

    /**
     * @brief 发送交易信号
     * @param signal 交易信号
     * @return true 发送成功，false 发送失败
     */
    bool send_signal(const TradingSignal& signal)
    {
        if (!validate_buffer(signal_buffer_.get())) {
            return false;
        }

        bool result = signal_buffer_->write_signal(signal);
        if (result) {
            signals_sent_++;
            update_signal_count(signal.type);
            update_heartbeat();
        }
        return result;
    }

    /**
     * @brief 批量发送交易信号
     * @param signals 交易信号列表
     * @return 成功发送的数量
     */
    size_t send_signals_batch(const std::vector<TradingSignal>& signals)
    {
        if (!validate_buffer(signal_buffer_.get())) {
            return 0;
        }

        size_t sent_count = 0;
        for (const auto& signal : signals) {
            if (signal_buffer_->write_signal(signal)) {
                sent_count++;
                update_signal_count(signal.type);
            } else {
                break; // 缓冲区满，停止发送
            }
        }

        if (sent_count > 0) {
            signals_sent_ += sent_count;
            update_heartbeat();
        }

        return sent_count;
    }

    /**
     * @brief 接收订单反馈
     * @param feedback 输出参数，接收到的订单反馈
     * @return true 成功接收，false 无反馈或失败
     */
    bool receive_order_feedback(OrderFeedback& feedback)
    {
        if (!validate_buffer(order_feedback_buffer_.get())) {
            return false;
        }

        bool result = order_feedback_buffer_->read_feedback(feedback);
        if (result) {
            feedbacks_received_++;
            update_heartbeat();
        }
        return result;
    }

    /**
     * @brief 批量接收订单反馈
     * @param feedbacks 输出参数，接收到的反馈列表
     * @param max_count 最大接收数量
     * @return 实际接收到的反馈数量
     */
    size_t receive_order_feedbacks_batch(std::vector<OrderFeedback>& feedbacks, size_t max_count)
    {
        if (!validate_buffer(order_feedback_buffer_.get())) {
            return 0;
        }

        feedbacks.clear();
        feedbacks.reserve(max_count);

        size_t count = 0;
        OrderFeedback feedback;
        while (count < max_count && order_feedback_buffer_->read_feedback(feedback)) {
            feedbacks.push_back(feedback);
            count++;
        }

        if (count > 0) {
            feedbacks_received_ += count;
            update_heartbeat();
        }

        return count;
    }

    /**
     * @brief 获取系统控制信息
     * @return 系统控制信息
     */
    SystemControlInfo get_control_info() const
    {
        if (!control_info_) {
            return SystemControlInfo{};
        }
        // ControlInfo没有get_system_control_info方法，手动构造
        SystemControlInfo info;
        info.trading_enabled = control_info_->can_place_order();
        info.risk_check_enabled = control_info_->is_risk_control_enabled();
        info.emergency_stop = control_info_->is_emergency_stop();
        auto config = control_info_->get_config();
        info.max_position_limit = config.max_position_limit;
        info.max_daily_loss = config.max_daily_loss;
        info.max_order_value = config.max_order_value;
        info.max_orders_per_second = config.max_orders_per_second;
        info.last_update_time = get_current_timestamp_ns();
        return info;
    }

    /**
     * @brief 获取系统状态
     * @return 系统状态
     */
    SystemState get_system_state() const
    {
        if (!control_info_) {
            return SystemState::STOPPED;
        }
        auto state = control_info_->get_system_state();
        // 转换ControlInfo::SystemState到shared_memory::SystemState
        switch (state) {
            case ControlInfo::SystemState::INITIALIZING:
                return SystemState::INITIALIZING;
            case ControlInfo::SystemState::RUNNING:
                return SystemState::RUNNING;
            case ControlInfo::SystemState::PAUSED:
                return SystemState::PAUSED;
            case ControlInfo::SystemState::STOPPING:
                return SystemState::STOPPING;
            case ControlInfo::SystemState::STOPPED:
                return SystemState::STOPPED;
            case ControlInfo::SystemState::ERROR:
                return SystemState::ERROR;
            default:
                return SystemState::STOPPED;
        }
    }

    /**
     * @brief 获取交易模式
     * @return 交易模式
     */
    TradingMode get_trading_mode() const
    {
        if (!control_info_) {
            return TradingMode::SIMULATION;
        }
        auto mode = control_info_->get_trading_mode();
        // 转换ControlInfo::TradingMode到shared_memory::TradingMode
        switch (mode) {
            case ControlInfo::TradingMode::SIMULATION:
                return TradingMode::SIMULATION;
            case ControlInfo::TradingMode::PAPER_TRADING:
                return TradingMode::PAPER_TRADING;
            case ControlInfo::TradingMode::LIVE_TRADING:
                return TradingMode::LIVE_TRADING;
            default:
                return TradingMode::SIMULATION;
        }
    }

    /**
     * @brief 检查是否允许交易
     * @return true 允许交易，false 不允许交易
     */
    bool is_trading_enabled() const
    {
        if (!control_info_) {
            return false;
        }
        // ControlInfo没有is_trading_enabled方法，使用can_place_order代替
        return control_info_->can_place_order();
    }

    /**
     * @brief 检查紧急停止状态
     * @return true 紧急停止，false 正常运行
     */
    bool is_emergency_stop() const
    {
        if (!control_info_) {
            return true; // 安全起见，默认为紧急停止
        }
        return control_info_->is_emergency_stop();
    }

    /**
     * @brief 更新策略心跳
     * @param strategy_id 策略ID
     */
    void update_strategy_heartbeat(const std::string& strategy_id)
    {
        (void)strategy_id; // 抑制未使用参数警告
        if (control_info_) {
            // ControlInfo没有update_strategy_heartbeat方法，使用update_heartbeat代替
            control_info_->update_heartbeat();
        }
        update_heartbeat();
    }

    /**
     * @brief 获取策略统计信息
     * @return 策略统计信息
     */
    struct StrategyStats {
        uint64_t signals_sent = 0;
        uint64_t feedbacks_received = 0;
        std::unordered_map<SignalType, uint64_t> signal_type_counts;
        SignalBuffer::Statistics signal_buffer_stats;
        OrderFeedbackBuffer::Statistics feedback_buffer_stats;
        PerformanceStats base_stats;
    };

    StrategyStats get_strategy_stats() const
    {
        StrategyStats stats;
        stats.signals_sent = signals_sent_;
        stats.feedbacks_received = feedbacks_received_;
        stats.signal_type_counts = signal_type_counts_;
        
        if (signal_buffer_) {
            stats.signal_buffer_stats = signal_buffer_->get_statistics();
        }
        if (order_feedback_buffer_) {
            stats.feedback_buffer_stats = order_feedback_buffer_->get_statistics();
        }
        
        stats.base_stats = get_base_stats();
        return stats;
    }

    /**
     * @brief 获取接口类型名称
     * @return 接口类型字符串
     */
    std::string get_interface_type() const override
    {
        return "StrategyInterface";
    }

    /**
     * @brief 执行健康检查
     * @return true 健康，false 不健康
     */
    bool health_check() const override
    {
        if (!BaseSharedMemoryInterface::health_check()) {
            return false;
        }

        // 检查各个组件状态
        if (!signal_buffer_) {
            return false;
        }

        if (!order_feedback_buffer_) {
            return false;
        }

        if (!control_info_) {
            return false;
        }

        return true;
    }

    /**
     * @brief 重置统计信息
     */
    void reset_stats() override
    {
        BaseSharedMemoryInterface::reset_stats();
        signals_sent_ = 0;
        feedbacks_received_ = 0;
        signal_type_counts_.clear();
        
        // 注意：缓冲区类暂时没有reset_statistics方法
        // 统计信息重置已在上面完成
    }

    /**
     * @brief 检查是否可以发送信号
     * @return true 可以发送，false 不能发送
     */
    bool can_send_signal() const
    {
        return signal_buffer_ && !signal_buffer_->full() && is_trading_enabled() && !is_emergency_stop();
    }

    /**
     * @brief 检查是否有待处理的订单反馈
     * @return true 有反馈，false 无反馈
     */
    bool has_pending_feedbacks() const
    {
        return order_feedback_buffer_ && !order_feedback_buffer_->empty();
    }

private:
    /**
     * @brief 更新信号类型计数
     * @param signal_type 信号类型
     */
    void update_signal_count(SignalType signal_type)
    {
        signal_type_counts_[signal_type]++;
    }

private:
    std::unique_ptr<SignalBuffer> signal_buffer_;                    ///< 信号缓冲区
    std::unique_ptr<OrderFeedbackBuffer> order_feedback_buffer_;     ///< 订单反馈缓冲区
    std::unique_ptr<ControlInfo> control_info_;                      ///< 控制信息
    
    std::atomic<uint64_t> signals_sent_{0};                         ///< 发送信号计数
    std::atomic<uint64_t> feedbacks_received_{0};                    ///< 接收反馈计数
    std::unordered_map<SignalType, uint64_t> signal_type_counts_;    ///< 信号类型计数
};

} // namespace interfaces
} // namespace shared_memory
} // namespace tes