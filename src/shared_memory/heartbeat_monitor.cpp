#include "core/common_types.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace tes {
namespace shared_memory {

// 心跳监控实现
class HeartbeatMonitor {
public:
    HeartbeatMonitor() : running_(false) {}
    
    void start() {
        running_ = true;
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        last_heartbeat_.store(now_ns);
    }
    
    void stop() {
        running_ = false;
    }
    
    void update_heartbeat() {
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        last_heartbeat_.store(now_ns);
        heartbeat_count_.fetch_add(1);
    }
    
    bool is_alive(uint32_t timeout_ms) const {
        if (!running_.load()) {
            return false;
        }
        
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        auto last_ns = last_heartbeat_.load();
        auto elapsed_ms = (now_ns - last_ns) / 1000000; // Convert ns to ms
        return elapsed_ms < timeout_ms;
    }
    
    uint64_t get_heartbeat_count() const {
        return heartbeat_count_.load();
    }
    
private:
    std::atomic<bool> running_;
    std::atomic<Timestamp> last_heartbeat_;
    std::atomic<uint64_t> heartbeat_count_{0};
};

} // namespace shared_memory
} // namespace tes