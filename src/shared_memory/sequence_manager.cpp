#include "core/common_types.h"
#include <atomic>

namespace tes {
namespace shared_memory {

// 序列号管理器
class SequenceManager {
public:
    SequenceManager() : next_sequence_(1) {}
    
    uint64_t get_next_sequence() {
        return next_sequence_.fetch_add(1);
    }
    
    uint64_t get_current_sequence() const {
        return next_sequence_.load();
    }
    
    void reset_sequence(uint64_t start_value = 1) {
        next_sequence_.store(start_value);
    }
    
private:
    std::atomic<uint64_t> next_sequence_;
};

} // namespace shared_memory
} // namespace tes