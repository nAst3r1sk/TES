#pragma once

#include "types.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

namespace tes {
namespace execution {

// 前向声明
class PositionExchangeAdapter;

// 持仓交易所适配器接口
class PositionExchangeAdapter {
public:
    virtual ~PositionExchangeAdapter() = default;
    
    // 从交易所获取持仓信息
    virtual std::vector<Position> fetch_positions() = 0;
    virtual Position fetch_position(const std::string& strategy_id, const std::string& instrument_id) = 0;
    
    // 向交易所同步持仓更新
    virtual bool sync_position_to_exchange(const Position& position) = 0;
    virtual bool sync_positions_to_exchange(const std::vector<Position>& positions) = 0;
    
    // 平仓操作
    virtual bool close_position_on_exchange(const std::string& strategy_id, const std::string& instrument_id, double quantity) = 0;
    
    // 获取实时市场数据
    virtual MarketData get_market_data(const std::string& instrument_id) = 0;
    virtual std::vector<MarketData> get_market_data_batch(const std::vector<std::string>& instrument_ids) = 0;
};

// 持仓事件类型
enum class PositionEventType {
    POSITION_OPENED,    // 持仓开仓
    POSITION_CLOSED,    // 持仓平仓
    POSITION_UPDATED,   // 持仓更新
    POSITION_EXPIRED    // 持仓过期
};

// 持仓事件
struct PositionEvent {
    std::string event_id;
    PositionEventType type;
    Position position;
    std::string description;
    std::chrono::high_resolution_clock::time_point timestamp;
    
    PositionEvent() : type(PositionEventType::POSITION_UPDATED) {
        timestamp = std::chrono::high_resolution_clock::now();
    }
};

// 持仓统计
struct PositionStatistics {
    uint64_t total_positions;
    uint64_t active_positions;
    uint64_t closed_positions;
    uint64_t total_trades_processed;
    double total_realized_pnl;
    double total_unrealized_pnl;
    std::chrono::high_resolution_clock::time_point last_update_time;
    std::chrono::high_resolution_clock::time_point last_trade_time;
};

class PositionManager {
public:
    // 配置结构
    struct Config {
        bool enable_pnl_calculation;       // 启用盈亏计算
        bool enable_position_tracking;     // 启用持仓跟踪
        bool enable_event_logging;         // 启用事件记录
        bool auto_close_zero_positions;    // 自动关闭零持仓
        uint32_t position_cleanup_interval_seconds; // 持仓清理间隔（秒）
        
        Config() : enable_pnl_calculation(true), enable_position_tracking(true),
                   enable_event_logging(true), auto_close_zero_positions(true),
                   position_cleanup_interval_seconds(300) {}
    };
    
    // 事件回调函数类型
    using PositionEventCallback = std::function<void(const PositionEvent&)>;
    
    // 策略映射回调函数类型
    using StrategyMappingCallback = std::function<std::string(const std::string&)>;
    
    PositionManager();
    ~PositionManager();
    
    // 生命周期管理
    bool initialize();
    bool start();
    void stop();
    void cleanup();
    
    // 交易处理
    void process_trade(const Trade& trade, const std::string& strategy_id);
    void process_trades(const std::vector<Trade>& trades);
    
    // 持仓查询
    Position get_position(const std::string& strategy_id, const std::string& instrument_id) const;
    std::vector<Position> get_positions_by_strategy(const std::string& strategy_id) const;
    std::vector<Position> get_positions_by_instrument(const std::string& instrument_id) const;
    std::vector<Position> get_all_positions() const;
    
    // 持仓管理
    void update_position(const Position& position);
    void close_position(const std::string& strategy_id, const std::string& instrument_id);
    void close_all_positions(const std::string& strategy_id);
    
    // 市场数据更新
    void update_market_data(const MarketData& market_data);
    void update_market_data_batch(const std::vector<MarketData>& market_data_list);
    
    // 盈亏计算
    double calculate_unrealized_pnl(const Position& position, double current_price) const;
    double calculate_unrealized_pnl(const std::string& strategy_id) const;
    double calculate_realized_pnl(const std::string& strategy_id) const;
    double calculate_total_pnl(const std::string& strategy_id) const;
    
    // 风险指标
    double calculate_total_exposure(const std::string& strategy_id) const;
    double calculate_net_position(const std::string& strategy_id, const std::string& instrument_id) const;
    std::unordered_map<std::string, double> get_exposure_by_instrument() const;
    
    // 事件管理
    void set_position_event_callback(PositionEventCallback callback);
    std::vector<PositionEvent> get_recent_events(uint32_t count = 100) const;
    
    // 配置和统计
    void set_config(const Config& config);
    Config get_config() const;
    PositionStatistics get_statistics() const;
    
    // 实用方法
    bool is_position_exists(const std::string& strategy_id, const std::string& instrument_id) const;
    std::string get_position_key(const std::string& strategy_id, const std::string& instrument_id) const;
    
    // 外部持仓同步方法
    void sync_position_from_exchange(const Position& position);
    void sync_positions_from_exchange(const std::vector<Position>& positions);
    bool remove_position(const std::string& strategy_id, const std::string& instrument_id);
    
    // 交易所适配器管理
    void set_exchange_adapter(std::shared_ptr<PositionExchangeAdapter> adapter);
    std::shared_ptr<PositionExchangeAdapter> get_exchange_adapter() const;
    bool has_exchange_adapter() const;
    
    // 持仓同步和状态管理
    void force_update_position(const Position& position);
    void batch_update_positions(const std::vector<Position>& positions);
    void refresh_positions_from_exchange();
    void clear_stale_positions(std::chrono::seconds max_age);
    
    // 策略映射功能
    void set_strategy_mapping_callback(StrategyMappingCallback callback);
    std::string map_exchange_strategy_id(const std::string& exchange_strategy_id) const;
    void add_strategy_mapping(const std::string& exchange_strategy_id, const std::string& internal_strategy_id);
    void remove_strategy_mapping(const std::string& exchange_strategy_id);
    std::unordered_map<std::string, std::string> get_strategy_mappings() const;
    
private:
    // 内部结构
    struct PositionData {
        Position position;
        double current_price;
        std::chrono::high_resolution_clock::time_point last_update_time;
        
        PositionData() : current_price(0.0) {
            last_update_time = std::chrono::high_resolution_clock::now();
        }
    };
    
    // 内部方法
    std::string generate_event_id();
    void log_position_event(const PositionEvent& event);
    void update_position_internal(const Position& position);
    void cleanup_zero_positions();
    void update_statistics();
    void worker_thread();
    
    // 成员变量
    mutable std::mutex positions_mutex_;
    mutable std::mutex market_data_mutex_;
    mutable std::mutex events_mutex_;
    mutable std::mutex config_mutex_;
    mutable std::mutex statistics_mutex_;
    
    std::unordered_map<std::string, PositionData> positions_; // position_key -> PositionData
    std::unordered_map<std::string, double> current_prices_;  // instrument_id -> price
    std::vector<PositionEvent> recent_events_;
    
    Config config_;
    PositionStatistics statistics_;
    PositionEventCallback event_callback_;
    
    std::atomic<bool> initialized_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> event_sequence_;
    
    std::unique_ptr<std::thread> worker_thread_;
    
    // 交易所适配器相关
    std::shared_ptr<PositionExchangeAdapter> exchange_adapter_;
    mutable std::mutex adapter_mutex_;
    
    // 策略映射相关
    StrategyMappingCallback strategy_mapping_callback_;
    std::unordered_map<std::string, std::string> strategy_mappings_; // exchange_strategy_id -> internal_strategy_id
    mutable std::mutex strategy_mapping_mutex_;
};

} // namespace execution
} // namespace tes