#pragma once

#include "../interfaces/execution_interface.h"
#include "../../execution/shared_memory_interface_legacy.h"
#include <memory>

namespace tes {
namespace shared_memory {
namespace adapters {

/**
 * @brief 执行层适配器
 */
class ExecutionAdapter
{
public:
    ExecutionAdapter()
        : impl_(std::make_unique<interfaces::ExecutionInterface>())
    {
    }
    
    virtual ~ExecutionAdapter() = default;

    ExecutionAdapter(const ExecutionAdapter&) = delete;
    ExecutionAdapter& operator=(const ExecutionAdapter&) = delete;

    /**
     * @brief 初始化接口
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize()
    {
        return impl_->initialize();
    }

    /**
     * @brief 初始化接口
     * @param enable_shared_memory 是否启用共享内存组件
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize(bool enable_shared_memory)
    {
        return impl_->initialize_with_shared_memory(enable_shared_memory);
    }

    /**
     * @brief 连接到共享内存
     * @return true 连接成功，false 连接失败
     */
    bool connect()
    {
        return impl_->connect();
    }

    /**
     * @brief 断开连接
     */
    void disconnect()
    {
        impl_->disconnect();
    }

    /**
     * @brief 检查是否已连接
     * @return true 已连接，false 未连接
     */
    bool is_connected() const
    {
        return impl_->is_connected();
    }

    /**
     * @brief 接收交易信号
     * @param signal 输出参数，接收到的信号
     * @return true 成功接收，false 无信号或失败
     */
    bool receive_signal(TradingSignal& signal)
    {
        return impl_->receive_signal(signal);
    }

    /**
     * @brief 批量接收交易信号
     * @param signals 输出参数，接收到的信号列表
     * @param max_count 最大接收数量
     * @return 实际接收到的信号数量
     */
    size_t receive_signals_batch(std::vector<TradingSignal>& signals, size_t max_count)
    {
        return impl_->receive_signals_batch(signals, max_count);
    }

    /**
     * @brief 发送订单反馈
     * @param feedback 订单反馈信息
     * @return true 发送成功，false 发送失败
     */
    bool send_order_feedback(const OrderFeedback& feedback)
    {
        return impl_->send_order_feedback(feedback);
    }

    /**
     * @brief 批量发送订单反馈
     * @param feedbacks 订单反馈列表
     * @return 成功发送的数量
     */
    size_t send_order_feedbacks_batch(const std::vector<OrderFeedback>& feedbacks)
    {
        return impl_->send_order_feedbacks_batch(feedbacks);
    }

    /**
     * @brief 获取统计信息
     * @return 统计信息结构
     */
    interfaces::ExecutionInterface::ExecutionStats get_statistics() const
    {
        return impl_->get_execution_stats();
    }

    /**
     * @brief 获取信号缓冲区统计信息
     * @return 信号缓冲区统计
     */
    SignalBuffer::Statistics get_signal_buffer_stats() const
    {
        return impl_->get_signal_buffer_stats();
    }

    /**
     * @brief 获取订单反馈缓冲区统计信息
     * @return 订单反馈缓冲区统计
     */
    OrderFeedbackBuffer::Statistics get_feedback_buffer_stats() const
    {
        return impl_->get_feedback_buffer_stats();
    }

    /**
     * @brief 执行健康检查
     * @return true 健康，false 不健康
     */
    bool health_check() const
    {
        return impl_->health_check();
    }

    /**
     * @brief 重置统计信息
     */
    void reset_stats()
    {
        impl_->reset_stats();
    }

    /**
     * @brief 检查信号缓冲区是否有数据
     * @return true 有数据，false 无数据
     */
    bool has_pending_signals() const
    {
        return impl_->has_pending_signals();
    }

    /**
     * @brief 检查订单反馈缓冲区是否有空间
     * @return true 有空间，false 缓冲区满
     */
    bool can_send_feedback() const
    {
        return impl_->can_send_feedback();
    }

    /**
     * @brief 更新心跳时间戳
     */
    void update_heartbeat()
    {
        impl_->update_heartbeat();
    }

    /**
     * @brief 获取最后心跳时间
     * @return 最后心跳时间戳
     */
    Timestamp get_last_heartbeat() const
    {
        return impl_->get_last_heartbeat();
    }

    /**
     * @brief 获取连接时间
     * @return 连接时间戳
     */
    Timestamp get_connection_time() const
    {
        return impl_->get_connection_time();
    }

    /**
     * @brief 获取接口类型名称
     * @return 接口类型字符串
     */
    std::string get_interface_type() const
    {
        return "ExecutionAdapter(" + impl_->get_interface_type() + ")";
    }

    // 为了兼容旧代码，提供一些旧接口的别名方法
    
    /**
     * @brief 旧接口兼容：获取信号
     * @param signal 输出参数
     * @return true 成功，false 失败
     */
    bool get_signal(TradingSignal& signal)
    {
        return receive_signal(signal);
    }

    /**
     * @brief 旧接口兼容：发送反馈
     * @param feedback 反馈信息
     * @return true 成功，false 失败
     */
    bool put_feedback(const OrderFeedback& feedback)
    {
        return send_order_feedback(feedback);
    }

    /**
     * @brief 旧接口兼容：检查连接状态
     * @return true 已连接，false 未连接
     */
    bool connected() const
    {
        return is_connected();
    }

private:
    std::unique_ptr<interfaces::ExecutionInterface> impl_;  ///< 新接口实现
};

} // namespace adapters
} // namespace shared_memory
} // namespace tes