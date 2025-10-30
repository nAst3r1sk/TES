#include "shared_memory/core/control_info.h"
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <thread>

namespace tes {
namespace shared_memory {

ControlInfo::ControlInfo(const std::string& name, bool create)
    : shm_name_("/tes_control_" + name), shm_fd_(-1), shared_data_(nullptr), is_creator_(create) {
    
    if (create) {
        if (!create_shared_memory()) {
            throw std::runtime_error("Failed to create shared memory for ControlInfo");
        }
        initialize_default_config();
    } else {
        if (!open_shared_memory()) {
            throw std::runtime_error("Failed to open shared memory for ControlInfo");
        }
    }
}

ControlInfo::~ControlInfo() {
    cleanup();
}

bool ControlInfo::create_shared_memory() {
    // 创建共享内存
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd_ == -1) {
        // 如果已存在，先删除再创建
        shm_unlink(shm_name_.c_str());
        shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (shm_fd_ == -1) {
            return false;
        }
    }
    
    // 设置共享内存大小
    if (ftruncate(shm_fd_, sizeof(SharedControlData)) == -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }
    
    // 映射共享内存
    shared_data_ = static_cast<SharedControlData*>(
        mmap(nullptr, sizeof(SharedControlData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0)
    );
    
    if (shared_data_ == MAP_FAILED) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }
    
    // 初始化共享数据
    new (shared_data_) SharedControlData();
    shared_data_->is_initialized.store(true);
    
    return true;
}

bool ControlInfo::open_shared_memory() {
    // 打开现有共享内存
    shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
    if (shm_fd_ == -1) {
        return false;
    }
    
    // 映射共享内存
    shared_data_ = static_cast<SharedControlData*>(
        mmap(nullptr, sizeof(SharedControlData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0)
    );
    
    if (shared_data_ == MAP_FAILED) {
        close(shm_fd_);
        return false;
    }
    
    // 等待初始化完成
    while (!shared_data_->is_initialized.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    return true;
}

void ControlInfo::cleanup() {
    if (shared_data_ != nullptr && shared_data_ != MAP_FAILED) {
        munmap(shared_data_, sizeof(SharedControlData));
        shared_data_ = nullptr;
    }
    
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    
    if (is_creator_) {
        shm_unlink(shm_name_.c_str());
    }
}

void ControlInfo::initialize_default_config() {
    if (!shared_data_) return;
    
    shared_data_->config.max_position_limit = 1000000.0;  // 100万
    shared_data_->config.max_daily_loss = 50000.0;        // 5万
    shared_data_->config.max_order_value = 100000.0;      // 10万
    shared_data_->config.max_orders_per_second = 100;
    shared_data_->config.heartbeat_interval_ms = 1000;    // 1秒
}

bool ControlInfo::set_system_state(SystemState state) {
    if (!shared_data_) return false;
    
    shared_data_->system_state.store(state);
    return true;
}

ControlInfo::SystemState ControlInfo::get_system_state() const {
    if (!shared_data_) return SystemState::ERROR;
    
    return shared_data_->system_state.load();
}

bool ControlInfo::set_trading_mode(TradingMode mode) {
    if (!shared_data_) return false;
    
    shared_data_->trading_mode.store(mode);
    return true;
}

ControlInfo::TradingMode ControlInfo::get_trading_mode() const {
    if (!shared_data_) return TradingMode::SIMULATION;
    
    return shared_data_->trading_mode.load();
}

void ControlInfo::trigger_emergency_stop() {
    if (!shared_data_) return;
    
    shared_data_->emergency_stop.store(true);
    shared_data_->system_state.store(SystemState::STOPPED);
}

void ControlInfo::clear_emergency_stop() {
    if (!shared_data_) return;
    
    shared_data_->emergency_stop.store(false);
}

bool ControlInfo::is_emergency_stop() const {
    if (!shared_data_) return true;
    
    return shared_data_->emergency_stop.load();
}

void ControlInfo::enable_risk_control(bool enable) {
    if (!shared_data_) return;
    
    shared_data_->risk_control_enabled.store(enable);
}

void ControlInfo::enable_order_routing(bool enable) {
    if (!shared_data_) return;
    
    shared_data_->order_routing_enabled.store(enable);
}

void ControlInfo::enable_market_data(bool enable) {
    if (!shared_data_) return;
    
    shared_data_->market_data_enabled.store(enable);
}

bool ControlInfo::is_risk_control_enabled() const {
    if (!shared_data_) return false;
    
    return shared_data_->risk_control_enabled.load();
}

bool ControlInfo::is_order_routing_enabled() const {
    if (!shared_data_) return false;
    
    return shared_data_->order_routing_enabled.load();
}

bool ControlInfo::is_market_data_enabled() const {
    if (!shared_data_) return false;
    
    return shared_data_->market_data_enabled.load();
}

void ControlInfo::update_heartbeat() {
    if (!shared_data_) return;
    
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    shared_data_->last_heartbeat.store(now_ns);
    shared_data_->heartbeat_counter.fetch_add(1);
}

Timestamp ControlInfo::get_last_heartbeat() const {
    if (!shared_data_) return Timestamp{};
    
    return shared_data_->last_heartbeat.load();
}

uint64_t ControlInfo::get_heartbeat_counter() const {
    if (!shared_data_) return 0;
    
    return shared_data_->heartbeat_counter.load();
}

bool ControlInfo::is_heartbeat_alive(uint32_t timeout_ms) const {
    if (!shared_data_) return false;
    
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    auto last_hb = shared_data_->last_heartbeat.load();
    auto elapsed_ns = now_ns - last_hb;
    auto elapsed_ms = elapsed_ns / 1000000; // 转换为毫秒
    
    return elapsed_ms < static_cast<int64_t>(timeout_ms);
}

void ControlInfo::update_active_strategies(uint32_t count) {
    if (!shared_data_) return;
    
    shared_data_->active_strategies.store(count);
}

void ControlInfo::update_active_orders(uint32_t count) {
    if (!shared_data_) return;
    
    shared_data_->active_orders.store(count);
}

void ControlInfo::update_pnl(double total_pnl, double daily_pnl) {
    if (!shared_data_) return;
    
    shared_data_->total_pnl.store(total_pnl);
    shared_data_->daily_pnl.store(daily_pnl);
}

uint32_t ControlInfo::get_active_strategies() const {
    if (!shared_data_) return 0;
    
    return shared_data_->active_strategies.load();
}

uint32_t ControlInfo::get_active_orders() const {
    if (!shared_data_) return 0;
    
    return shared_data_->active_orders.load();
}

double ControlInfo::get_total_pnl() const {
    if (!shared_data_) return 0.0;
    
    return shared_data_->total_pnl.load();
}

double ControlInfo::get_daily_pnl() const {
    if (!shared_data_) return 0.0;
    
    return shared_data_->daily_pnl.load();
}

bool ControlInfo::set_config(const SharedControlData::Config& config) {
    if (!shared_data_) return false;
    
    shared_data_->config = config;
    return true;
}

ControlInfo::SharedControlData::Config ControlInfo::get_config() const {
    if (!shared_data_) return {};
    
    return shared_data_->config;
}

bool ControlInfo::is_system_healthy() const {
    if (!shared_data_) return false;
    
    auto state = get_system_state();
    return state == SystemState::RUNNING && 
           !is_emergency_stop() &&
           is_heartbeat_alive(5000);  // 5秒超时
}

bool ControlInfo::can_place_order() const {
    return is_system_healthy() && 
           is_order_routing_enabled() &&
           is_risk_control_enabled();
}

bool ControlInfo::can_process_signal() const {
    return is_system_healthy() && 
           get_system_state() == SystemState::RUNNING;
}

std::string ControlInfo::get_system_state_string() const {
    switch (get_system_state()) {
        case SystemState::INITIALIZING: return "INITIALIZING";
        case SystemState::RUNNING: return "RUNNING";
        case SystemState::PAUSED: return "PAUSED";
        case SystemState::STOPPING: return "STOPPING";
        case SystemState::STOPPED: return "STOPPED";
        case SystemState::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string ControlInfo::get_trading_mode_string() const {
    switch (get_trading_mode()) {
        case TradingMode::SIMULATION: return "SIMULATION";
        case TradingMode::PAPER_TRADING: return "PAPER_TRADING";
        case TradingMode::LIVE_TRADING: return "LIVE_TRADING";
        default: return "UNKNOWN";
    }
}

} // namespace shared_memory
} // namespace tes