#include "core/common_types.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace tes {
namespace shared_memory {

// 共享内存管理器
class MemoryManager {
public:
    struct MemorySegment {
        std::string name;
        void* ptr;
        size_t size;
        int fd;
        bool is_creator;
        
        MemorySegment() : ptr(nullptr), size(0), fd(-1), is_creator(false) {}
    };
    
    static MemoryManager& instance() {
        static MemoryManager instance;
        return instance;
    }
    
    bool create_segment(const std::string& name, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (segments_.find(name) != segments_.end()) {
            return false; // 已存在
        }
        
        std::string shm_name = "/tes_mem_" + name;
        
        // 创建共享内存
        int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (fd == -1) {
            // 如果已存在，先删除再创建
            shm_unlink(shm_name.c_str());
            fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
            if (fd == -1) {
                return false;
            }
        }
        
        // 设置大小
        if (ftruncate(fd, size) == -1) {
            close(fd);
            shm_unlink(shm_name.c_str());
            return false;
        }
        
        // 映射内存
        void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            close(fd);
            shm_unlink(shm_name.c_str());
            return false;
        }
        
        MemorySegment segment;
        segment.name = shm_name;
        segment.ptr = ptr;
        segment.size = size;
        segment.fd = fd;
        segment.is_creator = true;
        
        segments_[name] = segment;
        return true;
    }
    
    bool open_segment(const std::string& name, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (segments_.find(name) != segments_.end()) {
            return true; // 已打开
        }
        
        std::string shm_name = "/tes_mem_" + name;
        
        // 打开共享内存
        int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
        if (fd == -1) {
            return false;
        }
        
        // 映射内存
        void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            close(fd);
            return false;
        }
        
        MemorySegment segment;
        segment.name = shm_name;
        segment.ptr = ptr;
        segment.size = size;
        segment.fd = fd;
        segment.is_creator = false;
        
        segments_[name] = segment;
        return true;
    }
    
    void* get_segment_ptr(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = segments_.find(name);
        if (it != segments_.end()) {
            return it->second.ptr;
        }
        return nullptr;
    }
    
    void close_segment(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = segments_.find(name);
        if (it != segments_.end()) {
            MemorySegment& segment = it->second;
            
            if (segment.ptr && segment.ptr != MAP_FAILED) {
                munmap(segment.ptr, segment.size);
            }
            
            if (segment.fd != -1) {
                close(segment.fd);
            }
            
            if (segment.is_creator) {
                shm_unlink(segment.name.c_str());
            }
            
            segments_.erase(it);
        }
    }
    
    ~MemoryManager() {
        // 清理所有段
        for (auto& pair : segments_) {
            close_segment(pair.first);
        }
    }
    
private:
    MemoryManager() = default;
    
    std::mutex mutex_;
    std::unordered_map<std::string, MemorySegment> segments_;
};

} // namespace shared_memory
} // namespace tes