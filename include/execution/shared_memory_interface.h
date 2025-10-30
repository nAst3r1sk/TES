#pragma once

/**
 * @file shared_memory_interface.h
 * @brief 执行层共享内存接口兼容性头文件
 */

#include "../shared_memory/adapters/execution_adapter.h"

namespace tes {
namespace execution {

/**
 * @brief 执行层共享内存接口
 */
using SharedMemoryInterface = shared_memory::adapters::ExecutionAdapter;

} // namespace execution
} // namespace tes

using ExecutionSharedMemoryInterface = tes::execution::SharedMemoryInterface;