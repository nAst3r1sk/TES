#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <fstream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <signal.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <set>
#include <filesystem>
#include <cmath>
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXHttpClient.h>

#include "crypto/crypto_modified.hpp"
#include "execution/types.h"
#include "execution/order_manager.h"
#include "execution/order_state_machine.h"
#include "3rd/gateway/include/binance_websocket.h"
#include "3rd/gateway/include/data_structures.h"
#include "3rd/gateway/include/config_manager.h"
#include "3rd/gateway/include/exchange_interface.h"

using namespace tes::execution;
using namespace trading;

std::atomic<bool> g_running(true);
std::atomic<bool> g_processing_positions(false);
std::mutex g_file_mutex;

// 交易规则结构
struct TradingRule {
    std::string symbol;
    int quantityPrecision;
    int pricePrecision;
    double minQty;
    double maxQty;
    double stepSize;
    double tickSize;
    double minNotional;
    
    TradingRule() : quantityPrecision(0), pricePrecision(0), minQty(0.0), 
                   maxQty(0.0), stepSize(0.0), tickSize(0.0), minNotional(0.0) {}
};

// 交易规则管理器
class TradingRuleManager {
public:
    static TradingRuleManager& getInstance() {
        static TradingRuleManager instance;
        return instance;
    }
    
    bool loadExchangeInfo(const std::string& apiKey, const std::string& apiSecret, bool testnet = false);
    TradingRule getTradingRule(const std::string& symbol) const;
    double formatQuantity(const std::string& symbol, double quantity) const;
    double formatPrice(const std::string& symbol, double price) const;
    bool isValidOrder(const std::string& symbol, double quantity, double price) const;
    
private:
    std::unordered_map<std::string, TradingRule> trading_rules_;
    std::mutex rules_mutex_;
    
    std::string makeHttpRequest(const std::string& url, const std::string& apiKey) const;
    bool parseExchangeInfo(const std::string& jsonData);
    bool saveExchangeInfoToFile(const std::string& jsonData) const;
};

// 系统配置结构
struct SystemConfig {
    std::string name;
    std::string version;
    int max_threads;
    int signaltrans_mode;
    std::string position_file;
    int update_interval_ms;
    double tolerance_threshold;
    bool enable_auto_sync;
    std::string output_directory;
    std::string filename_prefix;
    bool append_timestamp;
    bool pretty_print;
    std::vector<std::string> trading_exchanges;
    std::vector<std::string> trading_type;
    std::string signature_type;
    std::string ed25519_api_key;
    std::string ed25519_api_secret;
    std::string hmac_api_key;
    std::string hmac_api_secret;
    bool testnet;
    int timeout_ms;
    std::string websocket_endpoint;
    double quantity_threshold;
    double value_threshold;
    double market_impact_threshold;
    int default_duration_minutes;
    double min_slice_size;
    int max_slices;
    double default_participation_rate;
    int max_price_deviation_bps;
    
    SystemConfig() : max_threads(8), signaltrans_mode(1), update_interval_ms(100),
                     tolerance_threshold(0.001), enable_auto_sync(true),
                     output_directory("./output"), filename_prefix("order_feedback"),
                     append_timestamp(true), pretty_print(true), testnet(false),
                     timeout_ms(30000), quantity_threshold(10000.0),
                     value_threshold(1000000.0), market_impact_threshold(0.05),
                     default_duration_minutes(30), min_slice_size(100.0),
                     max_slices(200), default_participation_rate(0.2),
                     max_price_deviation_bps(50) {}
};

// 目标仓位信息结构
struct TargetPosition {
    int id;
    std::string symbol;
    double quantity;
    
    TargetPosition() : id(0), quantity(0.0) {}
    TargetPosition(int i, const std::string& s, double q) : id(i), symbol(s), quantity(q) {}
};

// 当前仓位信息结构
struct CurrentPosition {
    std::string symbol;
    double quantity;
    double entry_price;
    double unrealized_pnl;
    std::chrono::high_resolution_clock::time_point last_update;
    
    CurrentPosition() : quantity(0.0), entry_price(0.0), unrealized_pnl(0.0) {
        last_update = std::chrono::high_resolution_clock::now();
    }
};

// 行情数据结构
struct MarketDepth {
    std::string symbol;
    double bid_price;
    double ask_price;
    double bid_volume;
    double ask_volume;
    std::chrono::high_resolution_clock::time_point timestamp;
    
    MarketDepth() : bid_price(0.0), ask_price(0.0), bid_volume(0.0), ask_volume(0.0) {
        timestamp = std::chrono::high_resolution_clock::now();
    }
};

// TWAP订单结构
struct TWAPOrder {
    std::string symbol;
    double total_quantity;
    double remaining_quantity;
    double unfilled_quantity;        // 新增：累积的未成交数量
    std::string side;
    double target_price;
    int slices_count;
    int current_slice;               // 新增：当前切片索引
    std::chrono::milliseconds slice_interval;
    bool is_active;
    bool is_final_slice;             // 新增：是否为最后切片
    std::vector<std::string> pending_order_ids; // 新增：待处理订单ID列表
    std::chrono::time_point<std::chrono::steady_clock> slice_start_time; // 新增：切片开始时间
    
    TWAPOrder() : total_quantity(0.0), remaining_quantity(0.0), unfilled_quantity(0.0),
                  target_price(0.0), slices_count(0), current_slice(0),
                  slice_interval(30000), is_active(false), is_final_slice(false) {}
};

// 信号处理
void signal_handler(int signal)
{
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

class TradingSystemManager {
public:
    TradingSystemManager() 
        : position_file_path_("config/pos_update.json")
        , config_file_path_("config/system_config.json")
        , output_dir_("output")
        , last_file_check_time_(std::chrono::steady_clock::now())
        , is_finished_(false)
        , gateway_connected_(false)
        , positions_updated_(false)
        , market_data_updated_(false)
    {
        // 初始化订单管理器
        order_manager_ = std::unique_ptr<OrderManager>(new OrderManager());
        order_manager_->initialize();
        order_manager_->start();
        
        // 设置订单事件回调
        order_manager_->set_order_event_callback([this](const Order& order) {
            this->on_order_event(order);
        });
    }

    ~TradingSystemManager()
    {
        if (order_manager_) {
            order_manager_->stop();
        }
        cleanup();
    }

    bool initialize()
    {
        try {
            // 1. 读取系统配置文件
            if (!load_system_config()) {
                std::cerr << "Failed to load system configuration" << std::endl;
                return false;
            }
            
            // 2. 动态获取交易规则信息
            std::cout << "Loading exchange trading rules..." << std::endl;
            auto& ruleManager = TradingRuleManager::getInstance();
            if (!ruleManager.loadExchangeInfo(system_config_.ed25519_api_key, 
                                            system_config_.ed25519_api_secret, 
                                            system_config_.testnet)) {
                std::cerr << "Failed to load exchange trading rules" << std::endl;
                return false;
            }
            std::cout << "Exchange trading rules loaded successfully" << std::endl;
            
            // 3. 创建输出目录
            if (!create_directory(system_config_.output_directory)) {
                std::cerr << "Failed to create output directory: " << system_config_.output_directory << std::endl;
                return false;
            }

            // 4. 初始化订单状态机
            order_state_machine_.reset(new OrderStateMachine());
            OrderStateMachine::Config osm_config;
            osm_config.default_submit_timeout = std::chrono::milliseconds(5000);
            osm_config.default_cancel_timeout = std::chrono::milliseconds(3000);
            osm_config.max_retry_count = 3;
            osm_config.enable_auto_cleanup = true;
            
            if (!order_state_machine_->initialize(osm_config)) {
                std::cerr << "Failed to initialize order state machine" << std::endl;
                return false;
            }
            
            if (!order_state_machine_->start()) {
                std::cerr << "Failed to start order state machine" << std::endl;
                return false;
            }
            
            std::cout << "Order state machine initialized and started successfully" << std::endl;

            // 5. 初始化Gateway接口
            if (!initialize_gateway()) {
                std::cerr << "Failed to initialize gateway interface" << std::endl;
                return false;
            }

            std::cout << "Trading system initialized successfully" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Exception during initialization: " << e.what() << std::endl;
            return false;
        }
    }

    bool start()
    {
        try {
            std::cout << "Starting trading system..." << std::endl;
            
            // 启动账户更新线程
            std::cout << "Creating account update thread..." << std::endl;
            account_update_thread_.reset(new std::thread(&TradingSystemManager::account_update_worker, this));
            std::cout << "Account update thread created" << std::endl;
            
            // 启动仓位监控线程
            std::cout << "Creating position monitor thread..." << std::endl;
            position_monitor_thread_.reset(new std::thread(&TradingSystemManager::position_monitor_worker, this));
            std::cout << "Position monitor thread created" << std::endl;

            // 初始化市场数据订阅
            std::cout << "Initializing market subscriptions..." << std::endl;
            update_market_subscriptions();
            std::cout << "Market subscriptions initialized" << std::endl;

            std::cout << "Trading system started successfully" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Exception during start: " << e.what() << std::endl;
            return false;
        }
    }

    void run()
    {
        std::cout << "Trading system running..." << std::endl;
        
        while (g_running.load()) {
            try {
                // 主循环逻辑
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (const std::exception& e) {
                std::cerr << "Exception in main loop: " << e.what() << std::endl;
                return; // 失败后直接退出
            }
        }
    }

    void stop()
    {
        std::cout << "Stopping trading system..." << std::endl;
        g_running.store(false);

        if (account_update_thread_ && account_update_thread_->joinable()) {
            account_update_thread_->join();
        }
        
        if (position_monitor_thread_ && position_monitor_thread_->joinable()) {
            position_monitor_thread_->join();
        }

        // 断开Gateway连接
        if (binance_ws_) {
            binance_ws_->disconnect();
        }

        std::cout << "Trading system stopped" << std::endl;
    }

    void cleanup()
    {
        stop();
    }

private:
    // 成员变量
    std::string position_file_path_;
    std::string config_file_path_;
    std::string output_dir_;
    std::chrono::steady_clock::time_point last_file_check_time_;
    bool is_finished_;
    
    SystemConfig system_config_;
    std::unique_ptr<std::thread> account_update_thread_;
    std::unique_ptr<std::thread> position_monitor_thread_;
    
    std::vector<TargetPosition> target_positions_;
    std::mutex target_positions_mutex_;
    
    // 当前仓位信息存储
    std::unordered_map<std::string, CurrentPosition> current_positions_;
    std::mutex current_positions_mutex_;
    
    // 行情数据存储
    std::unordered_map<std::string, MarketDepth> market_depths_;
    std::mutex market_depths_mutex_;
    
    // TWAP订单管理
    std::vector<TWAPOrder> active_twap_orders_;
    std::mutex twap_orders_mutex_;
    
    // Gateway接口集成
    std::shared_ptr<IExchangeWebSocket> binance_ws_;
    std::mutex gateway_mutex_;
    bool gateway_connected_;
    
    // 当前订阅的交易对集合
    std::set<std::string> subscribed_symbols_;
    std::mutex subscribed_symbols_mutex_;
    
    // 实时数据接收标志
    std::atomic<bool> positions_updated_;
    std::atomic<bool> market_data_updated_;

    // 事件驱动架构 - 条件变量和同步机制
    std::condition_variable account_update_cv_;
    std::mutex account_update_mutex_;
    std::atomic<bool> account_data_ready_{false};
    
    std::condition_variable order_completion_cv_;
    std::mutex order_completion_mutex_;
    std::atomic<bool> order_completed_{false};
    
    std::condition_variable position_alignment_cv_;
    std::mutex position_alignment_mutex_;
    std::atomic<bool> position_alignment_completed_{false};
    
    // 超时配置
    static constexpr auto ACCOUNT_UPDATE_TIMEOUT = std::chrono::seconds(10);
    static constexpr auto ORDER_COMPLETION_TIMEOUT = std::chrono::seconds(15);
    static constexpr auto POSITION_ALIGNMENT_TIMEOUT = std::chrono::seconds(5);

    // 订单管理器
    std::unique_ptr<OrderManager> order_manager_;
    std::unique_ptr<OrderStateMachine> order_state_machine_;
    std::mutex pending_orders_mutex_;
    std::set<std::string> pending_orders_;
    
    // 订单错误记录
    std::unordered_map<std::string, std::string> order_errors_;
    std::mutex order_errors_mutex_;

    // 初始化Gateway接口
    bool initialize_gateway()
    {
        try {
            // 加载Gateway配置
            trading::ConfigManager& gateway_config = trading::ConfigManager::getInstance();
            if (!gateway_config.loadConfig(config_file_path_)) {
                std::cerr << "Failed to load gateway config from: " << config_file_path_ << std::endl;
                return false;
            }
            
            // 获取Binance配置
            if (!gateway_config.hasExchangeConfig("binance")) {
                std::cerr << "No binance configuration found in gateway config" << std::endl;
                return false;
            }
            
            ExchangeConfig binance_config = gateway_config.getExchangeConfig("binance");
            std::cout << "Binance config loaded, testnet: " << (binance_config.testnet ? "true" : "false") << std::endl;
            
            // 使用BinanceFactory创建WebSocket客户端
            BinanceFactory factory;
            auto client = factory.createWebSocketClient("binance");
            
            if (!client) {
                std::cerr << "Failed to create Binance WebSocket client" << std::endl;
                return false;
            }
            
            // 直接使用client，通过move语义转换为shared_ptr
             binance_ws_ = std::shared_ptr<IExchangeWebSocket>(std::move(client));
             
             std::cout << "Created " << binance_ws_->getExchangeName() << " WebSocket client" << std::endl;
             
             // 设置回调函数
             setup_gateway_callbacks(binance_ws_);
             
             // 连接到WebSocket
             if (!binance_ws_->connect()) {
                 std::cerr << "Failed to connect to Binance WebSocket" << std::endl;
                 return false;
             }
             
             // 等待连接稳定
             std::this_thread::sleep_for(std::chrono::seconds(3));
             
             // 连接到WebSocket API并进行认证
             std::cout << "Connecting to WebSocket API..." << std::endl;
             if (!binance_ws_->sessionLogon()) {
                 std::cout << "Warning: WebSocket API connection failed, account queries may not work" << std::endl;
                 // 不返回false，因为用户数据流可能仍然可用
             } else {
                 std::cout << "WebSocket API connected and authenticated successfully!" << std::endl;
             }
            
            gateway_connected_ = true;
            std::cout << "Gateway interface initialized successfully" << std::endl;
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Exception initializing gateway: " << e.what() << std::endl;
            return false;
        }
    }

    // 设置Gateway回调函数
    void setup_gateway_callbacks(std::shared_ptr<IExchangeWebSocket> client)
    {
        // 设置账户信息回调
        client->setAccountInfoCallback([this](const AccountInfoResponse& response) {
            this->on_account_info_received(response);
        });
        
        // 设置账户更新回调
        client->setAccountUpdateCallback([this](const AccountUpdate& update) {
            this->on_account_update_received(update);
        });
        
        // 设置仓位更新回调
        client->setPositionUpdateCallback([this](const PositionUpdate& update) {
            this->on_position_update_received(update);
        });
        
        // 设置深度数据回调
        client->setDepthUpdateCallback([this](const DepthUpdate& update) {
            this->on_depth_update_received(update);
        });
        
        // 设置订单响应回调
        client->setOrderResponseCallback([this](const OrderResponse& response) {
            this->on_order_response_received(response);
        });
        
        // 设置错误回调
        client->setErrorCallback([this](const std::string& error) {
            std::cerr << "Gateway error: " << error << std::endl;
        });
    }

    // Gateway回调处理函数
    // 从pos_update.json获取所有交易对
    std::set<std::string> get_all_symbols_from_config()
    {
        std::set<std::string> symbols;
        
        try {
            nlohmann::json pos_data;
            if (read_position_file(pos_data)) {
                if (pos_data.is_array()) {
                    for (const auto& item : pos_data) {
                        // 只处理包含symbol字段的对象
                        if (item.contains("symbol") && item["symbol"].is_string()) {
                            std::string symbol = item["symbol"];
                            symbols.insert(symbol);
                            std::cout << "Found symbol in config: " << symbol << std::endl;
                        }
                    }
                }
            }
            
            // 如果没有找到任何交易对，使用默认值
            if (symbols.empty()) {
                std::cout << "No symbols found in config, using defaults" << std::endl;
                return symbols;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error reading symbols from config: " << e.what() << std::endl;
            return symbols;
        }
        
        return symbols;
    }

    void on_account_info_received(const AccountInfoResponse& response)
    {
        std::lock_guard<std::mutex> lock(current_positions_mutex_);
        
        std::cout << "[DEBUG] Received account info with " << response.positions.size() << " positions" << std::endl;
        
        // 先记录当前缓存的仓位数据用于对比
        std::cout << "[DEBUG] Current cached positions before update:" << std::endl;
        for (const auto& pair : current_positions_) {
            std::cout << "[DEBUG]   " << pair.first << ": " << pair.second.quantity << std::endl;
        }
        
        // 清空当前仓位信息，确保使用最新的API数据
        current_positions_.clear();
        std::cout << "[DEBUG] Cleared all cached positions" << std::endl;
        
        // 计算每个交易对的净仓位（单向持仓模式）
        std::unordered_map<std::string, double> net_positions;
        
        for (const auto& pos : response.positions) {
            try {
                double position_amt = std::stod(pos.positionAmt);
                std::cout << "[DEBUG] Processing position from API: " << pos.symbol 
                         << " positionSide: " << pos.positionSide 
                         << " positionAmt: " << pos.positionAmt 
                         << " (parsed: " << position_amt << ")" << std::endl;
                
                // 在单向持仓模式下，positionSide应该是"BOTH"，positionAmt直接表示净仓位
                // 正数表示多头，负数表示空头
                // 记录所有仓位，包括零仓位，以确保数据完整性
                net_positions[pos.symbol] = position_amt;
                
                if (std::abs(position_amt) > system_config_.tolerance_threshold) {
                    std::cout << "[DEBUG] Found non-zero position: " << pos.symbol 
                             << " net position: " << position_amt << std::endl;
                } else {
                    std::cout << "[DEBUG] Found zero position: " << pos.symbol 
                             << " net position: " << position_amt << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "[ERROR] Error parsing position amount for " << pos.symbol << ": " << pos.positionAmt 
                         << " - " << e.what() << std::endl;
                continue;
            }
        }
        
        // 获取配置文件中的所有交易对
        std::set<std::string> config_symbols = get_all_symbols_from_config();
        
        // 合并API返回的仓位和配置文件中的交易对
        std::set<std::string> all_symbols = config_symbols;
        for (const auto& pair : net_positions) {
            all_symbols.insert(pair.first);
        }
        
        std::cout << "[DEBUG] Processing " << all_symbols.size() << " symbols total" << std::endl;
        
        // 更新所有交易对的仓位记录，强制使用最新API数据
        for (const auto& symbol : all_symbols) {
            double net_qty = 0.0;
            if (net_positions.find(symbol) != net_positions.end()) {
                net_qty = net_positions[symbol];
            }
            
            // 创建新的仓位记录，确保使用最新数据
            CurrentPosition current_pos;
            current_pos.symbol = symbol;
            current_pos.quantity = net_qty;
            current_pos.entry_price = 0.0;
            current_pos.unrealized_pnl = 0.0;
            current_pos.last_update = std::chrono::high_resolution_clock::now();
            
            // 直接插入新记录，避免查找和更新的竞态条件
            current_positions_[symbol] = current_pos;
            
            std::cout << "[DEBUG] Created/Updated position record: " << symbol 
                     << " quantity: " << net_qty 
                     << " (from " << (net_positions.find(symbol) != net_positions.end() ? "API" : "default") << ")" << std::endl;
        }
        
        std::cout << "[DEBUG] Position update complete. Total positions: " << current_positions_.size() << std::endl;
        
        // 验证关键仓位数据
        std::cout << "[DEBUG] Final position verification:" << std::endl;
        for (const auto& pair : current_positions_) {
            std::cout << "[DEBUG]   " << pair.first << ": " << pair.second.quantity << std::endl;
        }
        
        // 特别验证APRUSDT
        auto aprusdt_it = current_positions_.find("APRUSDT");
        if (aprusdt_it != current_positions_.end()) {
            std::cout << "[CRITICAL] APRUSDT position after update: " << aprusdt_it->second.quantity << std::endl;
        } else {
            std::cout << "[CRITICAL] APRUSDT position not found after update!" << std::endl;
        }
        
        positions_updated_.store(true);
        
        // 事件驱动架构 - 通知等待账户数据的线程
        {
            std::lock_guard<std::mutex> notify_lock(account_update_mutex_);
            account_data_ready_.store(true);
        }
        account_update_cv_.notify_all();
        std::cout << "[DEBUG] Account data ready, notified waiting threads" << std::endl;
    }

    void on_position_update_received(const PositionUpdate& update)
    {
        std::lock_guard<std::mutex> lock(current_positions_mutex_);
        
        try {
            CurrentPosition current_pos;
            current_pos.symbol = update.symbol;
            current_pos.quantity = std::stod(update.positionAmount);
            current_pos.entry_price = std::stod(update.entryPrice);
            current_pos.unrealized_pnl = std::stod(update.unrealizedPnl);
            current_pos.last_update = std::chrono::high_resolution_clock::now();
            
            current_positions_[update.symbol] = current_pos;
            
            std::cout << "Position update received: " << update.symbol 
                     << " quantity: " << current_pos.quantity 
                     << " entry_price: " << current_pos.entry_price 
                     << " unrealized_pnl: " << current_pos.unrealized_pnl << std::endl;
            
            positions_updated_.store(true);
        } catch (const std::exception& e) {
            std::cerr << "Error processing position update for " << update.symbol << ": " << e.what() << std::endl;
        }
    }

    void on_account_update_received(const AccountUpdate& update)
    {
        std::lock_guard<std::mutex> lock(current_positions_mutex_);
        
        std::cout << "[DEBUG] Account update received: eventType=" << update.eventType 
                 << " eventTime=" << update.eventTime 
                 << " transactionTime=" << update.transactionTime 
                 << " updateReason=" << update.updateReason << std::endl;
        
        // 处理仓位更新
        for (const auto& pos : update.positions) {
            try {
                double position_amt = std::stod(pos.positionAmount);
                double entry_price = std::stod(pos.entryPrice);
                double unrealized_pnl = std::stod(pos.unrealizedPnl);
                
                std::cout << "[DEBUG] Processing position from account update: " << pos.symbol 
                         << " positionSide: " << pos.positionSide 
                         << " positionAmount: " << pos.positionAmount 
                         << " (parsed: " << position_amt << ")"
                         << " entryPrice: " << pos.entryPrice 
                         << " unrealizedPnl: " << pos.unrealizedPnl << std::endl;
                
                // 检查是否已存在该交易对的仓位记录
                auto it = current_positions_.find(pos.symbol);
                if (it != current_positions_.end()) {
                    // 更新现有仓位
                    std::cout << "[DEBUG] Updating existing position: " << pos.symbol 
                             << " old quantity: " << it->second.quantity 
                             << " new quantity: " << position_amt << std::endl;
                    
                    it->second.quantity = position_amt;
                    it->second.entry_price = entry_price;
                    it->second.unrealized_pnl = unrealized_pnl;
                    it->second.last_update = std::chrono::high_resolution_clock::now();
                } else {
                    // 创建新的仓位记录
                    std::cout << "[DEBUG] Creating new position record: " << pos.symbol 
                             << " quantity: " << position_amt << std::endl;
                    
                    CurrentPosition current_pos;
                    current_pos.symbol = pos.symbol;
                    current_pos.quantity = position_amt;
                    current_pos.entry_price = entry_price;
                    current_pos.unrealized_pnl = unrealized_pnl;
                    current_pos.last_update = std::chrono::high_resolution_clock::now();
                    
                    current_positions_[pos.symbol] = current_pos;
                }
                
                std::cout << "[DEBUG] Position update completed: " << pos.symbol 
                         << " final quantity: " << current_positions_[pos.symbol].quantity 
                         << " entry_price: " << current_positions_[pos.symbol].entry_price 
                         << " unrealized_pnl: " << current_positions_[pos.symbol].unrealized_pnl << std::endl;
                
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Error processing position from account update for " << pos.symbol << ": " << e.what() << std::endl;
            }
        }
        
        std::cout << "[DEBUG] Account update processing complete. Total positions tracked: " << current_positions_.size() << std::endl;
        positions_updated_.store(true);
    }

    void on_depth_update_received(const DepthUpdate& update)
    {
        std::lock_guard<std::mutex> lock(market_depths_mutex_);
        
        if (!update.bids.empty() && !update.asks.empty()) {
            MarketDepth depth;
            depth.symbol = update.symbol;
            depth.bid_price = std::stod(update.bids[0].price);
            depth.ask_price = std::stod(update.asks[0].price);
            depth.bid_volume = std::stod(update.bids[0].quantity);
            depth.ask_volume = std::stod(update.asks[0].quantity);
            depth.timestamp = std::chrono::high_resolution_clock::now();
            
            market_depths_[update.symbol] = depth;
            
            static std::unordered_map<std::string, bool> first_update;
            if (!first_update[update.symbol]) {
                std::cout << "Market data update: " << update.symbol 
                         << " bid: " << depth.bid_price 
                         << " ask: " << depth.ask_price << std::endl;
                first_update[update.symbol] = true;
            }
            
            market_data_updated_.store(true);
        }
    }

    void on_order_response_received(const OrderResponse& response)
    {
        std::cout << "Order response: " << response.symbol 
                 << " side: " << response.side 
                 << " quantity: " << response.origQty 
                 << " status: " << response.status_str << std::endl;
        
        // 检查是否为空响应（订单失败的情况）
        if (response.symbol.empty() || response.side.empty() || response.status_str.empty()) {
            std::cout << "Empty order response detected - likely order failure, clearing pending orders" << std::endl;
            // 清理所有待处理订单，避免阻塞后续操作
            {
                std::lock_guard<std::mutex> lock(pending_orders_mutex_);
                pending_orders_.clear();
            }
            
            // 记录订单失败，并在短时间后重试仓位对齐
            std::cout << "Order failed, will retry position alignment in next cycle" << std::endl;
            return;
        }
        
        // 使用订单状态机处理订单事件
        if (order_state_machine_ && order_state_machine_->is_running()) {
            try {
                // 将订单状态字符串转换为OrderEvent
                tes::execution::OrderEvent event = tes::execution::OrderEvent::ERROR_OCCURRED;
                if (response.status_str == "NEW") {
                    event = tes::execution::OrderEvent::ACKNOWLEDGE;
                } else if (response.status_str == "FILLED") {
                    event = tes::execution::OrderEvent::FILL;
                } else if (response.status_str == "PARTIALLY_FILLED") {
                    event = tes::execution::OrderEvent::PARTIAL_FILL;
                } else if (response.status_str == "CANCELLED") {
                    event = tes::execution::OrderEvent::CANCEL_CONFIRM;
                } else if (response.status_str == "REJECTED") {
                    event = tes::execution::OrderEvent::REJECT;
                }
                
                // 处理订单事件
                if (event != tes::execution::OrderEvent::ERROR_OCCURRED) {
                    order_state_machine_->process_event(response.clientOrderId, event);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error processing order event in state machine: " << e.what() << std::endl;
            }
        }
        
        // 订单成交后，立即请求更新账户信息以获取最新仓位
        if (response.status_str == "FILLED" || response.status_str == "PARTIALLY_FILLED") {
            std::cout << "Order " << response.status_str << ", requesting account update..." << std::endl;
            
            // 处理部分成交情况
            double executed_qty = 0.0;
            double total_qty = 0.0;
            try {
                executed_qty = std::stod(response.executedQty.empty() ? response.origQty : response.executedQty);
                total_qty = std::stod(response.origQty);
            } catch (const std::exception& e) {
                std::cerr << "Error parsing order quantities: " << e.what() << std::endl;
                executed_qty = std::stod(response.origQty);
                total_qty = executed_qty;
            }
            
            double remaining_qty = total_qty - executed_qty;
            
            if (response.status_str == "PARTIALLY_FILLED" && remaining_qty > 0) {
                std::cout << "[TWAP_PARTIAL] Partial fill detected: executed=" << executed_qty 
                          << ", remaining=" << remaining_qty << std::endl;
                
                // 将未成交数量加入未完成数量池
                add_to_unfilled_pool(response.symbol, remaining_qty);
                
                // TODO: 实现撤单逻辑
                // cancel_remaining_order(response.orderId, response.symbol);
            }
            
            if (binance_ws_) {
                try {
                    // 请求账户信息更新
                    binance_ws_->requestAccountInfo();
                } catch (const std::exception& e) {
                    std::cerr << "Error requesting account info after order fill: " << e.what() << std::endl;
                }
            }
            
            // 更新TWAP执行进度（只更新已成交部分）
            if (executed_qty > 0) {
                update_twap_progress(response.symbol, executed_qty);
            }
            
            // 从待处理订单列表中移除
            {
                std::lock_guard<std::mutex> lock(pending_orders_mutex_);
                pending_orders_.erase(response.clientOrderId);
                
                // 同时移除基于订单特征的键
                std::string order_key = response.symbol + "_" + response.side + "_" + response.origQty;
                pending_orders_.erase(order_key);
            }
            
            // 事件驱动架构 - 通知订单执行完成
            {
                std::lock_guard<std::mutex> notify_lock(order_completion_mutex_);
                order_completed_.store(true);
            }
            order_completion_cv_.notify_all();
            std::cout << "[DEBUG] Order execution completed, notified waiting threads" << std::endl;
        } else if (response.status_str == "CANCELLED" || response.status_str == "REJECTED") {
            // 订单被取消或拒绝，从待处理列表中移除
            std::cout << "Order " << response.status_str << ", removing from pending list" << std::endl;
            
            // 获取失败订单的数量
            double failed_quantity = 0.0;
            try {
                failed_quantity = std::stod(response.origQty);
            } catch (const std::exception& e) {
                std::cerr << "Error parsing failed order quantity: " << e.what() << std::endl;
            }
            
            // 将失败订单数量重新纳入未完成数量池
            if (failed_quantity > 0) {
                std::cout << "[TWAP_FAILED_ORDER] Adding failed order quantity " << failed_quantity 
                          << " back to unfilled pool for " << response.symbol << std::endl;
                add_to_unfilled_pool(response.symbol, failed_quantity);
            }
            
            // 清理失败的订单
            cleanup_failed_order(response.symbol, response.clientOrderId);
            
            // 记录错误信息到feedback
            if (response.status_str == "REJECTED") {
                std::string error_msg = "Order rejected for " + response.symbol + ": " + response.side + " " + response.origQty;
                record_order_error(response.symbol, error_msg);
            }
            
            std::lock_guard<std::mutex> lock(pending_orders_mutex_);
            pending_orders_.erase(response.clientOrderId);
            
            std::string order_key = response.symbol + "_" + response.side + "_" + response.origQty;
            pending_orders_.erase(order_key);
        } else if (response.status_str == "NEW") {
            // 订单已创建，保持在待处理列表中直到成交或取消
            std::cout << "Order " << response.status_str << ", keeping in pending list until filled or cancelled" << std::endl;
            
            // 添加仓位变化检测机制，不仅依赖订单状态
            std::thread([this, symbol = response.symbol, client_order_id = response.clientOrderId, expected_qty = std::stod(response.origQty), side = response.side, orig_qty = response.origQty]() {
                std::this_thread::sleep_for(std::chrono::seconds(5)); // 5秒后开始检测
                
                // 获取订单提交前的仓位
                double initial_position = 0.0;
                {
                    std::lock_guard<std::mutex> lock(current_positions_mutex_);
                    auto it = current_positions_.find(symbol);
                    if (it != current_positions_.end()) {
                        initial_position = it->second.quantity;
                    }
                }
                
                // 定期检测仓位变化，最多检测6次（30秒）
                for (int i = 0; i < 6; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    
                    // 检查订单是否仍在待处理列表中
                    bool order_still_pending = false;
                    {
                        std::lock_guard<std::mutex> lock(pending_orders_mutex_);
                        order_still_pending = pending_orders_.count(client_order_id) > 0;
                    }
                    
                    if (!order_still_pending) {
                        std::cout << "[POSITION_CHECK] Order " << client_order_id << " no longer pending, stopping position check" << std::endl;
                        break;
                    }
                    
                    // 强制刷新仓位数据
                    if (binance_ws_) {
                        try {
                            binance_ws_->requestAccountInfo();
                            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 等待数据更新
                        } catch (const std::exception& e) {
                            std::cerr << "Error requesting account info for position check: " << e.what() << std::endl;
                        }
                    }
                    
                    // 检查仓位是否发生变化
                    double current_position = 0.0;
                    {
                        std::lock_guard<std::mutex> lock(current_positions_mutex_);
                        auto it = current_positions_.find(symbol);
                        if (it != current_positions_.end()) {
                            current_position = it->second.quantity;
                        }
                    }
                    
                    double position_change = current_position - initial_position;
                    double expected_change = (side == "BUY") ? expected_qty : -expected_qty;
                    
                    std::cout << "[POSITION_CHECK] " << symbol << " position change: " << position_change 
                              << ", expected: " << expected_change << std::endl;
                    
                    // 如果仓位变化符合预期，说明订单已成交
                    if (std::abs(position_change - expected_change) < 1.0) { // 允许1个单位的误差
                        std::cout << "[POSITION_CHECK] Order " << client_order_id << " detected as filled by position change" << std::endl;
                        
                        // 从待处理订单列表中移除
                        {
                             std::lock_guard<std::mutex> lock(pending_orders_mutex_);
                             pending_orders_.erase(client_order_id);
                             std::string order_key = symbol + "_" + side + "_" + orig_qty;
                             pending_orders_.erase(order_key);
                         }
                        
                        // 触发TWAP进度更新
                        update_twap_progress(symbol, expected_qty);
                        
                        // 通知订单执行完成
                        {
                            std::lock_guard<std::mutex> notify_lock(order_completion_mutex_);
                            order_completed_.store(true);
                        }
                        order_completion_cv_.notify_all();
                        std::cout << "[POSITION_CHECK] Order execution completed by position detection, notified waiting threads" << std::endl;
                        
                        break;
                    }
                }
                
                // 最终超时处理
                bool order_still_pending = false;
                {
                    std::lock_guard<std::mutex> lock(pending_orders_mutex_);
                    order_still_pending = pending_orders_.count(client_order_id) > 0;
                }
                
                if (order_still_pending) {
                    std::cout << "[TIMEOUT] Order " << client_order_id << " timeout after position checks, forcing TWAP continuation" << std::endl;
                    // 强制触发下一个TWAP切片
                    update_twap_progress(symbol, 0.0); // 使用0表示超时触发
                }
            }).detach();
        }
    }
    
    // 更新TWAP执行进度
    void update_twap_progress(const std::string& symbol, double executed_qty)
    {
        std::lock_guard<std::mutex> lock(twap_orders_mutex_);
        
        for (auto& twap_order : active_twap_orders_) {
            if (twap_order.symbol == symbol && twap_order.is_active) {
                if (executed_qty > 0) {
                    std::cout << "TWAP slice executed: " << symbol << " quantity: " << executed_qty << std::endl;
                } else {
                    std::cout << "TWAP timeout triggered for: " << symbol << ", forcing next slice" << std::endl;
                }
                
                // 检查当前仓位，确定是否需要继续TWAP执行
                double current_position = 0.0;
                {
                    std::lock_guard<std::mutex> pos_lock(current_positions_mutex_);
                    auto it = current_positions_.find(symbol);
                    if (it != current_positions_.end()) {
                        current_position = it->second.quantity;
                    }
                }
                
                // 获取目标仓位
                double target_position = 0.0;
                {
                    std::lock_guard<std::mutex> target_lock(target_positions_mutex_);
                    for (const auto& target : target_positions_) {
                        if (target.symbol == symbol) {
                            target_position = target.quantity;
                            break;
                        }
                    }
                }
                
                double remaining_qty = target_position - current_position;
                std::cout << "[TWAP_PROGRESS] " << symbol << " current: " << current_position 
                          << ", target: " << target_position << ", remaining: " << remaining_qty << std::endl;
                
                // 精确数量控制：不使用容差，确保100%执行目标数量
                std::cout << "[TWAP_EXACT_CONTROL] Exact quantity control enabled - no tolerance threshold applied" << std::endl;
                
                // 触发下一个切片的处理，使用正确的间隔时间
                std::thread([this, symbol, interval = twap_order.slice_interval]() {
                    std::this_thread::sleep_for(interval); // 使用TWAP设定的间隔时间
                    process_next_twap_slice(symbol);
                }).detach();
                
                break;
            }
        }
    }
    
    // 清理失败的订单
    // 记录订单错误信息
    void record_order_error(const std::string& symbol, const std::string& error_message)
    {
        std::lock_guard<std::mutex> lock(order_errors_mutex_);
        
        // 创建错误记录
        order_errors_[symbol] = error_message;
        
        std::cout << "[ERROR] Recording order error for " << symbol << ": " << error_message << std::endl;
    }

    void cleanup_failed_order(const std::string& symbol, const std::string& client_order_id)
    {
        std::cout << "Cleaning up failed order: " << symbol << " " << client_order_id << std::endl;
        
        // 停止相关的TWAP执行
        std::lock_guard<std::mutex> lock(twap_orders_mutex_);
        for (auto& twap_order : active_twap_orders_) {
            if (twap_order.symbol == symbol && twap_order.is_active) {
                twap_order.is_active = false;
                std::cout << "TWAP execution stopped due to order failure: " << symbol << std::endl;
                break;
            }
        }
    }

    // 订单事件处理
    void on_order_event(const Order& order)
    {
        std::cout << "Order event: " << order.instrument_id 
                 << " status: " << static_cast<int>(order.status) << std::endl;
        
        // 根据订单状态更新待处理订单列表
        if (order.status == tes::execution::OrderStatus::FILLED || 
            order.status == tes::execution::OrderStatus::CANCELLED ||
            order.status == tes::execution::OrderStatus::REJECTED) {
            std::lock_guard<std::mutex> lock(pending_orders_mutex_);
            // 注意：这里需要根据实际的订单ID映射来移除
            // pending_orders_.erase(order_id);
        }
    }

    // 配置加载方法
    bool load_system_config() {
        try {
            std::ifstream config_file(config_file_path_);
            if (!config_file.is_open()) {
                std::cerr << "Failed to open config file: " << config_file_path_ << std::endl;
                return false;
            }
            
            nlohmann::json config_json;
            config_file >> config_json;
            config_file.close();
            
            // 解析系统配置
            if (config_json.contains("system")) {
                auto& system = config_json["system"];
                if (system.contains("name")) system_config_.name = system["name"];
                if (system.contains("version")) system_config_.version = system["version"];
                if (system.contains("max_threads")) system_config_.max_threads = system["max_threads"];
                if (system.contains("signaltrans_mode")) system_config_.signaltrans_mode = system["signaltrans_mode"];
                
                // JSON文件配置
                if (system.contains("json_file_config")) {
                    auto& json_config = system["json_file_config"];
                    if (json_config.contains("position_file")) system_config_.position_file = json_config["position_file"];
                    if (json_config.contains("update_interval_ms")) system_config_.update_interval_ms = json_config["update_interval_ms"];
                    if (json_config.contains("tolerance_threshold")) system_config_.tolerance_threshold = json_config["tolerance_threshold"];
                    if (json_config.contains("enable_auto_sync")) system_config_.enable_auto_sync = json_config["enable_auto_sync"];
                }
                
                // 输出配置
                if (system.contains("json_feedback_config")) {
                    auto& feedback_config = system["json_feedback_config"];
                    if (feedback_config.contains("output_directory")) system_config_.output_directory = feedback_config["output_directory"];
                    if (feedback_config.contains("filename_prefix")) system_config_.filename_prefix = feedback_config["filename_prefix"];
                    if (feedback_config.contains("append_timestamp")) system_config_.append_timestamp = feedback_config["append_timestamp"];
                    if (feedback_config.contains("pretty_print")) system_config_.pretty_print = feedback_config["pretty_print"];
                }
            }
            
            // 解析gateway配置
            if (config_json.contains("gateway")) {
                auto& gateway = config_json["gateway"];
                if (gateway.contains("trading_exchanges") && gateway["trading_exchanges"].is_array()) {
                    system_config_.trading_exchanges.clear();
                    for (const auto& exchange : gateway["trading_exchanges"]) {
                        system_config_.trading_exchanges.push_back(exchange.get<std::string>());
                    }
                }
                if (gateway.contains("trading_type") && gateway["trading_type"].is_array()) {
                    system_config_.trading_type.clear();
                    for (const auto& type : gateway["trading_type"]) {
                        system_config_.trading_type.push_back(type.get<std::string>());
                    }
                }
            }
            
            // 解析exchanges配置
            if (config_json.contains("exchanges")) {
                auto& exchanges = config_json["exchanges"];
                
                // 解析Binance配置
                if (exchanges.contains("binance")) {
                    auto& binance = exchanges["binance"];
                    if (binance.contains("signature_type")) system_config_.signature_type = binance["signature_type"];
                    
                    // 解密ed25519 key和secret
                    if (binance.contains("ed25519_api_key")) {
                        std::string encrypted_key = binance["ed25519_api_key"];
                        try {
                            system_config_.ed25519_api_key = crypto::Cryptor::Decrypt("BINANCE", encrypted_key);
                            std::cout << "Successfully decrypted ed25519_api_key (first 10 chars): " 
                                     << system_config_.ed25519_api_key.substr(0, 10) << "..." << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Failed to decrypt ed25519_api_key: " << e.what() << std::endl;
                            return false;
                        }
                    }
                    
                    if (binance.contains("ed25519_api_secret")) {
                        std::string encrypted_secret = binance["ed25519_api_secret"];
                        try {
                            system_config_.ed25519_api_secret = crypto::Cryptor::Decrypt("BINANCE", encrypted_secret);
                            std::cout << "Successfully decrypted ed25519_api_secret" << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Failed to decrypt ed25519_api_secret: " << e.what() << std::endl;
                            return false;
                        }
                    }
                    
                    if (binance.contains("hmac_api_key")) {
                        std::string encrypted_key = binance["hmac_api_key"];
                        try {
                            system_config_.hmac_api_key = crypto::Cryptor::Decrypt("BINANCE", encrypted_key);
                            std::cout << "Successfully decrypted hmac_api_key" << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Failed to decrypt hmac_api_key: " << e.what() << std::endl;
                            return false;
                        }
                    }
                    
                    if (binance.contains("hmac_api_secret")) {
                        std::string encrypted_secret = binance["hmac_api_secret"];
                        try {
                            system_config_.hmac_api_secret = crypto::Cryptor::Decrypt("BINANCE", encrypted_secret);
                            std::cout << "Successfully decrypted hmac_api_secret" << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Failed to decrypt hmac_api_secret: " << e.what() << std::endl;
                            return false;
                        }
                    }
                    
                    if (binance.contains("testnet")) system_config_.testnet = binance["testnet"];
                    if (binance.contains("timeout_ms")) system_config_.timeout_ms = binance["timeout_ms"];
                    if (binance.contains("websocket_endpoint")) system_config_.websocket_endpoint = binance["websocket_endpoint"];
                    if (binance.contains("quantity_threshold")) system_config_.quantity_threshold = binance["quantity_threshold"];
                    if (binance.contains("value_threshold")) system_config_.value_threshold = binance["value_threshold"];
                    if (binance.contains("market_impact_threshold")) system_config_.market_impact_threshold = binance["market_impact_threshold"];
                    if (binance.contains("default_duration_minutes")) system_config_.default_duration_minutes = binance["default_duration_minutes"];
                    if (binance.contains("min_slice_size")) system_config_.min_slice_size = binance["min_slice_size"];
                    if (binance.contains("max_slices")) system_config_.max_slices = binance["max_slices"];
                    if (binance.contains("default_participation_rate")) system_config_.default_participation_rate = binance["default_participation_rate"];
                    if (binance.contains("max_price_deviation_bps")) system_config_.max_price_deviation_bps = binance["max_price_deviation_bps"];
                }
            }
            
            std::cout << "System configuration loaded successfully" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error loading system config: " << e.what() << std::endl;
            return false;
        }
    }

    bool create_directory(const std::string& path)
    {
        // 简化的目录创建逻辑
        return true;
    }

    // 动态更新市场数据订阅
    void update_market_subscriptions()
    {
        try {
            // 读取pos_update.json文件获取需要的交易对
            nlohmann::json pos_data;
            if (!read_position_file(pos_data)) {
                return;
            }
            
            // 解析目标仓位，获取所有交易对
            std::set<std::string> required_symbols;
            auto targets = parse_target_positions(pos_data);
            for (const auto& target : targets) {
                required_symbols.insert(target.symbol);
            }
            
            std::lock_guard<std::mutex> lock(subscribed_symbols_mutex_);
            
            // 取消不再需要的订阅
            for (const auto& symbol : subscribed_symbols_) {
                if (required_symbols.find(symbol) == required_symbols.end()) {
                    binance_ws_->unsubscribeDepthUpdate(symbol);
                    std::cout << "Unsubscribed from market data for " << symbol << std::endl;
                }
            }
            
            // 添加新的订阅
            for (const auto& symbol : required_symbols) {
                if (subscribed_symbols_.find(symbol) == subscribed_symbols_.end()) {
                    binance_ws_->subscribeDepthUpdate(symbol, 5, 100);
                    std::cout << "Subscribed to market data for " << symbol << std::endl;
                }
            }
            
            // 更新订阅列表
            subscribed_symbols_ = required_symbols;
            
        } catch (const std::exception& e) {
            std::cerr << "Error updating market subscriptions: " << e.what() << std::endl;
        }
    }

    void account_update_worker()
    {
        std::cout << "Account update thread started" << std::endl;
        
        while (g_running.load()) {
            try {
                // 使用真实的Gateway接口获取账户信息
                if (gateway_connected_ && binance_ws_) {
                    // 请求账户信息
                    binance_ws_->requestAccountInfo();
                    
                    // 动态订阅交易对的深度数据
                    update_market_subscriptions();
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(5));  // 每5秒请求一次账户信息
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in account update worker: " << e.what() << std::endl;
                return; // 失败后直接退出
            }
        }
        
        std::cout << "Account update thread stopped" << std::endl;
    }

    void position_monitor_worker()
    {
        std::cout << "Position monitor thread started" << std::endl;
        
        while (g_running.load()) {
            try {
                // 读取pos_update.json文件
                nlohmann::json pos_data;
                if (read_position_file(pos_data)) {
                    // 检查isFinished字段
                    int finished_status = get_finished_status(pos_data);
                    
                    if (finished_status == 0) {
                        // isFinished = 0，需要进行仓位对齐
                        std::cout << "Detected isFinished=0, starting position alignment..." << std::endl;
                        
                        // 先请求最新的账户信息
                        if (gateway_connected_ && binance_ws_) {
                            // 重置账户数据状态
                            account_data_ready_.store(false);
                            
                            binance_ws_->requestAccountInfo();
                            std::cout << "[DEBUG] Requested account info, waiting for response..." << std::endl;
                            
                            // 使用条件变量等待账户信息更新，替代固定sleep
                            std::unique_lock<std::mutex> lock(account_update_mutex_);
                            if (account_update_cv_.wait_for(lock, ACCOUNT_UPDATE_TIMEOUT, 
                                [this] { return account_data_ready_.load(); })) {
                                std::cout << "[DEBUG] Account data received, proceeding with position processing" << std::endl;
                            } else {
                                std::cout << "[WARNING] Account update timeout, proceeding anyway..." << std::endl;
                            }
                        }
                        
                        // 解析目标仓位
                        std::cout << "[DEBUG] About to parse target positions from JSON data..." << std::endl;
                        auto targets = parse_target_positions(pos_data);
                        std::cout << "[DEBUG] parse_target_positions returned " << targets.size() << " targets" << std::endl;
                        
                        if (!targets.empty()) {
                            std::cout << "[DEBUG] Found " << targets.size() << " target positions, calling process_target_positions..." << std::endl;
                            process_target_positions(targets);
                        } else {
                            std::cout << "[WARNING] No target positions found in pos_update.json - this may indicate a parsing issue" << std::endl;
                            std::cout << "[DEBUG] Raw JSON data: " << pos_data.dump(2) << std::endl;
                        }
                        
                        // 处理完成后等待更长时间，避免频繁操作
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    } else if (finished_status == 1) {
                        // isFinished = 1，略过处理
                        // 静默跳过，不输出日志避免刷屏
                        std::this_thread::sleep_for(std::chrono::milliseconds(system_config_.update_interval_ms));
                    } else {
                        std::cout << "Invalid or missing isFinished field in pos_update.json" << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(system_config_.update_interval_ms));
                    }
                } else {
                    std::cout << "Failed to read pos_update.json file" << std::endl;
                    return; // 失败后直接退出
                }
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in position monitor worker: " << e.what() << std::endl;
                return; // 失败后直接退出
            }
        }
        
        std::cout << "Position monitor thread stopped" << std::endl;
    }

    bool read_position_file(nlohmann::json& data)
    {
        try {
            std::ifstream file(position_file_path_);
            if (!file.is_open()) {
                return false;
            }
            
            file >> data;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error reading position file: " << e.what() << std::endl;
            return false;
        }
    }

    int get_finished_status(const nlohmann::json& data)
    {
        try {
            if (data.is_array()) {
                for (const auto& item : data) {
                    if (item.contains("isFinished")) {
                        return item["isFinished"];
                    }
                }
            }
            return -1;  // 表示未找到isFinished字段
        } catch (const std::exception& e) {
            std::cerr << "Error getting finished status: " << e.what() << std::endl;
            return -1;
        }
    }

    std::vector<TargetPosition> parse_target_positions(const nlohmann::json& data)
    {
        std::cout << "[DEBUG] Starting parse_target_positions..." << std::endl;
        std::vector<TargetPosition> targets;
        
        try {
            std::cout << "[DEBUG] JSON data type: " << (data.is_array() ? "array" : "not array") << std::endl;
            std::cout << "[DEBUG] JSON data size: " << data.size() << std::endl;
            
            if (data.is_array()) {
                std::cout << "[DEBUG] Processing JSON array with " << data.size() << " items" << std::endl;
                
                for (size_t i = 0; i < data.size(); ++i) {
                    const auto& item = data[i];
                    std::cout << "[DEBUG] Processing item " << i << ": " << item.dump() << std::endl;
                    
                    // 检查是否包含必要字段
                    bool has_id = item.contains("id");
                    bool has_symbol = item.contains("symbol");
                    bool has_quantity = item.contains("quantity");
                    
                    std::cout << "[DEBUG] Item " << i << " fields - id: " << has_id 
                             << ", symbol: " << has_symbol << ", quantity: " << has_quantity << std::endl;
                    
                    // 只处理包含id、symbol和quantity字段的对象
                    if (has_id && has_symbol && has_quantity) {
                        try {
                            TargetPosition target;
                            target.id = item["id"];
                            target.symbol = item["symbol"];
                            
                            // 详细记录quantity字段的处理
                            std::cout << "[DEBUG] Processing quantity field for " << target.symbol << std::endl;
                            std::cout << "[DEBUG] Quantity raw value: " << item["quantity"].dump() << std::endl;
                            std::cout << "[DEBUG] Quantity type: " << item["quantity"].type_name() << std::endl;
                            
                            std::string quantity_str = item["quantity"].get<std::string>();
                            std::cout << "[DEBUG] Quantity as string: '" << quantity_str << "'" << std::endl;
                            
                            target.quantity = std::stod(quantity_str);
                            std::cout << "[DEBUG] Parsed quantity: " << target.quantity << std::endl;
                            
                            targets.push_back(target);
                            std::cout << "[DEBUG] Successfully added target: " << target.symbol 
                                     << " (id=" << target.id << ", quantity=" << target.quantity << ")" << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "[ERROR] Failed to parse item " << i << ": " << e.what() << std::endl;
                        }
                    } else {
                        std::cout << "[DEBUG] Skipping item " << i << " - missing required fields" << std::endl;
                    }
                }
            } else {
                std::cout << "[DEBUG] JSON data is not an array, cannot parse target positions" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Exception in parse_target_positions: " << e.what() << std::endl;
        }
        
        std::cout << "[DEBUG] parse_target_positions completed. Found " << targets.size() << " target positions" << std::endl;
        return targets;
    }

    void process_target_positions(const std::vector<TargetPosition>& targets)
    {
        std::cout << "Processing " << targets.size() << " target positions" << std::endl;
        
        // 在处理仓位对齐前强制刷新仓位数据
        std::cout << "[DEBUG] Force refreshing position data before alignment..." << std::endl;
        if (gateway_connected_ && binance_ws_) {
            // 重置账户数据状态
            account_data_ready_.store(false);
            
            binance_ws_->requestAccountInfo();
            std::cout << "[DEBUG] Requested account info for position alignment..." << std::endl;
            
            // 使用条件变量等待账户信息更新，替代固定sleep 3秒
            std::unique_lock<std::mutex> lock(account_update_mutex_);
            if (account_update_cv_.wait_for(lock, ACCOUNT_UPDATE_TIMEOUT, 
                [this] { return account_data_ready_.load(); })) {
                std::cout << "[DEBUG] Position data refresh completed" << std::endl;
            } else {
                std::cout << "[WARNING] Position data refresh timeout, proceeding anyway..." << std::endl;
            }
        } else {
            std::cerr << "[ERROR] Gateway not connected, cannot refresh position data" << std::endl;
            return; // 失败后直接退出
        }
        
        for (const auto& target : targets) {
            std::cout << "Target: " << target.symbol << " quantity: " << target.quantity << std::endl;
            
            // 1. 获取当前仓位（从Gateway获取的真实数据）
            CurrentPosition current_pos = get_current_position(target.symbol);
            std::cout << "Current position for " << target.symbol << ": " << current_pos.quantity << std::endl;
            
            // 2. 计算需要调整的数量
            double position_diff = target.quantity - current_pos.quantity;
            std::cout << "Position difference: " << position_diff << std::endl;
            
            if (std::abs(position_diff) < system_config_.tolerance_threshold) {
                std::cout << "Position difference within tolerance, skipping " << target.symbol << std::endl;
                continue;
            }
            
            // 3. 获取行情数据（从Gateway获取的真实数据）
            MarketDepth depth = get_market_depth(target.symbol);
            if (depth.bid_price == 0.0 || depth.ask_price == 0.0) {
                std::cout << "No market data available for " << target.symbol << ", skipping" << std::endl;
                continue;
            }
            
            // 4. 智能平仓算法：根据当前仓位和目标仓位计算最优平仓策略
            execute_position_alignment(target.symbol, current_pos.quantity, target.quantity, depth);
        }
        
        // 等待一段时间让订单执行完成 - 使用条件变量替代固定sleep
        std::cout << "[DEBUG] Waiting for order execution completion..." << std::endl;
        order_completed_.store(false);
        
        std::unique_lock<std::mutex> order_lock(order_completion_mutex_);
        if (order_completion_cv_.wait_for(order_lock, ORDER_COMPLETION_TIMEOUT,
            [this] { return order_completed_.load(); })) {
            std::cout << "[DEBUG] Order execution completed" << std::endl;
        } else {
            std::cout << "[WARNING] Order execution timeout, proceeding with alignment check..." << std::endl;
            
            // 超时时强制检查仓位变化并继续TWAP执行
            std::cout << "[TIMEOUT_TWAP] Checking for position changes to continue TWAP execution..." << std::endl;
            
            // 检查是否有活跃的TWAP订单需要继续执行
            std::lock_guard<std::mutex> twap_lock(twap_orders_mutex_);
            for (auto& twap_order : active_twap_orders_) {
                if (twap_order.is_active) {
                    std::cout << "[TIMEOUT_TWAP] Found active TWAP for " << twap_order.symbol << ", forcing continuation" << std::endl;
                    
                    // 异步触发TWAP进度更新，使用0表示超时触发
                    std::thread([this, symbol = twap_order.symbol]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 短暂延迟
                        update_twap_progress(symbol, 0.0);
                    }).detach();
                }
            }
        }
        
        // 检查仓位是否已对齐
        std::cout << "[DEBUG] Checking if all positions are aligned..." << std::endl;
        
        // 重新获取最新的账户信息
        std::cout << "[DEBUG] Force refreshing position data for alignment check..." << std::endl;
        if (gateway_connected_ && binance_ws_) {
            // 重置账户数据状态
            account_data_ready_.store(false);
            
            binance_ws_->requestAccountInfo();
            std::cout << "[DEBUG] Requested account info for alignment check..." << std::endl;
            
            // 使用条件变量等待账户信息更新，替代固定sleep 3秒
            std::unique_lock<std::mutex> check_lock(account_update_mutex_);
            if (account_update_cv_.wait_for(check_lock, ACCOUNT_UPDATE_TIMEOUT, 
                [this] { return account_data_ready_.load(); })) {
                std::cout << "[DEBUG] Position data refresh for alignment check completed" << std::endl;
            } else {
                std::cout << "[WARNING] Position data refresh timeout for alignment check" << std::endl;
            }
        }
        
        // 检查仓位对齐状态
        if (check_positions_aligned(targets)) {
            std::cout << "[DEBUG] All positions aligned successfully. Completing position alignment process..." << std::endl;
            
            // 1. 生成仓位对齐反馈报告
            if (generate_position_feedback_report(targets)) {
                std::cout << "[DEBUG] Position feedback report generated successfully" << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to generate position feedback report" << std::endl;
            }
            
            // 2. 更新isFinished状态为1
            if (update_finished_status(1)) {
                std::cout << "[DEBUG] Position alignment completed and isFinished set to 1" << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to update isFinished status" << std::endl;
            }
        } else {
            std::cout << "[WARNING] Some positions are not yet aligned. Will retry in next cycle." << std::endl;
        }
        
        // 生成执行结果报告
        generate_execution_report(targets);
    }

    // 智能仓位对齐算法 - 集成TWAP和订单状态跟踪
    void execute_position_alignment(const std::string& symbol, double current_qty, double target_qty, const MarketDepth& depth)
    {
        std::cout << "Executing position alignment for " << symbol 
                 << " current NET: " << current_qty << " target NET: " << target_qty << std::endl;
        
        // 检查是否有该交易对的待处理订单或活跃TWAP执行
        {
            std::lock_guard<std::mutex> lock(pending_orders_mutex_);
            bool has_pending = false;
            for (const auto& order_key : pending_orders_) {
                if (order_key.find(symbol) != std::string::npos) {
                    has_pending = true;
                    break;
                }
            }
            
            // 检查是否有活跃的TWAP执行
            std::lock_guard<std::mutex> twap_lock(twap_orders_mutex_);
            for (const auto& twap_order : active_twap_orders_) {
                if (twap_order.symbol == symbol && twap_order.is_active) {
                    std::cout << "Position alignment skipped - active TWAP execution exists for " << symbol << std::endl;
                    return;
                }
            }
            
            if (has_pending) {
                std::cout << "Position alignment skipped - pending orders exist for " << symbol << std::endl;
                return;
            }
        }
        
        if (std::abs(current_qty - target_qty) < system_config_.tolerance_threshold) {
            std::cout << "Position already aligned within tolerance" << std::endl;
            return;
        }
        
        // 计算需要调整的净仓位数量
        double net_adjustment = target_qty - current_qty;
        std::cout << "Net adjustment needed: " << net_adjustment << std::endl;
        
        // 使用TWAP算法拆分大订单
        if (std::abs(net_adjustment) > system_config_.min_slice_size) {
            std::cout << "Large order detected, using TWAP algorithm for " << symbol << std::endl;
            execute_twap_order(symbol, net_adjustment, depth);
            return;
        }
        
        // 优化后的仓位对齐逻辑：区分开仓和平仓场景
        if (std::abs(current_qty) < system_config_.tolerance_threshold) {
            // 当前仓位为0，直接开仓到目标仓位
            std::cout << "Current position is zero, opening new position to target" << std::endl;
            
            if (target_qty < 0) {
                // 开空头仓位
                std::cout << "Opening short position: " << std::abs(target_qty) << std::endl;
                std::string side = "SELL";
                double price = depth.bid_price;
                place_real_order(symbol, std::abs(target_qty), side, price);
            } else if (target_qty > 0) {
                // 开多头仓位
                std::cout << "Opening long position: " << target_qty << std::endl;
                std::string side = "BUY";
                double price = depth.ask_price;
                place_real_order(symbol, target_qty, side, price);
            }
        } else {
            // 单向持仓模式：当前有仓位，需要调整
            // 在单向持仓模式下，多仓转换为空仓只需要填写超额quantity部分
            
            if ((current_qty > 0 && target_qty < 0) || (current_qty < 0 && target_qty > 0)) {
                // 需要反向调整：从多仓转空仓或从空仓转多仓
                double total_adjustment = std::abs(target_qty - current_qty);
                std::cout << "Position reversal needed, total adjustment: " << total_adjustment << std::endl;
                
                if (target_qty < 0) {
                    // 目标是空仓：需要卖出 (当前多仓 + 目标空仓的绝对值)
                    std::cout << "Converting long to short position: " << total_adjustment << std::endl;
                    std::string side = "SELL";
                    double price = depth.bid_price;
                    place_real_order(symbol, total_adjustment, side, price);
                } else {
                    // 目标是多仓：需要买入 (当前空仓的绝对值 + 目标多仓)
                    std::cout << "Converting short to long position: " << total_adjustment << std::endl;
                    std::string side = "BUY";
                    double price = depth.ask_price;
                    place_real_order(symbol, total_adjustment, side, price);
                }
            } else if (current_qty > 0 && target_qty > 0) {
                // 都是多仓，调整数量
                if (current_qty > target_qty) {
                    // 减少多仓：平仓部分多头
                    double close_qty = current_qty - target_qty;
                    std::cout << "Reducing long position by: " << close_qty << std::endl;
                    std::string side = "SELL";
                    double price = depth.bid_price;
                    place_close_order(symbol, close_qty, side, price);
                } else {
                    // 增加多仓：开更多多头
                    double add_qty = target_qty - current_qty;
                    std::cout << "Increasing long position by: " << add_qty << std::endl;
                    std::string side = "BUY";
                    double price = depth.ask_price;
                    place_real_order(symbol, add_qty, side, price);
                }
            } else if (current_qty < 0 && target_qty < 0) {
                // 都是空仓，调整数量
                if (std::abs(current_qty) > std::abs(target_qty)) {
                    // 减少空仓：平仓部分空头
                    double close_qty = std::abs(current_qty) - std::abs(target_qty);
                    std::cout << "Reducing short position by: " << close_qty << std::endl;
                    std::string side = "BUY";
                    double price = depth.ask_price;
                    place_close_order(symbol, close_qty, side, price);
                } else {
                    // 增加空仓：开更多空头
                    double add_qty = std::abs(target_qty) - std::abs(current_qty);
                    std::cout << "Increasing short position by: " << add_qty << std::endl;
                    std::string side = "SELL";
                    double price = depth.bid_price;
                    place_real_order(symbol, add_qty, side, price);
                }
            } else if (target_qty == 0) {
                // 目标是零仓位，平掉所有仓位
                if (current_qty > 0) {
                    // 当前有净多头，需要卖出平仓
                    std::cout << "Target is zero position, need to close all long positions" << std::endl;
                    std::string side = "SELL";
                    double price = depth.bid_price;
                    place_close_order(symbol, std::abs(current_qty), side, price);
                } else if (current_qty < 0) {
                    // 当前有净空头，需要买入平仓
                    std::cout << "Target is zero position, need to close all short positions" << std::endl;
                    std::string side = "BUY";
                    double price = depth.ask_price;
                    place_close_order(symbol, std::abs(current_qty), side, price);
                }
            }
        }
    }

    // TWAP订单执行方法
    void execute_twap_order(const std::string& symbol, double net_adjustment, const MarketDepth& depth)
    {
        std::cout << "Starting TWAP execution for " << symbol << " quantity: " << net_adjustment << std::endl;
        
        // 创建TWAP订单
        TWAPOrder twap_order;
        twap_order.symbol = symbol;
        twap_order.total_quantity = std::abs(net_adjustment);
        twap_order.remaining_quantity = std::abs(net_adjustment);
        twap_order.side = (net_adjustment > 0) ? "BUY" : "SELL";
        twap_order.target_price = (net_adjustment > 0) ? depth.ask_price : depth.bid_price;
        twap_order.is_active = true;
        
        // 计算切片参数 - 智能切片大小计算
        double base_slice_size = twap_order.total_quantity / 3.0;  // 基础切片大小（总量的1/3）
        
        // 动态调整最小切片大小，避免切片过大
        double adaptive_min_slice = std::min(
            system_config_.min_slice_size,     // 配置的最小切片大小 (100)
            twap_order.total_quantity * 0.4    // 总量的40%作为最大单次切片
        );
        
        double slice_size = std::min(adaptive_min_slice, base_slice_size);
        
        std::cout << "[TWAP_SLICE] Adaptive slice calculation for " << symbol 
                  << ": total=" << twap_order.total_quantity 
                  << ", base_slice=" << base_slice_size
                  << ", adaptive_min=" << adaptive_min_slice 
                  << ", final_slice=" << slice_size << std::endl;
        twap_order.slices_count = static_cast<int>(std::ceil(twap_order.total_quantity / slice_size));
        twap_order.slice_interval = std::chrono::milliseconds(3000); // 3秒间隔
        
        // 添加到活跃TWAP订单列表
        {
            std::lock_guard<std::mutex> lock(twap_orders_mutex_);
            active_twap_orders_.push_back(twap_order);
        }
        
        std::cout << "TWAP order created: " << twap_order.slices_count << " slices, "
                  << slice_size << " per slice, " << twap_order.slice_interval.count() << "ms interval" << std::endl;
        
        // 执行第一个切片
        execute_twap_slice(symbol, slice_size, twap_order.side, twap_order.target_price);
    }
    
    // 执行TWAP切片
    void execute_twap_slice(const std::string& symbol, double quantity, const std::string& side, double price)
    {
        std::cout << "Executing TWAP slice: " << symbol << " " << side << " " << quantity << " at " << price << std::endl;
        
        // 添加到待处理订单列表
        std::string order_key = symbol + "_" + side + "_" + std::to_string(quantity);
        {
            std::lock_guard<std::mutex> lock(pending_orders_mutex_);
            pending_orders_.insert(order_key);
        }
        
        // 下单
        place_real_order(symbol, quantity, side, price);
        
        // 不再使用固定时延，而是依赖订单回报触发下一个切片
        std::cout << "TWAP slice submitted, waiting for execution confirmation..." << std::endl;
    }
    
    // 处理下一个TWAP切片
    void process_next_twap_slice(const std::string& symbol)
    {
        // 使用补偿机制计算下一切片数量
        double compensated_slice_size = calculate_next_slice_with_compensation(symbol);
        
        if (compensated_slice_size <= 0) {
            std::cout << "[TWAP_ERROR] No valid slice size calculated for " << symbol << std::endl;
            return;
        }
        
        std::lock_guard<std::mutex> lock(twap_orders_mutex_);
        
        for (auto& twap_order : active_twap_orders_) {
            if (twap_order.symbol == symbol && twap_order.is_active) {
                // 更新切片索引
                twap_order.current_slice++;
                
                // 确保不超过剩余数量
                double actual_slice_size = std::min(compensated_slice_size, twap_order.remaining_quantity);
                
                twap_order.remaining_quantity -= actual_slice_size;
                
                std::cout << "TWAP progress for " << symbol << ": slice " << twap_order.current_slice 
                          << "/" << twap_order.slices_count << ", remaining " << twap_order.remaining_quantity 
                          << " of " << twap_order.total_quantity << std::endl;
                
                // 检查是否为最后切片：基于切片索引或剩余数量为0
                bool is_final = (twap_order.current_slice >= twap_order.slices_count) || 
                               (twap_order.remaining_quantity <= 0.0);
                
                if (is_final) {
                    // 最后切片：包含所有剩余数量，确保精确执行
                    actual_slice_size += twap_order.remaining_quantity;
                    twap_order.remaining_quantity = 0.0;
                    twap_order.is_final_slice = true;
                    
                    std::cout << "[TWAP_FINAL] Final slice for " << symbol 
                              << " with total quantity: " << actual_slice_size << std::endl;
                    
                    // 触发最后切片强制完成机制
                    execute_final_slice_with_guarantee(symbol);
                    return;
                }
                
                if (twap_order.remaining_quantity <= 0.0) {
                    // TWAP执行完成
                    twap_order.is_active = false;
                    std::cout << "TWAP execution completed for " << symbol << std::endl;
                } else {
                    // 获取最新市场数据
                    MarketDepth depth = get_market_depth(symbol);
                    if (depth.bid_price > 0 && depth.ask_price > 0) {
                        double price = (twap_order.side == "BUY") ? depth.ask_price : depth.bid_price;
                        
                        // 延迟执行下一个切片
                        std::thread([this, symbol, actual_slice_size, twap_order, price]() {
                            std::this_thread::sleep_for(twap_order.slice_interval);
                            execute_twap_slice(symbol, actual_slice_size, twap_order.side, price);
                        }).detach();
                    }
                }
                break;
            }
        }
    }

    // 双向持仓模式下的智能下单
    void place_hedge_order(const std::string& symbol, double quantity, const std::string& side, double price)
    {
        if (!gateway_connected_ || !binance_ws_) {
            std::cout << "Gateway not connected, cannot place hedge order" << std::endl;
            return;
        }

        try {
            // 构建订单请求
            OrderRequest order_req;
            order_req.symbol = symbol;
            order_req.side = side;
            order_req.type = "MARKET";
            order_req.quantity = std::to_string(quantity);
            // 市价单不需要设置价格
            // order_req.price = std::to_string(price);  // 注释掉价格设置
            order_req.timeInForce = "IOC";
            
            // 根据Binance官方文档，在双向持仓模式下positionSide必填
            // 且仅可选择 LONG 或 SHORT，不能使用 BOTH
            if (side == "BUY") {
                // 买入操作：在双向持仓模式下，买入会优先平空头，然后开多头
                order_req.positionSide = "LONG";
            } else if (side == "SELL") {
                // 卖出操作：在双向持仓模式下，卖出会优先平多头，然后开空头
                order_req.positionSide = "SHORT";
            }
            
            order_req.newClientOrderId = "TES_HEDGE_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

            // 通过Gateway下单
            binance_ws_->placeOrder(order_req);
            
            std::cout << "Placed hedge order: " << symbol << " " << side << " " << quantity 
                     << " at price: " << price << " positionSide: " << order_req.positionSide << std::endl;
                     
        } catch (const std::exception& e) {
            std::cerr << "Error placing hedge order: " << e.what() << std::endl;
        }
    }

    // 智能平仓订单 - 单向持仓模式
    void place_close_order(const std::string& symbol, double quantity, const std::string& side, double price)
    {
        if (!gateway_connected_ || !binance_ws_) {
            std::cout << "Gateway not connected, cannot place close order" << std::endl;
            return;
        }

        // 检查是否有相同的待处理订单
        std::string order_key = symbol + "_" + side + "_" + std::to_string(quantity);
        {
            std::lock_guard<std::mutex> lock(pending_orders_mutex_);
            if (pending_orders_.find(order_key) != pending_orders_.end()) {
                std::cout << "Similar order already pending, skipping: " << order_key << std::endl;
                return;
            }
            // 预先添加到待处理订单列表，防止重复下单
            pending_orders_.insert(order_key);
        }

        try {
            // 构建平仓订单请求 - 修改为市价单
            OrderRequest order_req;
            order_req.symbol = symbol;
            order_req.side = side;
            order_req.type = "MARKET";  // 使用市价单
            order_req.quantity = std::to_string(quantity);
            // 市价单不需要设置价格
            // order_req.price = std::to_string(price);  // 注释掉价格设置
            // 市价单不需要timeInForce参数，移除该设置
            // order_req.timeInForce = "GTC";  // 市价单不需要timeInForce参数
            
            // 根据Binance官方文档，单向持仓模式下：
            // positionSide持仓方向，单向持仓模式下非必填，默认且仅可填BOTH
            order_req.positionSide = "BOTH";
            
            // 在单向持仓模式下，平仓需要设置reduceOnly=true
            // 这样可以确保订单只用于平仓，不会开新仓位
            order_req.reduceOnly = "true";
            
            // closePosition填false，不与quantity合用
            order_req.closePosition = "false";
            
            std::string client_order_id = "TES_CLOSE_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            order_req.newClientOrderId = client_order_id;

            // 通过Gateway下单
            binance_ws_->placeOrder(order_req);
            
            std::cout << "Placed close market order (Single Position Mode): " << symbol << " " << side << " " << quantity 
                     << " reference price: " << price << " positionSide: BOTH reduceOnly: true" 
                     << " clientOrderId: " << client_order_id << std::endl;
                     
        } catch (const std::exception& e) {
            std::cerr << "Error placing close market order: " << e.what() << std::endl;
            // 下单失败时，从待处理列表中移除
            {
                std::lock_guard<std::mutex> lock(pending_orders_mutex_);
                pending_orders_.erase(order_key);
            }
        }
    }

    // 获取当前仓位信息（从Gateway获取的真实数据）
    CurrentPosition get_current_position(const std::string& symbol)
    {
        std::cout << "[DEBUG] get_current_position called for: " << symbol << std::endl;
        
        // 在获取仓位前，先强制刷新账户信息
        if (gateway_connected_ && binance_ws_) {
            std::cout << "[DEBUG] Refreshing account info before getting position for " << symbol << std::endl;
            
            // 重置positions_updated_标志
            positions_updated_.store(false);
            
            // 请求账户信息
            binance_ws_->requestAccountInfo();
            
            // 等待仓位数据更新完成，最多等待5秒
            auto start_time = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(5);
            
            while (!positions_updated_.load() && 
                   (std::chrono::steady_clock::now() - start_time) < timeout) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (!positions_updated_.load()) {
                std::cerr << "[WARNING] Position update timeout after 5 seconds for " << symbol << std::endl;
            } else {
                std::cout << "[DEBUG] Position data updated successfully for " << symbol << std::endl;
            }
        } else {
            std::cerr << "[ERROR] Gateway not connected, cannot refresh account info" << std::endl;
            CurrentPosition empty_pos;
            empty_pos.symbol = symbol;
            return empty_pos; // 失败后直接返回空仓位
        }
        
        // 使用独立的锁来访问仓位数据，确保数据一致性
        std::lock_guard<std::mutex> lock(current_positions_mutex_);
        
        // 先验证当前缓存的所有仓位数据
        std::cout << "[DEBUG] Current positions in cache:" << std::endl;
        for (const auto& pair : current_positions_) {
            std::cout << "[DEBUG]   " << pair.first << ": " << pair.second.quantity << std::endl;
        }
        
        auto it = current_positions_.find(symbol);
        if (it != current_positions_.end()) {
            std::cout << "[DEBUG] Found position for " << symbol << ": " << it->second.quantity << std::endl;
            
            // 验证数据的时效性
            auto now = std::chrono::high_resolution_clock::now();
            auto data_age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_update).count();
            std::cout << "[DEBUG] Position data age: " << data_age << " seconds" << std::endl;
            
            return it->second;
        }
        
        std::cout << "[DEBUG] No position found for " << symbol << ", returning empty position" << std::endl;
        CurrentPosition empty_pos;
        empty_pos.symbol = symbol;
        return empty_pos;  // 返回空仓位
    }

    // 获取行情数据（从Gateway获取的真实数据）
    MarketDepth get_market_depth(const std::string& symbol)
    {
        std::lock_guard<std::mutex> lock(market_depths_mutex_);
        auto it = market_depths_.find(symbol);
        if (it != market_depths_.end()) {
            return it->second;
        }
        return MarketDepth();  // 返回空行情数据
    }

    // 使用真实的Gateway接口下单 - 单向持仓模式
    void place_real_order(const std::string& symbol, double quantity, const std::string& side, double price)
    {
        if (!gateway_connected_ || !binance_ws_) {
            std::cout << "Gateway not connected, cannot place order" << std::endl;
            return;
        }

        if (!order_state_machine_ || !order_state_machine_->is_running()) {
            std::cout << "Order state machine not available, cannot place order" << std::endl;
            return;
        }

        try {
            // 获取交易规则管理器
            auto& ruleManager = TradingRuleManager::getInstance();
            
            // 格式化订单数量和价格
            double formatted_quantity = ruleManager.formatQuantity(symbol, quantity);
            double formatted_price = ruleManager.formatPrice(symbol, price);
            
            std::cout << "[INFO] Order formatting for " << symbol << ":" << std::endl;
            std::cout << "  - Original quantity: " << quantity << " -> Formatted: " << formatted_quantity << std::endl;
            std::cout << "  - Original price: " << price << " -> Formatted: " << formatted_price << std::endl;
            
            // 验证订单是否符合交易规则
            if (!ruleManager.isValidOrder(symbol, formatted_quantity, formatted_price)) {
                std::cerr << "[ERROR] Order validation failed for " << symbol << std::endl;
                return;
            }
            
            // 检查是否有重复订单（包括待处理订单和最近执行的订单）
            tes::execution::OrderSide order_side = (side == "BUY") ? tes::execution::OrderSide::BUY : tes::execution::OrderSide::SELL;
            
            // 检查待处理订单
            if (order_state_machine_->has_pending_order(symbol, order_side, formatted_quantity, formatted_price, 1e-6)) {
                std::cout << "Duplicate pending order detected for " << symbol << " " << side << " " << formatted_quantity 
                         << " at price " << formatted_price << ", skipping..." << std::endl;
                return;
            }
            
            // 检查最近30秒内是否有相同的已执行订单，防止重复下单
            if (order_state_machine_->has_recent_executed_order(symbol, order_side, formatted_quantity, formatted_price, std::chrono::milliseconds(30000), 1e-6)) {
                std::cout << "Recent executed order detected for " << symbol << " " << side << " " << formatted_quantity 
                         << " at price " << formatted_price << " within 30 seconds, skipping to prevent duplicate..." << std::endl;
                return;
            }

            // 创建订单对象 - 修改为市价单
            Order order;
            order.instrument_id = symbol;
            order.side = order_side;
            order.quantity = formatted_quantity;  // 使用格式化后的数量
            order.price = formatted_price;  // 使用格式化后的价格
            order.type = tes::execution::OrderType::MARKET;  // 改为市价单
            order.time_in_force = tes::execution::TimeInForce::IOC;  // 市价单使用IOC
            order.strategy_id = "position_alignment";

            // 通过状态机创建订单
            std::string order_id = order_state_machine_->create_order(order);
            if (order_id.empty()) {
                std::cerr << "Failed to create order in state machine" << std::endl;
                return;
            }

            // 构建订单请求 - 修改为市价单
            OrderRequest order_req;
            order_req.symbol = symbol;
            order_req.side = side;
            order_req.type = "MARKET";  // 使用市价单
            order_req.quantity = std::to_string(formatted_quantity);  // 使用格式化后的数量
            // 市价单不需要设置价格
            // order_req.price = std::to_string(formatted_price);  // 注释掉价格设置
            // 市价单不需要timeInForce参数，移除该设置
            // order_req.timeInForce = "IOC";  // 市价单不需要timeInForce参数
            
            // 根据Binance官方文档，单向持仓模式下：
            // positionSide持仓方向，单向持仓模式下非必填，默认且仅可填BOTH
            order_req.positionSide = "BOTH";
            
            // 根据官方文档：非双开模式下默认false
            // reduceOnly参数在单向持仓模式下默认为false，用于开仓
            order_req.reduceOnly = "false";
            
            // closePosition填false，不与quantity合用
            order_req.closePosition = "false";
            
            // 使用状态机生成的订单ID作为客户端订单ID
            order_req.newClientOrderId = order_id;

            // 更新状态机：提交订单
            order_state_machine_->process_event(order_id, OrderEvent::SUBMIT);

            // 通过Gateway下单
            binance_ws_->placeOrder(order_req);
            
            std::cout << "Placed market order (Single Position Mode): " << symbol << " " << side << " " << formatted_quantity 
                     << " reference price: " << formatted_price << " positionSide: BOTH reduceOnly: false OrderID: " << order_id << std::endl;
                     
        } catch (const std::exception& e) {
            std::cerr << "Error placing market order: " << e.what() << std::endl;
        }
    }

    void generate_execution_report(const std::vector<TargetPosition>& targets)
    {
        try {
            nlohmann::json report;
            report["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            report["completion_time"] = std::time(nullptr);
            report["status"] = "completed";
            report["message"] = "Position alignment completed successfully";
            report["processed_targets"] = targets.size();
            report["gateway_connected"] = gateway_connected_;
            
            // 保存报告到output目录
            std::string report_path = system_config_.output_directory + "/execution_report.json";
            std::ofstream report_file(report_path);
            if (report_file.is_open()) {
                if (system_config_.pretty_print) {
                    report_file << report.dump(4);
                } else {
                    report_file << report.dump();
                }
                report_file.close();
                std::cout << "Execution report saved to: " << report_path << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error generating execution report: " << e.what() << std::endl;
        }
    }

    // 更新pos_update.json中的isFinished状态
    bool update_finished_status(int status)
    {
        try {
            std::lock_guard<std::mutex> lock(g_file_mutex);
            
            nlohmann::json pos_data;
            if (!read_position_file(pos_data)) {
                std::cerr << "Failed to read position file for updating isFinished status" << std::endl;
                return false;
            }
            
            // 更新isFinished字段
            bool updated = false;
            if (pos_data.is_array()) {
                for (auto& item : pos_data) {
                    if (item.is_object() && item.contains("isFinished")) {
                        item["isFinished"] = status;
                        item["update_timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        updated = true;
                        break;
                    }
                }
            }
            
            if (!updated) {
                std::cerr << "Failed to find isFinished field in position file" << std::endl;
                return false;
            }
            
            // 写回文件
            std::ofstream file(position_file_path_);
            if (file.is_open()) {
                file << pos_data.dump(2);
                file.close();
                std::cout << "[DEBUG] Updated isFinished status to " << status << " in " << position_file_path_ << std::endl;
                return true;
            } else {
                std::cerr << "Failed to open position file for writing: " << position_file_path_ << std::endl;
                return false;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error updating isFinished status: " << e.what() << std::endl;
            return false;
        }
    }

    // 生成仓位对齐反馈报告
    bool generate_position_feedback_report(const std::vector<TargetPosition>& targets)
    {
        try {
            // 创建results目录（如果不存在）
            std::string results_dir = "results";
            
            // 使用系统调用创建目录
            system(("mkdir -p " + results_dir).c_str());
            
            // 生成时间戳文件名
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
            
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
            ss << "_" << ms.count() << "_0.json";
            
            std::string filename = "feedback_" + ss.str();
            std::string filepath = results_dir + "/" + filename;
            
            // 构建反馈报告JSON
            nlohmann::json feedback_report = nlohmann::json::array();
            
            // 添加每个交易对的信息
            for (const auto& target : targets) {
                nlohmann::json position_info;
                position_info["id"] = target.id;
                position_info["symbol"] = target.symbol;
                
                // 获取当前仓位
                CurrentPosition current_pos = get_current_position(target.symbol);
                position_info["current_quantity"] = std::to_string(current_pos.quantity);
                
                // 计算变化量：目标仓位 - 当前仓位
                double change_qty = target.quantity - current_pos.quantity;
                position_info["change_quantity"] = std::to_string(change_qty);
                
                // 检查是否有订单错误
                std::string error_message = "";
                {
                    std::lock_guard<std::mutex> lock(order_errors_mutex_);
                    auto error_it = order_errors_.find(target.symbol);
                    if (error_it != order_errors_.end()) {
                        error_message = error_it->second;
                    }
                }
                position_info["error_message"] = error_message;
                
                feedback_report.push_back(position_info);
            }
            
            // 读取pos_update.json获取额外信息
            nlohmann::json pos_data;
            double targetvalue = 0.0, longtarget = 0.0, shorttarget = 0.0;
            double update_timestamp = 0.0;
            
            if (read_position_file(pos_data) && pos_data.is_array()) {
                for (const auto& item : pos_data) {
                    if (item.is_object()) {
                        if (item.contains("targetvalue")) {
                            targetvalue = item["targetvalue"];
                        }
                        if (item.contains("longtarget")) {
                            longtarget = item["longtarget"];
                        }
                        if (item.contains("shorttarget")) {
                            shorttarget = item["shorttarget"];
                        }
                        if (item.contains("update_timestamp")) {
                            update_timestamp = item["update_timestamp"];
                        }
                    }
                }
            }
            
            // 添加汇总信息
            nlohmann::json summary;
            summary["isFinished"] = 1;
            summary["error_total"] = 0;
            summary["targetvalue"] = targetvalue;
            summary["longtarget"] = longtarget;
            summary["shorttarget"] = shorttarget;
            summary["update_timestamp"] = update_timestamp;
            
            feedback_report.push_back(summary);
            
            // 写入文件
            std::ofstream report_file(filepath);
            if (report_file.is_open()) {
                report_file << feedback_report.dump(2);
                report_file.close();
                std::cout << "[DEBUG] Position feedback report saved to: " << filepath << std::endl;
                return true;
            } else {
                std::cerr << "Failed to create feedback report file: " << filepath << std::endl;
                return false;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error generating position feedback report: " << e.what() << std::endl;
            return false;
        }
    }

    // 检查所有仓位是否已对齐
    bool check_positions_aligned(const std::vector<TargetPosition>& targets)
    {
        std::cout << "[DEBUG] Starting position alignment check with dynamic tolerance" << std::endl;
        
        for (const auto& target : targets) {
            CurrentPosition current_pos = get_current_position(target.symbol);
            double diff = std::abs(current_pos.quantity - target.quantity);
            
            // 计算动态容差：基于目标仓位的相对容差 + 最小绝对容差
            double dynamic_tolerance = std::max(
                system_config_.tolerance_threshold,  // 最小绝对容差 (0.000001)
                std::abs(target.quantity) * 0.05     // 目标仓位的5%作为相对容差
            );
            
            std::cout << "[DEBUG] Checking " << target.symbol 
                     << ": current=" << current_pos.quantity 
                     << ", target=" << target.quantity 
                     << ", diff=" << diff 
                     << ", dynamic_tolerance=" << dynamic_tolerance 
                     << " (absolute: " << system_config_.tolerance_threshold 
                     << ", relative 5%: " << std::abs(target.quantity) * 0.05 << ")" << std::endl;
            
            if (diff > dynamic_tolerance) {
                std::cout << "[DEBUG] Position not aligned for " << target.symbol 
                         << ": current=" << current_pos.quantity 
                         << ", target=" << target.quantity 
                         << ", diff=" << diff << " > dynamic_tolerance=" << dynamic_tolerance << std::endl;
                return false;
            } else {
                std::cout << "[DEBUG] Position aligned for " << target.symbol 
                         << ": diff=" << diff << " <= dynamic_tolerance=" << dynamic_tolerance << std::endl;
            }
        }
        
        std::cout << "[DEBUG] All positions are aligned within dynamic tolerance" << std::endl;
        return true;
    }
    
    // 新增：将未成交数量加入未完成数量池
    void add_to_unfilled_pool(const std::string& symbol, double unfilled_qty) {
        std::lock_guard<std::mutex> lock(twap_orders_mutex_);
        
        for (auto& twap_order : active_twap_orders_) {
            if (twap_order.symbol == symbol && twap_order.is_active) {
                twap_order.unfilled_quantity += unfilled_qty;
                
                std::cout << "[TWAP_UNFILLED] Added " << unfilled_qty 
                          << " to unfilled pool for " << symbol 
                          << ". Total unfilled: " << twap_order.unfilled_quantity << std::endl;
                break;
            }
        }
    }
    
    // 新增：计算包含补偿的下一切片数量
    double calculate_next_slice_with_compensation(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(twap_orders_mutex_);
        
        for (auto& twap_order : active_twap_orders_) {
            if (twap_order.symbol == symbol && twap_order.is_active) {
                // 基础切片大小计算
                double base_slice_size = twap_order.remaining_quantity / 3.0;
                double adaptive_min_slice = std::min(
                    system_config_.min_slice_size,
                    twap_order.total_quantity * 0.4
                );
                double base_slice = std::min(adaptive_min_slice, base_slice_size);
                
                // 加上累积的未成交数量
                double compensated_slice = base_slice + twap_order.unfilled_quantity;
                
                std::cout << "[TWAP_COMPENSATION] " << symbol 
                          << ": base=" << base_slice 
                          << ", unfilled=" << twap_order.unfilled_quantity
                          << ", total=" << compensated_slice << std::endl;
                
                // 清空未成交数量池（已合并到当前切片）
                twap_order.unfilled_quantity = 0.0;
                
                return compensated_slice;
            }
        }
        return 0.0;
     }
     
     // 新增：最后切片强制完成机制
     void execute_final_slice_with_guarantee(const std::string& symbol) {
         std::lock_guard<std::mutex> lock(twap_orders_mutex_);
         
         for (auto& twap_order : active_twap_orders_) {
             if (twap_order.symbol == symbol && twap_order.is_active) {
                 // 计算所有剩余数量（包括未成交累积）
                 double final_quantity = twap_order.remaining_quantity + twap_order.unfilled_quantity;
                 
                 if (final_quantity > 0) {
                     std::cout << "[TWAP_FINAL_GUARANTEE] Executing final slice for " << symbol 
                               << " with guaranteed completion: " << final_quantity << std::endl;
                     
                     // 标记为最后切片
                     twap_order.is_final_slice = true;
                     twap_order.remaining_quantity = 0.0;
                     twap_order.unfilled_quantity = 0.0;
                     
                     // 获取市场数据
                     MarketDepth depth = get_market_depth(symbol);
                     double price = (twap_order.side == "BUY") ? depth.ask_price : depth.bid_price;
                     
                     // 使用市价单确保成交
                     std::cout << "[TWAP_MARKET_ORDER] Using market order for guaranteed execution" << std::endl;
                     execute_twap_slice(symbol, final_quantity, twap_order.side, price);
                     
                     // 设置更短的监控时间
                     monitor_final_slice_completion(symbol, final_quantity);
                 }
                 
                 break;
             }
         }
     }
     
     // 新增：监控最后切片完成情况
     void monitor_final_slice_completion(const std::string& symbol, double expected_quantity) {
         std::thread([this, symbol, expected_quantity]() {
             std::this_thread::sleep_for(std::chrono::seconds(10)); // 10秒监控
             
             // 检查TWAP是否真正完成
             std::lock_guard<std::mutex> lock(twap_orders_mutex_);
             for (auto& twap_order : active_twap_orders_) {
                 if (twap_order.symbol == symbol && twap_order.is_active) {
                     std::cout << "[TWAP_FINAL_CHECK] Final slice monitoring timeout for " << symbol 
                               << ", forcing completion..." << std::endl;
                     
                     // 强制完成TWAP
                     twap_order.is_active = false;
                     twap_order.remaining_quantity = 0.0;
                     twap_order.unfilled_quantity = 0.0;
                     
                     std::cout << "[TWAP_FORCE_COMPLETE] TWAP forcibly completed for " << symbol << std::endl;
                     break;
                 }
             }
         }).detach();
     }
};

// TradingRuleManager 实现
bool TradingRuleManager::loadExchangeInfo(const std::string& apiKey, const std::string& apiSecret, bool testnet) {
    try {
        std::string baseUrl = testnet ? "https://testnet.binancefuture.com" : "https://fapi.binance.com";
        std::string url = baseUrl + "/fapi/v1/exchangeInfo";
        
        std::cout << "[INFO] Fetching exchange info from: " << url << std::endl;
        
        std::string response = makeHttpRequest(url, apiKey);
        if (response.empty()) {
            std::cerr << "[ERROR] Failed to fetch exchange info from API" << std::endl;
            return false;
        }
        
        if (!parseExchangeInfo(response)) {
            std::cerr << "[ERROR] Failed to parse exchange info response" << std::endl;
            return false;
        }
        
        if (!saveExchangeInfoToFile(response)) {
            std::cerr << "[WARNING] Failed to save exchange info to file" << std::endl;
        }
        
        std::cout << "[INFO] Successfully loaded " << trading_rules_.size() << " trading rules" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in loadExchangeInfo: " << e.what() << std::endl;
        return false;
    }
}

std::string TradingRuleManager::makeHttpRequest(const std::string& url, const std::string& apiKey) const {
    try {
        ix::HttpClient httpClient;
        ix::HttpRequestArgsPtr args = httpClient.createRequest();
        
        args->extraHeaders["X-MBX-APIKEY"] = apiKey;
        args->connectTimeout = 30;
        args->transferTimeout = 30;
        
        ix::HttpResponsePtr response = httpClient.get(url, args);
        
        if (response->statusCode == 200) {
            return response->body;
        } else {
            std::cerr << "[ERROR] HTTP request failed with status: " << response->statusCode << std::endl;
            std::cerr << "[ERROR] Response body: " << response->body << std::endl;
            return "";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in HTTP request: " << e.what() << std::endl;
        return "";
    }
}

bool TradingRuleManager::parseExchangeInfo(const std::string& jsonData) {
    try {
        nlohmann::json exchangeInfo = nlohmann::json::parse(jsonData);
        
        if (!exchangeInfo.contains("symbols") || !exchangeInfo["symbols"].is_array()) {
            std::cerr << "[ERROR] Invalid exchange info format: missing symbols array" << std::endl;
            return false;
        }
        
        std::lock_guard<std::mutex> lock(rules_mutex_);
        trading_rules_.clear();
        
        for (const auto& symbolInfo : exchangeInfo["symbols"]) {
            if (!symbolInfo.contains("symbol") || !symbolInfo.contains("filters")) {
                continue;
            }
            
            TradingRule rule;
            rule.symbol = symbolInfo["symbol"];
            rule.quantityPrecision = symbolInfo.value("quantityPrecision", 0);
            rule.pricePrecision = symbolInfo.value("pricePrecision", 0);
            
            // 解析过滤器
            for (const auto& filter : symbolInfo["filters"]) {
                if (!filter.contains("filterType")) continue;
                
                std::string filterType = filter["filterType"];
                
                if (filterType == "LOT_SIZE") {
                    rule.minQty = std::stod(filter.value("minQty", "0"));
                    rule.maxQty = std::stod(filter.value("maxQty", "0"));
                    rule.stepSize = std::stod(filter.value("stepSize", "0"));
                } else if (filterType == "PRICE_FILTER") {
                    rule.tickSize = std::stod(filter.value("tickSize", "0"));
                } else if (filterType == "MIN_NOTIONAL") {
                    rule.minNotional = std::stod(filter.value("notional", "0"));
                }
            }
            
            trading_rules_[rule.symbol] = rule;
            
            // 输出APRUSDT的规则信息用于调试
            if (rule.symbol == "APRUSDT") {
                std::cout << "[INFO] APRUSDT Trading Rules:" << std::endl;
                std::cout << "  - Quantity Precision: " << rule.quantityPrecision << std::endl;
                std::cout << "  - Price Precision: " << rule.pricePrecision << std::endl;
                std::cout << "  - Min Qty: " << rule.minQty << std::endl;
                std::cout << "  - Max Qty: " << rule.maxQty << std::endl;
                std::cout << "  - Step Size: " << rule.stepSize << std::endl;
                std::cout << "  - Tick Size: " << rule.tickSize << std::endl;
                std::cout << "  - Min Notional: " << rule.minNotional << std::endl;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception parsing exchange info: " << e.what() << std::endl;
        return false;
    }
}

bool TradingRuleManager::saveExchangeInfoToFile(const std::string& jsonData) const {
    try {
        std::ofstream file("config/exchange_info.json");
        if (!file.is_open()) {
            return false;
        }
        
        // 格式化JSON输出
        nlohmann::json parsed = nlohmann::json::parse(jsonData);
        file << parsed.dump(2);
        file.close();
        
        std::cout << "[INFO] Exchange info saved to config/exchange_info.json" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception saving exchange info: " << e.what() << std::endl;
        return false;
    }
}

TradingRule TradingRuleManager::getTradingRule(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(rules_mutex_));
    auto it = trading_rules_.find(symbol);
    if (it != trading_rules_.end()) {
        return it->second;
    }
    return TradingRule(); // 返回默认规则
}

double TradingRuleManager::formatQuantity(const std::string& symbol, double quantity) const {
    TradingRule rule = getTradingRule(symbol);
    if (rule.stepSize <= 0) {
        return quantity;
    }
    
    // 根据stepSize格式化数量
    double steps = std::floor(quantity / rule.stepSize);
    double formatted = steps * rule.stepSize;
    
    // 根据精度四舍五入
    double multiplier = std::pow(10.0, rule.quantityPrecision);
    formatted = std::round(formatted * multiplier) / multiplier;
    
    std::cout << "[DEBUG] Formatted quantity for " << symbol 
              << ": " << quantity << " -> " << formatted 
              << " (stepSize: " << rule.stepSize 
              << ", precision: " << rule.quantityPrecision << ")" << std::endl;
    
    return formatted;
}

double TradingRuleManager::formatPrice(const std::string& symbol, double price) const {
    TradingRule rule = getTradingRule(symbol);
    if (rule.tickSize <= 0) {
        return price;
    }
    
    // 根据tickSize格式化价格
    double ticks = std::round(price / rule.tickSize);
    double formatted = ticks * rule.tickSize;
    
    // 根据精度四舍五入
    double multiplier = std::pow(10.0, rule.pricePrecision);
    formatted = std::round(formatted * multiplier) / multiplier;
    
    return formatted;
}

bool TradingRuleManager::isValidOrder(const std::string& symbol, double quantity, double price) const {
    TradingRule rule = getTradingRule(symbol);
    
    // 检查数量范围
    if (quantity < rule.minQty || quantity > rule.maxQty) {
        std::cout << "[ERROR] Invalid quantity for " << symbol 
                  << ": " << quantity << " (min: " << rule.minQty 
                  << ", max: " << rule.maxQty << ")" << std::endl;
        return false;
    }
    
    // 检查最小名义价值
    double notional = quantity * price;
    if (notional < rule.minNotional) {
        std::cout << "[ERROR] Invalid notional for " << symbol 
                  << ": " << notional << " (min: " << rule.minNotional << ")" << std::endl;
        return false;
    }
    
    return true;
}

int main(int argc, char* argv[])
{
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        TradingSystemManager manager;
        
        // 初始化系统
        if (!manager.initialize()) {
            std::cerr << "Failed to initialize trading system" << std::endl;
            return 1;
        }
        
        // 启动系统
        if (!manager.start()) {
            std::cerr << "Failed to start trading system" << std::endl;
            return 1;
        }
        
        // 运行主循环
        manager.run();
        
        std::cout << "Trading system shutdown complete" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}