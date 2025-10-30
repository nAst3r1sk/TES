#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>

namespace tes {
namespace execution {

// 性能指标类型
enum class MetricType {
    LATENCY,           // 延迟
    THROUGHPUT,        // 吞吐量
    CPU_USAGE,         // CPU使用率
    MEMORY_USAGE,      // 内存使用率
    QUEUE_SIZE,        // 队列大小
    ERROR_RATE,        // 错误率
    SUCCESS_RATE       // 成功率
};

// 性能指标数据点
struct MetricDataPoint {
    std::chrono::high_resolution_clock::time_point timestamp;
    double value;
    std::string label;
    
    MetricDataPoint() : value(0.0) {
        timestamp = std::chrono::high_resolution_clock::now();
    }
    
    MetricDataPoint(double v, const std::string& l = "")
        : value(v), label(l) {
        timestamp = std::chrono::high_resolution_clock::now();
    }
};

// 性能统计信息
struct PerformanceStats {
    double min_value;
    double max_value;
    double avg_value;
    double p50_value;  // 50th percentile
    double p95_value;  // 95th percentile
    double p99_value;  // 99th percentile
    uint64_t sample_count;
    std::chrono::high_resolution_clock::time_point last_update;
    
    PerformanceStats() : min_value(0.0), max_value(0.0), avg_value(0.0),
                        p50_value(0.0), p95_value(0.0), p99_value(0.0),
                        sample_count(0) {
        last_update = std::chrono::high_resolution_clock::now();
    }
};

// 性能监控器
class PerformanceMonitor {
public:
    struct Config {
        uint32_t collection_interval_ms;   // 数据收集间隔（毫秒）
        uint32_t report_interval_ms;       // 报告间隔（毫秒）
        size_t max_data_points;            // 最大数据点数量
        bool enable_cpu_monitoring;        // 启用CPU监控
        bool enable_memory_monitoring;     // 启用内存监控
        bool enable_file_output;           // 启用文件输出
        std::string output_file_path;      // 输出文件路径
        
        Config() : collection_interval_ms(100), report_interval_ms(5000),
                  max_data_points(1000), enable_cpu_monitoring(true),
                  enable_memory_monitoring(true), enable_file_output(false),
                  output_file_path("performance_metrics.log") {}
    };
    
    PerformanceMonitor();
    ~PerformanceMonitor();
    
    // 生命周期管理
    bool initialize(const Config& config = Config());
    bool start();
    void stop();
    void cleanup();
    bool is_running() const { return running_.load(); }
    
    // 指标记录
    void record_latency(const std::string& operation, double latency_us);
    void record_throughput(const std::string& operation, uint64_t count, uint64_t duration_ms);
    void record_queue_size(const std::string& queue_name, size_t size);
    void record_error(const std::string& operation);
    void record_success(const std::string& operation);
    void record_custom_metric(const std::string& name, double value, const std::string& label = "");
    
    // 统计信息获取
    PerformanceStats get_latency_stats(const std::string& operation) const;
    PerformanceStats get_throughput_stats(const std::string& operation) const;
    PerformanceStats get_custom_stats(const std::string& name) const;
    
    // 系统资源监控
    double get_cpu_usage() const;
    double get_memory_usage() const;
    
    // 报告生成
    std::string generate_report() const;
    bool export_to_file(const std::string& file_path = "") const;
    
    // 配置管理
    void set_config(const Config& config);
    Config get_config() const;
    
    // 数据清理
    void clear_metrics();
    void clear_metric(const std::string& name);
    
private:
    struct MetricCollection {
        std::vector<MetricDataPoint> data_points;
        mutable std::mutex mutex;
        PerformanceStats cached_stats;
        bool stats_dirty;
        
        MetricCollection() : stats_dirty(true) {}
    };
    
    // 内部方法
    void monitoring_worker();
    void collect_system_metrics();
    void update_stats(MetricCollection& collection);
    PerformanceStats calculate_stats(const std::vector<MetricDataPoint>& data_points) const;
    void cleanup_old_data(MetricCollection& collection);
    std::string format_timestamp(const std::chrono::high_resolution_clock::time_point& tp) const;
    
    // CPU和内存监控
    double read_cpu_usage() const;
    double read_memory_usage() const;
    
    // 成员变量
    Config config_;
    mutable std::mutex config_mutex_;
    
    std::unordered_map<std::string, std::unique_ptr<MetricCollection>> metrics_;
    mutable std::shared_mutex metrics_mutex_;
    
    // 系统资源监控
    std::atomic<double> current_cpu_usage_;
    std::atomic<double> current_memory_usage_;
    
    // 错误和成功计数
    std::unordered_map<std::string, std::atomic<uint64_t>> error_counts_;
    std::unordered_map<std::string, std::atomic<uint64_t>> success_counts_;
    mutable std::mutex counters_mutex_;
    
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    
    std::thread monitoring_thread_;
    
    // 文件输出
    mutable std::mutex file_mutex_;
    std::unique_ptr<std::ofstream> output_file_;
    
    // 禁止拷贝和赋值
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;
};

// 性能监控辅助类 - RAII延迟测量
class LatencyMeasurer {
public:
    LatencyMeasurer(PerformanceMonitor* monitor, const std::string& operation)
        : monitor_(monitor), operation_(operation) {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    ~LatencyMeasurer() {
        if (monitor_) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_).count();
            monitor_->record_latency(operation_, static_cast<double>(duration));
        }
    }
    
private:
    PerformanceMonitor* monitor_;
    std::string operation_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// 便利宏定义
#define MEASURE_LATENCY(monitor, operation) \
    LatencyMeasurer _latency_measurer(monitor, operation)

} // namespace execution
} // namespace tes