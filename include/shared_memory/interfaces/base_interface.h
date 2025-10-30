#pragma once

#include "../core/signal_buffer.h"
#include "../core/order_feedback_buffer.h"
#include "../core/control_info.h"
#include "../core/common_types.h"
#include <memory>
#include <string>
#include <atomic>
#include <chrono>

namespace tes {
namespace shared_memory {
namespace interfaces {

/**
 * @brief 共享内存接口基类
 * 
 * 提供所有共享内存接口的通用功能，包括连接管理、基础统计信息等
 */
class BaseSharedMemoryInterface
{
public:
    BaseSharedMemoryInterface() = default;
    virtual ~BaseSharedMemoryInterface() = default;

    // 禁用拷贝构造和赋值
    BaseSharedMemoryInterface(const BaseSharedMemoryInterface&) = delete;
    BaseSharedMemoryInterface& operator=(const BaseSharedMemoryInterface&) = delete;

    /**
     * @brief 初始化共享内存接口
     * @return true 初始化成功，false 初始化失败
     */
    virtual bool initialize()
    {
        if (initialized_.load()) {
            return true;
        }
        
        // 基础初始化逻辑
        last_heartbeat_ = get_current_timestamp();
        initialized_.store(true);
        return true;
    }

    /**
     * @brief 连接到共享内存
     * @return true 连接成功，false 连接失败
     */
    virtual bool connect()
    {
        if (!initialized_.load()) {
            return false;
        }
        
        if (connected_.load()) {
            return true;
        }
        
        // 基础连接逻辑
        connected_.store(true);
        connection_time_ = get_current_timestamp();
        return true;
    }

    /**
     * @brief 断开共享内存连接
     */
    virtual void disconnect()
    {
        connected_.store(false);
        disconnection_time_ = get_current_timestamp();
    }

    /**
     * @brief 检查是否已连接
     * @return true 已连接，false 未连接
     */
    virtual bool is_connected() const
    {
        return connected_.load();
    }

    /**
     * @brief 检查是否已初始化
     * @return true 已初始化，false 未初始化
     */
    virtual bool is_initialized() const
    {
        return initialized_.load();
    }

    /**
     * @brief 更新心跳时间戳
     */
    virtual void update_heartbeat()
    {
        last_heartbeat_ = get_current_timestamp();
    }

    /**
     * @brief 获取最后心跳时间
     * @return 最后心跳时间戳
     */
    virtual Timestamp get_last_heartbeat() const
    {
        return last_heartbeat_;
    }

    /**
     * @brief 获取连接时间
     * @return 连接时间戳
     */
    virtual Timestamp get_connection_time() const
    {
        return connection_time_;
    }

    /**
     * @brief 获取基础统计信息
     * @return 性能统计结构
     */
    virtual PerformanceStats get_base_stats() const
    {
        PerformanceStats stats{};
        stats.connection_time = connection_time_;
        stats.last_heartbeat = last_heartbeat_;
        stats.is_connected = connected_.load();
        return stats;
    }

    /**
     * @brief 获取接口类型名称
     * @return 接口类型字符串
     */
    virtual std::string get_interface_type() const
    {
        return "BaseSharedMemoryInterface";
    }

    /**
     * @brief 执行健康检查
     * @return true 健康，false 不健康
     */
    virtual bool health_check() const
    {
        if (!initialized_.load() || !connected_.load()) {
            return false;
        }
        
        // 检查心跳是否超时（5秒）
        auto current_time = get_current_timestamp();
        Timestamp heartbeat_timeout = 5000000ULL; // 5秒，以微秒为单位
        
        return (current_time - last_heartbeat_) < heartbeat_timeout;
    }

    /**
     * @brief 重置统计信息
     */
    virtual void reset_stats()
    {
        last_heartbeat_ = get_current_timestamp();
        connection_time_ = 0;
        disconnection_time_ = 0;
    }

protected:
    /**
     * @brief 获取当前时间戳
     * @return 当前时间戳（微秒）
     */
    Timestamp get_current_timestamp() const
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    }

    /**
     * @brief 验证缓冲区有效性
     * @param buffer_ptr 缓冲区指针
     * @return true 有效，false 无效
     */
    template<typename T>
    bool validate_buffer(const T* buffer_ptr) const
    {
        return buffer_ptr != nullptr && initialized_.load() && connected_.load();
    }

    /**
     * @brief 记录错误信息
     * @param error_msg 错误消息
     */
    virtual void log_error(const std::string& error_msg) const
    {
        // 基础错误记录实现
        // 实际项目中可以集成日志系统
        (void)error_msg; // 避免unused parameter警告
    }

private:
    std::atomic<bool> initialized_{false};      ///< 初始化状态
    std::atomic<bool> connected_{false};        ///< 连接状态
    std::atomic<Timestamp> last_heartbeat_{0};  ///< 最后心跳时间
    Timestamp connection_time_{0};              ///< 连接时间
    Timestamp disconnection_time_{0};           ///< 断开连接时间

};

} // namespace interfaces
} // namespace shared_memory
} // namespace tes