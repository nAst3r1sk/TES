#include "core/common_types.h"
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace tes {
namespace shared_memory {

// 状态同步器
class StateSync {
public:
    enum class SyncState {
        IDLE = 0,
        SYNCING = 1,
        COMPLETED = 2,
        ERROR = 3
    };
    
    StateSync() : state_(SyncState::IDLE) {}
    
    void start_sync() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = SyncState::SYNCING;
        cv_.notify_all();
    }
    
    void complete_sync() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = SyncState::COMPLETED;
        cv_.notify_all();
    }
    
    void error_sync() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = SyncState::ERROR;
        cv_.notify_all();
    }
    
    bool wait_for_sync(uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                           [this] { return state_ == SyncState::COMPLETED; });
    }
    
    SyncState get_state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    SyncState state_;
};

} // namespace shared_memory
} // namespace tes