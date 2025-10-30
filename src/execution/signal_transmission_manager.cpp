#include "execution/signal_transmission_manager.h"
#include "execution/gateway_adapter.h"
#include "execution/types.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace tes {
namespace execution {

// 全局订单ID计数器和随机数生成器
static std::atomic<uint64_t> order_id_counter{0};
static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_int_distribution<uint32_t> dis(1000, 9999);

// 防重复订单提交机制
struct PendingOrderInfo {
    std::string symbol;
    double quantity;
    std::chrono::system_clock::time_point submit_time;
};
static std::mutex pending_orders_mutex;
static std::unordered_map<std::string, PendingOrderInfo> pending_orders; // client_order_id -> info

SignalTransmissionManager::SignalTransmissionManager()
    : initialized_(false)
    , running_(false)
{
    last_json_update_time_ = std::chrono::high_resolution_clock::now();
}

SignalTransmissionManager::~SignalTransmissionManager()
{
    stop();
    cleanup();
}

bool SignalTransmissionManager::initialize(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (initialized_.load()) {
        return true;
    }
    
    config_ = config;
    
    // 验证配置
    if (config_.mode == SignalTransmissionMode::JSON_FILE) {
        if (config_.json_file_path.empty()) {
            set_error("JSON file path is empty");
            return false;
        }
        
        // 检查JSON文件是否存在
        std::ifstream file(config_.json_file_path);
        if (!file.good()) {
            set_error("JSON file does not exist: " + config_.json_file_path);
            return false;
        }
    }
    
    initialized_.store(true);
    return true;
}

bool SignalTransmissionManager::start()
{
    if (!initialized_.load()) {
        set_error("SignalTransmissionManager not initialized");
        return false;
    }
    
    if (running_.load()) {
        return true;
    }
    
    running_.store(true);
    
    // 根据模式启动相应的工作线程
    if (config_.mode == SignalTransmissionMode::JSON_FILE) {
        if (config_.enable_auto_sync) {
            json_monitoring_thread_ = std::make_unique<std::thread>(&SignalTransmissionManager::json_monitoring_worker, this);
            position_sync_thread_ = std::make_unique<std::thread>(&SignalTransmissionManager::position_sync_worker, this);
        }
    }
    
    return true;
}

void SignalTransmissionManager::stop()
{
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 等待工作线程结束
    if (json_monitoring_thread_ && json_monitoring_thread_->joinable()) {
        json_monitoring_thread_->join();
    }
    
    if (position_sync_thread_ && position_sync_thread_->joinable()) {
        position_sync_thread_->join();
    }
}

void SignalTransmissionManager::cleanup()
{
    stop();
    
    position_manager_.reset();
    shared_memory_interface_.reset();
    
    initialized_.store(false);
}

bool SignalTransmissionManager::set_transmission_mode(SignalTransmissionMode mode)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (running_.load()) {
        set_error("Cannot change transmission mode while running");
        return false;
    }
    
    config_.mode = mode;
    return true;
}

SignalTransmissionMode SignalTransmissionManager::get_transmission_mode() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.mode;
}

bool SignalTransmissionManager::receive_signals(std::vector<shared_memory::TradingSignal>& signals, uint32_t max_count)
{
    signals.clear();
    
    if (config_.mode == SignalTransmissionMode::SHARED_MEMORY) {
        if (!shared_memory_interface_) {
            set_error("Shared memory interface not set");
            return false;
        }
        
        size_t count = shared_memory_interface_->receive_signals_batch(signals, max_count);
        return count > 0;
    }
    else if (config_.mode == SignalTransmissionMode::JSON_FILE) {
        // JSON文件模式下，信号通过仓位同步触发
        // 这里可以根据需要实现信号生成逻辑
        return true;
    }
    
    return false;
}

void SignalTransmissionManager::set_signal_callback(SignalCallback callback)
{
    signal_callback_ = callback;
}

bool SignalTransmissionManager::load_json_position_data(JsonPositionUpdate& data)
{
    std::lock_guard<std::mutex> lock(json_mutex_);
    
    try {
        std::ifstream file(config_.json_file_path);
        if (!file.is_open()) {
            set_error("Cannot open JSON file: " + config_.json_file_path);
            return false;
        }
        
        nlohmann::json json_data;
        file >> json_data;
        
        // 解析JSON数据
        if (json_data.is_array() && json_data.size() >= 4) {
            // 解析has_position
            if (json_data[0].contains("has_position")) {
                for (const auto& pos : json_data[0]["has_position"]) {
                    JsonPositionData position;
                    position.id = pos["id"];
                    position.symbol = pos["symbol"];
                    position.quantity = pos["quantity"];
                    data.has_position.push_back(position);
                }
            }
            
            // 解析no_position
            if (json_data[1].contains("no_position")) {
                for (const auto& pos : json_data[1]["no_position"]) {
                    JsonPositionData position;
                    position.id = pos["id"];
                    position.symbol = pos["symbol"];
                    position.quantity = pos["quantity"];
                    data.no_position.push_back(position);
                }
            }
            
            // 解析booksize等字段
            if (json_data.size() > 2) {
                data.booksize = json_data[2].value("booksize", 0.0);
                data.targetvalue = json_data[2].value("targetvalue", 0.0);
                data.longtarget = json_data[2].value("longtarget", 0.0);
                data.shorttarget = json_data[2].value("shorttarget", 0.0);
            }
            
            // 解析状态字段
            if (json_data.size() > 3) {
                data.isFinished = json_data[3].value("isFinished", 0);
                data.errorstring = json_data[3].value("errorstring", "");
                data.update_timestamp = json_data[3].value("update_timestamp", 0.0);
            }
        }
        
        return true;
    }
    catch (const std::exception& e) {
        set_error("Failed to parse JSON file: " + std::string(e.what()));
        return false;
    }
}

bool SignalTransmissionManager::save_json_position_data(const JsonPositionUpdate& data)
{
    std::lock_guard<std::mutex> lock(json_mutex_);
    
    try {
        nlohmann::json json_data = nlohmann::json::array();
        
        // 构建has_position
        nlohmann::json has_position_obj;
        nlohmann::json has_position_array = nlohmann::json::array();
        for (const auto& pos : data.has_position) {
            nlohmann::json pos_obj;
            pos_obj["id"] = pos.id;
            pos_obj["symbol"] = pos.symbol;
            pos_obj["quantity"] = pos.quantity;
            has_position_array.push_back(pos_obj);
        }
        has_position_obj["has_position"] = has_position_array;
        json_data.push_back(has_position_obj);
        
        // 构建no_position
        nlohmann::json no_position_obj;
        nlohmann::json no_position_array = nlohmann::json::array();
        for (const auto& pos : data.no_position) {
            nlohmann::json pos_obj;
            pos_obj["id"] = pos.id;
            pos_obj["symbol"] = pos.symbol;
            pos_obj["quantity"] = pos.quantity;
            no_position_array.push_back(pos_obj);
        }
        no_position_obj["no_position"] = no_position_array;
        json_data.push_back(no_position_obj);
        
        // 构建booksize等字段
        nlohmann::json booksize_obj;
        booksize_obj["booksize"] = data.booksize;
        booksize_obj["targetvalue"] = data.targetvalue;
        booksize_obj["longtarget"] = data.longtarget;
        booksize_obj["shorttarget"] = data.shorttarget;
        json_data.push_back(booksize_obj);
        
        // 构建状态字段
        nlohmann::json status_obj;
        status_obj["isFinished"] = data.isFinished;
        status_obj["errorstring"] = data.errorstring;
        status_obj["update_timestamp"] = data.update_timestamp;
        json_data.push_back(status_obj);
        
        // 写入文件
        std::ofstream file(config_.json_file_path);
        if (!file.is_open()) {
            set_error("Cannot write to JSON file: " + config_.json_file_path);
            return false;
        }
        
        file << json_data.dump(2);
        return true;
    }
    catch (const std::exception& e) {
        set_error("Failed to save JSON file: " + std::string(e.what()));
        return false;
    }
}

PositionSyncResult SignalTransmissionManager::sync_positions_with_json()
{
    std::cout << "[DEBUG] ========== Starting position sync with JSON ==========" << std::endl;
    std::cout << "[DEBUG] Config - max_position_diff: " << config_.max_position_diff << std::endl;
    std::cout << "[DEBUG] Config - precision_tolerance: " << config_.precision_tolerance << std::endl;
    
    PositionSyncResult result;
    result.success = false;
    result.positions_aligned = 0;
    result.positions_opened = 0;
    result.positions_closed = 0;
    result.booksize_aligned = false;
    result.targets_aligned = false;
    
    if (!position_manager_) {
        result.error_message = "Position manager not set";
        std::cout << "[DEBUG] ERROR: Position manager not set" << std::endl;
        return result;
    }
    
    JsonPositionUpdate json_data;
    if (!load_json_position_data(json_data)) {
        result.error_message = get_last_error();
        return result;
    }
    
    // 比较和对齐仓位
    if (!compare_positions_with_json(json_data, result)) {
        return result;
    }
    
    // 验证booksize和targets
    if (!validate_booksize_and_targets(json_data, result)) {
        return result;
    }
    
    // 更新JSON状态
    int isFinished = (result.booksize_aligned && result.targets_aligned) ? 1 : 2;
    std::string errorstring = result.success ? "" : result.error_message;
    update_json_status(isFinished, errorstring);
    
    // 只在出现错误时更新状态
    if (!result.success) {
        update_json_status(2, result.error_message);
    }
    
    result.success = true;
    return result;
}

void SignalTransmissionManager::set_position_manager(std::shared_ptr<PositionManager> position_manager)
{
    position_manager_ = position_manager;
}

void SignalTransmissionManager::set_shared_memory_interface(std::shared_ptr<SharedMemoryInterface> shared_memory_interface)
{
    shared_memory_interface_ = shared_memory_interface;
}

void SignalTransmissionManager::set_trading_interface(std::shared_ptr<GatewayAdapter> trading_interface)
{
    trading_interface_ = trading_interface;
}

void SignalTransmissionManager::set_position_sync_callback(PositionSyncCallback callback)
{
    position_sync_callback_ = callback;
}

void SignalTransmissionManager::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

SignalTransmissionManager::Config SignalTransmissionManager::get_config() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

bool SignalTransmissionManager::is_running() const
{
    return running_.load();
}

std::string SignalTransmissionManager::get_last_error() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void SignalTransmissionManager::json_monitoring_worker()
{
    while (running_.load()) {
        try {
            // 检查JSON文件更新时间
            JsonPositionUpdate current_data;
            if (load_json_position_data(current_data)) {
                if (current_data.update_timestamp > last_json_data_.update_timestamp) {
                    last_json_data_ = current_data;
                    last_json_update_time_ = std::chrono::high_resolution_clock::now();
                    
                    // 检查isFinished字段，如果为1则跳过仓位同步
                    if (current_data.isFinished == 1) {
                        std::cout << "[SignalTransmissionManager] isFinished=1, 跳过仓位同步" << std::endl;
                        continue;
                    }
                    
                    // 触发仓位同步
                    if (position_sync_callback_) {
                        auto result = sync_positions_with_json();
                        position_sync_callback_(result);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            set_error("JSON monitoring error: " + std::string(e.what()));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.json_update_interval_ms));
    }
}

void SignalTransmissionManager::position_sync_worker()
{
    while (running_.load()) {
        try {
            // 检查JSON数据中的isFinished字段
            JsonPositionUpdate current_data;
            if (load_json_position_data(current_data)) {
                if (current_data.isFinished == 1) {
                    std::cout << "[SignalTransmissionManager] position_sync_worker: isFinished=1, 跳过仓位同步" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(config_.sync_timeout_ms));
                    continue;
                }
            }
            
            if (position_sync_callback_) {
                auto result = sync_positions_with_json();
                if (!result.success) {
                    // 记录同步失败
                    std::cout << "Position sync failed: " << result.error_message << std::endl;
                }
            }
        }
        catch (const std::exception& e) {
            set_error("Position sync error: " + std::string(e.what()));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.sync_timeout_ms));
    }
}

bool SignalTransmissionManager::compare_positions_with_json(const JsonPositionUpdate& json_data, PositionSyncResult& result)
{
    // 处理has_position中的仓位
    for (const auto& json_pos : json_data.has_position) {
        double target_quantity = parse_quantity(json_pos.quantity);
        if (!align_position(json_pos.symbol, target_quantity, result)) {
            return false;
        }
    }
    
    // 处理no_position中的仓位（应该平仓）
    for (const auto& json_pos : json_data.no_position) {
        if (!close_position_if_not_in_json(json_pos.symbol, result)) {
            return false;
        }
    }
    
    return true;
}

bool SignalTransmissionManager::align_position(const std::string& symbol, double target_quantity, PositionSyncResult& result)
{
    if (!trading_interface_) {
        result.error_message = "Trading interface not available";
        return false;
    }
    
    // 从BinanceTradingInterface获取当前仓位
    double current_quantity = 0.0;
    auto position_ptr = trading_interface_->get_position(symbol);
    if (position_ptr) {
        current_quantity = position_ptr->net_quantity;
    }
    
    // 添加详细的调试输出
    std::cout << "[DEBUG] Position alignment check for " << symbol << ":" << std::endl;
    std::cout << "[DEBUG]   Current quantity: " << current_quantity << std::endl;
    std::cout << "[DEBUG]   Target quantity: " << target_quantity << std::endl;
    std::cout << "[DEBUG]   Difference: " << std::abs(target_quantity - current_quantity) << std::endl;
    std::cout << "[DEBUG]   Max position diff threshold: " << config_.max_position_diff << std::endl;
    std::cout << "[DEBUG]   Precision tolerance: " << config_.precision_tolerance << std::endl;
    
    // 获取账户信息
    if (trading_interface_) {
        std::cout << "[DEBUG] Getting account information..." << std::endl;
        // 这里可以添加获取账户余额的代码
    }
    
    // 检查是否在容忍范围内
    if (std::abs(target_quantity - current_quantity) <= config_.max_position_diff) {
        std::cout << "[DEBUG] Position within tolerance, no adjustment needed." << std::endl;
        result.positions_aligned++;
        return true;
    }
    
    std::cout << "[DEBUG] Position adjustment required for " << symbol << ": current=" << current_quantity 
              << ", target=" << target_quantity << ", diff=" << (target_quantity - current_quantity) << std::endl;
    
    // 当前没有仓位，直接开目标仓位
    if (std::abs(current_quantity) <= config_.precision_tolerance) {
        std::cout << "No current position detected. Opening target position directly." << std::endl;
        
        if (std::abs(target_quantity) > config_.max_position_diff) {
            if (trading_interface_) {
                Order open_order;
                open_order.instrument_id = symbol;
                open_order.quantity = std::abs(target_quantity);
                // 修复bug：根据目标仓位的方向确定订单方向
                open_order.side = (target_quantity > 0) ? OrderSide::BUY : OrderSide::SELL;
                open_order.type = OrderType::MARKET;
                open_order.client_order_id = "open_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                
                std::string order_id = trading_interface_->submit_order(open_order);
                if (!order_id.empty()) {
                    std::cout << "Successfully submitted open position order for " << symbol 
                             << ", quantity: " << std::abs(target_quantity) << ", side: " << (target_quantity > 0 ? "BUY" : "SELL") << ", order_id: " << order_id << std::endl;
                    result.positions_opened++;
                } else {
                    std::cout << "Failed to open target position for " << symbol 
                             << ": " << trading_interface_->get_last_error() << std::endl;
                    result.error_message = "Failed to open target position for " + symbol + ": " + trading_interface_->get_last_error();
                    result.success = false;
                    return false;
                }
            } else {
                std::cout << "Warning: Trading interface not available for opening position " << symbol << std::endl;
            }
        }
        return true;
    }
    
    // 判断是否为反向单（当前仓位和目标仓位方向相反）
    bool is_opposite_direction = (current_quantity > 0 && target_quantity < 0) || (current_quantity < 0 && target_quantity > 0);
    
    if (is_opposite_direction) {
        // 反向单：需要先平仓，再开仓
        std::cout << "Opposite direction detected. First closing current position, then opening target position." << std::endl;
        
        // 1. 先平掉当前仓位
        if (std::abs(current_quantity) > config_.max_position_diff) {
            if (trading_interface_) {
                // 使用submit_order来平仓，平仓方向与当前仓位相反
                Order close_order;
                close_order.instrument_id = symbol;
                close_order.quantity = std::abs(current_quantity);
                close_order.side = (current_quantity > 0) ? OrderSide::SELL : OrderSide::BUY; // 平仓方向相反
                close_order.type = OrderType::MARKET;
                close_order.client_order_id = "close_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                
                std::string order_id = trading_interface_->submit_order(close_order);
                if (!order_id.empty()) {
                    std::cout << "Successfully submitted close position order for " << symbol 
                             << ", quantity: " << std::abs(current_quantity) << ", order_id: " << order_id << std::endl;
                    result.positions_closed++;
                } else {
                    std::cout << "Failed to close current position for " << symbol 
                             << ": " << trading_interface_->get_last_error() << std::endl;
                    result.error_message = "Failed to close current position for " + symbol + ": " + trading_interface_->get_last_error();
                    result.success = false;
                    return false;
                }
            } else {
                std::cout << "Warning: Trading interface not available for closing position " << symbol << std::endl;
            }
        }
        
        // 2. 再开目标仓位
        if (std::abs(target_quantity) > config_.max_position_diff) {
            if (trading_interface_) {
                Order open_order;
                open_order.instrument_id = symbol;
                open_order.quantity = std::abs(target_quantity);
                // 根据目标仓位的方向确定订单方向
                open_order.side = (target_quantity > 0) ? OrderSide::BUY : OrderSide::SELL;
                open_order.type = OrderType::MARKET;
                open_order.client_order_id = "open_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                
                std::string order_id = trading_interface_->submit_order(open_order);
                if (!order_id.empty()) {
                    std::cout << "Successfully submitted open position order for " << symbol 
                             << ", quantity: " << std::abs(target_quantity) << ", order_id: " << order_id << std::endl;
                    result.positions_opened++;
                } else {
                    std::cout << "Failed to open target position for " << symbol 
                             << ": " << trading_interface_->get_last_error() << std::endl;
                    result.error_message = "Failed to open target position for " + symbol + ": " + trading_interface_->get_last_error();
                    result.success = false;
                    return false;
                }
            } else {
                std::cout << "Warning: Trading interface not available for opening position " << symbol << std::endl;
            }
        }
    } else {
        // 同向单：计算差值进行增减仓
        double diff = target_quantity - current_quantity;
        
        if (diff > 0) {
            // 需要增加仓位（开多仓或减少空仓）
            result.positions_opened++;
            std::cout << "Same direction: need to increase position for " << symbol << ", quantity: " << diff << std::endl;
            
            if (trading_interface_) {
                Order open_order;
                open_order.instrument_id = symbol;
                open_order.quantity = std::abs(diff);
                // 对于同向单增加仓位，应该根据diff的符号来确定订单方向
                // diff > 0 表示需要增加仓位：如果当前是多头，则买入更多；如果当前是空头，则卖出更多
                if (current_quantity >= 0) {
                    // 当前是多头或无仓位，增加仓位意味着买入
                    open_order.side = OrderSide::BUY;
                } else {
                    // 当前是空头，增加仓位意味着卖出更多
                    open_order.side = OrderSide::SELL;
                }
                open_order.type = OrderType::MARKET;
                // 清理过期的待处理订单（超过30秒）
                {
                    std::lock_guard<std::mutex> lock(pending_orders_mutex);
                    auto now = std::chrono::system_clock::now();
                    auto it = pending_orders.begin();
                    while (it != pending_orders.end()) {
                        if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.submit_time).count() > 30) {
                            it = pending_orders.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                
                // 检查是否有相同的订单正在处理
                bool has_pending_order = false;
                {
                    std::lock_guard<std::mutex> lock(pending_orders_mutex);
                    for (const auto& pair : pending_orders) {
                        if (pair.second.symbol == symbol && std::abs(pair.second.quantity - std::abs(diff)) < 1e-6) {
                            has_pending_order = true;
                            std::cout << "Skipping duplicate order for " << symbol << ", quantity: " << std::abs(diff) 
                                     << ", pending order: " << pair.first << std::endl;
                            break;
                        }
                    }
                }
                
                if (has_pending_order) {
                    return true; // 跳过重复订单，但不算错误
                }
                
                // 生成唯一的client_order_id：前缀 + 符号 + 时间戳 + 计数器 + 随机数
                auto now = std::chrono::system_clock::now();
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                auto counter = order_id_counter.fetch_add(1);
                auto random_num = dis(gen);
                open_order.client_order_id = "adjust_" + symbol + "_" + std::to_string(timestamp) + "_" + std::to_string(counter) + "_" + std::to_string(random_num);
                
                // 记录待处理订单
                {
                    std::lock_guard<std::mutex> lock(pending_orders_mutex);
                    pending_orders[open_order.client_order_id] = {symbol, std::abs(diff), now};
                }
                
                // 重试机制：最多重试3次
                std::string order_id;
                int retry_count = 0;
                const int max_retries = 3;
                
                while (retry_count < max_retries) {
                    order_id = trading_interface_->submit_order(open_order);
                    if (!order_id.empty()) {
                        std::cout << "Successfully submitted increase position order for " << symbol 
                                 << ", order_id: " << order_id << std::endl;
                        break;
                    } else {
                        retry_count++;
                        std::string error = trading_interface_->get_last_error();
                        std::cout << "Failed to increase position for " << symbol 
                                 << " (attempt " << retry_count << "/" << max_retries << "): " << error << std::endl;
                        
                        // 如果是ClientOrderId重复错误，重新生成ID
                        if (error.find("ClientOrderId is duplicated") != std::string::npos || 
                            error.find("-4116") != std::string::npos) {
                            auto new_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                            auto new_counter = order_id_counter.fetch_add(1);
                            auto new_random = dis(gen);
                            open_order.client_order_id = "adjust_" + symbol + "_" + std::to_string(new_timestamp) + "_" + std::to_string(new_counter) + "_" + std::to_string(new_random);
                            std::cout << "Regenerated client_order_id: " << open_order.client_order_id << std::endl;
                        }
                        
                        if (retry_count < max_retries) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100 * retry_count)); // 递增延迟
                        }
                    }
                }
                
                // 清理待处理订单记录
                {
                    std::lock_guard<std::mutex> lock(pending_orders_mutex);
                    pending_orders.erase(open_order.client_order_id);
                }
                
                if (order_id.empty()) {
                    result.error_message = "Failed to increase position for " + symbol + " after " + std::to_string(max_retries) + " attempts: " + trading_interface_->get_last_error();
                    result.success = false;
                    return false;
                }
            } else {
                std::cout << "Warning: Trading interface not available for increasing position " << symbol << std::endl;
            }
        } else if (diff < 0) {
            // 需要减少仓位（平多仓或减少空仓）
            result.positions_closed++;
            std::cout << "Same direction: need to decrease position for " << symbol << ", quantity: " << std::abs(diff) << std::endl;
            
            if (trading_interface_) {
                // 重试机制：最多重试3次
                bool close_result = false;
                int retry_count = 0;
                const int max_retries = 3;
                
                while (retry_count < max_retries) {
                    // 使用submit_order来减少仓位，减仓方向与当前仓位相反
                    Order close_order;
                    close_order.instrument_id = symbol;
                    close_order.quantity = std::abs(diff);
                    close_order.side = (current_quantity > 0) ? OrderSide::SELL : OrderSide::BUY; // 减仓方向相反
                    close_order.type = OrderType::MARKET;
                    close_order.client_order_id = "reduce_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                    
                    std::string order_id = trading_interface_->submit_order(close_order);
                    if (!order_id.empty()) {
                        std::cout << "Successfully submitted decrease position order for " << symbol << ", order_id: " << order_id << std::endl;
                        close_result = true;
                        break;
                    } else {
                        retry_count++;
                        std::string error = trading_interface_->get_last_error();
                        std::cout << "Failed to decrease position for " << symbol 
                                 << " (attempt " << retry_count << "/" << max_retries << "): " << error << std::endl;
                        
                        if (retry_count < max_retries) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100 * retry_count)); // 递增延迟
                        }
                    }
                }
                
                if (!close_result) {
                    result.error_message = "Failed to decrease position for " + symbol + " after " + std::to_string(max_retries) + " attempts: " + trading_interface_->get_last_error();
                    result.success = false;
                    return false;
                }
            } else {
                std::cout << "Warning: Trading interface not available for decreasing position " << symbol << std::endl;
            }
        }
    }
    
    return true;
}

bool SignalTransmissionManager::close_position_if_not_in_json(const std::string& symbol, PositionSyncResult& result)
{
    if (!position_manager_) {
        result.error_message = "Position manager not available";
        return false;
    }
    
    // 检查position_manager中是否有该symbol的仓位
    Position current_position = position_manager_->get_position("default", symbol);
    
    if (std::abs(current_position.net_quantity) > config_.precision_tolerance) {
        // 需要平仓
        result.positions_closed++;
        std::cout << "Need to close entire position for " << symbol 
                 << ", current quantity: " << current_position.net_quantity << std::endl;
        
        // 调用平仓逻辑，带重试机制
        if (trading_interface_) {
            bool close_result = false;
            int retry_count = 0;
            const int max_retries = 3;
            
            while (retry_count < max_retries) {
                // 使用submit_order来平仓，平仓方向与当前仓位相反
                Order close_order;
                close_order.instrument_id = symbol;
                close_order.quantity = std::abs(current_position.net_quantity);
                close_order.side = (current_position.net_quantity > 0) ? OrderSide::SELL : OrderSide::BUY; // 平仓方向相反
                close_order.type = OrderType::MARKET;
                close_order.client_order_id = "close_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                
                std::string order_id = trading_interface_->submit_order(close_order);
                if (!order_id.empty()) {
                    std::cout << "Successfully submitted close position order for " << symbol << ", order_id: " << order_id << std::endl;
                    close_result = true;
                    break;
                } else {
                    retry_count++;
                    std::string error = trading_interface_->get_last_error();
                    std::cout << "Failed to submit close position order for " << symbol 
                             << " (attempt " << retry_count << "/" << max_retries << "): " << error << std::endl;
                    
                    if (retry_count < max_retries) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100 * retry_count)); // 递增延迟
                    }
                }
            }
            
            if (!close_result) {
                result.error_message = "Failed to close position for " + symbol + " after " + std::to_string(max_retries) + " attempts: " + trading_interface_->get_last_error();
                return false;
            }
        } else {
            std::cout << "Warning: Trading interface not available for closing position " << symbol << std::endl;
        }
    }
    
    return true;
}

bool SignalTransmissionManager::validate_booksize_and_targets(const JsonPositionUpdate& json_data, PositionSyncResult& result)
{
    // 待实现booksize、targetvalue、longtarget、shorttarget的验证逻辑
    
    result.booksize_aligned = true;
    result.targets_aligned = true;
    
    return true;
}

void SignalTransmissionManager::update_json_status(int isFinished, const std::string& errorstring)
{
    JsonPositionUpdate data;
    if (load_json_position_data(data)) {
        data.isFinished = isFinished;
        data.errorstring = errorstring;
        data.update_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() / 1000.0;
        
        save_json_position_data(data);
    }
}

double SignalTransmissionManager::parse_quantity(const std::string& quantity_str)
{
    try {
        return std::stod(quantity_str);
    }
    catch (const std::exception&) {
        return 0.0;
    }
}

void SignalTransmissionManager::set_error(const std::string& error)
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
    std::cerr << "SignalTransmissionManager Error: " << error << std::endl;
}

} // namespace execution
} // namespace tes