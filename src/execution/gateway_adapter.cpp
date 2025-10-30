#include "execution/gateway_adapter.h"
#include "../../3rd/gateway/include/exchange_interface.h"
#include "../../3rd/gateway/include/binance_websocket.h"
#include "../../3rd/gateway/include/config_manager.h"
#include "../../3rd/gateway/include/data_structures.h"
#include <iostream>
#include <sstream>
#include <memory>

namespace tes {
namespace execution {

GatewayAdapter& GatewayAdapter::getInstance() {
    static GatewayAdapter instance;
    return instance;
}

GatewayAdapter::GatewayAdapter() 
    : initialized_(false)
    , running_(false)
    , connected_(false)
{
}

GatewayAdapter::~GatewayAdapter() {
    cleanup();
}

bool GatewayAdapter::initialize(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_.load()) {
        return true;
    }

    config_ = config;

    try {
        // 创建交易所配置
        trading::ExchangeConfig exchange_config;
        exchange_config.apiKey = config.api_key;
        exchange_config.apiSecret = config.api_secret;
        exchange_config.testnet = config.testnet;
        exchange_config.timeoutMs = config.timeout_ms;
        
        // 创建WebSocket客户端
        websocket_client_ = std::unique_ptr<trading::IExchangeWebSocket>(
            new trading::BinanceWebSocket(exchange_config));
        
        // 设置回调
        setup_callbacks();
        
        initialized_.store(true);
        std::cout << "GatewayAdapter initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        last_error_ = "Failed to initialize GatewayAdapter: " + std::string(e.what());
        std::cerr << last_error_ << std::endl;
        return false;
    }
}

bool GatewayAdapter::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        last_error_ = "GatewayAdapter not initialized";
        return false;
    }
    
    if (running_.load()) {
        return true;
    }

    try {
        // 连接WebSocket
        if (!websocket_client_->connect()) {
            last_error_ = "Failed to connect WebSocket";
            return false;
        }
        
        // 启动用户数据流
        if (!websocket_client_->startUserDataStream()) {
            last_error_ = "Failed to start user data stream";
            return false;
        }
        
        running_.store(true);
        connected_.store(true);
        std::cout << "GatewayAdapter started successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        last_error_ = "Failed to start GatewayAdapter: " + std::string(e.what());
        std::cerr << last_error_ << std::endl;
        return false;
    }
}

void GatewayAdapter::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!running_.load()) {
        return;
    }

    try {
        if (websocket_client_) {
            websocket_client_->closeUserDataStream();
            websocket_client_->disconnect();
        }
        
        running_.store(false);
        connected_.store(false);
        std::cout << "GatewayAdapter stopped" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error stopping GatewayAdapter: " << e.what() << std::endl;
    }
}

void GatewayAdapter::cleanup() {
    stop();
    
    std::lock_guard<std::mutex> lock(mutex_);
    websocket_client_.reset();
    initialized_.store(false);
}

bool GatewayAdapter::is_running() const {
    return running_.load();
}

bool GatewayAdapter::is_connected() const {
    return connected_.load() && websocket_client_ && websocket_client_->isConnected();
}

std::string GatewayAdapter::submit_order(const Order& order) {
    if (!is_running()) {
        last_error_ = "GatewayAdapter not running";
        return "";
    }

    try {
        // 创建订单请求
        std::string request_id = "order_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        
        // 使用trading命名空间的OrderRequest（在data_structures.h中定义）
        trading::OrderRequest request;
        request.symbol = order.instrument_id;
        request.quantity = std::to_string(order.quantity);
        request.price = std::to_string(order.price);
        request.newClientOrderId = order.client_order_id;
        
        // 转换订单方向
        if (order.side == OrderSide::BUY) {
            request.side = "BUY";
        } else {
            request.side = "SELL";
        }
        
        // 转换订单类型
        if (order.type == OrderType::LIMIT) {
            request.type = "LIMIT";
            request.timeInForce = "GTC";
        } else if (order.type == OrderType::MARKET) {
            request.type = "MARKET";
        }
        
        websocket_client_->placeOrder(request, request_id);
        return request_id;
        
    } catch (const std::exception& e) {
        last_error_ = "Failed to submit order: " + std::string(e.what());
        return "";
    }
}

bool GatewayAdapter::cancel_order(const std::string& order_id) {
    if (!websocket_client_ || !connected_.load()) {
        last_error_ = "Gateway not connected";
        return false;
    }

    try {
        // 直接构造CancelOrderRequest对象（在data_structures.h中定义）
        trading::CancelOrderRequest request;
        request.origClientOrderId = order_id;
        
        websocket_client_->cancelOrder(request);
        return true;
    } catch (const std::exception& e) {
        last_error_ = "Failed to cancel order: " + std::string(e.what());
        return false;
    }
}

bool GatewayAdapter::query_account_balance_ws() {
    if (!is_running()) {
        return false;
    }

    try {
        websocket_client_->requestAccountBalance();
        return true;
    } catch (const std::exception& e) {
        last_error_ = "Failed to query account balance: " + std::string(e.what());
        return false;
    }
}

bool GatewayAdapter::query_account_status_ws() {
    if (!is_running()) {
        return false;
    }

    try {
        websocket_client_->requestAccountInfo();
        return true;
    } catch (const std::exception& e) {
        last_error_ = "Failed to query account status: " + std::string(e.what());
        return false;
    }
}

void GatewayAdapter::setup_callbacks() {
    if (!websocket_client_) {
        return;
    }

    websocket_client_->setAccountUpdateCallback(
        [this](const trading::AccountUpdate& update) {
            on_account_update(update);
        });

    websocket_client_->setPositionUpdateCallback(
        [this](const trading::PositionUpdate& update) {
            on_position_update(update);
        });

    websocket_client_->setOrderUpdateCallback(
        [this](const trading::OrderUpdate& update) {
            on_order_update(update);
        });

    websocket_client_->setConnectionStatusCallback(
        [this](trading::ConnectionStatus status) {
            on_connection_status(status);
        });

    websocket_client_->setErrorCallback(
        [this](const std::string& error) {
            on_error(error);
        });
}

void GatewayAdapter::on_account_update(const trading::AccountUpdate& update) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // 更新缓存的余额信息
    for (const auto& balance : update.balances) {
        cached_balances_[balance.asset] = std::stod(balance.walletBalance);
    }
    
    // 更新缓存的持仓信息
    cached_positions_.clear();
    for (const auto& position : update.positions) {
        Position tes_position = convert_gateway_position_to_tes(position);
        cached_positions_.push_back(tes_position);
    }
}

void GatewayAdapter::on_position_update(const trading::PositionUpdate& update) {
    if (position_update_callback_) {
        trading::Position gateway_pos;
        gateway_pos.symbol = update.symbol;
        gateway_pos.positionAmount = update.positionAmount;
        gateway_pos.entryPrice = update.entryPrice;
        gateway_pos.unrealizedPnl = update.unrealizedPnl;
        gateway_pos.marginType = update.marginType;
        gateway_pos.positionSide = update.positionSide;
        
        Position tes_position = convert_gateway_position_to_tes(gateway_pos);
        position_update_callback_(tes_position);
    }
}

void GatewayAdapter::on_order_update(const trading::OrderUpdate& update) {
    if (order_update_callback_) {
        Order tes_order = convert_gateway_order_to_tes(update);
        order_update_callback_(tes_order);
    }
}

void GatewayAdapter::on_connection_status(trading::ConnectionStatus status) {
    connected_.store(status == trading::ConnectionStatus::CONNECTED);
}

void GatewayAdapter::on_error(const std::string& error) {
    last_error_ = error;
    if (error_callback_) {
        error_callback_(error);
    }
}

Order GatewayAdapter::convert_gateway_order_to_tes(const trading::OrderUpdate& gateway_order) const {
    Order tes_order;
    tes_order.order_id = gateway_order.orderId;
    tes_order.client_order_id = gateway_order.clientOrderId;
    tes_order.instrument_id = gateway_order.symbol;
    tes_order.quantity = std::stod(gateway_order.originalQuantity);
    tes_order.price = std::stod(gateway_order.originalPrice);
    
    // 转换订单方向
    if (gateway_order.side == "BUY") {
        tes_order.side = OrderSide::BUY;
    } else {
        tes_order.side = OrderSide::SELL;
    }
    
    // 转换订单状态
    if (gateway_order.orderStatus == "NEW") {
        tes_order.status = OrderStatus::SUBMITTED;
    } else if (gateway_order.orderStatus == "FILLED") {
        tes_order.status = OrderStatus::FILLED;
    } else if (gateway_order.orderStatus == "CANCELED") {
        tes_order.status = OrderStatus::CANCELLED;
    } else if (gateway_order.orderStatus == "PARTIALLY_FILLED") {
        tes_order.status = OrderStatus::PARTIALLY_FILLED;
    }
    
    return tes_order;
}

tes::execution::Position GatewayAdapter::convert_gateway_position_to_tes(const trading::Position& gateway_position) const {
    tes::execution::Position tes_position;
    tes_position.instrument_id = gateway_position.symbol;
    
    double pos_amount = std::stod(gateway_position.positionAmount);
    if (pos_amount > 0) {
        tes_position.long_quantity = pos_amount;
        tes_position.short_quantity = 0.0;
    } else {
        tes_position.long_quantity = 0.0;
        tes_position.short_quantity = -pos_amount;
    }
    tes_position.net_quantity = pos_amount;
    tes_position.average_cost = std::stod(gateway_position.entryPrice);
    tes_position.unrealized_pnl = std::stod(gateway_position.unrealizedPnl);
    
    return tes_position;
}

// 移除convert_tes_order_to_gateway方法实现

void GatewayAdapter::set_order_update_callback(OrderUpdateCallback callback) {
    order_update_callback_ = callback;
}

void GatewayAdapter::set_position_update_callback(PositionUpdateCallback callback) {
    position_update_callback_ = callback;
}

void GatewayAdapter::set_trade_execution_callback(TradeExecutionCallback callback) {
    trade_execution_callback_ = callback;
}

void GatewayAdapter::set_error_callback(ErrorCallback callback) {
    error_callback_ = callback;
}

std::string GatewayAdapter::get_last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

std::shared_ptr<Order> GatewayAdapter::get_order(const std::string& order_id) const {
    // 这里需要实现从缓存或通过API查询订单的逻辑
    return nullptr;
}

std::vector<std::shared_ptr<Order>> GatewayAdapter::get_active_orders() const {
    // 这里需要实现获取活跃订单的逻辑
    return {};
}

std::vector<Position> GatewayAdapter::get_positions(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    if (symbol.empty()) {
        return cached_positions_;
    }
    
    std::vector<Position> filtered_positions;
    for (const auto& pos : cached_positions_) {
        if (pos.instrument_id == symbol) {
            filtered_positions.push_back(pos);
        }
    }
    return filtered_positions;
}

std::shared_ptr<Position> GatewayAdapter::get_position(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    for (const auto& pos : cached_positions_) {
        if (pos.instrument_id == symbol) {
            return std::make_shared<Position>(pos);
        }
    }
    return nullptr;
}

double GatewayAdapter::get_account_balance(const std::string& asset) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = cached_balances_.find(asset);
    if (it != cached_balances_.end()) {
        return it->second;
    }
    return 0.0;
}

double GatewayAdapter::get_available_balance(const std::string& asset) const {
    // 简化实现，返回总余额
    return get_account_balance(asset);
}

} // namespace execution
} // namespace tes