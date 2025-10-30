#pragma once

#include "shared_memory/core/common_types.h"
#include <atomic>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

namespace tes {
namespace shared_memory {

// 系统控制信息
class ControlInfo {
public:
    enum class SystemState {
        INITIALIZING = 0,
        RUNNING = 1,
        PAUSED = 2,
        STOPPING = 3,
        STOPPED = 4,
        ERROR = 5
    };
    
    enum class TradingMode {
        SIMULATION = 0,
        PAPER_TRADING = 1,
        LIVE_TRADING = 2
    };
    
    struct alignas(64) SharedControlData {
        std::atomic<SystemState> system_state{SystemState::INITIALIZING};
        std::atomic<TradingMode> trading_mode{TradingMode::SIMULATION};
        std::atomic<bool> emergency_stop{false};
        std::atomic<bool> risk_control_enabled{true};
        std::atomic<bool> order_routing_enabled{true};
        std::atomic<bool> market_data_enabled{true};
        std::atomic<uint64_t> heartbeat_counter{0};
        std::atomic<Timestamp> last_heartbeat{Timestamp{}};
        std::atomic<uint32_t> active_strategies{0};
        std::atomic<uint32_t> active_orders{0};
        std::atomic<double> total_pnl{0.0};
        std::atomic<double> daily_pnl{0.0};
        std::atomic<bool> is_initialized{false};
        
        // 配置参数
        struct Config {
            double max_position_limit;
            double max_daily_loss;
            double max_order_value;
            uint32_t max_orders_per_second;
            uint32_t heartbeat_interval_ms;
        } config;
    };
    
    ControlInfo(const std::string& name, bool create = false);
    ~ControlInfo();
    
    // 系统状态控制
    bool set_system_state(SystemState state);
    SystemState get_system_state() const;
    
    // 交易模式控制
    bool set_trading_mode(TradingMode mode);
    TradingMode get_trading_mode() const;
    
    // 紧急停止
    void trigger_emergency_stop();
    void clear_emergency_stop();
    bool is_emergency_stop() const;
    
    // 功能开关
    void enable_risk_control(bool enable);
    void enable_order_routing(bool enable);
    void enable_market_data(bool enable);
    
    bool is_risk_control_enabled() const;
    bool is_order_routing_enabled() const;
    bool is_market_data_enabled() const;
    
    // 心跳管理
    void update_heartbeat();
    Timestamp get_last_heartbeat() const;
    uint64_t get_heartbeat_counter() const;
    bool is_heartbeat_alive(uint32_t timeout_ms) const;
    
    // 统计信息
    void update_active_strategies(uint32_t count);
    void update_active_orders(uint32_t count);
    void update_pnl(double total_pnl, double daily_pnl);
    
    uint32_t get_active_strategies() const;
    uint32_t get_active_orders() const;
    double get_total_pnl() const;
    double get_daily_pnl() const;
    
    // 配置管理
    bool set_config(const SharedControlData::Config& config);
    SharedControlData::Config get_config() const;
    
    // 系统检查
    bool is_system_healthy() const;
    bool can_place_order() const;
    bool can_process_signal() const;
    
    // 状态字符串
    std::string get_system_state_string() const;
    std::string get_trading_mode_string() const;
    
private:
    std::string shm_name_;
    int shm_fd_;
    SharedControlData* shared_data_;
    bool is_creator_;
    
    bool create_shared_memory();
    bool open_shared_memory();
    void cleanup();
    void initialize_default_config();
};

} // namespace shared_memory
} // namespace tes