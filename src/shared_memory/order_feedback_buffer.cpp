#include "shared_memory/core/order_feedback_buffer.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace tes {
namespace shared_memory {

OrderFeedbackBuffer::OrderFeedbackBuffer(const std::string& name, bool create)
    : shm_name_("/tes_feedback_" + name), shm_fd_(-1), shared_data_(nullptr), is_creator_(create)
{
    
    if (create) {
        if (!create_shared_memory()) {
            throw std::runtime_error("Failed to create shared memory for OrderFeedbackBuffer");
        }
    } else {
        if (!open_shared_memory()) {
            throw std::runtime_error("Failed to open shared memory for OrderFeedbackBuffer");
        }
    }
}

OrderFeedbackBuffer::~OrderFeedbackBuffer()
{
    cleanup();
}

bool OrderFeedbackBuffer::create_shared_memory()
{
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
    if (ftruncate(shm_fd_, sizeof(SharedData)) == -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }
    
    // 映射共享内存
    shared_data_ = static_cast<SharedData*>(
        mmap(nullptr, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0)
    );
    
    if (shared_data_ == MAP_FAILED) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }
    
    // 初始化共享数据
    new (shared_data_) SharedData();
    shared_data_->is_initialized.store(true);
    
    return true;
}

bool OrderFeedbackBuffer::open_shared_memory()
{
    // 打开现有共享内存
    shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
    if (shm_fd_ == -1) {
        return false;
    }
    
    // 映射共享内存
    shared_data_ = static_cast<SharedData*>(
        mmap(nullptr, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0)
    );
    
    if (shared_data_ == MAP_FAILED) {
        close(shm_fd_);
        return false;
    }
    
    // 等待初始化完成
    while (!shared_data_->is_initialized.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    return true;
}

void OrderFeedbackBuffer::cleanup()
{
    if (shared_data_ != nullptr && shared_data_ != MAP_FAILED) {
        munmap(shared_data_, sizeof(SharedData));
        shared_data_ = nullptr;
    }
    
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    
    if (is_creator_) {
        shm_unlink(shm_name_.c_str());
    }
}

bool OrderFeedbackBuffer::write_feedback(const OrderFeedback& feedback)
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        write_failures_.fetch_add(1);
        return false;
    }
    
    uint64_t current_write = shared_data_->write_index.load();
    uint64_t current_read = shared_data_->read_index.load();
    
    // 检查缓冲区是否已满
    if (current_write - current_read >= BUFFER_SIZE) {
        write_failures_.fetch_add(1);
        return false;
    }
    
    // 写入回报
    size_t index = current_write % BUFFER_SIZE;
    shared_data_->feedbacks[index] = feedback;
    shared_data_->feedbacks[index].sequence_id = current_write;
    shared_data_->feedbacks[index].timestamp = get_current_timestamp_ns();
    
    // 更新写索引
    shared_data_->write_index.store(current_write + 1);
    total_writes_.fetch_add(1);
    
    return true;
}

bool OrderFeedbackBuffer::read_feedback(OrderFeedback& feedback)
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        read_failures_.fetch_add(1);
        return false;
    }
    
    uint64_t current_read = shared_data_->read_index.load();
    uint64_t current_write = shared_data_->write_index.load();
    
    // 检查缓冲区是否为空
    if (current_read >= current_write) {
        read_failures_.fetch_add(1);
        return false;
    }
    
    // 读取回报
    size_t index = current_read % BUFFER_SIZE;
    feedback = shared_data_->feedbacks[index];
    
    // 更新读索引
    shared_data_->read_index.store(current_read + 1);
    total_reads_.fetch_add(1);
    
    return true;
}

size_t OrderFeedbackBuffer::read_feedbacks(OrderFeedback* feedbacks, size_t max_count)
{
    if (!shared_data_ || !shared_data_->is_initialized.load() || !feedbacks || max_count == 0) {
        return 0;
    }
    
    size_t count = 0;
    while (count < max_count) {
        if (!read_feedback(feedbacks[count])) {
            break;
        }
        count++;
    }
    
    return count;
}

bool OrderFeedbackBuffer::find_feedback_by_order_id(OrderId order_id, OrderFeedback& feedback)
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        return false;
    }
    
    uint64_t current_read = shared_data_->read_index.load();
    uint64_t current_write = shared_data_->write_index.load();
    
    // 从最新的回报开始搜索
    for (uint64_t i = current_write; i > current_read; --i) {
        size_t index = (i - 1) % BUFFER_SIZE;
        if (shared_data_->feedbacks[index].order_id == order_id) {
            feedback = shared_data_->feedbacks[index];
            return true;
        }
    }
    
    return false;
}

size_t OrderFeedbackBuffer::available_feedbacks() const
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        return 0;
    }
    
    uint64_t write_idx = shared_data_->write_index.load();
    uint64_t read_idx = shared_data_->read_index.load();
    
    return write_idx > read_idx ? write_idx - read_idx : 0;
}

bool OrderFeedbackBuffer::empty() const
{
    return available_feedbacks() == 0;
}

bool OrderFeedbackBuffer::full() const
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        return true;
    }
    
    uint64_t write_idx = shared_data_->write_index.load();
    uint64_t read_idx = shared_data_->read_index.load();
    
    return (write_idx - read_idx) >= BUFFER_SIZE;
}

void OrderFeedbackBuffer::clear()
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        return;
    }
    
    uint64_t write_idx = shared_data_->write_index.load();
    shared_data_->read_index.store(write_idx);
}

OrderFeedbackBuffer::Statistics OrderFeedbackBuffer::get_statistics() const
{
    return {
        total_writes_.load(),
        total_reads_.load(),
        write_failures_.load(),
        read_failures_.load(),
        duplicate_orders_.load()
    };
}

} // namespace shared_memory
} // namespace tes