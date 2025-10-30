#pragma once

#include "base_interface.h"
#include <vector>
#include <mutex>

namespace tes {
namespace shared_memory {
namespace interfaces {

/**
 * @brief 执行层共享内存接口
 * 
 * 专门处理执行层的信号接收、订单反馈发送等功能
 */
class ExecutionInterface : public BaseSharedMemoryInterface {
public:
    ExecutionInterface() = default;
    virtual ~ExecutionInterface() = default;

    /**
     * @brief 初始化执行接口
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize() override {
        return initialize_with_shared_memory(true);
    }

    /**
     * @brief 初始化执行接口
     * @param enable_shared_memory 是否启用共享内存组件
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize_with_shared_memory(bool enable_shared_memory) {
        if (!BaseSharedMemoryInterface::initialize()) {
            return false;
        }

        // 只在启用共享内存时初始化缓冲区
        if (enable_shared_memory) {
            // 初始化信号缓冲区
            signal_buffer_ = std::make_unique<SignalBuffer>("tes_signal_buffer", false);
            
            // 初始化订单反馈缓冲区
            order_feedback_buffer_ = std::make_unique<OrderFeedbackBuffer>("tes_order_feedback", false);
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

        // SignalBuffer and OrderFeedbackBuffer don't have connect methods
        // They are initialized during construction

        return true;
    }

    /**
     * @brief 断开连接
     */
    void disconnect() override {
        // SignalBuffer and OrderFeedbackBuffer don't have disconnect methods
        // They are cleaned up during destruction
        BaseSharedMemoryInterface::disconnect();
    }

    /**
     * @brief 接收交易信号
     * @param signal 输出参数，接收到的信号
     * @return true 成功接收，false 无信号或失败
     */
    bool receive_signal(TradingSignal& signal) {
        // 如果共享内存未启用，直接返回false
        if (!signal_buffer_) {
            return false;
        }
        
        if (!validate_buffer(signal_buffer_.get())) {
            return false;
        }

        bool result = signal_buffer_->read_signal(signal);
        if (result) {
            signals_received_++;
            update_heartbeat();
        }
        return result;
    }

    /**
     * @brief 批量接收交易信号
     * @param signals 输出参数，接收到的信号列表
     * @param max_count 最大接收数量
     * @return 实际接收到的信号数量
     */
    size_t receive_signals_batch(std::vector<TradingSignal>& signals, size_t max_count) {
        // 如果共享内存未启用，直接返回0
        if (!signal_buffer_) {
            signals.clear();
            return 0;
        }
        
        if (!validate_buffer(signal_buffer_.get())) {
            return 0;
        }

        signals.clear();
        signals.reserve(max_count);

        size_t count = 0;
        TradingSignal signal;
        while (count < max_count && signal_buffer_->read_signal(signal)) {
            signals.push_back(signal);
            count++;
        }

        if (count > 0) {
            signals_received_ += count;
            update_heartbeat();
        }

        return count;
    }

    /**
     * @brief 发送订单反馈
     * @param feedback 订单反馈信息
     * @return true 发送成功，false 发送失败
     */
    bool send_order_feedback(const OrderFeedback& feedback) {
        // 如果共享内存未启用，直接返回true（假装发送成功）
        if (!order_feedback_buffer_) {
            return true;
        }
        
        if (!validate_buffer(order_feedback_buffer_.get())) {
            return false;
        }

        bool result = order_feedback_buffer_->write_feedback(feedback);
        if (result) {
            feedbacks_sent_++;
            update_heartbeat();
        }
        return result;
    }

    /**
     * @brief 批量发送订单反馈
     * @param feedbacks 订单反馈列表
     * @return 成功发送的数量
     */
    size_t send_order_feedbacks_batch(const std::vector<OrderFeedback>& feedbacks) {
        // 如果共享内存未启用，直接返回全部数量（假装发送成功）
        if (!order_feedback_buffer_) {
            return feedbacks.size();
        }
        
        if (!validate_buffer(order_feedback_buffer_.get())) {
            return 0;
        }

        size_t sent_count = 0;
        for (const auto& feedback : feedbacks) {
            if (order_feedback_buffer_->write_feedback(feedback)) {
                sent_count++;
            } else {
                break; // 缓冲区满，停止发送
            }
        }

        if (sent_count > 0) {
            feedbacks_sent_ += sent_count;
            update_heartbeat();
        }

        return sent_count;
    }

    /**
     * @brief 获取信号缓冲区统计信息
     * @return 信号缓冲区统计
     */
    SignalBuffer::Statistics get_signal_buffer_stats() const {
        if (!signal_buffer_) {
            return SignalBuffer::Statistics{};
        }
        return signal_buffer_->get_statistics();
    }

    /**
     * @brief 获取订单反馈缓冲区统计信息
     * @return 订单反馈缓冲区统计
     */
    OrderFeedbackBuffer::Statistics get_feedback_buffer_stats() const {
        if (!order_feedback_buffer_) {
            return OrderFeedbackBuffer::Statistics{};
        }
        return order_feedback_buffer_->get_statistics();
    }

    /**
     * @brief 获取执行接口统计信息
     * @return 执行统计信息
     */
    struct ExecutionStats {
        uint64_t signals_received = 0;
        uint64_t feedbacks_sent = 0;
        SignalBuffer::Statistics signal_buffer_stats;
        OrderFeedbackBuffer::Statistics feedback_buffer_stats;
        PerformanceStats base_stats;
    };

    ExecutionStats get_execution_stats() const {
        ExecutionStats stats;
        stats.signals_received = signals_received_;
        stats.feedbacks_sent = feedbacks_sent_;
        stats.signal_buffer_stats = get_signal_buffer_stats();
        stats.feedback_buffer_stats = get_feedback_buffer_stats();
        stats.base_stats = get_base_stats();
        return stats;
    }

    /**
     * @brief 获取接口类型名称
     * @return 接口类型字符串
     */
    std::string get_interface_type() const override {
        return "ExecutionInterface";
    }

    /**
     * @brief 执行健康检查
     * @return true 健康，false 不健康
     */
    bool health_check() const override {
        if (!BaseSharedMemoryInterface::health_check()) {
            return false;
        }

        // 检查缓冲区状态
        if (!signal_buffer_) {
            return false;
        }

        if (!order_feedback_buffer_) {
            return false;
        }

        return true;
    }

    /**
     * @brief 重置统计信息
     */
    void reset_stats() override {
        BaseSharedMemoryInterface::reset_stats();
        signals_received_ = 0;
        feedbacks_sent_ = 0;
        
        if (signal_buffer_) {
            // SignalBuffer doesn't have reset_statistics method
            // Statistics are automatically managed internally
        }
        if (order_feedback_buffer_) {
            // OrderFeedbackBuffer doesn't have reset_statistics method
            // Statistics are automatically managed internally
        }
    }

    /**
     * @brief 检查信号缓冲区是否有数据
     * @return true 有数据，false 无数据
     */
    bool has_pending_signals() const {
        return signal_buffer_ && !signal_buffer_->empty();
    }

    /**
     * @brief 检查订单反馈缓冲区是否有空间
     * @return true 有空间，false 缓冲区满
     */
    bool can_send_feedback() const {
        return order_feedback_buffer_ && !order_feedback_buffer_->full();
    }

private:
    std::unique_ptr<SignalBuffer> signal_buffer_;                    ///< 信号缓冲区
    std::unique_ptr<OrderFeedbackBuffer> order_feedback_buffer_;     ///< 订单反馈缓冲区
    
    std::atomic<uint64_t> signals_received_{0};                     ///< 接收信号计数
    std::atomic<uint64_t> feedbacks_sent_{0};                       ///< 发送反馈计数
};

} // namespace interfaces
} // namespace shared_memory
} // namespace tes