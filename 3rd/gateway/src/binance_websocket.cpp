#include "binance_websocket.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <nlohmann/json.hpp>

namespace trading {

BinanceWebSocket::BinanceWebSocket(const ExchangeConfig& config)
    : config_(config)
    , apiKey_(config.getCurrentApiKey())
    , apiSecret_(config.getCurrentApiSecret())
    , timeoutMs_(config.timeoutMs)
    , reconnectIntervalMs_(5000)
    , status_(ConnectionStatus::DISCONNECTED)
    , running_(false)
    , userDataStreamActive_(false)
    , sessionAuthenticated_(false)
    , wsApiConnected_(false)
    , subscriptionId_(-1)
    , requestId_(1)
    , httpClient_(std::make_unique<ix::HttpClient>())
{
    // 配置HTTP客户端的TLS选项
    ix::SocketTLSOptions tlsOptions;
    tlsOptions.tls = true;
    tlsOptions.caFile = "SYSTEM";  // 使用系统证书
    tlsOptions.disable_hostname_validation = false;  // 启用主机名验证
    httpClient_->setTLSOptions(tlsOptions);
    
    // 设置基础API URL和WebSocket URL
    if (config_.testnet) {
        baseApiUrl_ = "https://testnet.binancefuture.com";
        wsUrl_ = "wss://stream.binancefuture.com";  // 市场数据流
        wsApiUrl_ = "wss://testnet.binancefuture.com/ws-fapi/v1";  // WebSocket API (备用)
        userDataStreamBaseUrl_ = "wss://stream.binancefuture.com/ws/";  // 用户数据流基础URL
    } else {
        baseApiUrl_ = "https://fapi.binance.com";
        wsUrl_ = "wss://fstream.binance.com";  // 市场数据流
        wsApiUrl_ = "wss://ws-fapi.binance.com/ws-fapi/v1";  // WebSocket API (备用)
        userDataStreamBaseUrl_ = "wss://fstream.binance.com/ws/";  // 用户数据流基础URL
    }
    
    webSocket_ = std::make_unique<ix::WebSocket>();
    webSocket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        onWebSocketMessage(msg);
    });
    
    // 初始化WebSocket API连接
    wsApiSocket_ = std::make_unique<ix::WebSocket>();
    wsApiSocket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        onWebSocketApiMessage(msg);
    });
}

BinanceWebSocket::~BinanceWebSocket() {
    disconnect();
}

bool BinanceWebSocket::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (status_ == ConnectionStatus::CONNECTED) {
        return true;
    }
    
    std::cout << "[INFO] Connecting to Binance WebSocket..." << std::endl;
    
    // 检查签名类型，决定连接方式
    if (config_.signatureType == "ed25519") {
        std::cout << "[INFO] Using Ed25519 signature, connecting via WebSocket API session authentication..." << std::endl;
        
        // 对于Ed25519，直接连接到WebSocket API并进行会话认证
        return sessionLogon();
    } else {
        std::cout << "[INFO] Using HMAC-SHA256 signature, connecting via REST API listenKey..." << std::endl;
        
        // 使用REST API创建listenKey (期货API的正确方式)
        if (!createListenKey()) {
            std::cout << "[ERROR] Failed to create listenKey via REST API" << std::endl;
            setConnectionStatus(ConnectionStatus::ERROR);
            return false;
        }
        
        // 连接用户数据流URL
        std::string userDataStreamUrl = userDataStreamBaseUrl_ + listenKey_;
        std::cout << "[INFO] Connecting to user data stream: " << userDataStreamUrl << std::endl;
        
        webSocket_->setUrl(userDataStreamUrl);
        
        // 设置TLS选项
        ix::SocketTLSOptions tlsOptions;
        tlsOptions.tls = true;
        tlsOptions.caFile = "NONE";
        tlsOptions.disable_hostname_validation = true;
        webSocket_->setTLSOptions(tlsOptions);
        
        webSocket_->start();
        
        // 等待连接建立
        int attempts = 0;
        while (status_ != ConnectionStatus::CONNECTED && attempts < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            attempts++;
        }
        
        if (status_ == ConnectionStatus::CONNECTED) {
            running_ = true;
            userDataStreamActive_ = true;
            startHeartbeat();
            std::cout << "[INFO] Successfully connected to Binance WebSocket" << std::endl;
            return true;
        } else {
            std::cout << "[ERROR] Failed to connect to Binance WebSocket" << std::endl;
            setConnectionStatus(ConnectionStatus::ERROR);
            return false;
        }
    }
}

void BinanceWebSocket::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << "[DEBUG] Disconnect called - sessionAuthenticated_: " << sessionAuthenticated_ 
              << ", subscriptionId_: " << subscriptionId_ << std::endl;
    
    running_ = false;
    userDataStreamActive_ = false;
    
    stopHeartbeat();
    
    // 取消订阅用户数据流
    if (sessionAuthenticated_ && subscriptionId_ >= 0) {
        std::cout << "[DEBUG] Calling unsubscribeUserDataStream..." << std::endl;
        unsubscribeUserDataStream();
    } else {
        std::cout << "[DEBUG] Skipping unsubscribe - sessionAuthenticated_: " << sessionAuthenticated_ 
                  << ", subscriptionId_: " << subscriptionId_ << std::endl;
    }
    
    // 登出会话
    if (sessionAuthenticated_) {
        sessionLogout();
    }
    
    if (webSocket_) {
        webSocket_->stop();
    }
    
    // 清理状态
    sessionAuthenticated_ = false;
    subscriptionId_ = -1;
    sessionId_.clear();
    
    setConnectionStatus(ConnectionStatus::DISCONNECTED);
}

bool BinanceWebSocket::isConnected() const {
    return status_ == ConnectionStatus::CONNECTED;
}

ConnectionStatus BinanceWebSocket::getStatus() const {
    return status_;
}

bool BinanceWebSocket::startUserDataStream() {
    return createListenKey();
}

bool BinanceWebSocket::keepaliveUserDataStream() {
    return extendListenKey();
}

bool BinanceWebSocket::closeUserDataStream() {
    return deleteListenKey();
}

void BinanceWebSocket::setAccountUpdateCallback(std::function<void(const AccountUpdate&)> callback) {
    accountUpdateCallback_ = callback;
}

void BinanceWebSocket::setPositionUpdateCallback(std::function<void(const PositionUpdate&)> callback) {
    positionUpdateCallback_ = callback;
}

void BinanceWebSocket::setOrderUpdateCallback(std::function<void(const OrderUpdate&)> callback) {
    orderUpdateCallback_ = callback;
}

void BinanceWebSocket::setConnectionStatusCallback(std::function<void(ConnectionStatus)> callback) {
    connectionStatusCallback_ = callback;
}

void BinanceWebSocket::setErrorCallback(std::function<void(const std::string&)> callback) {
    errorCallback_ = callback;
}

void BinanceWebSocket::setAccountBalanceCallback(std::function<void(const AccountBalanceResponse&)> callback) {
    accountBalanceCallback_ = callback;
}

void BinanceWebSocket::setAccountInfoCallback(std::function<void(const AccountInfoResponse&)> callback) {
    accountInfoCallback_ = callback;
}

void BinanceWebSocket::setOrderResponseCallback(std::function<void(const OrderResponse&)> callback) {
    orderResponseCallback_ = callback;
}

void BinanceWebSocket::setDepthUpdateCallback(std::function<void(const DepthUpdate&)> callback) {
    depthUpdateCallback_ = callback;
}

void BinanceWebSocket::setTradeLiteCallback(std::function<void(const TradeLite&)> callback) {
    tradeLiteCallback_ = callback;
}

void BinanceWebSocket::setApiCredentials(const std::string& apiKey, const std::string& apiSecret) {
    apiKey_ = apiKey;
    apiSecret_ = apiSecret;
}

void BinanceWebSocket::setTimeout(int timeoutMs) {
    timeoutMs_ = timeoutMs;
}

void BinanceWebSocket::setReconnectInterval(int intervalMs) {
    reconnectIntervalMs_ = intervalMs;
}

void BinanceWebSocket::requestAccountBalance(const std::string& requestId) {
    if (!wsApiConnected_) {
        std::cout << "WebSocket API not connected" << std::endl;
        return;
    }

    // 检查是否使用会话认证
    if (config_.signatureType == "ed25519" && sessionAuthenticated_) {
        // 会话认证模式下，也需要timestamp参数
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::ostringstream params;
        params << "\"recvWindow\":5000,\"timestamp\":" << timestamp;
        
        // 发送v2/account.balance请求
        sendWebSocketApiRequest("v2/account.balance", params.str());
    } else {
        // 传统模式，需要API密钥和签名
        // 生成时间戳
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // 构建查询字符串用于签名 - 按字母顺序排序参数
        std::string queryString = "apiKey=" + apiKey_ + "&timestamp=" + std::to_string(timestamp);
        std::string signature = generateWebSocketSignature(queryString);
        
        // 构建参数
        std::ostringstream params;
        params << "\"apiKey\":\"" << apiKey_ << "\","
               << "\"timestamp\":" << timestamp << ","
               << "\"signature\":\"" << signature << "\"";
        
        // 发送v2/account.balance请求
        sendWebSocketApiRequest("v2/account.balance", params.str());
    }
}

void BinanceWebSocket::requestAccountInfo(const std::string& requestId) {
    if (!wsApiConnected_) {
        std::cout << "WebSocket API not connected" << std::endl;
        return;
    }

    // 检查是否使用会话认证
    if (config_.signatureType == "ed25519" && sessionAuthenticated_) {
        // 会话认证模式下，也需要timestamp参数
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::ostringstream params;
        params << "\"recvWindow\":5000,\"timestamp\":" << timestamp;
        
        // 发送v2/account.status请求
        sendWebSocketApiRequest("v2/account.status", params.str());
    } else {
        // 传统模式，需要API密钥和签名
        // 生成时间戳
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // 构建查询字符串用于签名 - 按字母顺序排序参数
        std::string queryString = "apiKey=" + apiKey_ + "&timestamp=" + std::to_string(timestamp);
        std::string signature = generateWebSocketSignature(queryString);
        
        // 构建参数
        std::ostringstream params;
        params << "\"apiKey\":\"" << apiKey_ << "\","
               << "\"timestamp\":" << timestamp << ","
               << "\"signature\":\"" << signature << "\"";
        
        // 发送v2/account.status请求
        sendWebSocketApiRequest("v2/account.status", params.str());
    }
}

void BinanceWebSocket::requestPositionInfo(const std::string& requestId) {
    if (!wsApiConnected_) {
        std::cout << "WebSocket API not connected" << std::endl;
        return;
    }

    // 检查是否使用会话认证
    if (config_.signatureType == "ed25519" && sessionAuthenticated_) {
        // 会话认证模式下，仍需要timestamp参数
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::ostringstream params;
        params << "\"timestamp\":" << timestamp << ","
               << "\"recvWindow\":5000";
        
        // 发送v2/account.position请求
        sendWebSocketApiRequest("v2/account.position", params.str());
    } else {
        // 传统模式，需要API密钥和签名
        // 生成时间戳
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // 构建查询字符串用于签名 - 按字母顺序排序参数
        std::string queryString = "apiKey=" + apiKey_ + "&timestamp=" + std::to_string(timestamp);
        std::string signature = generateWebSocketSignature(queryString);
        
        // 构建参数
        std::ostringstream params;
        params << "\"apiKey\":\"" << apiKey_ << "\","
               << "\"timestamp\":" << timestamp << ","
               << "\"signature\":\"" << signature << "\"";
        
        // 发送v2/account.position请求
        sendWebSocketApiRequest("v2/account.position", params.str());
    }
}

void BinanceWebSocket::onWebSocketMessage(const ix::WebSocketMessagePtr& msg) {
    switch (msg->type) {
        case ix::WebSocketMessageType::Open:
            onWebSocketOpen();
            break;
        case ix::WebSocketMessageType::Close:
            onWebSocketClose();
            break;
        case ix::WebSocketMessageType::Error:
            onWebSocketError(msg->errorInfo.reason);
            break;
        case ix::WebSocketMessageType::Message:
            parseMessage(msg->str);
            break;
        default:
            break;
    }
}

void BinanceWebSocket::onWebSocketOpen() {
    setConnectionStatus(ConnectionStatus::CONNECTED);
    lastHeartbeat_ = std::chrono::steady_clock::now();
    std::cout << "Binance WebSocket connected" << std::endl;
}

void BinanceWebSocket::onWebSocketClose() {
    setConnectionStatus(ConnectionStatus::DISCONNECTED);
    std::cout << "Binance WebSocket disconnected" << std::endl;
    
    if (running_) {
        // 尝试重连
        attemptReconnect();
    }
}

void BinanceWebSocket::onWebSocketError(const std::string& error) {
    setConnectionStatus(ConnectionStatus::ERROR);
    if (errorCallback_) {
        errorCallback_("WebSocket error: " + error);
    }
    std::cerr << "Binance WebSocket error: " << error << std::endl;
}

void BinanceWebSocket::parseMessage(const std::string& message) {
    // 添加调试信息
    std::cout << "[DEBUG] Received WebSocket message: " << message << std::endl;
    
    yyjson_doc* doc = yyjson_read(message.c_str(), message.length(), 0);
    if (!doc) {
        if (errorCallback_) {
            errorCallback_("Failed to parse JSON message");
        }
        return;
    }
    
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root) {
        yyjson_doc_free(doc);
        return;
    }
    
    // 检查是否是用户数据流事件
    yyjson_val* e_val = yyjson_obj_get(root, "e");
    if (e_val) {
        std::string eventType = yyjson_get_str(e_val);
        
        if (eventType == "ACCOUNT_UPDATE") {
            parseAccountUpdate(root);
        } else if (eventType == "ORDER_TRADE_UPDATE") {
            parseOrderUpdate(root);
        } else if (eventType == "ORDER_TRADE_LITE") {
            parseTradeLite(root);
        } else if (eventType == "depthUpdate") {
            // 处理深度更新事件
            parseDepthUpdate(root);
        }
    } else {
        // 检查是否是深度更新消息（通过stream字段识别）
        yyjson_val* stream_val = yyjson_obj_get(root, "stream");
        if (stream_val) {
            std::string streamName = yyjson_get_str(stream_val);
            if (streamName.find("@depth") != std::string::npos) {
                // 这是深度更新消息，解析data字段
                yyjson_val* data_val = yyjson_obj_get(root, "data");
                if (data_val) {
                    parseDepthUpdate(data_val);
                }
            }
        } else {
            // 直接检查是否包含深度更新字段
            yyjson_val* u_val = yyjson_obj_get(root, "u");
            yyjson_val* U_val = yyjson_obj_get(root, "U");
            if (u_val && U_val) {
                // 这是深度更新消息
                parseDepthUpdate(root);
            }
        }
    }
    
    yyjson_doc_free(doc);
}

void BinanceWebSocket::parseAccountUpdate(yyjson_val* root) {
    AccountUpdate update;
    
    // 解析基本信息
    yyjson_val* eventTimeVal = yyjson_obj_get(root, "E");
    if (eventTimeVal && yyjson_is_int(eventTimeVal)) {
        update.eventTime = yyjson_get_int(eventTimeVal);
    }
    
    yyjson_val* transactionTimeVal = yyjson_obj_get(root, "T");
    if (transactionTimeVal && yyjson_is_int(transactionTimeVal)) {
        update.transactionTime = yyjson_get_int(transactionTimeVal);
    }
    
    update.eventType = "ACCOUNT_UPDATE";
    
    // 解析账户数据
    yyjson_val* accountVal = yyjson_obj_get(root, "a");
    if (!accountVal || !yyjson_is_obj(accountVal)) {
        return;
    }
    
    // 解析更新原因
    yyjson_val* reasonVal = yyjson_obj_get(accountVal, "m");
    if (reasonVal && yyjson_is_str(reasonVal)) {
        update.updateReason = yyjson_get_str(reasonVal);
    }
    
    // 解析余额信息
    yyjson_val* balancesArr = yyjson_obj_get(accountVal, "B");
    if (balancesArr && yyjson_is_arr(balancesArr)) {
        size_t idx, max;
        yyjson_val* balanceVal;
        yyjson_arr_foreach(balancesArr, idx, max, balanceVal) {
            if (!yyjson_is_obj(balanceVal)) continue;
            
            AssetBalance balance;
            
            yyjson_val* assetVal = yyjson_obj_get(balanceVal, "a");
            if (assetVal && yyjson_is_str(assetVal)) {
                balance.asset = yyjson_get_str(assetVal);
            }
            
            yyjson_val* walletBalanceVal = yyjson_obj_get(balanceVal, "wb");
            if (walletBalanceVal && yyjson_is_str(walletBalanceVal)) {
                balance.walletBalance = yyjson_get_str(walletBalanceVal);
            }
            
            yyjson_val* crossWalletBalanceVal = yyjson_obj_get(balanceVal, "cw");
            if (crossWalletBalanceVal && yyjson_is_str(crossWalletBalanceVal)) {
                balance.crossWalletBalance = yyjson_get_str(crossWalletBalanceVal);
            }
            
            yyjson_val* balanceChangeVal = yyjson_obj_get(balanceVal, "bc");
            if (balanceChangeVal && yyjson_is_str(balanceChangeVal)) {
                balance.balanceChange = yyjson_get_str(balanceChangeVal);
            }
            
            update.balances.push_back(balance);
        }
    }
    
    // 解析仓位信息
    yyjson_val* positionsArr = yyjson_obj_get(accountVal, "P");
    if (positionsArr && yyjson_is_arr(positionsArr)) {
        size_t idx, max;
        yyjson_val* positionVal;
        yyjson_arr_foreach(positionsArr, idx, max, positionVal) {
            if (!yyjson_is_obj(positionVal)) continue;
            
            Position position;
            
            yyjson_val* symbolVal = yyjson_obj_get(positionVal, "s");
            if (symbolVal && yyjson_is_str(symbolVal)) {
                position.symbol = yyjson_get_str(symbolVal);
            }
            
            yyjson_val* positionAmountVal = yyjson_obj_get(positionVal, "pa");
            if (positionAmountVal && yyjson_is_str(positionAmountVal)) {
                position.positionAmount = yyjson_get_str(positionAmountVal);
            }
            
            yyjson_val* entryPriceVal = yyjson_obj_get(positionVal, "ep");
            if (entryPriceVal && yyjson_is_str(entryPriceVal)) {
                position.entryPrice = yyjson_get_str(entryPriceVal);
            }
            
            yyjson_val* breakEvenPriceVal = yyjson_obj_get(positionVal, "bep");
            if (breakEvenPriceVal && yyjson_is_str(breakEvenPriceVal)) {
                position.breakEvenPrice = yyjson_get_str(breakEvenPriceVal);
            }
            
            yyjson_val* cumulativeRealizedVal = yyjson_obj_get(positionVal, "cr");
            if (cumulativeRealizedVal && yyjson_is_str(cumulativeRealizedVal)) {
                position.cumulativeRealized = yyjson_get_str(cumulativeRealizedVal);
            }
            
            yyjson_val* unrealizedPnlVal = yyjson_obj_get(positionVal, "up");
            if (unrealizedPnlVal && yyjson_is_str(unrealizedPnlVal)) {
                position.unrealizedPnl = yyjson_get_str(unrealizedPnlVal);
            }
            
            yyjson_val* marginTypeVal = yyjson_obj_get(positionVal, "mt");
            if (marginTypeVal && yyjson_is_str(marginTypeVal)) {
                position.marginType = yyjson_get_str(marginTypeVal);
            }
            
            yyjson_val* isolatedWalletVal = yyjson_obj_get(positionVal, "iw");
            if (isolatedWalletVal && yyjson_is_str(isolatedWalletVal)) {
                position.isolatedWallet = yyjson_get_str(isolatedWalletVal);
            }
            
            yyjson_val* positionSideVal = yyjson_obj_get(positionVal, "ps");
            if (positionSideVal && yyjson_is_str(positionSideVal)) {
                position.positionSide = yyjson_get_str(positionSideVal);
            }
            
            update.positions.push_back(position);
        }
    }
    
    // 调用回调函数
    if (accountUpdateCallback_) {
        accountUpdateCallback_(update);
    }
}

void BinanceWebSocket::parseOrderUpdate(yyjson_val* root) {
    // 订单更新解析实现
    OrderUpdate update;
    
    // 解析事件基本信息
    yyjson_val* eventTypeVal = yyjson_obj_get(root, "e");
    if (eventTypeVal && yyjson_is_str(eventTypeVal)) {
        update.eventType = yyjson_get_str(eventTypeVal);
    }
    
    yyjson_val* eventTimeVal = yyjson_obj_get(root, "E");
    if (eventTimeVal && yyjson_is_int(eventTimeVal)) {
        update.eventTime = yyjson_get_int(eventTimeVal);
    }
    
    yyjson_val* transactionTimeVal = yyjson_obj_get(root, "T");
    if (transactionTimeVal && yyjson_is_int(transactionTimeVal)) {
        update.transactionTime = yyjson_get_int(transactionTimeVal);
    }
    
    yyjson_val* orderVal = yyjson_obj_get(root, "o");
    if (!orderVal || !yyjson_is_obj(orderVal)) {
        return;
    }
    
    // 解析订单基本信息
    yyjson_val* symbolVal = yyjson_obj_get(orderVal, "s");
    if (symbolVal && yyjson_is_str(symbolVal)) {
        update.symbol = yyjson_get_str(symbolVal);
    }
    
    yyjson_val* clientOrderIdVal = yyjson_obj_get(orderVal, "c");
    if (clientOrderIdVal && yyjson_is_str(clientOrderIdVal)) {
        update.clientOrderId = yyjson_get_str(clientOrderIdVal);
    }
    
    yyjson_val* orderIdVal = yyjson_obj_get(orderVal, "i");
    if (orderIdVal && yyjson_is_int(orderIdVal)) {
        update.orderId = std::to_string(yyjson_get_int(orderIdVal));
    }
    
    yyjson_val* sideVal = yyjson_obj_get(orderVal, "S");
    if (sideVal && yyjson_is_str(sideVal)) {
        update.side = yyjson_get_str(sideVal);
    }
    
    yyjson_val* orderTypeVal = yyjson_obj_get(orderVal, "o");
    if (orderTypeVal && yyjson_is_str(orderTypeVal)) {
        update.orderType = yyjson_get_str(orderTypeVal);
    }
    
    yyjson_val* timeInForceVal = yyjson_obj_get(orderVal, "f");
    if (timeInForceVal && yyjson_is_str(timeInForceVal)) {
        update.timeInForce = yyjson_get_str(timeInForceVal);
    }
    
    // 解析价格和数量信息
    yyjson_val* originalQuantityVal = yyjson_obj_get(orderVal, "q");
    if (originalQuantityVal && yyjson_is_str(originalQuantityVal)) {
        update.originalQuantity = yyjson_get_str(originalQuantityVal);
    }
    
    yyjson_val* originalPriceVal = yyjson_obj_get(orderVal, "p");
    if (originalPriceVal && yyjson_is_str(originalPriceVal)) {
        update.originalPrice = yyjson_get_str(originalPriceVal);
    }
    
    yyjson_val* averagePriceVal = yyjson_obj_get(orderVal, "ap");
    if (averagePriceVal && yyjson_is_str(averagePriceVal)) {
        update.averagePrice = yyjson_get_str(averagePriceVal);
    }
    
    yyjson_val* stopPriceVal = yyjson_obj_get(orderVal, "sp");
    if (stopPriceVal && yyjson_is_str(stopPriceVal)) {
        update.stopPrice = yyjson_get_str(stopPriceVal);
    }
    
    // 解析执行信息
    yyjson_val* executionTypeVal = yyjson_obj_get(orderVal, "x");
    if (executionTypeVal && yyjson_is_str(executionTypeVal)) {
        update.executionType = yyjson_get_str(executionTypeVal);
    }
    
    yyjson_val* orderStatusVal = yyjson_obj_get(orderVal, "X");
    if (orderStatusVal && yyjson_is_str(orderStatusVal)) {
        update.orderStatus = yyjson_get_str(orderStatusVal);
    }
    
    yyjson_val* lastExecutedQuantityVal = yyjson_obj_get(orderVal, "l");
    if (lastExecutedQuantityVal && yyjson_is_str(lastExecutedQuantityVal)) {
        update.lastExecutedQuantity = yyjson_get_str(lastExecutedQuantityVal);
    }
    
    yyjson_val* cumulativeFilledQuantityVal = yyjson_obj_get(orderVal, "z");
    if (cumulativeFilledQuantityVal && yyjson_is_str(cumulativeFilledQuantityVal)) {
        update.cumulativeFilledQuantity = yyjson_get_str(cumulativeFilledQuantityVal);
    }
    
    yyjson_val* lastExecutedPriceVal = yyjson_obj_get(orderVal, "L");
    if (lastExecutedPriceVal && yyjson_is_str(lastExecutedPriceVal)) {
        update.lastExecutedPrice = yyjson_get_str(lastExecutedPriceVal);
    }
    
    // 解析手续费信息
    yyjson_val* commissionAssetVal = yyjson_obj_get(orderVal, "N");
    if (commissionAssetVal && yyjson_is_str(commissionAssetVal)) {
        update.commissionAsset = yyjson_get_str(commissionAssetVal);
    }
    
    yyjson_val* commissionAmountVal = yyjson_obj_get(orderVal, "n");
    if (commissionAmountVal && yyjson_is_str(commissionAmountVal)) {
        update.commissionAmount = yyjson_get_str(commissionAmountVal);
    }
    
    // 解析其他信息
    yyjson_val* tradeTimeVal = yyjson_obj_get(orderVal, "T");
    if (tradeTimeVal && yyjson_is_int(tradeTimeVal)) {
        update.tradeTime = yyjson_get_int(tradeTimeVal);
    }
    
    yyjson_val* tradeIdVal = yyjson_obj_get(orderVal, "t");
    if (tradeIdVal && yyjson_is_int(tradeIdVal)) {
        update.tradeId = yyjson_get_int(tradeIdVal);
    }
    
    yyjson_val* buyerOrderValueVal = yyjson_obj_get(orderVal, "b");
    if (buyerOrderValueVal && yyjson_is_str(buyerOrderValueVal)) {
        update.buyerOrderValue = yyjson_get_str(buyerOrderValueVal);
    }
    
    yyjson_val* sellerOrderValueVal = yyjson_obj_get(orderVal, "a");
    if (sellerOrderValueVal && yyjson_is_str(sellerOrderValueVal)) {
        update.sellerOrderValue = yyjson_get_str(sellerOrderValueVal);
    }
    
    yyjson_val* isMakerSideVal = yyjson_obj_get(orderVal, "m");
    if (isMakerSideVal && yyjson_is_bool(isMakerSideVal)) {
        update.isMakerSide = yyjson_get_bool(isMakerSideVal);
    }
    
    yyjson_val* isReduceOnlyVal = yyjson_obj_get(orderVal, "R");
    if (isReduceOnlyVal && yyjson_is_bool(isReduceOnlyVal)) {
        update.isReduceOnly = yyjson_get_bool(isReduceOnlyVal);
    }
    
    yyjson_val* workingTypeVal = yyjson_obj_get(orderVal, "wt");
    if (workingTypeVal && yyjson_is_str(workingTypeVal)) {
        update.workingType = yyjson_get_str(workingTypeVal);
    }
    
    yyjson_val* originalOrderTypeVal = yyjson_obj_get(orderVal, "ot");
    if (originalOrderTypeVal && yyjson_is_str(originalOrderTypeVal)) {
        update.originalOrderType = yyjson_get_str(originalOrderTypeVal);
    }
    
    yyjson_val* positionSideVal = yyjson_obj_get(orderVal, "ps");
    if (positionSideVal && yyjson_is_str(positionSideVal)) {
        update.positionSide = yyjson_get_str(positionSideVal);
    }
    
    yyjson_val* isClosePositionVal = yyjson_obj_get(orderVal, "cp");
    if (isClosePositionVal && yyjson_is_bool(isClosePositionVal)) {
        update.isClosePosition = yyjson_get_bool(isClosePositionVal);
    }
    
    yyjson_val* activationPriceVal = yyjson_obj_get(orderVal, "AP");
    if (activationPriceVal && yyjson_is_str(activationPriceVal)) {
        update.activationPrice = yyjson_get_str(activationPriceVal);
    }
    
    yyjson_val* callbackRateVal = yyjson_obj_get(orderVal, "cr");
    if (callbackRateVal && yyjson_is_str(callbackRateVal)) {
        update.callbackRate = yyjson_get_str(callbackRateVal);
    }
    
    yyjson_val* realizedProfitVal = yyjson_obj_get(orderVal, "rp");
    if (realizedProfitVal && yyjson_is_str(realizedProfitVal)) {
        update.realizedProfit = yyjson_get_str(realizedProfitVal);
    }
    
    yyjson_val* selfTradePreventionModeVal = yyjson_obj_get(orderVal, "V");
    if (selfTradePreventionModeVal && yyjson_is_str(selfTradePreventionModeVal)) {
        update.selfTradePreventionMode = yyjson_get_str(selfTradePreventionModeVal);
    }
    
    yyjson_val* priceMatchModeVal = yyjson_obj_get(orderVal, "pm");
    if (priceMatchModeVal && yyjson_is_str(priceMatchModeVal)) {
        update.priceMatchMode = yyjson_get_str(priceMatchModeVal);
    }
    
    yyjson_val* goodTillDateVal = yyjson_obj_get(orderVal, "gtd");
    if (goodTillDateVal && yyjson_is_int(goodTillDateVal)) {
        update.goodTillDate = yyjson_get_int(goodTillDateVal);
    }
    
    // 调用回调函数
    if (orderUpdateCallback_) {
        orderUpdateCallback_(update);
    }
}

bool BinanceWebSocket::createListenKey() {
    std::cout << "[DEBUG] Creating listen key..." << std::endl;
    std::cout << "[DEBUG] Base API URL: " << baseApiUrl_ << std::endl;
    
    std::string response = makeHttpRequest("POST", "/fapi/v1/listenKey");
    std::cout << "[DEBUG] HTTP Response: " << response << std::endl;
    
    if (response.empty()) {
        std::cout << "[ERROR] Empty response from createListenKey API" << std::endl;
        return false;
    }
    
    yyjson_doc* doc = yyjson_read(response.c_str(), response.length(), 0);
    if (!doc) {
        std::cout << "[ERROR] Failed to parse JSON response: " << response << std::endl;
        return false;
    }
    
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* listenKeyVal = yyjson_obj_get(root, "listenKey");
    
    if (listenKeyVal && yyjson_is_str(listenKeyVal)) {
        listenKey_ = yyjson_get_str(listenKeyVal);
        std::cout << "[DEBUG] Successfully created listen key: " << listenKey_ << std::endl;
        yyjson_doc_free(doc);
        return true;
    }
    
    std::cout << "[ERROR] No listenKey found in response" << std::endl;
    yyjson_doc_free(doc);
    return false;
}

bool BinanceWebSocket::extendListenKey() {
    if (listenKey_.empty()) {
        return false;
    }
    
    std::string response = makeHttpRequest("PUT", "/fapi/v1/listenKey");
    return !response.empty();
}

bool BinanceWebSocket::deleteListenKey() {
    if (listenKey_.empty()) {
        return true;
    }
    
    std::string response = makeHttpRequest("DELETE", "/fapi/v1/listenKey");
    return !response.empty();
}

std::string BinanceWebSocket::makeHttpRequest(const std::string& method, const std::string& endpoint, const std::string& params) {
    std::string url = baseApiUrl_ + endpoint;
    std::cout << "[DEBUG] Making HTTP request: " << method << " " << url << std::endl;
    std::cout << "[DEBUG] API Key: " << (apiKey_.empty() ? "EMPTY" : "SET") << std::endl;
    
    // 添加更详细的调试信息
    std::cout << "[DEBUG] Creating HTTP request args..." << std::endl;
    ix::HttpRequestArgsPtr args = httpClient_->createRequest();
    args->extraHeaders["X-MBX-APIKEY"] = apiKey_;
    args->connectTimeout = 30;  // 30秒连接超时
    args->transferTimeout = 30; // 30秒传输超时
    
    // 添加详细的TLS调试信息
    std::cout << "[DEBUG] HTTP Client TLS Options configured" << std::endl;
    
    if (!params.empty()) {
        if (method == "GET" || method == "DELETE") {
            url += "?" + params;
        } else {
            args->body = params;
            args->extraHeaders["Content-Type"] = "application/x-www-form-urlencoded";
        }
        std::cout << "[DEBUG] Request params: " << params << std::endl;
    }
    
    std::cout << "[DEBUG] Final URL: " << url << std::endl;
    std::cout << "[DEBUG] Sending HTTP request..." << std::endl;
    
    ix::HttpResponsePtr response;
    if (method == "POST") {
        response = httpClient_->post(url, "", args);
    } else if (method == "PUT") {
        response = httpClient_->put(url, "", args);
    } else if (method == "DELETE") {
        response = httpClient_->Delete(url, args);
    } else {
        response = httpClient_->get(url, args);
    }
    
    std::cout << "[DEBUG] HTTP request completed" << std::endl;
    
    if (response) {
        std::cout << "[DEBUG] HTTP Response Status: " << response->statusCode << std::endl;
        std::cout << "[DEBUG] HTTP Response Headers count: " << response->headers.size() << std::endl;
        std::cout << "[DEBUG] HTTP Response Body length: " << response->body.length() << std::endl;
        std::cout << "[DEBUG] HTTP Response Body: " << response->body << std::endl;
        std::cout << "[DEBUG] HTTP Error Message: " << response->errorMsg << std::endl;
        std::cout << "[DEBUG] HTTP Upload Size: " << response->uploadSize << std::endl;
        std::cout << "[DEBUG] HTTP Download Size: " << response->downloadSize << std::endl;
        
        if (response->statusCode == 200) {
            return response->body;
        } else {
            std::cout << "[ERROR] HTTP request failed with status code: " << response->statusCode << std::endl;
        }
    } else {
        std::cout << "[ERROR] No HTTP response received - response is null" << std::endl;
    }
    
    return "";
}

std::string BinanceWebSocket::generateSignature(const std::string& queryString) const {
    // 根据配置选择签名算法
    if (config_.signatureType == "ed25519") {
        return generateEd25519Signature(queryString);
    } else {
        // 默认使用HMAC-SHA256签名
        return generateHmacSha256Signature(queryString);
    }
}

std::string BinanceWebSocket::generateHmacSha256Signature(const std::string& queryString) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int hashLen;
    
    HMAC(EVP_sha256(), apiSecret_.c_str(), apiSecret_.length(),
         reinterpret_cast<const unsigned char*>(queryString.c_str()), queryString.length(),
         hash, &hashLen);
    
    std::stringstream ss;
    for (unsigned int i = 0; i < hashLen; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

std::string BinanceWebSocket::generateEd25519Signature(const std::string& queryString) const {
    // 获取Ed25519私钥文件路径（解密后的内容是文件路径）
    std::string keyPath = config_.getCurrentApiSecret();
    
    // 从文件路径读取Ed25519私钥内容
    std::ifstream keyFile(keyPath);
    if (!keyFile.is_open()) {
        std::cerr << "Failed to open Ed25519 private key file: " << keyPath << std::endl;
        return "";
    }
    
    std::string keyContent((std::istreambuf_iterator<char>(keyFile)),
                           std::istreambuf_iterator<char>());
    keyFile.close();
    
    // 创建BIO对象
    BIO* bio = BIO_new_mem_buf(keyContent.c_str(), -1);
    if (!bio) {
        std::cerr << "Failed to create BIO for Ed25519 key" << std::endl;
        return "";
    }
    
    // 读取PEM格式的私钥
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    
    if (!pkey) {
        std::cerr << "Failed to load Ed25519 private key from PEM format" << std::endl;
        return "";
    }
    
    // 创建签名上下文
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        std::cerr << "Failed to create MD context" << std::endl;
        return "";
    }
    
    // 初始化签名
    if (EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey) <= 0) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        std::cerr << "Failed to initialize Ed25519 signing" << std::endl;
        return "";
    }
    
    // 计算签名长度
    size_t siglen;
    if (EVP_DigestSign(mdctx, nullptr, &siglen, 
                       reinterpret_cast<const unsigned char*>(queryString.c_str()), 
                       queryString.length()) <= 0) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        std::cerr << "Failed to get Ed25519 signature length" << std::endl;
        return "";
    }
    
    // 生成签名
    std::vector<unsigned char> signature(siglen);
    if (EVP_DigestSign(mdctx, signature.data(), &siglen,
                       reinterpret_cast<const unsigned char*>(queryString.c_str()),
                       queryString.length()) <= 0) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        std::cerr << "Failed to generate Ed25519 signature" << std::endl;
        return "";
    }
    
    // 清理资源
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    
    // 转换为base64字符串
    std::string base64_signature;
    
    // 使用OpenSSL的base64编码
    BIO* b64_bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);  // 不添加换行符
    b64_bio = BIO_push(b64, b64_bio);
    
    BIO_write(b64_bio, signature.data(), siglen);
    BIO_flush(b64_bio);
    
    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(b64_bio, &bufferPtr);
    base64_signature = std::string(bufferPtr->data, bufferPtr->length);
    
    BIO_free_all(b64_bio);
    
    return base64_signature;
}

void BinanceWebSocket::startHeartbeat() {
    heartbeatThread_ = std::thread(&BinanceWebSocket::heartbeatLoop, this);
}

void BinanceWebSocket::stopHeartbeat() {
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
}

void BinanceWebSocket::heartbeatLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
        
        if (!running_) break;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat_).count();
        
        // 每30分钟刷新listenKey
        if (elapsed > LISTEN_KEY_REFRESH_INTERVAL_MS) {
            if (!extendListenKey()) {
                if (errorCallback_) {
                    errorCallback_("Failed to refresh listenKey");
                }
            }
            lastHeartbeat_ = now;
        }
    }
}

void BinanceWebSocket::attemptReconnect() {
    if (!running_) return;
    
    setConnectionStatus(ConnectionStatus::RECONNECTING);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(reconnectIntervalMs_));
    
    if (running_) {
        connect();
    }
}

void BinanceWebSocket::setConnectionStatus(ConnectionStatus status) {
    status_ = status;
    if (connectionStatusCallback_) {
        connectionStatusCallback_(status);
    }
}

void BinanceWebSocket::onWebSocketApiMessage(const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message) {
        std::cout << "[DEBUG] WebSocket API received: " << msg->str << std::endl;
        parseWebSocketApiMessage(msg->str);
    } else if (msg->type == ix::WebSocketMessageType::Open) {
        std::cout << "[INFO] WebSocket API connected" << std::endl;
        wsApiConnected_ = true;
    } else if (msg->type == ix::WebSocketMessageType::Close) {
        std::cout << "[INFO] WebSocket API disconnected" << std::endl;
        wsApiConnected_ = false;
    } else if (msg->type == ix::WebSocketMessageType::Error) {
        std::cout << "[ERROR] WebSocket API error: " << msg->errorInfo.reason << std::endl;
        wsApiConnected_ = false;
    }
}

void BinanceWebSocket::parseWebSocketApiMessage(const std::string& message) {
    std::cout << "[DEBUG] Response: " << message << std::endl;
    
    yyjson_doc* doc = yyjson_read(message.c_str(), message.length(), 0);
    if (!doc) {
        std::cout << "[ERROR] Failed to parse WebSocket API JSON: " << message << std::endl;
        return;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root) {
        yyjson_doc_free(doc);
        return;
    }

    // 获取请求ID
    yyjson_val* idVal = yyjson_obj_get(root, "id");
    std::string requestId = idVal && yyjson_is_str(idVal) ? yyjson_get_str(idVal) : "";

    // 首先检查是否是session.logon响应（通过检查result中的apiKey字段）
    yyjson_val* result = yyjson_obj_get(root, "result");
    if (result) {
        yyjson_val* apiKeyVal = yyjson_obj_get(result, "apiKey");
        if (apiKeyVal) {
            // 这是session.logon响应
            sessionAuthenticated_ = true;
            std::cout << "[INFO] WebSocket API session authenticated successfully" << std::endl;
            yyjson_doc_free(doc);
            return;
        }
    }

    // 检查是否有错误响应
    yyjson_val* error = yyjson_obj_get(root, "error");
    if (error && requestId == "1") {
        // 这可能是session.logon的错误响应
        yyjson_val* code = yyjson_obj_get(error, "code");
        yyjson_val* msg = yyjson_obj_get(error, "msg");
        std::cout << "[ERROR] Session logon failed - Code: " << (code ? yyjson_get_int(code) : 0) 
                  << ", Message: " << (msg ? yyjson_get_str(msg) : "Unknown") << std::endl;
        yyjson_doc_free(doc);
        return;
    }

    // 改进的响应类型识别：基于响应内容结构而不是硬编码ID
    if (result) {
        // 检查是否是账户信息响应（包含assets和positions字段）
        yyjson_val* assets = yyjson_obj_get(result, "assets");
        yyjson_val* positions = yyjson_obj_get(result, "positions");
        if (assets && positions && yyjson_is_arr(assets) && yyjson_is_arr(positions)) {
            std::cout << "[DEBUG] Detected account info response based on structure" << std::endl;
            parseAccountInfoResponse(root);
            yyjson_doc_free(doc);
            return;
        }

        // 检查是否是账户余额响应（包含balance字段的数组）
        if (yyjson_is_arr(result)) {
            size_t idx, max;
            yyjson_val* item;
            yyjson_arr_foreach(result, idx, max, item) {
                if (yyjson_is_obj(item)) {
                    yyjson_val* balance = yyjson_obj_get(item, "balance");
                    if (balance) {
                        std::cout << "[DEBUG] Detected account balance response based on structure" << std::endl;
                        parseAccountBalanceResponse(root);
                        yyjson_doc_free(doc);
                        return;
                    }
                }
                break; // 只检查第一个元素
            }
        }

        // 检查是否是订单相关响应（order.place 或 order.cancel）
        yyjson_val* orderIdVal = yyjson_obj_get(result, "orderId");
        yyjson_val* symbolVal = yyjson_obj_get(result, "symbol");
        if (orderIdVal && symbolVal) {
            // 这是订单响应
            parseOrderResponse(root);
            yyjson_doc_free(doc);
            return;
        }

        // 检查是否是listenKey响应
        yyjson_val* listenKeyVal = yyjson_obj_get(result, "listenKey");
        if (listenKeyVal && yyjson_is_str(listenKeyVal)) {
            listenKey_ = yyjson_get_str(listenKeyVal);
            std::cout << "[INFO] Received listenKey via WebSocket API: " << listenKey_ << std::endl;
            yyjson_doc_free(doc);
            return;
        }

        // 检查是否是userDataStream.subscribe响应
        yyjson_val* subscriptionIdVal = yyjson_obj_get(result, "subscriptionId");
        if (subscriptionIdVal && yyjson_is_int(subscriptionIdVal)) {
            subscriptionId_ = yyjson_get_int(subscriptionIdVal);
            userDataStreamActive_ = true;
            std::cout << "[INFO] User data stream subscribed successfully, subscriptionId: " << subscriptionId_ << std::endl;
            yyjson_doc_free(doc);
            return;
        }

        // 检查是否是userDataStream.unsubscribe响应
        if (yyjson_is_null(result)) {
            // unsubscribe成功时result为null
            if (subscriptionId_ >= 0) {
                std::cout << "[INFO] User data stream unsubscribed successfully, subscriptionId: " << subscriptionId_ << std::endl;
                subscriptionId_ = -1;
                userDataStreamActive_ = false;
                yyjson_doc_free(doc);
                return;
            }
        }
    }

    // 保留原有的ID路由作为后备方案
    // ID "2" 是账户余额请求
    if (requestId == "2") {
        parseAccountBalanceResponse(root);
        yyjson_doc_free(doc);
        return;
    }

    // ID "3" 是账户信息请求
    if (requestId == "3") {
        parseAccountInfoResponse(root);
        yyjson_doc_free(doc);
        return;
    }

    // ID "4" 是持仓信息请求
    if (requestId == "4") {
        parsePositionInfoResponse(root);
        yyjson_doc_free(doc);
        return;
    }

    // 检查错误
    if (error) {
        yyjson_val* code = yyjson_obj_get(error, "code");
        yyjson_val* msg = yyjson_obj_get(error, "msg");
        std::cout << "[ERROR] WebSocket API error - Code: " << (code ? yyjson_get_int(code) : 0) 
                  << ", Message: " << (msg ? yyjson_get_str(msg) : "Unknown") << std::endl;
        
        // 如果是订单相关错误，也通过订单回调返回
        if (orderResponseCallback_) {
            OrderResponse orderResp;
            orderResp.success = false;
            orderResp.errorCode = code ? yyjson_get_int(code) : 0;
            orderResp.errorMessage = msg ? yyjson_get_str(msg) : "Unknown error";
            orderResponseCallback_(orderResp);
        }
    }

    yyjson_doc_free(doc);
}

bool BinanceWebSocket::createListenKeyViaWebSocket() {
    if (!wsApiConnected_) {
        // 首先连接到WebSocket API
        std::cout << "[INFO] Connecting to WebSocket API: " << wsApiUrl_ << std::endl;
        wsApiSocket_->setUrl(wsApiUrl_);
        
        // 设置TLS选项
        ix::SocketTLSOptions tlsOptions;
        tlsOptions.tls = true;
        tlsOptions.caFile = "NONE";
        tlsOptions.disable_hostname_validation = true;
        wsApiSocket_->setTLSOptions(tlsOptions);
        
        wsApiSocket_->start();
        
        // 等待连接建立
        int attempts = 0;
        while (!wsApiConnected_ && attempts < 50) {  // 最多等待5秒
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            attempts++;
        }
        
        if (!wsApiConnected_) {
            std::cout << "[ERROR] Failed to connect to WebSocket API" << std::endl;
            return false;
        }
    }

    // 构建userDataStream.start请求
    std::string requestId = std::to_string(requestId_++);
    std::string request = R"({
        "id": ")" + requestId + R"(",
        "method": "userDataStream.start",
        "params": {
            "apiKey": ")" + apiKey_ + R"("
        }
    })";

    std::cout << "[DEBUG] Sending WebSocket API request: " << request << std::endl;
    
    // 发送请求
    wsApiSocket_->send(request);
    
    // 等待响应 (listenKey会在onWebSocketApiMessage中处理)
    int attempts = 0;
    while (listenKey_.empty() && attempts < 50) {  // 最多等待5秒
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        attempts++;
    }
    
    if (listenKey_.empty()) {
        std::cout << "[ERROR] Failed to create listenKey via WebSocket API" << std::endl;
        return false;
    }
    
    std::cout << "[INFO] Successfully created listenKey via WebSocket API: " << listenKey_ << std::endl;
    return true;
}

bool BinanceWebSocket::keepaliveListenKeyViaWebSocket() {
    if (!wsApiConnected_ || listenKey_.empty()) {
        std::cout << "[ERROR] WebSocket API not connected or no listenKey available" << std::endl;
        return false;
    }

    // 构建userDataStream.ping请求
    std::string requestId = std::to_string(requestId_++);
    std::string request = R"({
        "id": ")" + requestId + R"(",
        "method": "userDataStream.ping",
        "params": {
            "apiKey": ")" + apiKey_ + R"(",
            "listenKey": ")" + listenKey_ + R"("
        }
    })";

    std::cout << "[DEBUG] Sending keepalive request: " << request << std::endl;
    wsApiSocket_->send(request);
    
    return true;
}

bool BinanceWebSocket::closeListenKeyViaWebSocket() {
    if (!wsApiConnected_ || listenKey_.empty()) {
        std::cout << "[ERROR] WebSocket API not connected or no listenKey available" << std::endl;
        return false;
    }

    // 构建userDataStream.stop请求
    std::string requestId = std::to_string(requestId_++);
    std::string request = R"({
        "id": ")" + requestId + R"(",
        "method": "userDataStream.stop",
        "params": {
            "apiKey": ")" + apiKey_ + R"(",
            "listenKey": ")" + listenKey_ + R"("
        }
    })";

    std::cout << "[DEBUG] Sending close listenKey request: " << request << std::endl;
    wsApiSocket_->send(request);
    
    listenKey_.clear();
    return true;
}

bool BinanceWebSocket::sessionLogon() {
    if (!wsApiSocket_) {
        std::cout << "[ERROR] WebSocket API not initialized" << std::endl;
        return false;
    }

    // 根据官方文档，只有Ed25519签名支持会话认证
    if (config_.signatureType != "ed25519") {
        std::cout << "[ERROR] Session authentication only supports Ed25519 keys" << std::endl;
        return false;
    }

    // 连接WebSocket API
    std::string wsApiUrl = config_.testnet ? 
        "wss://testnet.binancefuture.com/ws-fapi/v1" : 
        "wss://ws-fapi.binance.com/ws-fapi/v1";
    
    std::cout << "[INFO] Connecting to WebSocket API: " << wsApiUrl << std::endl;
    wsApiSocket_->setUrl(wsApiUrl);
    
    // 设置TLS选项
    ix::SocketTLSOptions tlsOptions;
    tlsOptions.tls = true;
    tlsOptions.caFile = "SYSTEM";
    tlsOptions.disable_hostname_validation = false;
    wsApiSocket_->setTLSOptions(tlsOptions);
    
    wsApiSocket_->start();
    
    // 等待连接建立
    int attempts = 0;
    while (wsApiSocket_->getReadyState() != ix::ReadyState::Open && attempts < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        attempts++;
    }
    
    if (wsApiSocket_->getReadyState() == ix::ReadyState::Open) {
        wsApiConnected_ = true;
        std::cout << "[INFO] WebSocket API connected successfully" << std::endl;
        
        // 发送session.logon请求
        bool sessionResult = performSessionLogon();
        
        if (sessionResult) {
            // 会话认证成功后，建立传统WebSocket连接用于市场数据订阅
            // 根据官方文档，使用组合流端点支持多个交易对
            // 动态读取pos_update.json中的交易对
            std::string streams = buildMarketDataStreams();
            if (streams.empty()) {
                // 如果读取失败，使用默认值
                streams = "btcusdt@depth5";
                std::cout << "[WARNING] Failed to read symbols from pos_update.json, using default streams" << std::endl;
            }
            
            std::string marketDataUrl = config_.testnet ? 
                "wss://stream.binancefuture.com/stream?streams=" + streams : 
                "wss://fstream.binance.com/stream?streams=" + streams;
            
            std::cout << "[INFO] Connecting to market data stream: " << marketDataUrl << std::endl;
            webSocket_->setUrl(marketDataUrl);
            webSocket_->setTLSOptions(tlsOptions);
            webSocket_->start();
            
            // 等待市场数据连接建立
            attempts = 0;
            while (webSocket_->getReadyState() != ix::ReadyState::Open && attempts < 50) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                attempts++;
            }
            
            if (webSocket_->getReadyState() == ix::ReadyState::Open) {
                setConnectionStatus(ConnectionStatus::CONNECTED);
                std::cout << "[INFO] Market data stream connected successfully" << std::endl;
            } else {
                std::cout << "[WARNING] Failed to connect to market data stream, depth subscriptions will not work" << std::endl;
            }
        }
        
        return sessionResult;
    } else {
        std::cout << "[ERROR] Failed to connect to WebSocket API" << std::endl;
        return false;
    }
}

bool BinanceWebSocket::performSessionLogon() {
    // 生成时间戳
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // 构建请求参数
    std::ostringstream paramsStream;
    paramsStream << "apiKey=" << apiKey_ 
                 << "&timestamp=" << now;
    std::string params = paramsStream.str();
    
    // 生成Ed25519签名
    std::string signature = generateEd25519Signature(params);
    if (signature.empty() || signature.find("Error:") == 0) {
        std::cout << "[ERROR] Failed to generate Ed25519 signature: " << signature << std::endl;
        return false;
    }
    
    // 构建完整的请求参数
    std::ostringstream fullParamsStream;
    fullParamsStream << params << "&signature=" << signature;
    
    // 构建session.logon请求
    std::ostringstream requestStream;
    requestStream << "{"
                  << "\"id\":\"" << requestId_++ << "\","
                  << "\"method\":\"session.logon\","
                  << "\"params\":{"
                  << "\"apiKey\":\"" << apiKey_ << "\","
                  << "\"signature\":\"" << signature << "\","
                  << "\"timestamp\":" << now
                  << "}"
                  << "}";
    
    std::string request = requestStream.str();
    std::cout << "[INFO] Sending session.logon request..." << std::endl;
    std::cout << "[DEBUG] Request: " << request << std::endl;
    
    // 发送请求
    wsApiSocket_->send(request);
    
    // 等待响应
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    return sessionAuthenticated_;
}

bool BinanceWebSocket::sessionLogout() {
    if (!wsApiConnected_ || !sessionAuthenticated_) {
        std::cerr << "WebSocket API not connected or not authenticated" << std::endl;
        return false;
    }
    
    // 构建session.logout请求
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    
    // 添加基本字段
    yyjson_mut_obj_add_str(doc, root, "id", std::to_string(++requestId_).c_str());
    yyjson_mut_obj_add_str(doc, root, "method", "session.logout");
    
    // 添加参数对象
    yyjson_mut_val* params = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "params", params);
    
    // 添加API密钥
    yyjson_mut_obj_add_str(doc, params, "apiKey", config_.apiKey.c_str());
    
    // 添加时间戳
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    yyjson_mut_obj_add_str(doc, params, "timestamp", std::to_string(timestamp).c_str());
    
    // 生成签名
    std::string signaturePayload = "apiKey=" + config_.apiKey + "&timestamp=" + std::to_string(timestamp);
    std::string signature = generateSignature(signaturePayload);
    yyjson_mut_obj_add_str(doc, params, "signature", signature.c_str());
    
    // 转换为JSON字符串
    char* json_str = yyjson_mut_write(doc, 0, nullptr);
    std::string message(json_str);
    free(json_str);
    yyjson_mut_doc_free(doc);
    
    std::cout << "Sending session.logout request: " << message << std::endl;
    
    // 发送请求
    wsApiSocket_->send(message);
    
    // 重置认证状态
    sessionAuthenticated_ = false;
    sessionId_.clear();
    
    return true;
}

bool BinanceWebSocket::sessionStatus() {
    if (!wsApiConnected_) {
        std::cerr << "WebSocket API not connected" << std::endl;
        return false;
    }
    
    // 构建session.status请求
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    
    // 添加基本字段
    yyjson_mut_obj_add_str(doc, root, "id", std::to_string(++requestId_).c_str());
    yyjson_mut_obj_add_str(doc, root, "method", "session.status");
    
    // 添加参数对象
    yyjson_mut_val* params = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "params", params);
    
    // 添加API密钥
    yyjson_mut_obj_add_str(doc, params, "apiKey", config_.apiKey.c_str());
    
    // 添加时间戳
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    yyjson_mut_obj_add_str(doc, params, "timestamp", std::to_string(timestamp).c_str());
    
    // 生成签名
    std::string signaturePayload = "apiKey=" + config_.apiKey + "&timestamp=" + std::to_string(timestamp);
    std::string signature = generateSignature(signaturePayload);
    yyjson_mut_obj_add_str(doc, params, "signature", signature.c_str());
    
    // 转换为JSON字符串
    char* json_str = yyjson_mut_write(doc, 0, nullptr);
    std::string message(json_str);
    free(json_str);
    yyjson_mut_doc_free(doc);
    
    std::cout << "Sending session.status request: " << message << std::endl;
    
    // 发送请求
    wsApiSocket_->send(message);
    
    return true;
}

bool BinanceWebSocket::subscribeUserDataStream() {
    if (!sessionAuthenticated_) {
        if (errorCallback_) {
            errorCallback_("Session not authenticated");
        }
        return false;
    }
    
    sendWebSocketRequest("userDataStream.subscribe", "");
    
    // 等待订阅响应（仅在连接状态为CONNECTED时等待）
    auto startTime = std::chrono::steady_clock::now();
    while (subscriptionId_ < 0 && status_ == ConnectionStatus::CONNECTED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > std::chrono::milliseconds(5000)) {  // 5秒超时
            if (errorCallback_) {
                errorCallback_("User data stream subscription timeout");
            }
            return false;
        }
    }
    
    return subscriptionId_ >= 0;
}

bool BinanceWebSocket::unsubscribeUserDataStream() {
    if (subscriptionId_ < 0) {
        std::cout << "[DEBUG] No active subscription to unsubscribe (subscriptionId_: " << subscriptionId_ << ")" << std::endl;
        return true;
    }
    
    std::cout << "[DEBUG] Unsubscribing user data stream with subscriptionId: " << subscriptionId_ << std::endl;
    
    std::ostringstream params;
    params << "\"subscriptionId\":" << subscriptionId_;
    
    sendWebSocketRequest("userDataStream.unsubscribe", params.str());
    subscriptionId_ = -1;
    userDataStreamActive_ = false;
    
    std::cout << "[DEBUG] User data stream unsubscribe request sent" << std::endl;
    return true;
}

void BinanceWebSocket::sendWebSocketApiRequest(const std::string& method, const std::string& params) {
    if (!wsApiSocket_ || wsApiSocket_->getReadyState() != ix::ReadyState::Open) {
        std::cout << "[ERROR] WebSocket API not connected" << std::endl;
        return;
    }
    
    std::string id = std::to_string(requestId_++);
    
    std::string request = "{"
        "\"id\":\"" + id + "\","
        "\"method\":\"" + method + "\"";
    
    if (!params.empty()) {
        request += ",\"params\":{" + params + "}";
    }
    
    request += "}";
    
    wsApiSocket_->send(request);
    std::cout << "[DEBUG] Request: " << request << std::endl;
}

void BinanceWebSocket::sendWebSocketRequest(const std::string& method, const std::string& params) {
    if (!wsApiSocket_ || wsApiSocket_->getReadyState() != ix::ReadyState::Open) {
        std::cout << "[ERROR] WebSocket API not connected" << std::endl;
        return;
    }
    
    std::string id = std::to_string(requestId_++);
    
    std::string request = "{"
        "\"id\":\"" + id + "\","
        "\"method\":\"" + method + "\"";
    
    if (!params.empty()) {
        request += ",\"params\":{" + params + "}";
    }
    
    request += "}";
    
    wsApiSocket_->send(request);
    std::cout << "[INFO] Sent WebSocket API request: " << request << std::endl;
}

std::string BinanceWebSocket::generateWebSocketSignature(const std::string& params) const {
    // 根据配置选择签名算法
    if (config_.signatureType == "ed25519") {
        return generateEd25519Signature(params);
    } else {
        // 默认使用HMAC-SHA256签名
        return generateHmacSha256Signature(params);
    }
}

// BinanceFactory实现
std::unique_ptr<IExchangeWebSocket> BinanceFactory::createWebSocketClient(const std::string& exchangeName) {
    if (exchangeName != "binance") {
        return nullptr;
    }
    
    ConfigManager& configManager = ConfigManager::getInstance();
    ExchangeConfig config = configManager.getExchangeConfig("binance");
    
    return std::make_unique<BinanceWebSocket>(config);
}

std::vector<std::string> BinanceFactory::getSupportedExchanges() const {
    return {"binance"};
}

void BinanceWebSocket::parseAccountBalanceResponse(yyjson_val* root) {
    AccountBalanceResponse response;
    
    // 解析基本信息
    yyjson_val* idVal = yyjson_obj_get(root, "id");
    if (idVal && yyjson_is_str(idVal)) {
        response.id = yyjson_get_str(idVal);
    }
    
    yyjson_val* statusVal = yyjson_obj_get(root, "status");
    if (statusVal && yyjson_is_int(statusVal)) {
        response.status = yyjson_get_int(statusVal);
    }
    
    // 解析结果数组
    yyjson_val* result = yyjson_obj_get(root, "result");
    if (result && yyjson_is_arr(result)) {
        size_t idx, max;
        yyjson_val* asset_val;
        yyjson_arr_foreach(result, idx, max, asset_val) {
            if (yyjson_is_obj(asset_val)) {
                AccountAsset asset;
                
                yyjson_val* assetName = yyjson_obj_get(asset_val, "asset");
                if (assetName && yyjson_is_str(assetName)) {
                    asset.asset = yyjson_get_str(assetName);
                }
                
                yyjson_val* balance = yyjson_obj_get(asset_val, "balance");
                if (balance && yyjson_is_str(balance)) {
                    asset.walletBalance = yyjson_get_str(balance);
                }
                
                yyjson_val* crossWalletBalance = yyjson_obj_get(asset_val, "crossWalletBalance");
                if (crossWalletBalance && yyjson_is_str(crossWalletBalance)) {
                    asset.crossWalletBalance = yyjson_get_str(crossWalletBalance);
                }
                
                yyjson_val* crossUnPnl = yyjson_obj_get(asset_val, "crossUnPnl");
                if (crossUnPnl && yyjson_is_str(crossUnPnl)) {
                    asset.crossUnPnl = yyjson_get_str(crossUnPnl);
                }
                
                yyjson_val* availableBalance = yyjson_obj_get(asset_val, "availableBalance");
                if (availableBalance && yyjson_is_str(availableBalance)) {
                    asset.availableBalance = yyjson_get_str(availableBalance);
                }
                
                yyjson_val* maxWithdrawAmount = yyjson_obj_get(asset_val, "maxWithdrawAmount");
                if (maxWithdrawAmount && yyjson_is_str(maxWithdrawAmount)) {
                    asset.maxWithdrawAmount = yyjson_get_str(maxWithdrawAmount);
                }
                
                yyjson_val* updateTime = yyjson_obj_get(asset_val, "updateTime");
                if (updateTime && yyjson_is_int(updateTime)) {
                    asset.updateTime = yyjson_get_int(updateTime);
                }
                
                response.result.push_back(asset);
            }
        }
    }
    
    // 调用回调函数
    if (accountBalanceCallback_) {
        accountBalanceCallback_(response);
    }
}

void BinanceWebSocket::parseAccountInfoResponse(yyjson_val* root) {
    AccountInfoResponse response;
    
    // 解析基本信息
    yyjson_val* idVal = yyjson_obj_get(root, "id");
    if (idVal && yyjson_is_str(idVal)) {
        response.id = yyjson_get_str(idVal);
    }
    
    yyjson_val* statusVal = yyjson_obj_get(root, "status");
    if (statusVal && yyjson_is_int(statusVal)) {
        response.status = yyjson_get_int(statusVal);
    }
    
    // 解析结果对象
    yyjson_val* result = yyjson_obj_get(root, "result");
    if (result && yyjson_is_obj(result)) {
        // 解析总计信息
        yyjson_val* totalInitialMargin = yyjson_obj_get(result, "totalInitialMargin");
        if (totalInitialMargin && yyjson_is_str(totalInitialMargin)) {
            response.totalInitialMargin = yyjson_get_str(totalInitialMargin);
        }
        
        yyjson_val* totalMaintMargin = yyjson_obj_get(result, "totalMaintMargin");
        if (totalMaintMargin && yyjson_is_str(totalMaintMargin)) {
            response.totalMaintMargin = yyjson_get_str(totalMaintMargin);
        }
        
        yyjson_val* totalWalletBalance = yyjson_obj_get(result, "totalWalletBalance");
        if (totalWalletBalance && yyjson_is_str(totalWalletBalance)) {
            response.totalWalletBalance = yyjson_get_str(totalWalletBalance);
        }
        
        yyjson_val* totalUnrealizedProfit = yyjson_obj_get(result, "totalUnrealizedProfit");
        if (totalUnrealizedProfit && yyjson_is_str(totalUnrealizedProfit)) {
            response.totalUnrealizedProfit = yyjson_get_str(totalUnrealizedProfit);
        }
        
        yyjson_val* totalMarginBalance = yyjson_obj_get(result, "totalMarginBalance");
        if (totalMarginBalance && yyjson_is_str(totalMarginBalance)) {
            response.totalMarginBalance = yyjson_get_str(totalMarginBalance);
        }
        
        yyjson_val* totalPositionInitialMargin = yyjson_obj_get(result, "totalPositionInitialMargin");
        if (totalPositionInitialMargin && yyjson_is_str(totalPositionInitialMargin)) {
            response.totalPositionInitialMargin = yyjson_get_str(totalPositionInitialMargin);
        }
        
        yyjson_val* totalOpenOrderInitialMargin = yyjson_obj_get(result, "totalOpenOrderInitialMargin");
        if (totalOpenOrderInitialMargin && yyjson_is_str(totalOpenOrderInitialMargin)) {
            response.totalOpenOrderInitialMargin = yyjson_get_str(totalOpenOrderInitialMargin);
        }
        
        yyjson_val* totalCrossWalletBalance = yyjson_obj_get(result, "totalCrossWalletBalance");
        if (totalCrossWalletBalance && yyjson_is_str(totalCrossWalletBalance)) {
            response.totalCrossWalletBalance = yyjson_get_str(totalCrossWalletBalance);
        }
        
        yyjson_val* totalCrossUnPnl = yyjson_obj_get(result, "totalCrossUnPnl");
        if (totalCrossUnPnl && yyjson_is_str(totalCrossUnPnl)) {
            response.totalCrossUnPnl = yyjson_get_str(totalCrossUnPnl);
        }
        
        yyjson_val* availableBalance = yyjson_obj_get(result, "availableBalance");
        if (availableBalance && yyjson_is_str(availableBalance)) {
            response.availableBalance = yyjson_get_str(availableBalance);
        }
        
        yyjson_val* maxWithdrawAmount = yyjson_obj_get(result, "maxWithdrawAmount");
        if (maxWithdrawAmount && yyjson_is_str(maxWithdrawAmount)) {
            response.maxWithdrawAmount = yyjson_get_str(maxWithdrawAmount);
        }
        
        // 解析资产数组
        yyjson_val* assets = yyjson_obj_get(result, "assets");
        if (assets && yyjson_is_arr(assets)) {
            size_t idx, max;
            yyjson_val* asset_val;
            yyjson_arr_foreach(assets, idx, max, asset_val) {
                if (yyjson_is_obj(asset_val)) {
                    AccountAsset asset;
                    
                    yyjson_val* assetName = yyjson_obj_get(asset_val, "asset");
                    if (assetName && yyjson_is_str(assetName)) {
                        asset.asset = yyjson_get_str(assetName);
                    }
                    
                    yyjson_val* walletBalance = yyjson_obj_get(asset_val, "walletBalance");
                    if (walletBalance && yyjson_is_str(walletBalance)) {
                        asset.walletBalance = yyjson_get_str(walletBalance);
                    }
                    
                    yyjson_val* unrealizedProfit = yyjson_obj_get(asset_val, "unrealizedProfit");
                    if (unrealizedProfit && yyjson_is_str(unrealizedProfit)) {
                        asset.unrealizedProfit = yyjson_get_str(unrealizedProfit);
                    }
                    
                    yyjson_val* marginBalance = yyjson_obj_get(asset_val, "marginBalance");
                    if (marginBalance && yyjson_is_str(marginBalance)) {
                        asset.marginBalance = yyjson_get_str(marginBalance);
                    }
                    
                    yyjson_val* maintMargin = yyjson_obj_get(asset_val, "maintMargin");
                    if (maintMargin && yyjson_is_str(maintMargin)) {
                        asset.maintMargin = yyjson_get_str(maintMargin);
                    }
                    
                    yyjson_val* initialMargin = yyjson_obj_get(asset_val, "initialMargin");
                    if (initialMargin && yyjson_is_str(initialMargin)) {
                        asset.initialMargin = yyjson_get_str(initialMargin);
                    }
                    
                    yyjson_val* positionInitialMargin = yyjson_obj_get(asset_val, "positionInitialMargin");
                    if (positionInitialMargin && yyjson_is_str(positionInitialMargin)) {
                        asset.positionInitialMargin = yyjson_get_str(positionInitialMargin);
                    }
                    
                    yyjson_val* openOrderInitialMargin = yyjson_obj_get(asset_val, "openOrderInitialMargin");
                    if (openOrderInitialMargin && yyjson_is_str(openOrderInitialMargin)) {
                        asset.openOrderInitialMargin = yyjson_get_str(openOrderInitialMargin);
                    }
                    
                    yyjson_val* crossWalletBalance = yyjson_obj_get(asset_val, "crossWalletBalance");
                    if (crossWalletBalance && yyjson_is_str(crossWalletBalance)) {
                        asset.crossWalletBalance = yyjson_get_str(crossWalletBalance);
                    }
                    
                    yyjson_val* crossUnPnl = yyjson_obj_get(asset_val, "crossUnPnl");
                    if (crossUnPnl && yyjson_is_str(crossUnPnl)) {
                        asset.crossUnPnl = yyjson_get_str(crossUnPnl);
                    }
                    
                    yyjson_val* availableBalance = yyjson_obj_get(asset_val, "availableBalance");
                    if (availableBalance && yyjson_is_str(availableBalance)) {
                        asset.availableBalance = yyjson_get_str(availableBalance);
                    }
                    
                    yyjson_val* maxWithdrawAmount = yyjson_obj_get(asset_val, "maxWithdrawAmount");
                    if (maxWithdrawAmount && yyjson_is_str(maxWithdrawAmount)) {
                        asset.maxWithdrawAmount = yyjson_get_str(maxWithdrawAmount);
                    }
                    
                    yyjson_val* updateTime = yyjson_obj_get(asset_val, "updateTime");
                    if (updateTime && yyjson_is_int(updateTime)) {
                        asset.updateTime = yyjson_get_int(updateTime);
                    }
                    
                    response.assets.push_back(asset);
                }
            }
        }
        
        // 解析持仓数组
        yyjson_val* positions = yyjson_obj_get(result, "positions");
        if (positions && yyjson_is_arr(positions)) {
            size_t idx, max;
            yyjson_val* position_val;
            yyjson_arr_foreach(positions, idx, max, position_val) {
                if (yyjson_is_obj(position_val)) {
                    AccountPosition position;
                    
                    yyjson_val* symbol = yyjson_obj_get(position_val, "symbol");
                    if (symbol && yyjson_is_str(symbol)) {
                        position.symbol = yyjson_get_str(symbol);
                    }
                    
                    yyjson_val* positionSide = yyjson_obj_get(position_val, "positionSide");
                    if (positionSide && yyjson_is_str(positionSide)) {
                        position.positionSide = yyjson_get_str(positionSide);
                    }
                    
                    yyjson_val* positionAmt = yyjson_obj_get(position_val, "positionAmt");
                    if (positionAmt && yyjson_is_str(positionAmt)) {
                        position.positionAmt = yyjson_get_str(positionAmt);
                    }
                    
                    yyjson_val* unrealizedProfit = yyjson_obj_get(position_val, "unrealizedProfit");
                    if (unrealizedProfit && yyjson_is_str(unrealizedProfit)) {
                        position.unrealizedProfit = yyjson_get_str(unrealizedProfit);
                    }
                    
                    yyjson_val* isolatedMargin = yyjson_obj_get(position_val, "isolatedMargin");
                    if (isolatedMargin && yyjson_is_str(isolatedMargin)) {
                        position.isolatedMargin = yyjson_get_str(isolatedMargin);
                    }
                    
                    yyjson_val* notional = yyjson_obj_get(position_val, "notional");
                    if (notional && yyjson_is_str(notional)) {
                        position.notional = yyjson_get_str(notional);
                    }
                    
                    yyjson_val* isolatedWallet = yyjson_obj_get(position_val, "isolatedWallet");
                    if (isolatedWallet && yyjson_is_str(isolatedWallet)) {
                        position.isolatedWallet = yyjson_get_str(isolatedWallet);
                    }
                    
                    yyjson_val* initialMargin = yyjson_obj_get(position_val, "initialMargin");
                    if (initialMargin && yyjson_is_str(initialMargin)) {
                        position.initialMargin = yyjson_get_str(initialMargin);
                    }
                    
                    yyjson_val* maintMargin = yyjson_obj_get(position_val, "maintMargin");
                    if (maintMargin && yyjson_is_str(maintMargin)) {
                        position.maintMargin = yyjson_get_str(maintMargin);
                    }
                    
                    yyjson_val* updateTime = yyjson_obj_get(position_val, "updateTime");
                    if (updateTime && yyjson_is_int(updateTime)) {
                        position.updateTime = yyjson_get_int(updateTime);
                    }
                    
                    response.positions.push_back(position);
                }
            }
        }
    }
    
    // 调用回调函数
    if (accountInfoCallback_) {
        accountInfoCallback_(response);
    }
}

void BinanceWebSocket::parsePositionInfoResponse(yyjson_val* root) {
    std::cout << "[INFO] Parsing position info response" << std::endl;
    
    yyjson_val* result = yyjson_obj_get(root, "result");
    if (!result) {
        std::cout << "[ERROR] No result field in position info response" << std::endl;
        return;
    }

    // 检查是否是数组或对象
    if (yyjson_is_arr(result)) {
        // 如果是数组，按原逻辑处理
        size_t idx, max;
        yyjson_val* position;
        yyjson_arr_foreach(result, idx, max, position) {
            if (!yyjson_is_obj(position)) continue;

            // 解析持仓信息
            yyjson_val* symbolVal = yyjson_obj_get(position, "symbol");
            yyjson_val* positionAmtVal = yyjson_obj_get(position, "positionAmt");
            yyjson_val* entryPriceVal = yyjson_obj_get(position, "entryPrice");
            yyjson_val* markPriceVal = yyjson_obj_get(position, "markPrice");
            yyjson_val* unRealizedPnlVal = yyjson_obj_get(position, "unRealizedPnl");
            yyjson_val* positionSideVal = yyjson_obj_get(position, "positionSide");

            std::string symbol = symbolVal && yyjson_is_str(symbolVal) ? yyjson_get_str(symbolVal) : "";
            std::string positionAmt = positionAmtVal && yyjson_is_str(positionAmtVal) ? yyjson_get_str(positionAmtVal) : "0";
            std::string entryPrice = entryPriceVal && yyjson_is_str(entryPriceVal) ? yyjson_get_str(entryPriceVal) : "0";
            std::string markPrice = markPriceVal && yyjson_is_str(markPriceVal) ? yyjson_get_str(markPriceVal) : "0";
            std::string unRealizedPnl = unRealizedPnlVal && yyjson_is_str(unRealizedPnlVal) ? yyjson_get_str(unRealizedPnlVal) : "0";
            std::string positionSide = positionSideVal && yyjson_is_str(positionSideVal) ? yyjson_get_str(positionSideVal) : "";

            // 只显示有持仓的合约
            if (std::stod(positionAmt) != 0.0) {
                std::cout << "[INFO] Position - Symbol: " << symbol 
                          << ", Amount: " << positionAmt 
                          << ", Entry Price: " << entryPrice 
                          << ", Mark Price: " << markPrice 
                          << ", Unrealized PnL: " << unRealizedPnl 
                          << ", Side: " << positionSide << std::endl;
            }
        }
    } else if (yyjson_is_obj(result)) {
        // 如果是对象，可能是错误响应或单个持仓信息
        yyjson_val* code = yyjson_obj_get(result, "code");
        yyjson_val* msg = yyjson_obj_get(result, "msg");
        
        if (code && yyjson_is_int(code)) {
            int errorCode = yyjson_get_int(code);
            std::string errorMsg = msg && yyjson_is_str(msg) ? yyjson_get_str(msg) : "Unknown error";
            std::cout << "[ERROR] Position info error - Code: " << errorCode << ", Message: " << errorMsg << std::endl;
        } else {
            // 尝试解析为单个持仓信息
            yyjson_val* symbolVal = yyjson_obj_get(result, "symbol");
            if (symbolVal && yyjson_is_str(symbolVal)) {
                std::string symbol = yyjson_get_str(symbolVal);
                std::cout << "[INFO] Single position info for symbol: " << symbol << std::endl;
            } else {
                std::cout << "[WARNING] Position info result is an object but not in expected format" << std::endl;
            }
        }
    } else {
        std::cout << "[ERROR] Position info result is neither an array nor an object" << std::endl;
    }
}

void BinanceWebSocket::placeOrder(const OrderRequest& orderRequest, const std::string& requestId) {
    if (!sessionAuthenticated_) {
        std::cout << "[ERROR] Session not authenticated, cannot place order" << std::endl;
        return;
    }
    
    std::string id = requestId.empty() ? std::to_string(requestId_++) : requestId;
    
    // 构建订单参数
    std::ostringstream params;
    params << "\"symbol\":\"" << orderRequest.symbol << "\""
           << ",\"side\":\"" << orderRequest.side << "\""
           << ",\"type\":\"" << orderRequest.type << "\"";
    
    if (!orderRequest.positionSide.empty()) {
        params << ",\"positionSide\":\"" << orderRequest.positionSide << "\"";
    }
    
    if (!orderRequest.quantity.empty()) {
        params << ",\"quantity\":\"" << orderRequest.quantity << "\"";
    }
    
    if (!orderRequest.price.empty()) {
        params << ",\"price\":\"" << orderRequest.price << "\"";
    }
    
    if (!orderRequest.newClientOrderId.empty()) {
        params << ",\"newClientOrderId\":\"" << orderRequest.newClientOrderId << "\"";
    }
    
    if (!orderRequest.stopPrice.empty()) {
        params << ",\"stopPrice\":\"" << orderRequest.stopPrice << "\"";
    }
    
    if (!orderRequest.closePosition.empty()) {
        params << ",\"closePosition\":" << orderRequest.closePosition;
    }
    
    if (!orderRequest.activationPrice.empty()) {
        params << ",\"activationPrice\":\"" << orderRequest.activationPrice << "\"";
    }
    
    if (!orderRequest.callbackRate.empty()) {
        params << ",\"callbackRate\":\"" << orderRequest.callbackRate << "\"";
    }
    
    if (!orderRequest.timeInForce.empty()) {
        params << ",\"timeInForce\":\"" << orderRequest.timeInForce << "\"";
    }
    
    if (!orderRequest.workingType.empty()) {
        params << ",\"workingType\":\"" << orderRequest.workingType << "\"";
    }
    
    if (!orderRequest.priceProtect.empty()) {
        params << ",\"priceProtect\":" << orderRequest.priceProtect;
    }
    
    if (!orderRequest.newOrderRespType.empty()) {
        params << ",\"newOrderRespType\":\"" << orderRequest.newOrderRespType << "\"";
    }
    
    if (!orderRequest.priceMatch.empty()) {
        params << ",\"priceMatch\":\"" << orderRequest.priceMatch << "\"";
    }
    
    if (!orderRequest.selfTradePreventionMode.empty()) {
        params << ",\"selfTradePreventionMode\":\"" << orderRequest.selfTradePreventionMode << "\"";
    }
    
    if (orderRequest.goodTillDate > 0) {
        params << ",\"goodTillDate\":" << orderRequest.goodTillDate;
    }
    
    if (!orderRequest.reduceOnly.empty()) {
        params << ",\"reduceOnly\":" << orderRequest.reduceOnly;
    }
    
    // 添加必需的timestamp参数
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    params << ",\"timestamp\":" << timestamp;
    
    std::string paramsStr = params.str();
    std::cout << "[INFO] Placing order with params: " << paramsStr << std::endl;
    
    sendWebSocketApiRequest("order.place", paramsStr);
}

void BinanceWebSocket::cancelOrder(const CancelOrderRequest& cancelRequest, const std::string& requestId) {
    if (!sessionAuthenticated_) {
        std::cout << "[ERROR] Session not authenticated, cannot cancel order" << std::endl;
        return;
    }
    
    std::string id = requestId.empty() ? std::to_string(requestId_++) : requestId;
    
    // 构建撤单参数
    std::ostringstream params;
    params << "\"symbol\":\"" << cancelRequest.symbol << "\"";
    
    if (cancelRequest.orderId > 0) {
        params << ",\"orderId\":" << cancelRequest.orderId;
    }
    
    if (!cancelRequest.origClientOrderId.empty()) {
        params << ",\"origClientOrderId\":\"" << cancelRequest.origClientOrderId << "\"";
    }
    
    // 添加必需的timestamp参数
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    params << ",\"timestamp\":" << timestamp;
    
    std::string paramsStr = params.str();
    std::cout << "[INFO] Canceling order with params: " << paramsStr << std::endl;
    
    sendWebSocketApiRequest("order.cancel", paramsStr);
}

bool BinanceWebSocket::subscribeDepthUpdate(const std::string& symbol, int levels, int updateSpeed) {
    if (!isConnected()) {
        std::cout << "[ERROR] WebSocket not connected, cannot subscribe to depth updates" << std::endl;
        return false;
    }
    
    // 根据官方文档，现在直接连接到深度数据流，无需发送订阅消息
    // 连接URL已经包含了流名称：btcusdt@depth5
    std::cout << "[INFO] Depth subscription is handled by direct stream connection" << std::endl;
    return true;
}

bool BinanceWebSocket::unsubscribeDepthUpdate(const std::string& symbol) {
    if (!isConnected()) {
        std::cout << "[ERROR] WebSocket not connected, cannot unsubscribe from depth updates" << std::endl;
        return false;
    }
    
    // 构建深度取消订阅流名称（使用通配符匹配所有深度级别）
    std::string streamName = symbol + "@depth20@100ms";
    
    std::ostringstream unsubscribeMsg;
    unsubscribeMsg << "{"
                   << "\"method\":\"UNSUBSCRIBE\","
                   << "\"params\":[\"" << streamName << "\"],"
                   << "\"id\":" << ++subscriptionId_
                   << "}";
    
    std::string message = unsubscribeMsg.str();
    std::cout << "[INFO] Unsubscribing from depth updates: " << message << std::endl;
    
    webSocket_->send(message);
    return true;
}

bool BinanceWebSocket::subscribeTradeLite() {
    if (!sessionAuthenticated_) {
        std::cout << "[ERROR] Session not authenticated, cannot subscribe to trade lite" << std::endl;
        return false;
    }
    
    // 精简交易推送需要通过用户数据流订阅
    std::cout << "[INFO] Trade lite events are automatically pushed through user data stream when authenticated" << std::endl;
    return true;
}

bool BinanceWebSocket::unsubscribeTradeLite() {
    if (!sessionAuthenticated_) {
        std::cout << "[ERROR] Session not authenticated, cannot unsubscribe from trade lite" << std::endl;
        return false;
    }
    
    // 精简交易推送通过用户数据流自动推送，无需单独取消订阅
    std::cout << "[INFO] Trade lite events are part of user data stream, no separate unsubscribe needed" << std::endl;
    return true;
}

void BinanceWebSocket::parseOrderResponse(yyjson_val* root) {
    if (!orderResponseCallback_) {
        return;
    }
    
    OrderResponse orderResp;
    orderResp.success = true;
    
    yyjson_val* result = yyjson_obj_get(root, "result");
    if (!result) {
        orderResp.success = false;
        orderResp.errorMessage = "No result in order response";
        orderResponseCallback_(orderResp);
        return;
    }
    
    // 解析订单响应字段
    yyjson_val* val;
    
    if ((val = yyjson_obj_get(result, "symbol")) && yyjson_is_str(val)) {
        orderResp.symbol = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "orderId")) && yyjson_is_num(val)) {
        orderResp.orderId = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(result, "clientOrderId")) && yyjson_is_str(val)) {
        orderResp.clientOrderId = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "price")) && yyjson_is_str(val)) {
        orderResp.price = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "origQty")) && yyjson_is_str(val)) {
        orderResp.origQty = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "executedQty")) && yyjson_is_str(val)) {
        orderResp.executedQty = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "cummulativeQuoteQty")) && yyjson_is_str(val)) {
        orderResp.cummulativeQuoteQty = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "status")) && yyjson_is_str(val)) {
        orderResp.status_str = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "timeInForce")) && yyjson_is_str(val)) {
        orderResp.timeInForce = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "type")) && yyjson_is_str(val)) {
        orderResp.type = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "side")) && yyjson_is_str(val)) {
        orderResp.side = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "positionSide")) && yyjson_is_str(val)) {
        orderResp.positionSide = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "reduceOnly")) && yyjson_is_bool(val)) {
        orderResp.reduceOnly = yyjson_get_bool(val);
    }
    
    if ((val = yyjson_obj_get(result, "closePosition")) && yyjson_is_bool(val)) {
        orderResp.closePosition = yyjson_get_bool(val);
    }
    
    if ((val = yyjson_obj_get(result, "activatePrice")) && yyjson_is_str(val)) {
        orderResp.activatePrice = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "priceRate")) && yyjson_is_str(val)) {
        orderResp.priceRate = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "updateTime")) && yyjson_is_num(val)) {
        orderResp.updateTime = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(result, "workingType")) && yyjson_is_str(val)) {
        orderResp.workingType = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "priceProtect")) && yyjson_is_bool(val)) {
        orderResp.priceProtect = yyjson_get_bool(val);
    }
    
    if ((val = yyjson_obj_get(result, "priceMatch")) && yyjson_is_str(val)) {
        orderResp.priceMatch = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "selfTradePreventionMode")) && yyjson_is_str(val)) {
        orderResp.selfTradePreventionMode = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(result, "goodTillDate")) && yyjson_is_num(val)) {
        orderResp.goodTillDate = yyjson_get_int(val);
    }
    
    std::cout << "[INFO] Order response parsed - Symbol: " << orderResp.symbol 
              << ", OrderId: " << orderResp.orderId 
              << ", Status: " << orderResp.status << std::endl;
    
    orderResponseCallback_(orderResp);
}

void BinanceWebSocket::parseDepthUpdate(yyjson_val* root) {
    if (!depthUpdateCallback_) {
        return;
    }
    
    DepthUpdate depthUpdate;
    yyjson_val* val;
    
    if ((val = yyjson_obj_get(root, "s")) && yyjson_is_str(val)) {
        depthUpdate.symbol = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "E")) && yyjson_is_num(val)) {
        depthUpdate.eventTime = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(root, "T")) && yyjson_is_num(val)) {
        depthUpdate.transactionTime = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(root, "u")) && yyjson_is_num(val)) {
        depthUpdate.finalUpdateId = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(root, "U")) && yyjson_is_num(val)) {
        depthUpdate.firstUpdateId = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(root, "pu")) && yyjson_is_num(val)) {
        depthUpdate.prevFinalUpdateId = yyjson_get_int(val);
    }
    
    // 解析买盘数据
    yyjson_val* bids = yyjson_obj_get(root, "b");
    if (bids && yyjson_is_arr(bids)) {
        size_t idx, max;
        yyjson_val* bid;
        yyjson_arr_foreach(bids, idx, max, bid) {
            if (yyjson_is_arr(bid) && yyjson_arr_size(bid) >= 2) {
                PriceLevel level;
                yyjson_val* priceVal = yyjson_arr_get(bid, 0);
                yyjson_val* qtyVal = yyjson_arr_get(bid, 1);
                
                if (priceVal && yyjson_is_str(priceVal)) {
                    level.price = yyjson_get_str(priceVal);
                }
                if (qtyVal && yyjson_is_str(qtyVal)) {
                    level.quantity = yyjson_get_str(qtyVal);
                }
                
                depthUpdate.bids.push_back(level);
            }
        }
    }
    
    // 解析卖盘数据
    yyjson_val* asks = yyjson_obj_get(root, "a");
    if (asks && yyjson_is_arr(asks)) {
        size_t idx, max;
        yyjson_val* ask;
        yyjson_arr_foreach(asks, idx, max, ask) {
            if (yyjson_is_arr(ask) && yyjson_arr_size(ask) >= 2) {
                PriceLevel level;
                yyjson_val* priceVal = yyjson_arr_get(ask, 0);
                yyjson_val* qtyVal = yyjson_arr_get(ask, 1);
                
                if (priceVal && yyjson_is_str(priceVal)) {
                    level.price = yyjson_get_str(priceVal);
                }
                if (qtyVal && yyjson_is_str(qtyVal)) {
                    level.quantity = yyjson_get_str(qtyVal);
                }
                
                depthUpdate.asks.push_back(level);
            }
        }
    }
    
    std::cout << "[INFO] Depth update parsed - Symbol: " << depthUpdate.symbol 
              << ", Bids: " << depthUpdate.bids.size() 
              << ", Asks: " << depthUpdate.asks.size() << std::endl;
    
    depthUpdateCallback_(depthUpdate);
}

void BinanceWebSocket::parseTradeLite(yyjson_val* root) {
    if (!tradeLiteCallback_) {
        return;
    }
    
    TradeLite tradeLite;
    yyjson_val* val;
    
    if ((val = yyjson_obj_get(root, "s")) && yyjson_is_str(val)) {
        tradeLite.symbol = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "q")) && yyjson_is_str(val)) {
        tradeLite.quantity = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "p")) && yyjson_is_str(val)) {
        tradeLite.price = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "X")) && yyjson_is_str(val)) {
        tradeLite.orderStatus = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "l")) && yyjson_is_str(val)) {
        tradeLite.lastFilledQuantity = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "z")) && yyjson_is_str(val)) {
        tradeLite.cumulativeFilledQuantity = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "L")) && yyjson_is_str(val)) {
        tradeLite.lastFilledPrice = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "N")) && yyjson_is_str(val)) {
        tradeLite.commissionAsset = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "n")) && yyjson_is_str(val)) {
        tradeLite.commission = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "T")) && yyjson_is_num(val)) {
        tradeLite.orderTradeTime = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(root, "t")) && yyjson_is_num(val)) {
        tradeLite.tradeId = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(root, "b")) && yyjson_is_str(val)) {
        tradeLite.buyerOrderId = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "a")) && yyjson_is_str(val)) {
        tradeLite.sellerOrderId = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "m")) && yyjson_is_bool(val)) {
        tradeLite.isMarkerSide = yyjson_get_bool(val);
    }
    
    if ((val = yyjson_obj_get(root, "R")) && yyjson_is_bool(val)) {
        tradeLite.isReduceOnly = yyjson_get_bool(val);
    }
    
    if ((val = yyjson_obj_get(root, "wt")) && yyjson_is_str(val)) {
        tradeLite.stopPriceWorkingType = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "ot")) && yyjson_is_str(val)) {
        tradeLite.originalOrderType = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "ps")) && yyjson_is_str(val)) {
        tradeLite.positionSide = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "cp")) && yyjson_is_bool(val)) {
        tradeLite.isClosePosition = yyjson_get_bool(val);
    }
    
    if ((val = yyjson_obj_get(root, "AP")) && yyjson_is_str(val)) {
        tradeLite.activationPrice = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "cr")) && yyjson_is_str(val)) {
        tradeLite.callbackRate = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "rp")) && yyjson_is_str(val)) {
        tradeLite.realizedPnl = yyjson_get_str(val);
    }
    
    if ((val = yyjson_obj_get(root, "pP")) && yyjson_is_bool(val)) {
        tradeLite.priceProtect = yyjson_get_bool(val);
    }
    
    if ((val = yyjson_obj_get(root, "si")) && yyjson_is_num(val)) {
        tradeLite.stopPriceId = yyjson_get_int(val);
    }
    
    if ((val = yyjson_obj_get(root, "ss")) && yyjson_is_num(val)) {
        tradeLite.strategyId = yyjson_get_int(val);
    }
    
    std::cout << "[INFO] Trade lite parsed - Symbol: " << tradeLite.symbol 
              << ", Quantity: " << tradeLite.quantity 
              << ", Price: " << tradeLite.price 
              << ", Status: " << tradeLite.orderStatus << std::endl;
    
    tradeLiteCallback_(tradeLite);
}

// 动态构建市场数据流
std::string BinanceWebSocket::buildMarketDataStreams() const {
    std::vector<std::string> symbols;
    
    try {
        // 读取pos_update.json文件
        std::ifstream file("./config/pos_update.json");
        if (!file.is_open()) {
            std::cout << "[WARNING] Cannot open pos_update.json file" << std::endl;
            return "";
        }
        
        nlohmann::json pos_data;
        file >> pos_data;
        file.close();
        
        if (pos_data.is_array()) {
            for (const auto& item : pos_data) {
                // 只处理包含symbol字段的对象
                if (item.is_object() && item.contains("symbol") && item["symbol"].is_string()) {
                    std::string symbol = item["symbol"];
                    // 转换为小写
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    symbols.push_back(symbol + "@depth5");
                    std::cout << "[INFO] Added symbol to market data stream: " << symbol << std::endl;
                }
            }
        }
        
        if (symbols.empty()) {
            std::cout << "[WARNING] No symbols found in pos_update.json" << std::endl;
            return "";
        }
        
        // 构建流字符串
        std::string streams;
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) {
                streams += "/";
            }
            streams += symbols[i];
        }
        
        std::cout << "[INFO] Built market data streams: " << streams << std::endl;
        return streams;
        
    } catch (const std::exception& e) {
        std::cout << "[ERROR] Failed to read pos_update.json: " << e.what() << std::endl;
        return "";
    }
}

} // namespace trading