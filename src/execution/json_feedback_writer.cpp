#include "execution/json_feedback_writer.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace tes {
namespace execution {

JsonFeedbackWriter::JsonFeedbackWriter()
    : initialized_(false)
    , current_entries_count_(0) {
    statistics_.total_written = 0;
    statistics_.files_created = 0;
    statistics_.write_errors = 0;
    statistics_.last_write_time = std::chrono::system_clock::now();
}

JsonFeedbackWriter::~JsonFeedbackWriter() {
    if (current_file_ && current_file_->is_open()) {
        flush();
        current_file_->close();
    }
}

bool JsonFeedbackWriter::initialize(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_ = config;
    
    // 确保输出目录存在
    if (!ensure_output_directory()) {
        std::cerr << "Failed to create output directory: " << config_.output_directory << std::endl;
        return false;
    }
    
    // 如果是单文件模式，立即创建文件
    if (config_.single_file_mode) {
        if (!create_new_file()) {
            std::cerr << "Failed to create initial file" << std::endl;
            return false;
        }
    }
    
    initialized_ = true;
    return true;
}

bool JsonFeedbackWriter::write_order_feedback(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "JsonFeedbackWriter not initialized" << std::endl;
        statistics_.write_errors++;
        return false;
    }
    
    try {
        // 检查是否需要创建新文件或轮转文件
        if (!current_file_ || !current_file_->is_open() || should_rotate_file()) {
            if (!create_new_file()) {
                std::cerr << "Failed to create new file for order feedback" << std::endl;
                statistics_.write_errors++;
                return false;
            }
        }
        
        // 转换订单为JSON
        nlohmann::json order_json = order_to_json(order);
        
        // 写入JSON
        if (config_.pretty_print) {
            *current_file_ << order_json.dump(2) << std::endl;
        } else {
            *current_file_ << order_json.dump() << std::endl;
        }
        
        current_file_->flush();
        current_entries_count_++;
        statistics_.total_written++;
        statistics_.last_write_time = std::chrono::system_clock::now();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error writing order feedback: " << e.what() << std::endl;
        statistics_.write_errors++;
        return false;
    }
}

bool JsonFeedbackWriter::write_order_feedbacks(const std::vector<Order>& orders) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "JsonFeedbackWriter not initialized" << std::endl;
        statistics_.write_errors++;
        return false;
    }
    
    try {
        // 检查是否需要创建新文件
        if (!current_file_ || !current_file_->is_open()) {
            if (!create_new_file()) {
                std::cerr << "Failed to create new file for batch order feedback" << std::endl;
                statistics_.write_errors++;
                return false;
            }
        }
        
        nlohmann::json batch_json = nlohmann::json::array();
        
        for (const auto& order : orders) {
            batch_json.push_back(order_to_json(order));
        }
        
        // 写入批量JSON
        if (config_.pretty_print) {
            *current_file_ << batch_json.dump(2) << std::endl;
        } else {
            *current_file_ << batch_json.dump() << std::endl;
        }
        
        current_file_->flush();
        current_entries_count_ += orders.size();
        statistics_.total_written += orders.size();
        statistics_.last_write_time = std::chrono::system_clock::now();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error writing batch order feedback: " << e.what() << std::endl;
        statistics_.write_errors++;
        return false;
    }
}

void JsonFeedbackWriter::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (current_file_ && current_file_->is_open()) {
        current_file_->flush();
    }
}

JsonFeedbackWriter::Statistics JsonFeedbackWriter::get_statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

void JsonFeedbackWriter::set_config(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

JsonFeedbackWriter::Config JsonFeedbackWriter::get_config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool JsonFeedbackWriter::create_new_file() {
    // 关闭当前文件
    if (current_file_ && current_file_->is_open()) {
        current_file_->close();
    }
    
    // 生成新文件名
    current_filename_ = generate_filename();
    std::string full_path = config_.output_directory + "/" + current_filename_;
    
    // 创建新文件
    current_file_ = std::make_unique<std::ofstream>(full_path, std::ios::out | std::ios::app);
    
    if (!current_file_->is_open()) {
        std::cerr << "Failed to open file: " << full_path << std::endl;
        return false;
    }
    
    current_entries_count_ = 0;
    statistics_.files_created++;
    
    std::cout << "Created new order feedback file: " << full_path << std::endl;
    return true;
}

std::string JsonFeedbackWriter::generate_filename() const {
    std::stringstream ss;
    ss << config_.filename_prefix;
    
    if (config_.append_timestamp) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        ss << "_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
           << "_" << std::setfill('0') << std::setw(3) << ms.count();
    }
    
    if (!config_.single_file_mode) {
        ss << "_" << statistics_.files_created;
    }
    
    ss << ".json";
    return ss.str();
}

bool JsonFeedbackWriter::should_rotate_file() const {
    if (config_.single_file_mode) {
        return false;
    }
    
    // 检查条目数限制
    if (current_entries_count_ >= config_.max_entries_per_file) {
        return true;
    }
    
    // 检查文件大小限制
    size_t current_size = get_current_file_size();
    if (current_size >= config_.max_file_size_mb * 1024 * 1024) {
        return true;
    }
    
    return false;
}

bool JsonFeedbackWriter::ensure_output_directory() const {
    try {
        std::filesystem::path dir_path(config_.output_directory);
        if (!std::filesystem::exists(dir_path)) {
            return std::filesystem::create_directories(dir_path);
        }
        return std::filesystem::is_directory(dir_path);
    } catch (const std::exception& e) {
        std::cerr << "Error ensuring output directory: " << e.what() << std::endl;
        return false;
    }
}

nlohmann::json JsonFeedbackWriter::order_to_json(const Order& order) const {
    nlohmann::json j;
    
    j["order_id"] = order.order_id;
    j["client_order_id"] = order.client_order_id;
    j["instrument_id"] = order.instrument_id;
    j["strategy_id"] = order.strategy_id;
    
    // 订单方向
    j["side"] = (order.side == OrderSide::BUY) ? "BUY" : "SELL";
    
    // 订单类型
    switch (order.type) {
        case OrderType::MARKET:
            j["type"] = "MARKET";
            break;
        case OrderType::LIMIT:
            j["type"] = "LIMIT";
            break;
        case OrderType::STOP:
            j["type"] = "STOP";
            break;
        case OrderType::STOP_LIMIT:
            j["type"] = "STOP_LIMIT";
            break;
        default:
            j["type"] = "UNKNOWN";
            break;
    }
    
    // 订单状态
    switch (order.status) {
        case OrderStatus::PENDING:
            j["status"] = "PENDING";
            break;
        case OrderStatus::SUBMITTED:
            j["status"] = "SUBMITTED";
            break;
        case OrderStatus::PARTIALLY_FILLED:
            j["status"] = "PARTIALLY_FILLED";
            break;
        case OrderStatus::FILLED:
            j["status"] = "FILLED";
            break;
        case OrderStatus::CANCELLED:
            j["status"] = "CANCELLED";
            break;
        case OrderStatus::REJECTED:
            j["status"] = "REJECTED";
            break;
        case OrderStatus::ERROR:
            j["status"] = "ERROR";
            break;
        default:
            j["status"] = "UNKNOWN";
            break;
    }
    
    // 时间有效性
    switch (order.time_in_force) {
        case TimeInForce::DAY:
            j["time_in_force"] = "DAY";
            break;
        case TimeInForce::GTC:
            j["time_in_force"] = "GTC";
            break;
        case TimeInForce::IOC:
            j["time_in_force"] = "IOC";
            break;
        case TimeInForce::FOK:
            j["time_in_force"] = "FOK";
            break;
        default:
            j["time_in_force"] = "UNKNOWN";
            break;
    }
    
    // 数量和价格信息
    j["quantity"] = order.quantity;
    j["price"] = order.price;
    j["filled_quantity"] = order.filled_quantity;
    j["average_price"] = order.average_price;
    
    // 时间戳
    auto create_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        order.create_time.time_since_epoch()).count();
    auto update_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        order.update_time.time_since_epoch()).count();
    auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        order.timestamp.time_since_epoch()).count();
    
    j["create_time"] = create_time_ms;
    j["update_time"] = update_time_ms;
    j["timestamp"] = timestamp_ms;
    
    // 错误信息
    j["error_message"] = order.error_message;
    
    // 添加写入时间戳
    auto now = std::chrono::system_clock::now();
    auto write_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    j["feedback_write_time"] = write_time_ms;
    
    return j;
}

size_t JsonFeedbackWriter::get_current_file_size() const {
    if (!current_file_ || !current_file_->is_open()) {
        return 0;
    }
    
    try {
        std::string full_path = config_.output_directory + "/" + current_filename_;
        if (std::filesystem::exists(full_path)) {
            return std::filesystem::file_size(full_path);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error getting file size: " << e.what() << std::endl;
    }
    
    return 0;
}

} // namespace execution
} // namespace tes