#pragma once

#include "lockfree_queue.h"
#include "thread_pool.h"
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <shared_mutex>

namespace tes {
namespace execution {

// 异步回调事件类型
enum class CallbackEventType {
    ORDER_CREATED,
    ORDER_FILLED,
    ORDER_CANCELLED,
    ORDER_REJECTED,
    EXECUTION_STARTED,
    EXECUTION_COMPLETED,
    EXECUTION_FAILED,
    MARKET_DATA_UPDATE,
    POSITION_UPDATE
};

// 异步回调事件
struct AsyncCallbackEvent {
    CallbackEventType type;
    std::string event_id;
    std::string execution_id;
    std::string order_id;
    std::chrono::high_resolution_clock::time_point timestamp;
    std::string data; // JSON格式的事件数据
    
    AsyncCallbackEvent() : type(CallbackEventType::ORDER_CREATED), 
                          timestamp(std::chrono::high_resolution_clock::now()) {}
    
    AsyncCallbackEvent(CallbackEventType t, const std::string& eid, const std::string& d)
        : type(t), execution_id(eid), data(d), 
          timestamp(std::chrono::high_resolution_clock::now()) {
        event_id = generate_event_id();
    }
    
private:
    std::string generate_event_id() const {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        return "evt_" + std::to_string(now) + "_" + std::to_string(counter.fetch_add(1));
    }
};

// 回调函数类型定义
using AsyncCallback = std::function<void(const AsyncCallbackEvent&)>;
using CallbackFilter = std::function<bool(const AsyncCallbackEvent&)>;

// 异步回调管理器
class AsyncCallbackManager {
public:
    struct Config {
        size_t thread_pool_size;        // 回调处理线程池大小
        size_t max_queue_size;          // 最大队列大小
        uint32_t batch_size;            // 批处理大小
        uint32_t flush_interval_ms;     // 刷新间隔（毫秒）
        bool enable_priority_queue;     // 启用优先级队列
        
        Config() : thread_pool_size(2), max_queue_size(10000), 
                  batch_size(50), flush_interval_ms(10), 
                  enable_priority_queue(false) {}
    };
    
    struct Statistics {
        std::atomic<uint64_t> total_events{0};
        std::atomic<uint64_t> processed_events{0};
        std::atomic<uint64_t> dropped_events{0};
        std::atomic<uint64_t> callback_errors{0};
        std::atomic<double> average_processing_time{0.0};
        std::chrono::high_resolution_clock::time_point last_event_time;
        
        Statistics() = default;
        Statistics(const Statistics& other) {
            total_events.store(other.total_events.load());
            processed_events.store(other.processed_events.load());
            dropped_events.store(other.dropped_events.load());
            callback_errors.store(other.callback_errors.load());
            average_processing_time.store(other.average_processing_time.load());
            last_event_time = other.last_event_time;
        }
        Statistics& operator=(const Statistics& other) {
            if (this != &other) {
                total_events.store(other.total_events.load());
                processed_events.store(other.processed_events.load());
                dropped_events.store(other.dropped_events.load());
                callback_errors.store(other.callback_errors.load());
                average_processing_time.store(other.average_processing_time.load());
                last_event_time = other.last_event_time;
            }
            return *this;
        }
    };
    
    AsyncCallbackManager();
    ~AsyncCallbackManager();
    
    // 生命周期管理
    bool initialize(const Config& config = Config());
    bool start();
    void stop();
    void cleanup();
    bool is_running() const { return running_.load(); }
    
    // 回调注册
    std::string register_callback(CallbackEventType type, AsyncCallback callback);
    std::string register_filtered_callback(CallbackFilter filter, AsyncCallback callback);
    bool unregister_callback(const std::string& callback_id);
    void clear_callbacks();
    
    // 事件发布
    bool publish_event(const AsyncCallbackEvent& event);
    bool publish_event(CallbackEventType type, const std::string& execution_id, const std::string& data);
    
    // 批量事件发布
    bool publish_events(const std::vector<AsyncCallbackEvent>& events);
    
    // 配置和统计
    void set_config(const Config& config);
    Config get_config() const;
    Statistics get_statistics() const;
    
    // 队列状态
    size_t get_queue_size() const;
    bool is_queue_full() const;
    
private:
    struct CallbackInfo {
        std::string id;
        CallbackEventType type;
        AsyncCallback callback;
        CallbackFilter filter;
        bool has_filter;
        std::chrono::high_resolution_clock::time_point registered_time;
        
        CallbackInfo(const std::string& callback_id, CallbackEventType t, AsyncCallback cb)
            : id(callback_id), type(t), callback(std::move(cb)), has_filter(false),
              registered_time(std::chrono::high_resolution_clock::now()) {}
              
        CallbackInfo(const std::string& callback_id, CallbackFilter f, AsyncCallback cb)
            : id(callback_id), type(CallbackEventType::ORDER_CREATED), 
              callback(std::move(cb)), filter(std::move(f)), has_filter(true),
              registered_time(std::chrono::high_resolution_clock::now()) {}
    };
    
    // 内部方法
    void event_processing_worker();
    void process_event_batch();
    void process_single_event(const AsyncCallbackEvent& event);
    void invoke_callbacks(const AsyncCallbackEvent& event);
    std::string generate_callback_id() const;
    
    // 成员变量
    Config config_;
    mutable std::mutex config_mutex_;
    
    std::unique_ptr<ThreadPool> callback_thread_pool_;
    std::unique_ptr<MPMCLockFreeQueue<AsyncCallbackEvent>> event_queue_;
    
    std::vector<CallbackInfo> callbacks_;
    mutable std::shared_mutex callbacks_mutex_;
    
    Statistics statistics_;
    mutable std::mutex statistics_mutex_;
    
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    
    std::thread processing_thread_;
    
    // 禁止拷贝和赋值
    AsyncCallbackManager(const AsyncCallbackManager&) = delete;
    AsyncCallbackManager& operator=(const AsyncCallbackManager&) = delete;
};

} // namespace execution
} // namespace tes