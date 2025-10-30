#include "execution/binance_account_websocket.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <random>
#include <iostream>
#include <cstring>

namespace tes {
namespace execution {

BinanceAccountWebSocket::BinanceAccountWebSocket()
    : initialized_(false)
    , connected_(false)
    , should_reconnect_(false)
    , stop_threads_(false)
    , reconnect_attempts_(0) {
}

BinanceAccountWebSocket::~BinanceAccountWebSocket() {
    cleanup();
}

bool BinanceAccountWebSocket::initialize(const Config& config) {
    if (initialized_.load()) {
        return true;
    }
    
    try {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
        
        // 验证必要的配置
        if (config_.api_key.empty() || config_.api_secret.empty()) {
            set_error("API key and secret are required");
            return false;
        }
        
        if (config_.base_url.empty()) {
            set_error("Base URL is required");
            return false;
        }
        
        initialized_.store(true);
        return true;
        
    } catch (const std::exception& e) {
        set_error("Exception during initialization: " + std::string(e.what()));
        return false;
    }
}

bool BinanceAccountWebSocket::connect() {
    if (!initialized_.load()) {
        set_error("WebSocket client not initialized");
        return false;
    }
    
    if (connected_.load()) {
        return true;
    }
    
    try {
        stop_threads_.store(false);
        should_reconnect_.store(true);
        
        // 启动WebSocket线程
        websocket_thread_ = std::make_unique<std::thread>(&BinanceAccountWebSocket::websocket_thread, this);
        
        // 启动重连线程（如果启用自动重连）
        if (config_.enable_auto_reconnect) {
            reconnect_thread_ = std::make_unique<std::thread>(&BinanceAccountWebSocket::reconnect_thread, this);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        set_error("Exception during connection: " + std::string(e.what()));
        return false;
    }
}

void BinanceAccountWebSocket::disconnect() {
    should_reconnect_.store(false);
    connected_.store(false);
    stop_threads_.store(true);
    
    // 等待线程结束
    if (websocket_thread_ && websocket_thread_->joinable()) {
        websocket_thread_->join();
    }
    
    if (reconnect_thread_ && reconnect_thread_->joinable()) {
        reconnect_thread_->join();
    }
}

void BinanceAccountWebSocket::cleanup() {
    disconnect();
    
    websocket_thread_.reset();
    reconnect_thread_.reset();
    
    initialized_.store(false);
}

bool BinanceAccountWebSocket::is_connected() const {
    return connected_.load();
}

bool BinanceAccountWebSocket::is_initialized() const {
    return initialized_.load();
}

bool BinanceAccountWebSocket::query_account_balance() {
    // User Data Stream模式下，账户余额通过ACCOUNT_UPDATE事件自动推送
    // 这里只是触发一次REST API查询来获取初始数据
    if (!connected_.load()) {
        set_error("WebSocket not connected");
        return false;
    }
    
    // 在User Data Stream模式下，我们不需要主动查询
    // 账户数据会通过WebSocket自动推送
    std::cout << "User Data Stream模式：账户余额将通过WebSocket事件自动更新" << std::endl;
    return true;
}

bool BinanceAccountWebSocket::query_account_status() {
    // User Data Stream模式下，账户状态通过ACCOUNT_UPDATE事件自动推送
    if (!connected_.load()) {
        set_error("WebSocket not connected");
        return false;
    }
    
    // 在User Data Stream模式下，我们不需要主动查询
    // 账户数据会通过WebSocket自动推送
    std::cout << "User Data Stream模式：账户状态将通过WebSocket事件自动更新" << std::endl;
    return true;
}

void BinanceAccountWebSocket::set_balance_callback(BalanceCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    balance_callback_ = callback;
}

void BinanceAccountWebSocket::set_account_status_callback(AccountStatusCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    account_status_callback_ = callback;
}

void BinanceAccountWebSocket::set_error_callback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = callback;
}

void BinanceAccountWebSocket::set_config(const Config& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

BinanceAccountWebSocket::Config BinanceAccountWebSocket::get_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

std::string BinanceAccountWebSocket::get_last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

// 私有方法实现
void BinanceAccountWebSocket::websocket_thread() {
    std::cout << "[WebSocket] Starting WebSocket thread..." << std::endl;
    
    // 创建WebSocket客户端
    ws_client_ = std::make_unique<tes::utils::WebSocketClient>();
    
    // 设置回调函数
    ws_client_->setConnectCallback([this]() {
        connected_.store(true);
        std::cout << "[WebSocket] Connected successfully" << std::endl;
    });
    
    ws_client_->setDisconnectCallback([this]() {
        connected_.store(false);
        std::cout << "[WebSocket] Disconnected" << std::endl;
    });
    
    ws_client_->setMessageCallback([this](const std::string& message) {
        handle_message(message);
    });
    
    ws_client_->setErrorCallback([this](const std::string& error) {
        set_error("WebSocket error: " + error);
        std::cout << "[WebSocket] Error: " << error << std::endl;
    });
    
    // 连接到WebSocket服务器
    std::cout << "[WebSocket] Connecting to: " << config_.base_url << std::endl;
    if (!ws_client_->connect(config_.base_url)) {
        set_error("Failed to connect to WebSocket: " + ws_client_->getLastError());
        return;
    }
    
    // 保持连接活跃
    while (!stop_threads_.load() && ws_client_->isConnected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    // 断开连接
    if (ws_client_) {
        ws_client_->disconnect();
        ws_client_.reset();
    }
    
    connected_.store(false);
    std::cout << "[WebSocket] WebSocket thread stopped" << std::endl;
}

void BinanceAccountWebSocket::reconnect_thread() {
    while (!stop_threads_.load() && should_reconnect_.load()) {
        if (!connected_.load() && reconnect_attempts_ < config_.max_reconnect_attempts) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_reconnect_time_);
            
            if (elapsed.count() >= config_.reconnect_interval_ms) {
                std::cout << "Attempting to reconnect... (attempt " << (reconnect_attempts_ + 1) << ")" << std::endl;
                
                // 尝试重连
                reconnect_attempts_++;
                last_reconnect_time_ = now;
                
                // 这里应该实现实际的重连逻辑
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool BinanceAccountWebSocket::send_request(const nlohmann::json& request) {
    if (!connected_.load() || !ws_client_) {
        set_error("WebSocket not connected");
        return false;
    }
    
    try {
        std::string message = request.dump();
        // std::cout << "[WebSocket] Sending request: " << message << std::endl;
        
        if (!ws_client_->send(message)) {
            set_error("Failed to send WebSocket message: " + ws_client_->getLastError());
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        set_error("Failed to send request: " + std::string(e.what()));
        return false;
    }
}

void BinanceAccountWebSocket::handle_message(const std::string& message) {
    try {
        nlohmann::json response = nlohmann::json::parse(message);
        
        // 处理User Data Stream事件
        if (response.contains("e")) {
            std::string event_type = response["e"];
            
            if (event_type == "ACCOUNT_UPDATE") {
                // 账户更新事件
                std::cout << "[WebSocket] Received ACCOUNT_UPDATE event" << std::endl;
                handle_account_update_event(response);
            } else if (event_type == "ORDER_TRADE_UPDATE") {
                // 订单交易更新事件
                std::cout << "[WebSocket] Received ORDER_TRADE_UPDATE event" << std::endl;
                // 可以在这里处理订单更新
            } else {
                std::cout << "[WebSocket] Received unknown event: " << event_type << std::endl;
            }
        } else if (response.contains("id")) {
            // 处理API响应（如果有的话）
            if (response.contains("result") && response["result"].is_array()) {
                // 检查result数组中的第一个元素来判断响应类型
                if (!response["result"].empty()) {
                    const auto& first_item = response["result"][0];
                    if (first_item.contains("balance") && first_item.contains("crossWalletBalance")) {
                        // 这是余额响应
                        handle_balance_response(response);
                    } else if (first_item.contains("assets") && first_item.contains("positions")) {
                        // 这是账户状态响应
                        handle_account_status_response(response);
                    }
                }
            } else if (response.contains("result") && response["result"].is_object()) {
                // 单个对象结果，可能是账户状态
                const auto& result = response["result"];
                if (result.contains("assets") && result.contains("positions")) {
                    handle_account_status_response(response);
                }
            }
        } else if (response.contains("error")) {
            std::string error_msg = "API Error: " + response["error"].dump();
            set_error(error_msg);
            trigger_error_callback(error_msg);
        } else {
            std::cout << "[WebSocket] Received message: " << message << std::endl;
        }
        
    } catch (const std::exception& e) {
        set_error("Failed to parse WebSocket message: " + std::string(e.what()));
        std::cout << "[WebSocket] Parse error: " << e.what() << std::endl;
        std::cout << "[WebSocket] Raw message: " << message << std::endl;
    }
}

void BinanceAccountWebSocket::handle_balance_response(const nlohmann::json& response) {
    try {
        if (response.contains("result") && response["status"] == 200) {
            std::vector<AccountBalance> balances;
            
            for (const auto& balance_json : response["result"]) {
                AccountBalance balance = parse_account_balance(balance_json);
                balances.push_back(balance);
            }
            
            // 触发回调
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (balance_callback_) {
                balance_callback_(balances);
            }
        }
        
    } catch (const std::exception& e) {
        set_error("Exception in handle_balance_response: " + std::string(e.what()));
    }
}

void BinanceAccountWebSocket::handle_account_status_response(const nlohmann::json& response) {
    try {
        if (response.contains("result") && response["status"] == 200) {
            AccountStatus status = parse_account_status(response["result"]);
            
            // 触发回调
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (account_status_callback_) {
                account_status_callback_(status);
            }
        }
        
    } catch (const std::exception& e) {
        set_error("Exception in handle_account_status_response: " + std::string(e.what()));
    }
}

void BinanceAccountWebSocket::handle_account_update_event(const nlohmann::json& event) {
    try {
        std::cout << "[WebSocket] Processing ACCOUNT_UPDATE event" << std::endl;
        
        if (event.contains("a")) {
            const auto& account_data = event["a"];
            
            // 构建AccountStatus对象
            AccountStatus status;
            
            // 解析余额信息
            if (account_data.contains("B")) {
                for (const auto& balance : account_data["B"]) {
                    AccountAsset asset;
                    asset.asset = balance.value("a", "");
                    asset.wallet_balance = balance.value("wb", "0");
                    asset.cross_wallet_balance = balance.value("cw", "0");
                    asset.available_balance = balance.value("bc", "0");
                    status.assets.push_back(asset);
                }
            }
            
            // 解析持仓信息
            if (account_data.contains("P")) {
                for (const auto& position : account_data["P"]) {
                    PositionInfo pos;
                    pos.symbol = position.value("s", "");
                    pos.position_side = position.value("ps", "");
                    pos.position_amt = position.value("pa", "0");
                    pos.unrealized_profit = position.value("up", "0");
                    pos.isolated_margin = position.value("iw", "0");
                    status.positions.push_back(pos);
                    
                    std::cout << "[WebSocket] Position: " << pos.symbol 
                              << " Side: " << pos.position_side 
                              << " Amount: " << pos.position_amt 
                              << " PnL: " << pos.unrealized_profit << std::endl;
                }
            }
            
            // 触发回调
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (account_status_callback_) {
                account_status_callback_(status);
            }
        }
        
    } catch (const std::exception& e) {
        set_error("Exception in handle_account_update_event: " + std::string(e.what()));
        std::cout << "[WebSocket] Error processing ACCOUNT_UPDATE: " << e.what() << std::endl;
    }
}

// 数据解析方法
BinanceAccountWebSocket::AccountBalance BinanceAccountWebSocket::parse_account_balance(const nlohmann::json& balance_json) {
    AccountBalance balance;
    
    if (balance_json.contains("accountAlias")) {
        balance.account_alias = balance_json["accountAlias"];
    }
    if (balance_json.contains("asset")) {
        balance.asset = balance_json["asset"];
    }
    if (balance_json.contains("balance")) {
        balance.balance = balance_json["balance"];
    }
    if (balance_json.contains("crossWalletBalance")) {
        balance.cross_wallet_balance = balance_json["crossWalletBalance"];
    }
    if (balance_json.contains("crossUnPnl")) {
        balance.cross_un_pnl = balance_json["crossUnPnl"];
    }
    if (balance_json.contains("availableBalance")) {
        balance.available_balance = balance_json["availableBalance"];
    }
    if (balance_json.contains("maxWithdrawAmount")) {
        balance.max_withdraw_amount = balance_json["maxWithdrawAmount"];
    }
    if (balance_json.contains("marginAvailable")) {
        balance.margin_available = balance_json["marginAvailable"];
    }
    if (balance_json.contains("updateTime")) {
        balance.update_time = balance_json["updateTime"];
    }
    
    return balance;
}

BinanceAccountWebSocket::AccountAsset BinanceAccountWebSocket::parse_account_asset(const nlohmann::json& asset_json) {
    AccountAsset asset;
    
    if (asset_json.contains("asset")) {
        asset.asset = asset_json["asset"];
    }
    if (asset_json.contains("walletBalance")) {
        asset.wallet_balance = asset_json["walletBalance"];
    }
    if (asset_json.contains("unrealizedProfit")) {
        asset.unrealized_profit = asset_json["unrealizedProfit"];
    }
    if (asset_json.contains("marginBalance")) {
        asset.margin_balance = asset_json["marginBalance"];
    }
    if (asset_json.contains("maintMargin")) {
        asset.maint_margin = asset_json["maintMargin"];
    }
    if (asset_json.contains("initialMargin")) {
        asset.initial_margin = asset_json["initialMargin"];
    }
    if (asset_json.contains("positionInitialMargin")) {
        asset.position_initial_margin = asset_json["positionInitialMargin"];
    }
    if (asset_json.contains("openOrderInitialMargin")) {
        asset.open_order_initial_margin = asset_json["openOrderInitialMargin"];
    }
    if (asset_json.contains("crossWalletBalance")) {
        asset.cross_wallet_balance = asset_json["crossWalletBalance"];
    }
    if (asset_json.contains("crossUnPnl")) {
        asset.cross_un_pnl = asset_json["crossUnPnl"];
    }
    if (asset_json.contains("availableBalance")) {
        asset.available_balance = asset_json["availableBalance"];
    }
    if (asset_json.contains("maxWithdrawAmount")) {
        asset.max_withdraw_amount = asset_json["maxWithdrawAmount"];
    }
    if (asset_json.contains("updateTime")) {
        asset.update_time = asset_json["updateTime"];
    }
    
    return asset;
}

BinanceAccountWebSocket::PositionInfo BinanceAccountWebSocket::parse_position_info(const nlohmann::json& position_json) {
    PositionInfo position;
    
    if (position_json.contains("symbol")) {
        position.symbol = position_json["symbol"];
    }
    if (position_json.contains("positionSide")) {
        position.position_side = position_json["positionSide"];
    }
    if (position_json.contains("positionAmt")) {
        position.position_amt = position_json["positionAmt"];
    }
    if (position_json.contains("unrealizedProfit")) {
        position.unrealized_profit = position_json["unrealizedProfit"];
    }
    if (position_json.contains("isolatedMargin")) {
        position.isolated_margin = position_json["isolatedMargin"];
    }
    if (position_json.contains("notional")) {
        position.notional = position_json["notional"];
    }
    if (position_json.contains("isolatedWallet")) {
        position.isolated_wallet = position_json["isolatedWallet"];
    }
    if (position_json.contains("initialMargin")) {
        position.initial_margin = position_json["initialMargin"];
    }
    if (position_json.contains("maintMargin")) {
        position.maint_margin = position_json["maintMargin"];
    }
    if (position_json.contains("updateTime")) {
        position.update_time = position_json["updateTime"];
    }
    
    return position;
}

BinanceAccountWebSocket::AccountStatus BinanceAccountWebSocket::parse_account_status(const nlohmann::json& status_json) {
    AccountStatus status;
    
    // 解析总体信息
    if (status_json.contains("totalInitialMargin")) {
        status.total_initial_margin = status_json["totalInitialMargin"];
    }
    if (status_json.contains("totalMaintMargin")) {
        status.total_maint_margin = status_json["totalMaintMargin"];
    }
    if (status_json.contains("totalWalletBalance")) {
        status.total_wallet_balance = status_json["totalWalletBalance"];
    }
    if (status_json.contains("totalUnrealizedProfit")) {
        status.total_unrealized_profit = status_json["totalUnrealizedProfit"];
    }
    if (status_json.contains("totalMarginBalance")) {
        status.total_margin_balance = status_json["totalMarginBalance"];
    }
    if (status_json.contains("totalPositionInitialMargin")) {
        status.total_position_initial_margin = status_json["totalPositionInitialMargin"];
    }
    if (status_json.contains("totalOpenOrderInitialMargin")) {
        status.total_open_order_initial_margin = status_json["totalOpenOrderInitialMargin"];
    }
    if (status_json.contains("totalCrossWalletBalance")) {
        status.total_cross_wallet_balance = status_json["totalCrossWalletBalance"];
    }
    if (status_json.contains("totalCrossUnPnl")) {
        status.total_cross_un_pnl = status_json["totalCrossUnPnl"];
    }
    if (status_json.contains("availableBalance")) {
        status.available_balance = status_json["availableBalance"];
    }
    if (status_json.contains("maxWithdrawAmount")) {
        status.max_withdraw_amount = status_json["maxWithdrawAmount"];
    }
    
    // 解析资产列表
    if (status_json.contains("assets")) {
        for (const auto& asset_json : status_json["assets"]) {
            AccountAsset asset = parse_account_asset(asset_json);
            status.assets.push_back(asset);
        }
    }
    
    // 解析持仓列表
    if (status_json.contains("positions")) {
        for (const auto& position_json : status_json["positions"]) {
            PositionInfo position = parse_position_info(position_json);
            status.positions.push_back(position);
        }
    }
    
    return status;
}

// 签名和认证方法
std::string BinanceAccountWebSocket::generate_signature(const std::string& query_string) const {
    unsigned char* digest = HMAC(EVP_sha256(),
                                config_.api_secret.c_str(), config_.api_secret.length(),
                                reinterpret_cast<const unsigned char*>(query_string.c_str()), query_string.length(),
                                nullptr, nullptr);
    
    std::ostringstream signature_stream;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        signature_stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    
    return signature_stream.str();
}

std::string BinanceAccountWebSocket::generate_request_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    std::ostringstream id_stream;
    id_stream << "req_" << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()
              << "_" << dis(gen);
    
    return id_stream.str();
}

uint64_t BinanceAccountWebSocket::get_timestamp() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void BinanceAccountWebSocket::set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

void BinanceAccountWebSocket::trigger_error_callback(const std::string& error) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) {
        error_callback_(error);
    }
}

} // namespace execution
} // namespace tes