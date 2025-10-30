#pragma once

/**
 * @file adapters.h
 * @brief 共享内存适配器统一头文件
 */

#include "execution_adapter.h"
#include "strategy_adapter.h"
#include "monitoring_adapter.h"

namespace tes {
namespace shared_memory {
namespace adapters {

/**
 * @brief 适配器工厂类
 * 
 * 提供创建各种适配器实例的工厂方法
 */
class AdapterFactory {
public:
    /**
     * @brief 创建执行层适配器
     * @return 执行层适配器的唯一指针
     */
    static std::unique_ptr<ExecutionAdapter> create_execution_adapter() {
        return std::make_unique<ExecutionAdapter>();
    }

    /**
     * @brief 创建策略层适配器
     * @return 策略层适配器的唯一指针
     */
    static std::unique_ptr<StrategyAdapter> create_strategy_adapter() {
        return std::make_unique<StrategyAdapter>();
    }

    /**
     * @brief 创建监控层适配器
     * @return 监控层适配器的唯一指针
     */
    static std::unique_ptr<MonitoringAdapter> create_monitoring_adapter() {
        return std::make_unique<MonitoringAdapter>();
    }
};

/**
 * @brief 适配器类型枚举
 */
enum class AdapterType {
    EXECUTION,   ///< 执行层适配器
    STRATEGY,    ///< 策略层适配器
    MONITORING   ///< 监控层适配器
};

/**
 * @brief 获取适配器类型名称
 * @param type 适配器类型
 * @return 类型名称字符串
 */
inline std::string get_adapter_type_name(AdapterType type) {
    switch (type) {
        case AdapterType::EXECUTION:
            return "ExecutionAdapter";
        case AdapterType::STRATEGY:
            return "StrategyAdapter";
        case AdapterType::MONITORING:
            return "MonitoringAdapter";
        default:
            return "UnknownAdapter";
    }
}

} // namespace adapters
} // namespace shared_memory
} // namespace tes