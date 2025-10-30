#pragma once

#include "types.h"
#include <string>
#include <mutex>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>

namespace tes {
namespace execution {

/**
 * JSON订单反馈写入器
 * 用于在JSON信号传递模式下将订单反馈写入JSON文件
 */
class JsonFeedbackWriter {
public:
    struct Config {
        std::string output_directory;        // 输出目录路径
        std::string filename_prefix;         // 文件名前缀
        bool append_timestamp;               // 是否在文件名中添加时间戳
        bool single_file_mode;               // 单文件模式（所有反馈写入同一文件）
        size_t max_file_size_mb;            // 最大文件大小（MB）
        size_t max_entries_per_file;        // 每个文件最大条目数
        bool pretty_print;                   // 是否格式化输出JSON
        
        Config() 
            : output_directory("./result")
            , filename_prefix("order_feedback")
            , append_timestamp(true)
            , single_file_mode(false)
            , max_file_size_mb(10)
            , max_entries_per_file(1000)
            , pretty_print(true) {}
    };
    
    JsonFeedbackWriter();
    ~JsonFeedbackWriter();
    
    // 初始化
    bool initialize(const Config& config);
    
    // 写入订单反馈
    bool write_order_feedback(const Order& order);
    
    // 批量写入订单反馈
    bool write_order_feedbacks(const std::vector<Order>& orders);
    
    // 刷新缓冲区
    void flush();
    
    // 获取统计信息
    struct Statistics {
        size_t total_written;        // 总写入数量
        size_t files_created;        // 创建的文件数
        size_t write_errors;         // 写入错误数
        std::chrono::system_clock::time_point last_write_time;  // 最后写入时间
    };
    
    Statistics get_statistics() const;
    
    // 设置配置
    void set_config(const Config& config);
    Config get_config() const;
    
private:
    // 创建新文件
    bool create_new_file();
    
    // 生成文件名
    std::string generate_filename() const;
    
    // 检查是否需要轮转文件
    bool should_rotate_file() const;
    
    // 确保输出目录存在
    bool ensure_output_directory() const;
    
    // 将Order转换为JSON
    nlohmann::json order_to_json(const Order& order) const;
    
    // 获取当前文件大小
    size_t get_current_file_size() const;
    
    mutable std::mutex mutex_;
    Config config_;
    Statistics statistics_;
    
    std::string current_filename_;
    std::unique_ptr<std::ofstream> current_file_;
    size_t current_entries_count_;
    bool initialized_;
};

} // namespace execution
} // namespace tes