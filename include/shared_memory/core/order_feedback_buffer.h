#pragma once

#include "common_types.h"
#include "../../common/common_types.h"
#include <atomic>
#include <memory>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

namespace tes {
namespace shared_memory {

// 订单回报缓冲区
class OrderFeedbackBuffer {
public:
    static constexpr size_t BUFFER_SIZE = constants::MAX_ORDER_BUFFER_SIZE;
    
    // 使用common_types.h中的OrderFeedback定义
    
    struct alignas(64) SharedData {
        std::atomic<uint64_t> write_index{0};
        std::atomic<uint64_t> read_index{0};
        OrderFeedback feedbacks[BUFFER_SIZE];
        std::atomic<bool> is_initialized{false};
    };
    
    OrderFeedbackBuffer(const std::string& name, bool create = false);
    ~OrderFeedbackBuffer();
    
    // 写入订单回报
    bool write_feedback(const OrderFeedback& feedback);
    
    // 读取订单回报
    bool read_feedback(OrderFeedback& feedback);
    
    // 批量读取订单回报
    size_t read_feedbacks(OrderFeedback* feedbacks, size_t max_count);
    
    // 根据订单ID查找回报
    bool find_feedback_by_order_id(OrderId order_id, OrderFeedback& feedback);
    
    // 获取可用回报数量
    size_t available_feedbacks() const;
    
    // 检查缓冲区状态
    bool empty() const;
    bool full() const;
    void clear();
    
    // 统计信息
    struct Statistics {
        uint64_t total_writes;
        uint64_t total_reads;
        uint64_t write_failures;
        uint64_t read_failures;
        uint64_t duplicate_orders;
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
    mutable std::atomic<uint64_t> duplicate_orders_{0};
    
    bool create_shared_memory();
    bool open_shared_memory();
    void cleanup();
};

} // namespace shared_memory
} // namespace tes