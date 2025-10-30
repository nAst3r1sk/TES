#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <vector>
#include "config_manager.h"
#include "binance_websocket.h"
#include "exchange_interface.h"

using namespace trading;

std::atomic<bool> running(true);

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

void printAccountUpdate(const AccountUpdate& update) {
    std::cout << "\n=== Account Update ===" << std::endl;
    std::cout << "Event Type: " << update.eventType << std::endl;
    std::cout << "Event Time: " << update.eventTime << std::endl;
    std::cout << "Transaction Time: " << update.transactionTime << std::endl;
    std::cout << "Update Reason: " << update.updateReason << std::endl;
    
    std::cout << "\nBalances (" << update.balances.size() << " assets):" << std::endl;
    for (const auto& balance : update.balances) {
        std::cout << "  Asset: " << balance.asset 
                  << ", Wallet Balance: " << balance.walletBalance
                  << ", Cross Wallet Balance: " << balance.crossWalletBalance
                  << ", Balance Change: " << balance.balanceChange << std::endl;
    }
    
    std::cout << "\nPositions (" << update.positions.size() << " symbols):" << std::endl;
    int activePositions = 0;
    for (const auto& position : update.positions) {
        if (position.positionAmount != "0" && position.positionAmount != "0.00000000") { // 只显示非零仓位
            activePositions++;
            std::cout << "  Symbol: " << position.symbol
                      << ", Position: " << position.positionAmount
                      << ", Entry Price: " << position.entryPrice
                      << ", Break Even Price: " << position.breakEvenPrice
                      << ", Unrealized PnL: " << position.unrealizedPnl
                      << ", Margin Type: " << position.marginType
                      << ", Position Side: " << position.positionSide << std::endl;
        }
    }
    if (activePositions == 0) {
        std::cout << "  No active positions" << std::endl;
    }
    std::cout << "======================" << std::endl;
}

void printPositionUpdate(const PositionUpdate& update) {
    std::cout << "\n=== Position Update ===" << std::endl;
    std::cout << "Symbol: " << update.symbol << std::endl;
    std::cout << "Position Amount: " << update.positionAmount << std::endl;
    std::cout << "Entry Price: " << update.entryPrice << std::endl;
    std::cout << "Unrealized PnL: " << update.unrealizedPnl << std::endl;
    std::cout << "Position Side: " << update.positionSide << std::endl;
    std::cout << "========================" << std::endl;
}

void printOrderUpdate(const OrderUpdate& update) {
    std::cout << "\n=== Order Update ===" << std::endl;
    
    // 基本事件信息
    std::cout << "Event Type: " << update.eventType << std::endl;
    std::cout << "Event Time: " << update.eventTime << std::endl;
    std::cout << "Transaction Time: " << update.transactionTime << std::endl;
    
    // 订单基本信息
    std::cout << "Symbol: " << update.symbol << std::endl;
    std::cout << "Order ID: " << update.orderId << std::endl;
    std::cout << "Client Order ID: " << update.clientOrderId << std::endl;
    std::cout << "Side: " << update.side << std::endl;
    std::cout << "Order Type: " << update.orderType << std::endl;
    std::cout << "Time In Force: " << update.timeInForce << std::endl;
    
    // 价格和数量信息
    std::cout << "Original Quantity: " << update.originalQuantity << std::endl;
    std::cout << "Original Price: " << update.originalPrice << std::endl;
    if (!update.averagePrice.empty() && update.averagePrice != "0") {
        std::cout << "Average Price: " << update.averagePrice << std::endl;
    }
    if (!update.stopPrice.empty() && update.stopPrice != "0") {
        std::cout << "Stop Price: " << update.stopPrice << std::endl;
    }
    
    // 执行信息
    std::cout << "Execution Type: " << update.executionType << std::endl;
    std::cout << "Order Status: " << update.orderStatus << std::endl;
    std::cout << "Last Executed Quantity: " << update.lastExecutedQuantity << std::endl;
    std::cout << "Cumulative Filled Quantity: " << update.cumulativeFilledQuantity << std::endl;
    if (!update.lastExecutedPrice.empty() && update.lastExecutedPrice != "0") {
        std::cout << "Last Executed Price: " << update.lastExecutedPrice << std::endl;
    }
    
    // 手续费信息
    if (!update.commissionAsset.empty()) {
        std::cout << "Commission: " << update.commissionAmount << " " << update.commissionAsset << std::endl;
    }
    
    // 成交信息
    if (update.tradeId > 0) {
        std::cout << "Trade ID: " << update.tradeId << std::endl;
        std::cout << "Trade Time: " << update.tradeTime << std::endl;
        std::cout << "Is Maker Side: " << (update.isMakerSide ? "Yes" : "No") << std::endl;
    }
    
    // 持仓相关信息
    if (!update.positionSide.empty()) {
        std::cout << "Position Side: " << update.positionSide << std::endl;
    }
    if (update.isReduceOnly) {
        std::cout << "Reduce Only: Yes" << std::endl;
    }
    if (update.isClosePosition) {
        std::cout << "Close Position: Yes" << std::endl;
    }
    
    // 实现盈亏
    if (!update.realizedProfit.empty() && update.realizedProfit != "0") {
        std::cout << "Realized Profit: " << update.realizedProfit << std::endl;
    }
    
    // 追踪止损信息
    if (!update.activationPrice.empty() && update.activationPrice != "0") {
        std::cout << "Activation Price: " << update.activationPrice << std::endl;
    }
    if (!update.callbackRate.empty() && update.callbackRate != "0") {
        std::cout << "Callback Rate: " << update.callbackRate << std::endl;
    }
    
    // 其他信息
    if (!update.workingType.empty()) {
        std::cout << "Working Type: " << update.workingType << std::endl;
    }
    if (!update.originalOrderType.empty() && update.originalOrderType != update.orderType) {
        std::cout << "Original Order Type: " << update.originalOrderType << std::endl;
    }
    if (!update.selfTradePreventionMode.empty()) {
        std::cout << "STP Mode: " << update.selfTradePreventionMode << std::endl;
    }
    if (!update.priceMatchMode.empty()) {
        std::cout << "Price Match Mode: " << update.priceMatchMode << std::endl;
    }
    if (update.goodTillDate > 0) {
        std::cout << "Good Till Date: " << update.goodTillDate << std::endl;
    }
    
    std::cout << "=====================" << std::endl;
}

void printConnectionStatus(ConnectionStatus status) {
    std::string statusStr;
    switch (status) {
        case ConnectionStatus::DISCONNECTED:
            statusStr = "DISCONNECTED";
            break;
        case ConnectionStatus::CONNECTING:
            statusStr = "CONNECTING";
            break;
        case ConnectionStatus::CONNECTED:
            statusStr = "CONNECTED";
            break;
        case ConnectionStatus::RECONNECTING:
            statusStr = "RECONNECTING";
            break;
        case ConnectionStatus::ERROR:
            statusStr = "ERROR";
            break;
    }
    std::cout << "Connection Status: " << statusStr << std::endl;
}

void printError(const std::string& error) {
    std::cout << "Error: " << error << std::endl;
}

void printAccountBalance(const AccountBalanceResponse& response) {
    std::cout << "\n=== Account Balance Response ===" << std::endl;
    std::cout << "Request ID: " << response.id << std::endl;
    std::cout << "Status: " << response.status << std::endl;
    
    // 过滤出有余额的资产
    std::vector<AccountAsset> nonZeroAssets;
    for (const auto& asset : response.result) {
        // 检查是否有非零余额
        bool hasBalance = false;
        try {
            if (std::stod(asset.walletBalance) != 0.0 || 
                std::stod(asset.crossWalletBalance) != 0.0 || 
                std::stod(asset.availableBalance) != 0.0 ||
                std::stod(asset.maxWithdrawAmount) != 0.0) {
                hasBalance = true;
            }
        } catch (...) {
            // 如果转换失败，仍然显示该资产
            hasBalance = true;
        }
        
        if (hasBalance) {
            nonZeroAssets.push_back(asset);
        }
    }
    
    std::cout << "\n总资产数量: " << response.result.size() << std::endl;
    std::cout << "非零余额资产: " << nonZeroAssets.size() << std::endl;
    
    if (nonZeroAssets.empty()) {
        std::cout << "\n所有资产余额均为零。" << std::endl;
        std::cout << "支持的资产类型: ";
        for (size_t i = 0; i < std::min(response.result.size(), size_t(10)); ++i) {
            std::cout << response.result[i].asset;
            if (i < std::min(response.result.size(), size_t(10)) - 1) std::cout << ", ";
        }
        if (response.result.size() > 10) {
            std::cout << " ... (+" << (response.result.size() - 10) << " more)";
        }
        std::cout << std::endl;
    } else {
        std::cout << std::string(120, '-') << std::endl;
        std::cout << std::left 
                  << std::setw(8) << "资产"
                  << std::setw(18) << "钱包余额"
                  << std::setw(18) << "交叉钱包余额"
                  << std::setw(18) << "可用余额"
                  << std::setw(18) << "最大提取"
                  << std::setw(15) << "交叉未实现盈亏"
                  << std::setw(15) << "更新时间"
                  << std::endl;
        std::cout << std::string(120, '-') << std::endl;
        
        for (const auto& asset : nonZeroAssets) {
            // 格式化时间戳
            std::string timeStr = "N/A";
            if (asset.updateTime > 0) {
                auto time_t_val = static_cast<time_t>(asset.updateTime / 1000);
                auto tm_val = *std::localtime(&time_t_val);
                std::ostringstream timeStream;
                timeStream << std::put_time(&tm_val, "%m-%d %H:%M");
                timeStr = timeStream.str();
            }
            
            std::cout << std::left 
                      << std::setw(8) << asset.asset
                      << std::setw(18) << asset.walletBalance
                      << std::setw(18) << asset.crossWalletBalance
                      << std::setw(18) << asset.availableBalance
                      << std::setw(18) << asset.maxWithdrawAmount
                      << std::setw(15) << asset.crossUnPnl
                      << std::setw(15) << timeStr
                      << std::endl;
        }
        
        std::cout << std::string(120, '-') << std::endl;
    }
    
    std::cout << "=================================" << std::endl;
}

void printAccountInfo(const AccountInfoResponse& response) {
    std::cout << "\n=== Account Information ===" << std::endl;
    std::cout << "Fee Tier: " << response.feeTier << std::endl;
    std::cout << "Can Trade: " << (response.canTrade ? "Yes" : "No") << std::endl;
    std::cout << "Can Deposit: " << (response.canDeposit ? "Yes" : "No") << std::endl;
    std::cout << "Can Withdraw: " << (response.canWithdraw ? "Yes" : "No") << std::endl;
    std::cout << "Update Time: " << response.updateTime << std::endl;
    std::cout << "Multi-Assets Margin: " << (response.multiAssetsMargin ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Trade Group ID: " << response.tradeGroupId << std::endl;
    
    std::cout << "\nAssets (" << response.assets.size() << " total):" << std::endl;
    for (const auto& asset : response.assets) {
        if (asset.walletBalance != "0" && asset.walletBalance != "0.00000000") { // 只显示非零余额
            std::cout << "  Asset: " << asset.asset 
                      << ", Wallet Balance: " << asset.walletBalance
                      << ", Unrealized PnL: " << asset.unrealizedProfit
                      << ", Margin Balance: " << asset.marginBalance
                      << ", Maint Margin: " << asset.maintMargin
                      << ", Initial Margin: " << asset.initialMargin
                      << ", Position Initial Margin: " << asset.positionInitialMargin
                      << ", Open Order Initial Margin: " << asset.openOrderInitialMargin
                      << ", Cross Wallet Balance: " << asset.crossWalletBalance
                      << ", Cross Unrealized PnL: " << asset.crossUnPnl
                      << ", Available Balance: " << asset.availableBalance
                      << ", Max Withdraw Amount: " << asset.maxWithdrawAmount
                      << ", Update Time: " << asset.updateTime << std::endl;
        }
    }
    
    std::cout << "\nPositions (" << response.positions.size() << " total):" << std::endl;
    int activePositions = 0;
    for (const auto& position : response.positions) {
        if (position.positionAmt != "0" && position.positionAmt != "0.00000000") { // 只显示非零仓位
            activePositions++;
            std::cout << "  Symbol: " << position.symbol
                      << ", Position Amount: " << position.positionAmt
                      << ", Entry Price: " << position.entryPrice
                      << ", Break Even Price: " << position.breakEvenPrice
                      << ", Mark Price: " << position.markPrice
                      << ", Unrealized PnL: " << position.unrealizedProfit
                      << ", Liquidation Price: " << position.liquidationPrice
                      << ", Leverage: " << position.leverage
                      << ", Max Notional Value: " << position.maxNotionalValue
                      << ", Margin Type: " << position.marginType
                      << ", Isolated Margin: " << position.isolatedMargin
                      << ", Is Auto Add Margin: " << (position.isAutoAddMargin ? "Yes" : "No")
                      << ", Position Side: " << position.positionSide
                      << ", Notional: " << position.notional
                      << ", Isolated Wallet: " << position.isolatedWallet
                      << ", Update Time: " << position.updateTime
                      << ", Bid Notional: " << position.bidNotional
                      << ", Ask Notional: " << position.askNotional << std::endl;
        }
    }
    if (activePositions == 0) {
        std::cout << "  No active positions" << std::endl;
    }
    std::cout << "===========================" << std::endl;
}

void printOrderResponse(const OrderResponse& response) {
    std::cout << "\n=== Order Response ===" << std::endl;
    std::cout << "Success: " << (response.success ? "Yes" : "No") << std::endl;
    
    if (!response.success) {
        std::cout << "Error Code: " << response.errorCode << std::endl;
        std::cout << "Error Message: " << response.errorMessage << std::endl;
    } else {
        std::cout << "Symbol: " << response.symbol << std::endl;
        std::cout << "Order ID: " << response.orderId << std::endl;
        std::cout << "Client Order ID: " << response.clientOrderId << std::endl;
        std::cout << "Price: " << response.price << std::endl;
        std::cout << "Original Quantity: " << response.origQty << std::endl;
        std::cout << "Executed Quantity: " << response.executedQty << std::endl;
        std::cout << "Cumulative Quote Quantity: " << response.cummulativeQuoteQty << std::endl;
        std::cout << "Status: " << response.status << std::endl;
        std::cout << "Time In Force: " << response.timeInForce << std::endl;
        std::cout << "Type: " << response.type << std::endl;
        std::cout << "Side: " << response.side << std::endl;
        std::cout << "Position Side: " << response.positionSide << std::endl;
        std::cout << "Reduce Only: " << (response.reduceOnly ? "Yes" : "No") << std::endl;
        std::cout << "Close Position: " << (response.closePosition ? "Yes" : "No") << std::endl;
        if (!response.activatePrice.empty()) {
            std::cout << "Activate Price: " << response.activatePrice << std::endl;
        }
        if (!response.priceRate.empty()) {
            std::cout << "Price Rate: " << response.priceRate << std::endl;
        }
        std::cout << "Update Time: " << response.updateTime << std::endl;
        std::cout << "Working Type: " << response.workingType << std::endl;
        std::cout << "Price Protect: " << (response.priceProtect ? "Yes" : "No") << std::endl;
        std::cout << "Price Match: " << response.priceMatch << std::endl;
        std::cout << "Self Trade Prevention Mode: " << response.selfTradePreventionMode << std::endl;
        if (response.goodTillDate > 0) {
            std::cout << "Good Till Date: " << response.goodTillDate << std::endl;
        }
    }
    std::cout << "======================" << std::endl;
}

void printDepthUpdate(const DepthUpdate& update) {
    std::cout << "\n=== Depth Update ===" << std::endl;
    std::cout << "Symbol: " << update.symbol << std::endl;
    std::cout << "Event Time: " << update.eventTime << std::endl;
    std::cout << "Transaction Time: " << update.transactionTime << std::endl;
    std::cout << "First Update ID: " << update.firstUpdateId << std::endl;
    std::cout << "Final Update ID: " << update.finalUpdateId << std::endl;
    std::cout << "Previous Final Update ID: " << update.prevFinalUpdateId << std::endl;
    
    std::cout << "\nBids (" << update.bids.size() << " levels):" << std::endl;
    for (size_t i = 0; i < std::min(update.bids.size(), size_t(5)); ++i) {
        const auto& bid = update.bids[i];
        std::cout << "  Price: " << bid.price << ", Quantity: " << bid.quantity << std::endl;
    }
    if (update.bids.size() > 5) {
        std::cout << "  ... and " << (update.bids.size() - 5) << " more levels" << std::endl;
    }
    
    std::cout << "\nAsks (" << update.asks.size() << " levels):" << std::endl;
    for (size_t i = 0; i < std::min(update.asks.size(), size_t(5)); ++i) {
        const auto& ask = update.asks[i];
        std::cout << "  Price: " << ask.price << ", Quantity: " << ask.quantity << std::endl;
    }
    if (update.asks.size() > 5) {
        std::cout << "  ... and " << (update.asks.size() - 5) << " more levels" << std::endl;
    }
    std::cout << "====================" << std::endl;
}

void printTradeLite(const TradeLite& trade) {
    std::cout << "\n=== Trade Lite ===" << std::endl;
    std::cout << "Symbol: " << trade.symbol << std::endl;
    std::cout << "Quantity: " << trade.quantity << std::endl;
    std::cout << "Price: " << trade.price << std::endl;
    std::cout << "Order Status: " << trade.orderStatus << std::endl;
    std::cout << "Last Filled Quantity: " << trade.lastFilledQuantity << std::endl;
    std::cout << "Cumulative Filled Quantity: " << trade.cumulativeFilledQuantity << std::endl;
    std::cout << "Last Filled Price: " << trade.lastFilledPrice << std::endl;
    std::cout << "Commission Asset: " << trade.commissionAsset << std::endl;
    std::cout << "Commission: " << trade.commission << std::endl;
    std::cout << "Order Trade Time: " << trade.orderTradeTime << std::endl;
    std::cout << "Trade ID: " << trade.tradeId << std::endl;
    std::cout << "Buyer Order ID: " << trade.buyerOrderId << std::endl;
    std::cout << "Seller Order ID: " << trade.sellerOrderId << std::endl;
    std::cout << "Is Marker Side: " << (trade.isMarkerSide ? "Yes" : "No") << std::endl;
    std::cout << "Is Reduce Only: " << (trade.isReduceOnly ? "Yes" : "No") << std::endl;
    std::cout << "Stop Price Working Type: " << trade.stopPriceWorkingType << std::endl;
    std::cout << "Original Order Type: " << trade.originalOrderType << std::endl;
    std::cout << "Position Side: " << trade.positionSide << std::endl;
    std::cout << "Is Close Position: " << (trade.isClosePosition ? "Yes" : "No") << std::endl;
    if (!trade.activationPrice.empty()) {
        std::cout << "Activation Price: " << trade.activationPrice << std::endl;
    }
    if (!trade.callbackRate.empty()) {
        std::cout << "Callback Rate: " << trade.callbackRate << std::endl;
    }
    std::cout << "Realized PnL: " << trade.realizedPnl << std::endl;
    std::cout << "Price Protect: " << (trade.priceProtect ? "Yes" : "No") << std::endl;
    if (trade.stopPriceId > 0) {
        std::cout << "Stop Price ID: " << trade.stopPriceId << std::endl;
    }
    if (trade.strategyId > 0) {
        std::cout << "Strategy ID: " << trade.strategyId << std::endl;
    }
    std::cout << "==================" << std::endl;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "=== Binance WebSocket API Test ===" << std::endl;
    
    // 加载配置
    ConfigManager& configManager = ConfigManager::getInstance();
    std::string configPath = "../config/system_config_live.json";
    
    if (argc > 1) {
        configPath = argv[1];
    }
    
    if (!configManager.loadConfig(configPath)) {
        std::cerr << "Failed to load config from: " << configPath << std::endl;
        return 1;
    }
    
    std::cout << "Config loaded successfully" << std::endl;
    std::cout << "System: " << configManager.getSystemName() 
              << " v" << configManager.getSystemVersion() << std::endl;
    
    // 检查币安配置
    if (!configManager.hasExchangeConfig("binance")) {
        std::cerr << "Binance configuration not found" << std::endl;
        return 1;
    }
    
    ExchangeConfig binanceConfig = configManager.getExchangeConfig("binance");
    std::cout << "Binance config loaded, testnet: " << (binanceConfig.testnet ? "true" : "false") << std::endl;
    
    // 创建币安WebSocket客户端
    BinanceFactory factory;
    auto client = factory.createWebSocketClient("binance");
    
    if (!client) {
        std::cerr << "Failed to create Binance WebSocket client" << std::endl;
        return 1;
    }
    
    std::cout << "Created " << client->getExchangeName() << " WebSocket client" << std::endl;
    
    // 设置回调函数
    client->setAccountUpdateCallback(printAccountUpdate);
    client->setPositionUpdateCallback(printPositionUpdate);
    client->setOrderUpdateCallback(printOrderUpdate);
    client->setConnectionStatusCallback(printConnectionStatus);
    client->setErrorCallback(printError);
    
    // 设置账户余额和账户信息回调
    client->setAccountBalanceCallback(printAccountBalance);
    client->setAccountInfoCallback(printAccountInfo);
    
    // 设置新功能回调
    client->setOrderResponseCallback(printOrderResponse);
    client->setDepthUpdateCallback(printDepthUpdate);
    client->setTradeLiteCallback(printTradeLite);
    
    // 连接到WebSocket
    if (!client->connect()) {
        std::cout << "[ERROR] Failed to connect to WebSocket" << std::endl;
        return -1;
    }

    // 等待用户数据流连接稳定
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 连接到WebSocket API并进行认证
    std::cout << "Connecting to WebSocket API..." << std::endl;
    if (!client->sessionLogon()) {
        std::cout << "Warning: WebSocket API connection failed, account queries may not work" << std::endl;
    } else {
        std::cout << "WebSocket API connected and authenticated successfully!" << std::endl;
        
        // 连接成功后，发送测试请求
        std::cout << "\nRequesting account balance..." << std::endl;
        client->requestAccountBalance();
        
        // 等待账户余额响应
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "Requesting account information..." << std::endl;
        client->requestAccountInfo();
        
        // 等待账户信息响应
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 请求持仓信息
        std::cout << "Requesting position information..." << std::endl;
        client->requestPositionInfo();
        
        // 等待持仓信息响应
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 订阅用户数据流
        std::cout << "\nSubscribing to user data stream..." << std::endl;
        client->subscribeUserDataStream();
        
        // 等待用户数据流订阅
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 测试深度订阅
        std::cout << "\nSubscribing to BTCUSDT depth updates..." << std::endl;
        client->subscribeDepthUpdate("BTCUSDT", 5, 1000);
        
        // 等待深度数据
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        // 测试精简交易推送订阅
        std::cout << "\nSubscribing to trade lite updates..." << std::endl;
        client->subscribeTradeLite();
        
        // 等待交易数据
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        // 测试下单功能（小额测试单）
        std::cout << "\nTesting order placement (BTCUSDT limit order)..." << std::endl;
        OrderRequest orderRequest;
        orderRequest.symbol = "BTCUSDT";
        orderRequest.side = "BUY";
        orderRequest.type = "LIMIT";
        orderRequest.quantity = "0.001";  // 小额测试
        orderRequest.price = "30000";     // 远低于市价，不会成交
        orderRequest.timeInForce = "GTC";
        orderRequest.positionSide = "BOTH";
        
        client->placeOrder(orderRequest);
        
        // 等待订单响应
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    std::cout << "Press Ctrl+C to exit\n" << std::endl;
    
    // 主循环，监听更新
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Disconnecting..." << std::endl;
    client->disconnect();
    
    return 0;
}