#include "core/order_report_buffer.h"
#include <stdexcept>
#include <chrono>
#include <cstring>

namespace tes {
namespace shared_memory {

OrderReportBuffer::OrderReportBuffer(const std::string& name, size_t capacity, bool create)
    : shm_name_("/tes_order_report_" + name), shm_fd_(-1), buffer_(nullptr), is_creator_(create)
{
    if (create) {
        if (!create_shared_memory(capacity)) {
            throw std::runtime_error("Failed to create shared memory for OrderReportBuffer");
        }
    } else {
        if (!open_shared_memory()) {
            throw std::runtime_error("Failed to open shared memory for OrderReportBuffer");
        }
    }
}

OrderReportBuffer::~OrderReportBuffer()
{
    cleanup();
}

bool OrderReportBuffer::create_shared_memory(size_t capacity)
{
    size_t total_size = sizeof(SharedOrderReportBuffer) + capacity * sizeof(OrderReport);
    
    // 创建共享内存
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd_ == -1) {
        // 如果已存在，先删除再创建
        shm_unlink(shm_name_.c_str());
        shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (shm_fd_ == -1) {
            return false;
        }
    }
    
    // 设置共享内存大小
    if (ftruncate(shm_fd_, total_size) == -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }
    
    // 映射共享内存
    buffer_ = static_cast<SharedOrderReportBuffer*>(
        mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0)
    );
    
    if (buffer_ == MAP_FAILED) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }
    
    // 初始化缓冲区
    new (buffer_) SharedOrderReportBuffer();
    buffer_->capacity = capacity;
    buffer_->head.store(0);
    buffer_->tail.store(0);
    buffer_->is_initialized.store(true);
    
    return true;
}

bool OrderReportBuffer::open_shared_memory()
{
    // 先尝试获取共享内存大小
    shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
    if (shm_fd_ == -1) {
        return false;
    }
    
    struct stat shm_stat;
    if (fstat(shm_fd_, &shm_stat) == -1) {
        close(shm_fd_);
        return false;
    }
    
    // 映射共享内存
    buffer_ = static_cast<SharedOrderReportBuffer*>(
        mmap(nullptr, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0)
    );
    
    if (buffer_ == MAP_FAILED) {
        close(shm_fd_);
        return false;
    }
    
    // 等待初始化完成
    while (!buffer_->is_initialized.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    return true;
}

void OrderReportBuffer::cleanup()
{
    if (buffer_ != nullptr && buffer_ != MAP_FAILED) {
        size_t total_size = sizeof(SharedOrderReportBuffer) + buffer_->capacity * sizeof(OrderReport);
        munmap(buffer_, total_size);
        buffer_ = nullptr;
    }
    
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    
    if (is_creator_) {
        shm_unlink(shm_name_.c_str());
    }
}

bool OrderReportBuffer::push(const OrderReport& report)
{
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return false;
    }
    
    size_t current_tail = buffer_->tail.load();
    size_t next_tail = (current_tail + 1) % buffer_->capacity;
    
    // 检查缓冲区是否已满
    if (next_tail == buffer_->head.load()) {
        return false; // 缓冲区已满
    }
    
    // 写入数据
    buffer_->reports[current_tail] = report;
    buffer_->reports[current_tail].timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    // 更新尾指针
    buffer_->tail.store(next_tail);
    
    return true;
}

bool OrderReportBuffer::pop(OrderReport& report)
{
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return false;
    }
    
    size_t current_head = buffer_->head.load();
    
    // 检查缓冲区是否为空
    if (current_head == buffer_->tail.load()) {
        return false; // 缓冲区为空
    }
    
    // 读取数据
    report = buffer_->reports[current_head];
    
    // 更新头指针
    size_t next_head = (current_head + 1) % buffer_->capacity;
    buffer_->head.store(next_head);
    
    return true;
}

bool OrderReportBuffer::try_pop(OrderReport& report, std::chrono::milliseconds timeout)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (std::chrono::high_resolution_clock::now() - start_time < timeout) {
        if (pop(report)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    return false;
}

size_t OrderReportBuffer::size() const
{
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return 0;
    }
    
    size_t head = buffer_->head.load();
    size_t tail = buffer_->tail.load();
    
    if (tail >= head) {
        return tail - head;
    } else {
        return buffer_->capacity - head + tail;
    }
}

size_t OrderReportBuffer::capacity() const
{
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return 0;
    }
    return buffer_->capacity;
}

bool OrderReportBuffer::empty() const
{
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return true;
    }
    return buffer_->head.load() == buffer_->tail.load();
}

bool OrderReportBuffer::full() const
{
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return false;
    }
    
    size_t head = buffer_->head.load();
    size_t tail = buffer_->tail.load();
    size_t next_tail = (tail + 1) % buffer_->capacity;
    
    return next_tail == head;
}

void OrderReportBuffer::clear()
{
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return;
    }
    
    buffer_->head.store(0);
    buffer_->tail.store(0);
}

OrderReportBuffer::Statistics OrderReportBuffer::get_statistics() const
{
    Statistics stats;
    
    if (!buffer_ || !buffer_->is_initialized.load()) {
        return stats;
    }
    
    stats.total_capacity = buffer_->capacity;
    stats.current_size = size();
    stats.is_empty = empty();
    stats.is_full = full();
    stats.utilization_ratio = static_cast<double>(stats.current_size) / stats.total_capacity;
    
    return stats;
}

} // namespace shared_memory
} // namespace tes