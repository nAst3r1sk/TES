#include "shared_memory/core/signal_buffer.h"
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>

namespace tes {
namespace shared_memory {

SignalBuffer::SignalBuffer(const std::string& name, bool create)
    : shm_name_("/tes_signal_" + name), shm_fd_(-1), shared_data_(nullptr), is_creator_(create)
{
    
    if (create) {
        if (!create_shared_memory()) {
            throw std::runtime_error("Failed to create shared memory for SignalBuffer");
        }
    } else {
        if (!open_shared_memory()) {
            throw std::runtime_error("Failed to open shared memory for SignalBuffer");
        }
    }
}

SignalBuffer::~SignalBuffer()
{
    cleanup();
}

bool SignalBuffer::create_shared_memory()
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

bool SignalBuffer::open_shared_memory()
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

void SignalBuffer::cleanup()
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

bool SignalBuffer::write_signal(const TradingSignal& signal)
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
    
    // 写入信号
    size_t index = current_write % BUFFER_SIZE;
    shared_data_->signals[index] = signal;
    shared_data_->signals[index].sequence_id = current_write;
    
    // 更新写索引
    shared_data_->write_index.store(current_write + 1);
    total_writes_.fetch_add(1);
    
    return true;
}

bool SignalBuffer::read_signal(TradingSignal& signal)
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
    
    // 读取信号
    size_t index = current_read % BUFFER_SIZE;
    signal = shared_data_->signals[index];
    
    // 更新读索引
    shared_data_->read_index.store(current_read + 1);
    total_reads_.fetch_add(1);
    
    return true;
}

size_t SignalBuffer::read_signals(TradingSignal* signals, size_t max_count)
{
    if (!shared_data_ || !shared_data_->is_initialized.load() || !signals || max_count == 0) {
        return 0;
    }
    
    size_t count = 0;
    while (count < max_count) {
        if (!read_signal(signals[count])) {
            break;
        }
        count++;
    }
    
    return count;
}

size_t SignalBuffer::available_signals() const
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        return 0;
    }
    
    uint64_t write_idx = shared_data_->write_index.load();
    uint64_t read_idx = shared_data_->read_index.load();
    
    return write_idx > read_idx ? write_idx - read_idx : 0;
}

bool SignalBuffer::empty() const
{
    return available_signals() == 0;
}

bool SignalBuffer::full() const
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        return true;
    }
    
    uint64_t write_idx = shared_data_->write_index.load();
    uint64_t read_idx = shared_data_->read_index.load();
    
    return (write_idx - read_idx) >= BUFFER_SIZE;
}

void SignalBuffer::clear()
{
    if (!shared_data_ || !shared_data_->is_initialized.load()) {
        return;
    }
    
    uint64_t write_idx = shared_data_->write_index.load();
    shared_data_->read_index.store(write_idx);
}

SignalBuffer::Statistics SignalBuffer::get_statistics() const
{
    return {
        total_writes_.load(),
        total_reads_.load(),
        write_failures_.load(),
        read_failures_.load()
    };
}

} // namespace shared_memory
} // namespace tes