#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include "utils/websocket_client.h"

namespace tes {
namespace execution {

/**
 * @brief Binance账户WebSocket API客户端
 * 
 * 实现Binance期货WebSocket API的账户查询功能，包括：
 * - v2/account.balance: 查询账户余额
 * - v2/account.status: 查询账户信息
 */
class BinanceAccountWebSocket {
public:
    /**
     * @brief 账户余额信息
     */
    struct AccountBalance {
        std::string account_alias;      // 账户唯一识别码
        std::string asset;              // 资产
        std::string balance;            // 总余额
        std::string cross_wallet_balance; // 全仓余额
        std::string cross_un_pnl;       // 全仓持仓未实现盈亏
        std::string available_balance;  // 下单可用余额
        std::string max_withdraw_amount; // 最大可转出余额
        bool margin_available;          // 是否可用作联合保证金
        uint64_t update_time;           // 更新时间
    };
    
    /**
     * @brief 账户资产信息
     */
    struct AccountAsset {
        std::string asset;                    // 资产
        std::string wallet_balance;           // 余额
        std::string unrealized_profit;        // 未实现盈亏
        std::string margin_balance;           // 保证金余额
        std::string maint_margin;             // 维持保证金
        std::string initial_margin;           // 当前所需起始保证金
        std::string position_initial_margin;  // 持仓所需起始保证金
        std::string open_order_initial_margin; // 当前挂单所需起始保证金
        std::string cross_wallet_balance;     // 全仓账户余额
        std::string cross_un_pnl;             // 全仓持仓未实现盈亏
        std::string available_balance;        // 可用余额
        std::string max_withdraw_amount;      // 最大可转出余额
        uint64_t update_time;                 // 更新时间
    };
    
    /**
     * @brief 持仓信息
     */
    struct PositionInfo {
        std::string symbol;             // 交易对
        std::string position_side;      // 持仓方向
        std::string position_amt;       // 持仓数量
        std::string unrealized_profit;  // 持仓未实现盈亏
        std::string isolated_margin;    // 逐仓保证金
        std::string notional;           // 名义价值
        std::string isolated_wallet;    // 逐仓钱包余额
        std::string initial_margin;     // 持仓所需起始保证金
        std::string maint_margin;       // 维持保证金
        uint64_t update_time;           // 更新时间
    };
    
    /**
     * @brief 账户状态信息
     */
    struct AccountStatus {
        std::string total_initial_margin;         // 当前所需起始保证金总额
        std::string total_maint_margin;           // 维持保证金总额
        std::string total_wallet_balance;         // 账户总余额
        std::string total_unrealized_profit;      // 持仓未实现盈亏总额
        std::string total_margin_balance;         // 保证金总余额
        std::string total_position_initial_margin; // 持仓所需起始保证金
        std::string total_open_order_initial_margin; // 当前挂单所需起始保证金
        std::string total_cross_wallet_balance;   // 全仓账户余额
        std::string total_cross_un_pnl;           // 全仓持仓未实现盈亏总额
        std::string available_balance;            // 可用余额
        std::string max_withdraw_amount;          // 最大可转出余额
        
        std::vector<AccountAsset> assets;         // 资产列表
        std::vector<PositionInfo> positions;      // 持仓列表
    };
    
    /**
     * @brief 配置结构
     */
    struct Config {
        std::string api_key;
        std::string api_secret;
        std::string base_url = "wss://testnet.binancefuture.com/ws-fapi/v1";
        uint32_t timeout_ms = 5000;
        uint32_t reconnect_interval_ms = 5000;
        uint32_t max_reconnect_attempts = 10;
        bool enable_auto_reconnect = true;
        
        Config() = default;
    };
    
    // 回调函数类型
    using BalanceCallback = std::function<void(const std::vector<AccountBalance>&)>;
    using AccountStatusCallback = std::function<void(const AccountStatus&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    BinanceAccountWebSocket();
    ~BinanceAccountWebSocket();
    
    // 禁用拷贝构造和赋值
    BinanceAccountWebSocket(const BinanceAccountWebSocket&) = delete;
    BinanceAccountWebSocket& operator=(const BinanceAccountWebSocket&) = delete;
    
    // 生命周期管理
    bool initialize(const Config& config);
    bool connect();
    void disconnect();
    void cleanup();
    
    // 状态查询
    bool is_connected() const;
    bool is_initialized() const;
    
    // 账户查询接口
    bool query_account_balance();
    bool query_account_status();
    
    // 回调设置
    void set_balance_callback(BalanceCallback callback);
    void set_account_status_callback(AccountStatusCallback callback);
    void set_error_callback(ErrorCallback callback);
    
    // 配置管理
    void set_config(const Config& config);
    Config get_config() const;
    
    // 错误处理
    std::string get_last_error() const;
    
private:
    // 内部方法
    void websocket_thread();
    void reconnect_thread();
    bool send_request(const nlohmann::json& request);
    void handle_message(const std::string& message);
    void handle_balance_response(const nlohmann::json& response);
    void handle_account_status_response(const nlohmann::json& response);
    void handle_account_update_event(const nlohmann::json& event);
    
    // 数据解析方法
    AccountBalance parse_account_balance(const nlohmann::json& balance_json);
    AccountAsset parse_account_asset(const nlohmann::json& asset_json);
    PositionInfo parse_position_info(const nlohmann::json& position_json);
    AccountStatus parse_account_status(const nlohmann::json& status_json);
    
    // 签名和认证
    std::string generate_signature(const std::string& query_string) const;
    std::string generate_request_id() const;
    uint64_t get_timestamp() const;
    
    // 错误处理
    void set_error(const std::string& error);
    void trigger_error_callback(const std::string& error);
    
    // 成员变量
    mutable std::mutex config_mutex_;
    mutable std::mutex error_mutex_;
    mutable std::mutex callback_mutex_;
    
    Config config_;
    std::string last_error_;
    
    std::atomic<bool> initialized_;
    std::atomic<bool> connected_;
    std::atomic<bool> should_reconnect_;
    std::atomic<bool> stop_threads_;
    
    // 线程和连接管理
    std::unique_ptr<std::thread> websocket_thread_;
    std::unique_ptr<std::thread> reconnect_thread_;
    std::unique_ptr<tes::utils::WebSocketClient> ws_client_;
    
    // 回调函数
    BalanceCallback balance_callback_;
    AccountStatusCallback account_status_callback_;
    ErrorCallback error_callback_;
    
    // 连接状态
    uint32_t reconnect_attempts_;
    std::chrono::steady_clock::time_point last_reconnect_time_;
};

} // namespace execution
} // namespace tes