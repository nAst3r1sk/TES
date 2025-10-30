#pragma once

#include "types.h"
#include "position_manager.h"
#include "shared_memory_interface.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <fstream>
#include <nlohmann/json.hpp>

// Forward declaration
namespace tes {
namespace execution {
    class GatewayAdapter;
}
}

namespace tes {
namespace execution {

// 信号传递模式
enum class SignalTransmissionMode {
    SHARED_MEMORY = 0,  // 共享内存模式
    JSON_FILE = 1       // JSON文件模式
};

// JSON文件仓位数据结构
struct JsonPositionData {
    int id;
    std::string symbol;
    std::string quantity;
};

struct JsonPositionUpdate {
    std::vector<JsonPositionData> has_position;
    std::vector<JsonPositionData> no_position;
    double booksize;
    double targetvalue;
    double longtarget;
    double shorttarget;
    int isFinished;
    std::string errorstring;
    double update_timestamp;
};

// 仓位同步结果
struct PositionSyncResult {
    bool success;
    std::string error_message;
    int positions_aligned;
    int positions_opened;
    int positions_closed;
    bool booksize_aligned;
    bool targets_aligned;
};

class SignalTransmissionManager {
public:
    // 配置结构
    struct Config {
        SignalTransmissionMode mode;
        std::string json_file_path;
        uint32_t json_update_interval_ms;
        double precision_tolerance;
        double max_position_diff;
        uint32_t sync_timeout_ms;
        bool enable_auto_sync;
        
        Config() : mode(SignalTransmissionMode::SHARED_MEMORY),
                   json_file_path("config/pos_update.json"),
                   json_update_interval_ms(1000),
                   precision_tolerance(0.00001),
                   max_position_diff(0.00001),
                   sync_timeout_ms(5000),
                   enable_auto_sync(true) {}
    };
    
    using PositionSyncCallback = std::function<void(const PositionSyncResult&)>;
    using SignalCallback = std::function<void(const shared_memory::TradingSignal&)>;
    
    SignalTransmissionManager();
    ~SignalTransmissionManager();
    
    // 生命周期管理
    bool initialize(const Config& config);
    bool start();
    void stop();
    void cleanup();
    
    // 模式切换
    bool set_transmission_mode(SignalTransmissionMode mode);
    SignalTransmissionMode get_transmission_mode() const;
    
    // 信号处理
    bool receive_signals(std::vector<shared_memory::TradingSignal>& signals, uint32_t max_count = 100);
    void set_signal_callback(SignalCallback callback);
    
    // JSON文件模式专用
    bool load_json_position_data(JsonPositionUpdate& data);
    bool save_json_position_data(const JsonPositionUpdate& data);
    PositionSyncResult sync_positions_with_json();
    
    // 仓位管理集成
    void set_position_manager(std::shared_ptr<PositionManager> position_manager);
    void set_shared_memory_interface(std::shared_ptr<SharedMemoryInterface> shared_memory_interface);
    void set_trading_interface(std::shared_ptr<GatewayAdapter> trading_interface);
    
    // 回调设置
    void set_position_sync_callback(PositionSyncCallback callback);
    
    // 配置和状态
    void set_config(const Config& config);
    Config get_config() const;
    bool is_running() const;
    std::string get_last_error() const;
    
private:
    // 内部方法
    void json_monitoring_worker();
    void position_sync_worker();
    bool compare_positions_with_json(const JsonPositionUpdate& json_data, PositionSyncResult& result);
    bool align_position(const std::string& symbol, double target_quantity, PositionSyncResult& result);
    bool close_position_if_not_in_json(const std::string& symbol, PositionSyncResult& result);
    bool validate_booksize_and_targets(const JsonPositionUpdate& json_data, PositionSyncResult& result);
    void update_json_status(int isFinished, const std::string& errorstring);
    double parse_quantity(const std::string& quantity_str);
    void set_error(const std::string& error);
    
    // 成员变量
    mutable std::mutex config_mutex_;
    mutable std::mutex error_mutex_;
    mutable std::mutex json_mutex_;
    
    Config config_;
    std::string last_error_;
    
    std::atomic<bool> initialized_;
    std::atomic<bool> running_;
    
    // 组件引用
    std::shared_ptr<PositionManager> position_manager_;
    std::shared_ptr<SharedMemoryInterface> shared_memory_interface_;
    std::shared_ptr<GatewayAdapter> trading_interface_;
    
    // 工作线程
    std::unique_ptr<std::thread> json_monitoring_thread_;
    std::unique_ptr<std::thread> position_sync_thread_;
    
    // 回调函数
    PositionSyncCallback position_sync_callback_;
    SignalCallback signal_callback_;
    
    // JSON文件相关
    std::chrono::high_resolution_clock::time_point last_json_update_time_;
    JsonPositionUpdate last_json_data_;
};

} // namespace execution
} // namespace tes