#include "async_callback_manager.h"
#include <algorithm>
#include <sstream>
#include <shared_mutex>

namespace tes {
namespace execution {

AsyncCallbackManager::AsyncCallbackManager()
    : running_(false)
    , initialized_(false)
{}

AsyncCallbackManager::~AsyncCallbackManager()
{
    stop();
    cleanup();
}

bool AsyncCallbackManager::initialize(const Config& config)
{
    if (initialized_.load()) {
        return true;
    }
    
    try {
        config_ = config;
        
        // 初始化线程池
        callback_thread_pool_ = std::make_unique<ThreadPool>(config_.thread_pool_size);
        
        // 初始化事件队列
        event_queue_ = std::make_unique<MPMCLockFreeQueue<AsyncCallbackEvent>>();
        
        // 初始化统计信息
        statistics_.total_events.store(0);
        statistics_.processed_events.store(0);
        statistics_.dropped_events.store(0);
        statistics_.callback_errors.store(0);
        statistics_.average_processing_time.store(0.0);
        statistics_.last_event_time = std::chrono::high_resolution_clock::now();
        
        initialized_.store(true);
        return true;
        
    } catch (const std::exception& e) {
        initialized_.store(false);
        return false;
    }
}

bool AsyncCallbackManager::start()
{
    if (!initialized_.load() || running_.load()) {
        return false;
    }
    
    try {
        running_.store(true);
        
        // 启动事件处理线程
        processing_thread_ = std::thread(&AsyncCallbackManager::event_processing_worker, this);
        
        return true;
        
    } catch (const std::exception& e) {
        running_.store(false);
        return false;
    }
}

void AsyncCallbackManager::stop()
{
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 等待处理线程结束
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
}

void AsyncCallbackManager::cleanup()
{
    stop();
    
    // 清理回调
    {
        std::unique_lock<std::shared_mutex> lock(callbacks_mutex_);
        callbacks_.clear();
    }
    
    // 重置状态
    initialized_.store(false);
}

std::string AsyncCallbackManager::register_callback(CallbackEventType type, AsyncCallback callback) {
    std::string callback_id = generate_callback_id();
    
    {
        std::unique_lock<std::shared_mutex> lock(callbacks_mutex_);
        callbacks_.emplace_back(callback_id, type, std::move(callback));
    }
    
    return callback_id;
}

std::string AsyncCallbackManager::register_filtered_callback(CallbackFilter filter, AsyncCallback callback) {
    std::string callback_id = generate_callback_id();
    
    {
        std::unique_lock<std::shared_mutex> lock(callbacks_mutex_);
        callbacks_.emplace_back(callback_id, std::move(filter), std::move(callback));
    }
    
    return callback_id;
}

bool AsyncCallbackManager::unregister_callback(const std::string& callback_id) {
    std::unique_lock<std::shared_mutex> lock(callbacks_mutex_);
    
    auto it = std::find_if(callbacks_.begin(), callbacks_.end(),
        [&callback_id](const CallbackInfo& info) {
            return info.id == callback_id;
        });
    
    if (it != callbacks_.end()) {
        callbacks_.erase(it);
        return true;
    }
    
    return false;
}

void AsyncCallbackManager::clear_callbacks() {
    std::unique_lock<std::shared_mutex> lock(callbacks_mutex_);
    callbacks_.clear();
}

bool AsyncCallbackManager::publish_event(const AsyncCallbackEvent& event) {
    if (!running_.load()) {
        return false;
    }
    
    // 检查队列是否已满
    if (is_queue_full()) {
        statistics_.dropped_events.fetch_add(1);
        return false;
    }
    
    event_queue_->enqueue(event);
    statistics_.total_events.fetch_add(1);
    statistics_.last_event_time = std::chrono::high_resolution_clock::now();
    
    return true;
}

bool AsyncCallbackManager::publish_event(CallbackEventType type, const std::string& execution_id, const std::string& data) {
    AsyncCallbackEvent event(type, execution_id, data);
    return publish_event(event);
}

bool AsyncCallbackManager::publish_events(const std::vector<AsyncCallbackEvent>& events) {
    if (!running_.load()) {
        return false;
    }
    
    size_t published = 0;
    for (const auto& event : events) {
        if (publish_event(event)) {
            published++;
        }
    }
    
    return published == events.size();
}

void AsyncCallbackManager::set_config(const Config& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

AsyncCallbackManager::Config AsyncCallbackManager::get_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

AsyncCallbackManager::Statistics AsyncCallbackManager::get_statistics() const {
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    Statistics result;
    result.total_events.store(statistics_.total_events.load());
    result.processed_events.store(statistics_.processed_events.load());
    result.dropped_events.store(statistics_.dropped_events.load());
    result.callback_errors.store(statistics_.callback_errors.load());
    result.average_processing_time.store(statistics_.average_processing_time.load());
    result.last_event_time = statistics_.last_event_time;
    return result;
}

size_t AsyncCallbackManager::get_queue_size() const {
    return event_queue_ ? event_queue_->size() : 0;
}

bool AsyncCallbackManager::is_queue_full() const {
    return get_queue_size() >= config_.max_queue_size;
}

void AsyncCallbackManager::event_processing_worker() {
    while (running_.load()) {
        process_event_batch();
        
        //std::this_thread::sleep_for(std::chrono::milliseconds(config_.flush_interval_ms));
    }
    
    // 处理剩余事件
    process_event_batch();
}

void AsyncCallbackManager::process_event_batch() {
    std::vector<AsyncCallbackEvent> batch;
    batch.reserve(config_.batch_size);
    
    // 从队列中取出一批事件
    AsyncCallbackEvent event;
    while (batch.size() < config_.batch_size && event_queue_->dequeue(event)) {
        batch.push_back(std::move(event));
    }
    
    // 并发处理事件批次
    if (!batch.empty() && callback_thread_pool_) {
        std::vector<std::future<void>> futures;
        futures.reserve(batch.size());
        
        for (const auto& evt : batch) {
            auto future = callback_thread_pool_->enqueue([this, evt]() {
                process_single_event(evt);
            });
            futures.push_back(std::move(future));
        }
        
        // 等待所有事件处理完成
        for (auto& future : futures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                statistics_.callback_errors.fetch_add(1);
            }
        }
    }
}

void AsyncCallbackManager::process_single_event(const AsyncCallbackEvent& event) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        invoke_callbacks(event);
        statistics_.processed_events.fetch_add(1);
        
    } catch (const std::exception& e) {
        statistics_.callback_errors.fetch_add(1);
    }
    
    // 更新平均处理时间
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    // 移动平均
    double current_avg = statistics_.average_processing_time.load();
    double new_avg = (current_avg * 0.9) + (duration * 0.1);
    statistics_.average_processing_time.store(new_avg);
}

void AsyncCallbackManager::invoke_callbacks(const AsyncCallbackEvent& event) {
    std::shared_lock<std::shared_mutex> lock(callbacks_mutex_);
    
    for (const auto& callback_info : callbacks_) {
        try {
            bool should_invoke = false;
            
            if (callback_info.has_filter) {
                // 使用过滤器判断
                should_invoke = callback_info.filter(event);
            } else {
                // 使用事件类型判断
                should_invoke = (callback_info.type == event.type);
            }
            
            if (should_invoke) {
                callback_info.callback(event);
            }
            
        } catch (const std::exception& e) {
            statistics_.callback_errors.fetch_add(1);
        }
    }
}

std::string AsyncCallbackManager::generate_callback_id() const {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    return "cb_" + std::to_string(now) + "_" + std::to_string(counter.fetch_add(1));
}

} // namespace execution
} // namespace tes