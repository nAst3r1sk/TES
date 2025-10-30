#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

namespace tes {
namespace execution {

class ThreadPool {
public:
    ThreadPool(size_t threads);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
    
    // 获取当前队列中的任务数量
    size_t get_queue_size() const;
    
    // 获取活跃线程数量
    size_t get_thread_count() const;
    
private:
    // 工作线程
    std::vector< std::thread > workers;
    // 任务队列
    std::queue< std::function<void()> > tasks;
    
    // 同步
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};
 
// 添加新任务到线程池
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // 不允许在停止的线程池中加入新任务
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

} // namespace execution
} // namespace tes