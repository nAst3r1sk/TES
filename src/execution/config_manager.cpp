#include "execution/config_manager.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>

// 简单的ExchangeConfig结构定义，避免依赖gateway
namespace trading {
    struct ExchangeConfig {
        std::string apiKey;
        std::string apiSecret;
        bool testnet = false;
        int syncIntervalMs = 100;
        int timeoutMs = 5000;
        std::map<std::string, std::map<std::string, std::string>> baseUrls;
    };
}

namespace tes {
namespace execution {

ConfigManager::ConfigManager()
    : hot_reload_enabled_(false)
    , stop_file_watcher_(false) {
}

ConfigManager::~ConfigManager() {
    disable_hot_reload();
}

bool ConfigManager::load_config(const std::string& config_file) {
    try {
        if (!std::filesystem::exists(config_file)) {
            set_error("Config file does not exist: " + config_file);
            return false;
        }
        
        std::ifstream file(config_file);
        if (!file.is_open()) {
            set_error("Failed to open config file: " + config_file);
            return false;
        }
        
        nlohmann::json json_config;
        file >> json_config;
        file.close();
        
        return load_from_json(json_config);
        
    } catch (const std::exception& e) {
        set_error("Exception loading config: " + std::string(e.what()));
        return false;
    }
}

bool ConfigManager::save_config(const std::string& config_file) const {
    try {
        // 确保目录存在
        std::filesystem::path file_path(config_file);
        std::filesystem::path dir_path = file_path.parent_path();
        if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
            std::filesystem::create_directories(dir_path);
        }
        
        std::ofstream file(config_file);
        if (!file.is_open()) {
            set_error("Failed to create config file: " + config_file);
            return false;
        }
        
        nlohmann::json json_config = to_json();
        file << json_config.dump(4);
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        set_error("Exception saving config: " + std::string(e.what()));
        return false;
    }
}

bool ConfigManager::load_from_json(const nlohmann::json& json_config) {
    try {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        parse_json_config(json_config);
        
        // 验证配置
        if (!validate_config()) {
            return false;
        }
        
        // 通知配置变更
        notify_config_change();
        
        return true;
        
    } catch (const std::exception& e) {
        set_error("Exception parsing JSON config: " + std::string(e.what()));
        return false;
    }
}

nlohmann::json ConfigManager::to_json() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return create_json_config();
}

const ConfigManager::SystemConfig& ConfigManager::get_system_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return system_config_;
}

void ConfigManager::set_system_config(const SystemConfig& config) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        system_config_ = config;
    }
    
    notify_config_change();
}

// 暂时注释掉to_exchange_config方法实现，避免依赖问题
/*
trading::ExchangeConfig ConfigManager::to_exchange_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    trading::ExchangeConfig exchange_config;
    exchange_config.apiKey = system_config_.api_key;
    exchange_config.apiSecret = system_config_.api_secret;
    exchange_config.testnet = system_config_.testnet;
    exchange_config.syncIntervalMs = system_config_.sync_interval_ms;
    exchange_config.timeoutMs = system_config_.timeout_ms;
    
    // 设置基础URL
    if (!system_config_.base_urls.spot.live.empty()) {
        exchange_config.baseUrls["spot"]["live"] = system_config_.base_urls.spot.live;
    }
    if (!system_config_.base_urls.spot.testnet.empty()) {
        exchange_config.baseUrls["spot"]["testnet"] = system_config_.base_urls.spot.testnet;
    }
    if (!system_config_.base_urls.futures.live.empty()) {
        exchange_config.baseUrls["futures"]["live"] = system_config_.base_urls.futures.live;
    }
    if (!system_config_.base_urls.futures.testnet.empty()) {
        exchange_config.baseUrls["futures"]["testnet"] = system_config_.base_urls.futures.testnet;
    }
    
    return exchange_config;
}
*/







bool ConfigManager::validate_config() const {
    validation_errors_.clear();
    
    try {
        validate_api_credentials();
        validate_trading_parameters();
        validate_system_parameters();
        
        return validation_errors_.empty();
        
    } catch (const std::exception& e) {
        validation_errors_.push_back("Validation exception: " + std::string(e.what()));
        return false;
    }
}

std::vector<std::string> ConfigManager::get_validation_errors() const {
    return validation_errors_;
}

void ConfigManager::load_from_environment() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // 加载环境变量
    std::string api_key = get_environment_variable("BINANCE_API_KEY");
    if (!api_key.empty()) {
        system_config_.api_key = api_key;
    }
    
    std::string api_secret = get_environment_variable("BINANCE_API_SECRET");
    if (!api_secret.empty()) {
        system_config_.api_secret = api_secret;
    }
    
    std::string testnet = get_environment_variable("BINANCE_TESTNET");
    if (!testnet.empty()) {
        system_config_.testnet = (testnet == "true" || testnet == "1");
    }
    
    std::string trading_type = get_environment_variable("TRADING_TYPE");
    if (!trading_type.empty()) {
        // Parse comma-separated trading types
        system_config_.trading_type.clear();
        std::stringstream ss(trading_type);
        std::string item;
        while (std::getline(ss, item, ',')) {
            // Trim whitespace
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            if (!item.empty()) {
                system_config_.trading_type.push_back(item);
            }
        }
    }
    
    std::string log_level = get_environment_variable("LOG_LEVEL");
    if (!log_level.empty()) {
        system_config_.log_level = log_level;
    }
}

void ConfigManager::set_environment_variable(const std::string& key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
}

std::string ConfigManager::get_environment_variable(const std::string& key, const std::string& default_value) const {
    const char* value = std::getenv(key.c_str());
    return value ? std::string(value) : default_value;
}

void ConfigManager::enable_hot_reload(const std::string& config_file) {
    if (hot_reload_enabled_.load()) {
        disable_hot_reload();
    }
    
    watched_config_file_ = config_file;
    hot_reload_enabled_.store(true);
    stop_file_watcher_.store(false);
    
    // 记录初始修改时间
    if (std::filesystem::exists(config_file)) {
        last_modified_time_ = std::filesystem::last_write_time(config_file);
    }
    
    // 启动文件监控线程
    file_watcher_thread_ = std::make_unique<std::thread>(&ConfigManager::file_watcher_thread, this);
}

void ConfigManager::disable_hot_reload() {
    if (!hot_reload_enabled_.load()) {
        return;
    }
    
    hot_reload_enabled_.store(false);
    stop_file_watcher_.store(true);
    
    if (file_watcher_thread_ && file_watcher_thread_->joinable()) {
        file_watcher_thread_->join();
    }
    file_watcher_thread_.reset();
}

bool ConfigManager::is_hot_reload_enabled() const {
    return hot_reload_enabled_.load();
}

void ConfigManager::set_config_change_callback(ConfigChangeCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    config_change_callback_ = callback;
}

std::string ConfigManager::get_last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

// 私有方法实现
void ConfigManager::parse_json_config(const nlohmann::json& json) {
    // 系统配置
    if (json.contains("system")) {
        auto system = json["system"];
        if (system.contains("name")) {
            system_config_.system_name = system["name"];
        }
        if (system.contains("version")) {
            system_config_.version = system["version"];
        }
        if (system.contains("log_level")) {
            system_config_.log_level = system["log_level"];
        }
        if (system.contains("max_threads")) {
            system_config_.max_threads = system["max_threads"];
        }
    }
    
    // 信号传递模式
    if (json.contains("signaltrans_mode")) {
        system_config_.signaltrans_mode = json["signaltrans_mode"];
    }
    
    // 共享内存配置
    if (json.contains("shared_memory_config")) {
        auto shared_memory = json["shared_memory_config"];
        if (shared_memory.contains("buffer_size")) {
            system_config_.buffer_size = shared_memory["buffer_size"];
        }
        if (shared_memory.contains("max_signals")) {
            system_config_.max_signals = shared_memory["max_signals"];
        }
        if (shared_memory.contains("signal_buffer_size")) {
            system_config_.signal_buffer_size = shared_memory["signal_buffer_size"];
        }
        if (shared_memory.contains("order_report_buffer_size")) {
            system_config_.order_report_buffer_size = shared_memory["order_report_buffer_size"];
        }
        if (shared_memory.contains("cleanup_interval_ms")) {
            system_config_.cleanup_interval_ms = shared_memory["cleanup_interval_ms"];
        }
    }
    
    // JSON文件配置
    if (json.contains("json_file_config")) {
        auto json_file = json["json_file_config"];
        if (json_file.contains("position_file")) {
            system_config_.position_file = json_file["position_file"];
        }
        if (json_file.contains("update_interval_ms")) {
            system_config_.update_interval_ms = json_file["update_interval_ms"];
        }
        if (json_file.contains("tolerance_threshold")) {
            system_config_.tolerance_threshold = json_file["tolerance_threshold"];
        }
    }
    
    // 仓位同步配置
    if (json.contains("position_sync_config")) {
        auto position_sync = json["position_sync_config"];
        if (position_sync.contains("precision_tolerance")) {
            system_config_.precision_tolerance = position_sync["precision_tolerance"];
        }
        if (position_sync.contains("max_position_diff")) {
            system_config_.max_position_diff = position_sync["max_position_diff"];
        }
        if (position_sync.contains("sync_timeout_ms")) {
            system_config_.sync_timeout_ms = position_sync["sync_timeout_ms"];
        }
    }
    
    // 交易配置
    if (json.contains("trading")) {
        auto trading = json["trading"];
        if (trading.contains("trading_exchanges") && trading["trading_exchanges"].is_array()) {
            system_config_.trading_exchanges.clear();
            for (const auto& exchange : trading["trading_exchanges"]) {
                system_config_.trading_exchanges.push_back(exchange.get<std::string>());
            }
        }
        if (trading.contains("trading_type") && trading["trading_type"].is_array()) {
            system_config_.trading_type.clear();
            for (const auto& type : trading["trading_type"]) {
                system_config_.trading_type.push_back(type.get<std::string>());
            }
        } else if (trading.contains("trading_type") && trading["trading_type"].is_string()) {
            // 向后兼容：如果是字符串，转换为数组
            system_config_.trading_type.clear();
            system_config_.trading_type.push_back(trading["trading_type"].get<std::string>());
        }
        if (trading.contains("default_quantity")) {
            system_config_.default_quantity = trading["default_quantity"];
        }
        if (trading.contains("max_order_size")) {
            system_config_.max_order_size = trading["max_order_size"];
        }
        if (trading.contains("enable_risk_control")) {
            system_config_.enable_risk_control = trading["enable_risk_control"];
        }
        if (trading.contains("enable_position_tracking")) {
            system_config_.enable_position_tracking = trading["enable_position_tracking"];
        }
        if (trading.contains("enable_algorithm_execution")) {
            system_config_.enable_algorithm_execution = trading["enable_algorithm_execution"];
        }
        if (trading.contains("enable_order_feedback")) {
            system_config_.enable_order_feedback = trading["enable_order_feedback"];
        }
    }
    
    // Binance API配置 - 从exchanges.binance读取
    if (json.contains("exchanges") && json["exchanges"].contains("binance")) {
        auto binance = json["exchanges"]["binance"];
        if (binance.contains("api_key")) {
            system_config_.api_key = binance["api_key"];
        }
        if (binance.contains("api_secret")) {
            system_config_.api_secret = binance["api_secret"];
        }
        if (binance.contains("testnet")) {
            system_config_.testnet = binance["testnet"];
        }
        if (binance.contains("enable_websocket")) {
            system_config_.enable_websocket = binance["enable_websocket"];
        }
        if (binance.contains("enable_user_data_stream")) {
            system_config_.enable_user_data_stream = binance["enable_user_data_stream"];
        }
        if (binance.contains("sync_interval_ms")) {
            system_config_.sync_interval_ms = binance["sync_interval_ms"];
        }
        if (binance.contains("timeout_ms")) {
            system_config_.timeout_ms = binance["timeout_ms"];
        }
        if (binance.contains("base_urls")) {
            auto base_urls = binance["base_urls"];
            if (base_urls.contains("spot")) {
                auto spot = base_urls["spot"];
                if (spot.contains("live")) {
                    system_config_.base_urls.spot.live = spot["live"];
                }
                if (spot.contains("testnet")) {
                    system_config_.base_urls.spot.testnet = spot["testnet"];
                }
            }
            if (base_urls.contains("futures")) {
                auto futures = base_urls["futures"];
                if (futures.contains("live")) {
                    system_config_.base_urls.futures.live = futures["live"];
                }
                if (futures.contains("testnet")) {
                    system_config_.base_urls.futures.testnet = futures["testnet"];
                }
            }
        }
    }
    
    // 执行控制器配置
    if (json.contains("execution")) {
        auto execution = json["execution"];
        if (execution.contains("worker_thread_count")) {
            system_config_.worker_thread_count = execution["worker_thread_count"];
        }
        if (execution.contains("signal_processing_interval_ms")) {
            system_config_.signal_processing_interval_ms = execution["signal_processing_interval_ms"];
        }
        if (execution.contains("heartbeat_interval_ms")) {
            system_config_.heartbeat_interval_ms = execution["heartbeat_interval_ms"];
        }
        if (execution.contains("statistics_update_interval_ms")) {
            system_config_.statistics_update_interval_ms = execution["statistics_update_interval_ms"];
        }
    }
    
    // TWAP算法配置
    if (json.contains("twap_algorithm")) {
        auto twap = json["twap_algorithm"];
        if (twap.contains("quantity_threshold")) {
            system_config_.twap_quantity_threshold = twap["quantity_threshold"];
        }
        if (twap.contains("value_threshold")) {
            system_config_.twap_value_threshold = twap["value_threshold"];
        }
        if (twap.contains("market_impact_threshold")) {
            system_config_.twap_market_impact_threshold = twap["market_impact_threshold"];
        }
        if (twap.contains("default_duration_minutes")) {
            system_config_.twap_default_duration_minutes = twap["default_duration_minutes"];
        }
        if (twap.contains("min_slice_size")) {
            system_config_.twap_min_slice_size = twap["min_slice_size"];
        }
        if (twap.contains("max_slices")) {
            system_config_.twap_max_slices = twap["max_slices"];
        }
        if (twap.contains("default_participation_rate")) {
            system_config_.twap_default_participation_rate = twap["default_participation_rate"];
        }
        if (twap.contains("max_price_deviation_bps")) {
            system_config_.twap_max_price_deviation_bps = twap["max_price_deviation_bps"];
        }
    }
    
    // 风险控制配置
    if (json.contains("risk_control")) {
        auto risk = json["risk_control"];
        if (risk.contains("max_position_size")) {
            system_config_.risk_max_position_size = risk["max_position_size"];
        }
        if (risk.contains("max_order_size")) {
            system_config_.risk_max_order_size = risk["max_order_size"];
        }
        if (risk.contains("max_daily_loss")) {
            system_config_.risk_max_daily_loss = risk["max_daily_loss"];
        }
        if (risk.contains("max_total_exposure")) {
            system_config_.risk_max_total_exposure = risk["max_total_exposure"];
        }
        if (risk.contains("max_orders_per_second")) {
            system_config_.risk_max_orders_per_second = risk["max_orders_per_second"];
        }
        if (risk.contains("max_orders_per_minute")) {
            system_config_.risk_max_orders_per_minute = risk["max_orders_per_minute"];
        }
    }
    
    // 市场数据配置
    if (json.contains("market_data")) {
        auto market_data = json["market_data"];
        if (market_data.contains("update_interval_ms")) {
            system_config_.market_data_update_interval_ms = market_data["update_interval_ms"];
        }
        if (market_data.contains("price_volatility")) {
            system_config_.price_volatility = market_data["price_volatility"];
        }
        if (market_data.contains("volume_volatility")) {
            system_config_.volume_volatility = market_data["volume_volatility"];
        }
        if (market_data.contains("spread_ratio")) {
            system_config_.spread_ratio = market_data["spread_ratio"];
        }
    }
    
    // 日志配置
    if (json.contains("logging")) {
        auto logging = json["logging"];
        if (logging.contains("file")) {
            system_config_.log_file = logging["file"];
        }
        if (logging.contains("console")) {
            system_config_.console_output = logging["console"];
        }
        if (logging.contains("max_file_size_mb")) {
            system_config_.max_file_size_mb = logging["max_file_size_mb"];
        }
        if (logging.contains("max_files")) {
            system_config_.max_files = logging["max_files"];
        }
        if (logging.contains("async_logging")) {
            system_config_.async_logging = logging["async_logging"];
        }
    }
    
    // 监控配置
    if (json.contains("monitoring")) {
        auto monitoring = json["monitoring"];
        if (monitoring.contains("enable_performance_monitoring")) {
            system_config_.enable_performance_monitoring = monitoring["enable_performance_monitoring"];
        }
        if (monitoring.contains("enable_buffer_monitoring")) {
            system_config_.enable_buffer_monitoring = monitoring["enable_buffer_monitoring"];
        }
        if (monitoring.contains("enable_system_monitoring")) {
            system_config_.enable_system_monitoring = monitoring["enable_system_monitoring"];
        }
        if (monitoring.contains("monitoring_interval_ms")) {
            system_config_.monitoring_interval_ms = monitoring["monitoring_interval_ms"];
        }
        if (monitoring.contains("alert_threshold_percent")) {
            system_config_.alert_threshold_percent = monitoring["alert_threshold_percent"];
        }
    }
}

nlohmann::json ConfigManager::create_json_config() const {
    nlohmann::json json;
    
    // 系统配置
    json["system"]["name"] = system_config_.system_name;
    json["system"]["version"] = system_config_.version;
    json["system"]["log_level"] = system_config_.log_level;
    json["system"]["max_threads"] = system_config_.max_threads;
    
    // 信号传递模式
    json["signaltrans_mode"] = system_config_.signaltrans_mode;
    json["description"] = "Signal trans mode: 0=shared memory, 1=json file";
    
    // 共享内存配置
    json["shared_memory_config"]["buffer_size"] = system_config_.buffer_size;
    json["shared_memory_config"]["max_signals"] = system_config_.max_signals;
    json["shared_memory_config"]["signal_buffer_size"] = system_config_.signal_buffer_size;
    json["shared_memory_config"]["order_report_buffer_size"] = system_config_.order_report_buffer_size;
    json["shared_memory_config"]["cleanup_interval_ms"] = system_config_.cleanup_interval_ms;
    
    // JSON文件配置
    json["json_file_config"]["position_file"] = system_config_.position_file;
    json["json_file_config"]["update_interval_ms"] = system_config_.update_interval_ms;
    json["json_file_config"]["tolerance_threshold"] = system_config_.tolerance_threshold;
    
    // 仓位同步配置
    json["position_sync_config"]["precision_tolerance"] = system_config_.precision_tolerance;
    json["position_sync_config"]["max_position_diff"] = system_config_.max_position_diff;
    json["position_sync_config"]["sync_timeout_ms"] = system_config_.sync_timeout_ms;
    
    // 交易配置
    json["trading"]["trading_exchanges"] = system_config_.trading_exchanges;
    json["trading"]["trading_type"] = system_config_.trading_type;
    json["trading"]["default_quantity"] = system_config_.default_quantity;
    json["trading"]["max_order_size"] = system_config_.max_order_size;
    json["trading"]["enable_risk_control"] = system_config_.enable_risk_control;
    json["trading"]["enable_position_tracking"] = system_config_.enable_position_tracking;
    json["trading"]["enable_algorithm_execution"] = system_config_.enable_algorithm_execution;
    json["trading"]["enable_order_feedback"] = system_config_.enable_order_feedback;
    
    // Binance API配置 - 保存到exchanges.binance
    json["exchanges"]["binance"]["api_key"] = system_config_.api_key;
    json["exchanges"]["binance"]["api_secret"] = system_config_.api_secret;
    json["exchanges"]["binance"]["testnet"] = system_config_.testnet;
    json["exchanges"]["binance"]["enable_websocket"] = system_config_.enable_websocket;
    json["exchanges"]["binance"]["enable_user_data_stream"] = system_config_.enable_user_data_stream;
    json["exchanges"]["binance"]["sync_interval_ms"] = system_config_.sync_interval_ms;
    json["exchanges"]["binance"]["timeout_ms"] = system_config_.timeout_ms;
    json["exchanges"]["binance"]["base_urls"]["spot"]["live"] = system_config_.base_urls.spot.live;
    json["exchanges"]["binance"]["base_urls"]["spot"]["testnet"] = system_config_.base_urls.spot.testnet;
    json["exchanges"]["binance"]["base_urls"]["futures"]["live"] = system_config_.base_urls.futures.live;
    json["exchanges"]["binance"]["base_urls"]["futures"]["testnet"] = system_config_.base_urls.futures.testnet;
    
    // 执行控制器配置
    json["execution"]["worker_thread_count"] = system_config_.worker_thread_count;
    json["execution"]["signal_processing_interval_ms"] = system_config_.signal_processing_interval_ms;
    json["execution"]["heartbeat_interval_ms"] = system_config_.heartbeat_interval_ms;
    json["execution"]["statistics_update_interval_ms"] = system_config_.statistics_update_interval_ms;
    
    // TWAP算法配置
    json["twap_algorithm"]["quantity_threshold"] = system_config_.twap_quantity_threshold;
    json["twap_algorithm"]["value_threshold"] = system_config_.twap_value_threshold;
    json["twap_algorithm"]["market_impact_threshold"] = system_config_.twap_market_impact_threshold;
    json["twap_algorithm"]["default_duration_minutes"] = system_config_.twap_default_duration_minutes;
    json["twap_algorithm"]["min_slice_size"] = system_config_.twap_min_slice_size;
    json["twap_algorithm"]["max_slices"] = system_config_.twap_max_slices;
    json["twap_algorithm"]["default_participation_rate"] = system_config_.twap_default_participation_rate;
    json["twap_algorithm"]["max_price_deviation_bps"] = system_config_.twap_max_price_deviation_bps;
    
    // 风险控制配置
    json["risk_control"]["max_position_size"] = system_config_.risk_max_position_size;
    json["risk_control"]["max_order_size"] = system_config_.risk_max_order_size;
    json["risk_control"]["max_daily_loss"] = system_config_.risk_max_daily_loss;
    json["risk_control"]["max_total_exposure"] = system_config_.risk_max_total_exposure;
    json["risk_control"]["max_orders_per_second"] = system_config_.risk_max_orders_per_second;
    json["risk_control"]["max_orders_per_minute"] = system_config_.risk_max_orders_per_minute;
    
    // 市场数据配置
    json["market_data"]["update_interval_ms"] = system_config_.market_data_update_interval_ms;
    json["market_data"]["price_volatility"] = system_config_.price_volatility;
    json["market_data"]["volume_volatility"] = system_config_.volume_volatility;
    json["market_data"]["spread_ratio"] = system_config_.spread_ratio;
    
    // 日志配置
    json["logging"]["file"] = system_config_.log_file;
    json["logging"]["console"] = system_config_.console_output;
    json["logging"]["max_file_size_mb"] = system_config_.max_file_size_mb;
    json["logging"]["max_files"] = system_config_.max_files;
    json["logging"]["async_logging"] = system_config_.async_logging;
    
    // 监控配置
    json["monitoring"]["enable_performance_monitoring"] = system_config_.enable_performance_monitoring;
    json["monitoring"]["enable_buffer_monitoring"] = system_config_.enable_buffer_monitoring;
    json["monitoring"]["enable_system_monitoring"] = system_config_.enable_system_monitoring;
    json["monitoring"]["monitoring_interval_ms"] = system_config_.monitoring_interval_ms;
    json["monitoring"]["alert_threshold_percent"] = system_config_.alert_threshold_percent;
    
    return json;
}

void ConfigManager::validate_api_credentials() const {
    // 检查是否启用了Binance交易
    bool binance_enabled = std::find(system_config_.trading_exchanges.begin(), 
                                   system_config_.trading_exchanges.end(), 
                                   "binance") != system_config_.trading_exchanges.end();
    
    if (binance_enabled) {
        if (system_config_.api_key.empty()) {
            validation_errors_.push_back("Binance API key is required when trading is enabled");
        }
        if (system_config_.api_secret.empty()) {
            validation_errors_.push_back("Binance API secret is required when trading is enabled");
        }
    }
}

void ConfigManager::validate_trading_parameters() const {
    if (system_config_.default_quantity <= 0) {
        validation_errors_.push_back("Default quantity must be positive");
    }
    if (system_config_.max_order_size <= 0) {
        validation_errors_.push_back("Max order size must be positive");
    }
    if (system_config_.default_quantity > system_config_.max_order_size) {
        validation_errors_.push_back("Default quantity cannot exceed max order size");
    }
    // 验证trading_type数组中的每个值
    for (const auto& type : system_config_.trading_type) {
        if (type != "spot" && type != "futures") {
            validation_errors_.push_back("Trading type must be 'spot' or 'futures', found: " + type);
        }
    }
}

void ConfigManager::validate_system_parameters() const {
    if (system_config_.max_threads == 0) {
        validation_errors_.push_back("Max threads must be greater than 0");
    }
    if (system_config_.signal_buffer_size == 0) {
        validation_errors_.push_back("Signal buffer size must be greater than 0");
    }
    if (system_config_.order_report_buffer_size == 0) {
        validation_errors_.push_back("Order report buffer size must be greater than 0");
    }
    if (system_config_.sync_interval_ms == 0) {
        validation_errors_.push_back("Sync interval must be greater than 0");
    }
    if (system_config_.timeout_ms == 0) {
        validation_errors_.push_back("Timeout must be greater than 0");
    }
}

void ConfigManager::set_error(const std::string& error) const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

void ConfigManager::notify_config_change() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (config_change_callback_) {
        config_change_callback_(system_config_);
    }
}

void ConfigManager::file_watcher_thread() {
    while (!stop_file_watcher_.load()) {
        try {
            if (check_file_modified(watched_config_file_)) {
                // 文件已修改，重新加载配置
                if (load_config(watched_config_file_)) {
                    std::cout << "Config file reloaded: " << watched_config_file_ << std::endl;
                } else {
                    std::cerr << "Failed to reload config file: " << watched_config_file_ << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in file watcher: " << e.what() << std::endl;
        }
        
        // 等待1秒后再次检查
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool ConfigManager::check_file_modified(const std::string& file_path) const {
    if (!std::filesystem::exists(file_path)) {
        return false;
    }
    
    auto current_time = std::filesystem::last_write_time(file_path);
    if (current_time > last_modified_time_) {
        const_cast<ConfigManager*>(this)->last_modified_time_ = current_time;
        return true;
    }
    
    return false;
}

// 全局配置管理器实现
std::unique_ptr<ConfigManager> GlobalConfigManager::instance_;
std::once_flag GlobalConfigManager::init_flag_;

ConfigManager& GlobalConfigManager::instance() {
    std::call_once(init_flag_, []() {
        instance_ = std::make_unique<ConfigManager>();
    });
    return *instance_;
}

bool GlobalConfigManager::initialize(const std::string& config_file) {
    auto& config_manager = instance();
    
    // 首先从环境变量加载
    config_manager.load_from_environment();
    
    // 如果提供了配置文件，则加载它
    if (!config_file.empty()) {
        return config_manager.load_config(config_file);
    }
    
    return true;
}

void GlobalConfigManager::cleanup() {
    if (instance_) {
        instance_->disable_hot_reload();
        instance_.reset();
    }
}

} // namespace execution
} // namespace tes