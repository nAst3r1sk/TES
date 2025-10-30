#pragma once

#include "common_types.h"
#include <atomic>
#include <memory>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace tes {
namespace shared_memory {

// 无锁环形缓冲区用于交易信号
class SignalBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 1024; // 最大信号缓冲区大小
    
    struct alignas(64) SharedData {
        std::atomic<uint64_t> write_index{0};
        std::atomic<uint64_t> read_index{0};
        TradingSignal signals[BUFFER_SIZE];
        std::atomic<bool> is_initialized{false};
    };
    
    SignalBuffer(const std::string& name, bool create = false);
    ~SignalBuffer();
    
    // 写入信号（生产者）
    bool write_signal(const TradingSignal& signal);
    
    // 读取信号（消费者）
    bool read_signal(TradingSignal& signal);
    
    // 批量读取信号
    size_t read_signals(TradingSignal* signals, size_t max_count);
    
    // 获取可用信号数量
    size_t available_signals() const;
    
    // 检查缓冲区是否为空
    bool empty() const;
    
    // 检查缓冲区是否已满
    bool full() const;
    
    // 清空缓冲区
    void clear();
    
    // 获取统计信息
    struct Statistics {
        uint64_t total_writes;
        uint64_t total_reads;
        uint64_t write_failures;
        uint64_t read_failures;
    };
    
    Statistics get_statistics() const;
    
private:
    std::string shm_name_;
    int shm_fd_;
    SharedData* shared_data_;
    bool is_creator_;
    
    mutable std::atomic<uint64_t> total_writes_{0};
    mutable std::atomic<uint64_t> total_reads_{0};
    mutable std::atomic<uint64_t> write_failures_{0};
    mutable std::atomic<uint64_t> read_failures_{0};
    
    bool create_shared_memory();
    bool open_shared_memory();
    void cleanup();
};

} // namespace shared_memory
} // namespace tes