#pragma once

#include "common/common_types.h"
#include <atomic>
#include <string>
#include <chrono>
#include <thread>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace tes {
namespace shared_memory {

// 订单回报结构
struct OrderReport {
    uint64_t order_id;
    std::string symbol;
    Side side;
    OrderType type;
    OrderStatus status;
    double quantity;
    double filled_quantity;
    double price;
    double avg_fill_price;
    double commission;
    uint64_t timestamp;
    std::string error_message;
    
    OrderReport() 
        : order_id(0), side(Side::BUY), type(OrderType::MARKET)
        , status(OrderStatus::PENDING), quantity(0.0), filled_quantity(0.0)
        , price(0.0), avg_fill_price(0.0), commission(0.0), timestamp(0) {}
};

// 共享内存中的订单回报缓冲区结构
struct SharedOrderReportBuffer {
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    size_t capacity;
    std::atomic<bool> is_initialized;
    OrderReport reports[]; // 柔性数组成员
    
    SharedOrderReportBuffer() : head(0), tail(0), capacity(0), is_initialized(false) {}
};

// 订单回报缓冲区类
class OrderReportBuffer {
public:
    // 统计信息结构
    struct Statistics {
        size_t total_capacity;
        size_t current_size;
        bool is_empty;
        bool is_full;
        double utilization_ratio;
        
        Statistics() : total_capacity(0), current_size(0), is_empty(true)
                     , is_full(false), utilization_ratio(0.0) {}
    };
    
    // 构造函数和析构函数
    OrderReportBuffer(const std::string& name, size_t capacity = 10000, bool create = true);
    ~OrderReportBuffer();
    
    // 禁用拷贝构造和赋值
    OrderReportBuffer(const OrderReportBuffer&) = delete;
    OrderReportBuffer& operator=(const OrderReportBuffer&) = delete;
    
    // 基本操作
    bool push(const OrderReport& report);
    bool pop(OrderReport& report);
    bool try_pop(OrderReport& report, std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
    
    // 状态查询
    size_t size() const;
    size_t capacity() const;
    bool empty() const;
    bool full() const;
    void clear();
    
    // 统计信息
    Statistics get_statistics() const;
    
    // 清理资源
    void cleanup();
    
private:
    // 共享内存管理
    bool create_shared_memory(size_t capacity);
    bool open_shared_memory();
    
    // 成员变量
    std::string shm_name_;
    int shm_fd_;
    SharedOrderReportBuffer* buffer_;
    bool is_creator_;
};

} // namespace shared_memory
} // namespace tes