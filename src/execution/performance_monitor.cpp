#include "performance_monitor.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/resource.h>
#include <shared_mutex>

namespace tes {
namespace execution {

PerformanceMonitor::PerformanceMonitor()
    : running_(false)
    , initialized_(false)
    , current_cpu_usage_(0.0)
    , current_memory_usage_(0.0) {
}

PerformanceMonitor::~PerformanceMonitor() {
    stop();
    cleanup();
}

bool PerformanceMonitor::initialize(const Config& config) {
    if (initialized_.load()) {
        return true;
    }
    
    try {
        config_ = config;
        
        // 初始化文件输出
        if (config_.enable_file_output) {
            output_file_ = std::make_unique<std::ofstream>(config_.output_file_path, std::ios::app);
            if (!output_file_->is_open()) {
                return false;
            }
        }
        
        initialized_.store(true);
        return true;
        
    } catch (const std::exception& e) {
        initialized_.store(false);
        return false;
    }
}

bool PerformanceMonitor::start() {
    if (!initialized_.load() || running_.load()) {
        return false;
    }
    
    try {
        running_.store(true);
        
        // 启动监控线程
        monitoring_thread_ = std::thread(&PerformanceMonitor::monitoring_worker, this);
        
        return true;
        
    } catch (const std::exception& e) {
        running_.store(false);
        return false;
    }
}

void PerformanceMonitor::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 等待监控线程结束
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

void PerformanceMonitor::cleanup() {
    stop();
    
    // 清理指标数据
    {
        std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
        metrics_.clear();
    }
    
    // 关闭文件
    if (output_file_ && output_file_->is_open()) {
        output_file_->close();
    }
    
    initialized_.store(false);
}

void PerformanceMonitor::record_latency(const std::string& operation, double latency_us) {
    std::shared_lock<std::shared_mutex> read_lock(metrics_mutex_);
    
    auto it = metrics_.find("latency_" + operation);
    if (it == metrics_.end()) {
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(metrics_mutex_);
        metrics_["latency_" + operation] = std::make_unique<MetricCollection>();
        it = metrics_.find("latency_" + operation);
    }
    
    if (it != metrics_.end()) {
        std::lock_guard<std::mutex> lock(it->second->mutex);
        it->second->data_points.emplace_back(latency_us, operation);
        it->second->stats_dirty = true;
        
        // 清理旧数据
        cleanup_old_data(*it->second);
    }
}

void PerformanceMonitor::record_throughput(const std::string& operation, uint64_t count, uint64_t duration_ms) {
    if (duration_ms == 0) return;
    
    double throughput = static_cast<double>(count) / (static_cast<double>(duration_ms) / 1000.0);
    
    std::shared_lock<std::shared_mutex> read_lock(metrics_mutex_);
    
    auto it = metrics_.find("throughput_" + operation);
    if (it == metrics_.end()) {
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(metrics_mutex_);
        metrics_["throughput_" + operation] = std::make_unique<MetricCollection>();
        it = metrics_.find("throughput_" + operation);
    }
    
    if (it != metrics_.end()) {
        std::lock_guard<std::mutex> lock(it->second->mutex);
        it->second->data_points.emplace_back(throughput, operation);
        it->second->stats_dirty = true;
        
        cleanup_old_data(*it->second);
    }
}

void PerformanceMonitor::record_queue_size(const std::string& queue_name, size_t size) {
    record_custom_metric("queue_size_" + queue_name, static_cast<double>(size), queue_name);
}

void PerformanceMonitor::record_error(const std::string& operation) {
    std::lock_guard<std::mutex> lock(counters_mutex_);
    error_counts_[operation].fetch_add(1);
}

void PerformanceMonitor::record_success(const std::string& operation) {
    std::lock_guard<std::mutex> lock(counters_mutex_);
    success_counts_[operation].fetch_add(1);
}

void PerformanceMonitor::record_custom_metric(const std::string& name, double value, const std::string& label) {
    std::shared_lock<std::shared_mutex> read_lock(metrics_mutex_);
    
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(metrics_mutex_);
        metrics_[name] = std::make_unique<MetricCollection>();
        it = metrics_.find(name);
    }
    
    if (it != metrics_.end()) {
        std::lock_guard<std::mutex> lock(it->second->mutex);
        it->second->data_points.emplace_back(value, label);
        it->second->stats_dirty = true;
        
        cleanup_old_data(*it->second);
    }
}

PerformanceStats PerformanceMonitor::get_latency_stats(const std::string& operation) const {
    return get_custom_stats("latency_" + operation);
}

PerformanceStats PerformanceMonitor::get_throughput_stats(const std::string& operation) const {
    return get_custom_stats("throughput_" + operation);
}

PerformanceStats PerformanceMonitor::get_custom_stats(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(metrics_mutex_);
    
    auto it = metrics_.find(name);
    if (it != metrics_.end()) {
        std::lock_guard<std::mutex> metric_lock(it->second->mutex);
        
        if (it->second->stats_dirty) {
            const_cast<PerformanceMonitor*>(this)->update_stats(*it->second);
        }
        
        return it->second->cached_stats;
    }
    
    return PerformanceStats();
}

double PerformanceMonitor::get_cpu_usage() const {
    return current_cpu_usage_.load();
}

double PerformanceMonitor::get_memory_usage() const {
    return current_memory_usage_.load();
}

std::string PerformanceMonitor::generate_report() const {
    std::ostringstream report;
    
    report << "=== Performance Monitor Report ===\n";
    report << "Timestamp: " << format_timestamp(std::chrono::high_resolution_clock::now()) << "\n\n";
    
    // 系统资源
    report << "System Resources:\n";
    report << "  CPU Usage: " << std::fixed << std::setprecision(2) << get_cpu_usage() << "%\n";
    report << "  Memory Usage: " << std::fixed << std::setprecision(2) << get_memory_usage() << "%\n\n";
    
    // 指标统计
    std::shared_lock<std::shared_mutex> lock(metrics_mutex_);
    
    for (const auto& [name, collection] : metrics_) {
        std::lock_guard<std::mutex> metric_lock(collection->mutex);
        
        if (collection->stats_dirty) {
            const_cast<PerformanceMonitor*>(this)->update_stats(*collection);
        }
        
        const auto& stats = collection->cached_stats;
        
        report << "Metric: " << name << "\n";
        report << "  Sample Count: " << stats.sample_count << "\n";
        report << "  Min: " << std::fixed << std::setprecision(3) << stats.min_value << "\n";
        report << "  Max: " << std::fixed << std::setprecision(3) << stats.max_value << "\n";
        report << "  Avg: " << std::fixed << std::setprecision(3) << stats.avg_value << "\n";
        report << "  P50: " << std::fixed << std::setprecision(3) << stats.p50_value << "\n";
        report << "  P95: " << std::fixed << std::setprecision(3) << stats.p95_value << "\n";
        report << "  P99: " << std::fixed << std::setprecision(3) << stats.p99_value << "\n\n";
    }
    
    // 错误和成功率
    std::lock_guard<std::mutex> counters_lock(counters_mutex_);
    
    report << "Error/Success Rates:\n";
    for (const auto& [operation, error_count] : error_counts_) {
        uint64_t errors = error_count.load();
        uint64_t successes = 0;
        
        auto success_it = success_counts_.find(operation);
        if (success_it != success_counts_.end()) {
            successes = success_it->second.load();
        }
        
        uint64_t total = errors + successes;
        double error_rate = total > 0 ? (static_cast<double>(errors) / total) * 100.0 : 0.0;
        double success_rate = total > 0 ? (static_cast<double>(successes) / total) * 100.0 : 0.0;
        
        report << "  " << operation << ":\n";
        report << "    Total: " << total << ", Errors: " << errors << ", Successes: " << successes << "\n";
        report << "    Error Rate: " << std::fixed << std::setprecision(2) << error_rate << "%\n";
        report << "    Success Rate: " << std::fixed << std::setprecision(2) << success_rate << "%\n\n";
    }
    
    return report.str();
}

bool PerformanceMonitor::export_to_file(const std::string& file_path) const {
    std::string path = file_path.empty() ? config_.output_file_path : file_path;
    
    try {
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        file << generate_report();
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

void PerformanceMonitor::set_config(const Config& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

PerformanceMonitor::Config PerformanceMonitor::get_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

void PerformanceMonitor::clear_metrics() {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    metrics_.clear();
}

void PerformanceMonitor::clear_metric(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    metrics_.erase(name);
}

void PerformanceMonitor::monitoring_worker() {
    while (running_.load()) {
        collect_system_metrics();
        
        // 定期输出报告到文件
        if (config_.enable_file_output && output_file_ && output_file_->is_open()) {
            std::lock_guard<std::mutex> file_lock(file_mutex_);
            *output_file_ << generate_report() << std::flush;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.collection_interval_ms));
    }
}

void PerformanceMonitor::collect_system_metrics() {
    if (config_.enable_cpu_monitoring) {
        current_cpu_usage_.store(read_cpu_usage());
    }
    
    if (config_.enable_memory_monitoring) {
        current_memory_usage_.store(read_memory_usage());
    }
}

void PerformanceMonitor::update_stats(MetricCollection& collection) {
    collection.cached_stats = calculate_stats(collection.data_points);
    collection.stats_dirty = false;
}

PerformanceStats PerformanceMonitor::calculate_stats(const std::vector<MetricDataPoint>& data_points) const {
    PerformanceStats stats;
    
    if (data_points.empty()) {
        return stats;
    }
    
    std::vector<double> values;
    values.reserve(data_points.size());
    
    double sum = 0.0;
    for (const auto& point : data_points) {
        values.push_back(point.value);
        sum += point.value;
    }
    
    std::sort(values.begin(), values.end());
    
    stats.sample_count = values.size();
    stats.min_value = values.front();
    stats.max_value = values.back();
    stats.avg_value = sum / values.size();
    
    // 计算百分位数
    size_t p50_idx = static_cast<size_t>(values.size() * 0.5);
    size_t p95_idx = static_cast<size_t>(values.size() * 0.95);
    size_t p99_idx = static_cast<size_t>(values.size() * 0.99);
    
    stats.p50_value = values[std::min(p50_idx, values.size() - 1)];
    stats.p95_value = values[std::min(p95_idx, values.size() - 1)];
    stats.p99_value = values[std::min(p99_idx, values.size() - 1)];
    
    stats.last_update = std::chrono::high_resolution_clock::now();
    
    return stats;
}

void PerformanceMonitor::cleanup_old_data(MetricCollection& collection) {
    if (collection.data_points.size() > config_.max_data_points) {
        size_t remove_count = collection.data_points.size() - config_.max_data_points;
        collection.data_points.erase(collection.data_points.begin(), 
                                   collection.data_points.begin() + remove_count);
        collection.stats_dirty = true;
    }
}

std::string PerformanceMonitor::format_timestamp(const std::chrono::high_resolution_clock::time_point& tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + 
        std::chrono::duration_cast<std::chrono::system_clock::duration>(tp - std::chrono::high_resolution_clock::now()));
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

double PerformanceMonitor::read_cpu_usage() const {
    // 简化的CPU使用率读取实现
    static auto last_time = std::chrono::high_resolution_clock::now();
    static struct rusage last_usage;
    static bool first_call = true;
    
    struct rusage current_usage;
    auto current_time = std::chrono::high_resolution_clock::now();
    
    if (getrusage(RUSAGE_SELF, &current_usage) != 0) {
        return 0.0;
    }
    
    if (first_call) {
        last_usage = current_usage;
        last_time = current_time;
        first_call = false;
        return 0.0;
    }
    
    auto time_diff = std::chrono::duration_cast<std::chrono::microseconds>(current_time - last_time).count();
    if (time_diff == 0) {
        return 0.0;
    }
    
    auto user_time_diff = (current_usage.ru_utime.tv_sec - last_usage.ru_utime.tv_sec) * 1000000 +
                         (current_usage.ru_utime.tv_usec - last_usage.ru_utime.tv_usec);
    auto sys_time_diff = (current_usage.ru_stime.tv_sec - last_usage.ru_stime.tv_sec) * 1000000 +
                        (current_usage.ru_stime.tv_usec - last_usage.ru_stime.tv_usec);
    
    double cpu_usage = static_cast<double>(user_time_diff + sys_time_diff) / time_diff * 100.0;
    
    last_usage = current_usage;
    last_time = current_time;
    
    return std::min(cpu_usage, 100.0);
}

double PerformanceMonitor::read_memory_usage() const {
    // 简化的内存使用率读取实现
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
    
    // 返回RSS内存使用量（KB）
    return static_cast<double>(usage.ru_maxrss) / 1024.0; // 转换为MB
}

} // namespace execution
} // namespace tes