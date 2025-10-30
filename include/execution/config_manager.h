#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <filesystem>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

// Forward declarations for td components
namespace trading {
namespace utils {
    class ConfigManager;
}

// Forward declaration for ExchangeConfig
struct ExchangeConfig;
}

namespace tes {
namespace execution {

/**
 * @brief 配置管理器
 * 
 * 统一管理TES和TD系统的配置，提供配置转换和加载功能
 */
class ConfigManager {
public:
    /**
     * @brief 系统配置结构
     */
    struct SystemConfig {
        // 系统基本配置
        std::string system_name;
        std::string version;
        std::string log_level;
        uint32_t max_threads;
        
        // 信号传递配置
        int signaltrans_mode;
        
        // 共享内存配置
        uint32_t buffer_size;
        uint32_t max_signals;
        uint32_t signal_buffer_size;
        uint32_t order_report_buffer_size;
        uint32_t cleanup_interval_ms;
        
        // JSON文件配置
        std::string position_file;
        uint32_t update_interval_ms;
        double tolerance_threshold;
        
        // 仓位同步配置
        double precision_tolerance;
        double max_position_diff;
        uint32_t sync_timeout_ms;
        
        // 交易配置
        std::vector<std::string> trading_exchanges;
        std::vector<std::string> trading_type;
        double default_quantity;
        double max_order_size;
        bool enable_risk_control;
        bool enable_position_tracking;
        bool enable_algorithm_execution;
        bool enable_order_feedback;
        
        // Binance API配置
        std::string api_key;
        std::string api_secret;
        bool testnet;
        bool enable_websocket;
        bool enable_user_data_stream;
        uint32_t sync_interval_ms;
        uint32_t timeout_ms;
        
        // Binance URL配置
        struct BaseUrls {
            struct SpotUrls {
                std::string live;
                std::string testnet;
            } spot;
            
            struct FuturesUrls {
                std::string live;
                std::string testnet;
            } futures;
        } base_urls;
        
        // 执行控制器配置
        uint32_t worker_thread_count;
        uint32_t signal_processing_interval_ms;
        uint32_t heartbeat_interval_ms;
        uint32_t statistics_update_interval_ms;
        
        // TWAP算法配置
        double twap_quantity_threshold;
        double twap_value_threshold;
        double twap_market_impact_threshold;
        uint32_t twap_default_duration_minutes;
        double twap_min_slice_size;
        uint32_t twap_max_slices;
        double twap_default_participation_rate;
        uint32_t twap_max_price_deviation_bps;
        
        // 风险控制配置
        double risk_max_position_size;
        double risk_max_order_size;
        double risk_max_daily_loss;
        double risk_max_total_exposure;
        uint32_t risk_max_orders_per_second;
        uint32_t risk_max_orders_per_minute;
        
        // 市场数据配置
        uint32_t market_data_update_interval_ms;
        double price_volatility;
        double volume_volatility;
        double spread_ratio;
        
        // 日志配置
        std::string log_file;
        bool console_output;
        uint32_t max_file_size_mb;
        uint32_t max_files;
        bool async_logging;
        
        // 监控配置
        bool enable_performance_monitoring;
        bool enable_buffer_monitoring;
        bool enable_system_monitoring;
        uint32_t monitoring_interval_ms;
        uint32_t alert_threshold_percent;
        
        SystemConfig() {
            // 初始化关键字段的默认值
            max_threads = 1;
            signal_buffer_size = 1000;
            order_report_buffer_size = 1000;
            sync_interval_ms = 1000;  // 默认1秒
            timeout_ms = 15000;       // 默认15秒
        }
    };
    
    ConfigManager();
    ~ConfigManager();
    
    // 禁用拷贝构造和赋值
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    // 配置加载和保存
    bool load_config(const std::string& config_file);
    bool save_config(const std::string& config_file) const;
    bool load_from_json(const nlohmann::json& json_config);
    nlohmann::json to_json() const;
    
    // 配置访问
    const SystemConfig& get_system_config() const;
    void set_system_config(const SystemConfig& config);
    
    // 配置转换方法 - 暂时移除，避免依赖问题
    // trading::ExchangeConfig to_exchange_config() const;
    
    // 配置验证
    bool validate_config() const;
    std::vector<std::string> get_validation_errors() const;
    
    // 环境变量支持
    void load_from_environment();
    void set_environment_variable(const std::string& key, const std::string& value);
    std::string get_environment_variable(const std::string& key, const std::string& default_value = "") const;
    
    // 配置热更新
    void enable_hot_reload(const std::string& config_file);
    void disable_hot_reload();
    bool is_hot_reload_enabled() const;
    
    // 回调函数类型
    using ConfigChangeCallback = std::function<void(const SystemConfig&)>;
    void set_config_change_callback(ConfigChangeCallback callback);
    
    // 错误处理
    std::string get_last_error() const;
    
private:
    // 内部方法
    void parse_json_config(const nlohmann::json& json);
    nlohmann::json create_json_config() const;
    void validate_api_credentials() const;
    void validate_trading_parameters() const;
    void validate_system_parameters() const;
    void set_error(const std::string& error) const;
    void notify_config_change();
    
    // 热更新相关
    void file_watcher_thread();
    bool check_file_modified(const std::string& file_path) const;
    
    // 成员变量
    mutable std::mutex config_mutex_;
    mutable std::mutex error_mutex_;
    
    SystemConfig system_config_;
    mutable std::string last_error_;
    mutable std::vector<std::string> validation_errors_;
    
    // 热更新相关
    std::atomic<bool> hot_reload_enabled_;
    std::string watched_config_file_;
    std::unique_ptr<std::thread> file_watcher_thread_;
    std::atomic<bool> stop_file_watcher_;
    std::filesystem::file_time_type last_modified_time_;
    
    // 回调函数
    ConfigChangeCallback config_change_callback_;
    std::mutex callback_mutex_;
};

/**
 * @brief 全局配置管理器单例
 */
class GlobalConfigManager {
public:
    static ConfigManager& instance();
    static bool initialize(const std::string& config_file = "");
    static void cleanup();
    
private:
    static std::unique_ptr<ConfigManager> instance_;
    static std::once_flag init_flag_;
};

} // namespace execution
} // namespace tes