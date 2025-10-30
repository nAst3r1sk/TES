#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace trading {

/**
 * @brief 资产余额信息
 */
struct AssetBalance {
    std::string asset;              // 资产名称 (如 "USDT", "BTC")
    std::string walletBalance;      // 钱包余额
    std::string crossWalletBalance; // 除去逐仓仓位保证金的钱包余额
    std::string balanceChange;      // 除去盈亏与交易手续费以外的钱包余额改变量
    
    AssetBalance() = default;
    AssetBalance(const std::string& a, const std::string& wb, 
                 const std::string& cw, const std::string& bc)
        : asset(a), walletBalance(wb), crossWalletBalance(cw), balanceChange(bc) {}
};

/**
 * @brief 仓位信息
 */
struct Position {
    std::string symbol;             // 交易对 (如 "BTCUSDT")
    std::string positionAmount;     // 仓位数量
    std::string entryPrice;         // 入仓价格
    std::string breakEvenPrice;     // 盈亏平衡价
    std::string cumulativeRealized; // (费前)累计实现损益
    std::string unrealizedPnl;      // 持仓未实现盈亏
    std::string marginType;         // 保证金模式 ("isolated" 或 "cross")
    std::string isolatedWallet;     // 若为逐仓，仓位保证金
    std::string positionSide;       // 持仓方向 ("LONG", "SHORT", "BOTH")
    
    Position() = default;
    Position(const std::string& s, const std::string& pa, const std::string& ep,
             const std::string& bep, const std::string& cr, const std::string& up,
             const std::string& mt, const std::string& iw, const std::string& ps)
        : symbol(s), positionAmount(pa), entryPrice(ep), breakEvenPrice(bep),
          cumulativeRealized(cr), unrealizedPnl(up), marginType(mt),
          isolatedWallet(iw), positionSide(ps) {}
};

/**
 * @brief 账户更新事件
 */
struct AccountUpdate {
    std::string eventType;                    // 事件类型 "ACCOUNT_UPDATE"
    int64_t eventTime;                        // 事件时间
    int64_t transactionTime;                  // 撮合时间
    std::string updateReason;                 // 事件推出原因 ("ORDER", "FUNDING", etc.)
    std::vector<AssetBalance> balances;       // 余额信息
    std::vector<Position> positions;          // 仓位信息
    
    AccountUpdate() : eventTime(0), transactionTime(0) {}
};

/**
 * @brief 仓位更新事件
 */
struct PositionUpdate {
    std::string symbol;
    std::string positionAmount;
    std::string entryPrice;
    std::string unrealizedPnl;
    std::string marginType;
    std::string positionSide;
    int64_t updateTime;
    
    PositionUpdate() : updateTime(0) {}
};

/**
 * @brief 订单状态
 */
enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED,
    EXPIRED
};

/**
 * @brief 订单类型
 */
enum class OrderType {
    LIMIT,
    MARKET,
    STOP,
    STOP_MARKET,
    TAKE_PROFIT,
    TAKE_PROFIT_MARKET
};

/**
 * @brief 订单方向
 */
enum class OrderSide {
    BUY,
    SELL
};

/**
 * @brief 订单更新事件
 */
struct OrderUpdate {
    // 基本信息
    std::string eventType;          // 事件类型 "ORDER_TRADE_UPDATE"
    int64_t eventTime;              // 事件时间
    int64_t transactionTime;        // 撮合时间
    
    // 订单基本信息
    std::string symbol;             // 交易对
    std::string clientOrderId;      // 客户端自定订单ID
    std::string orderId;            // 订单ID
    std::string side;               // 订单方向 ("BUY", "SELL")
    std::string orderType;          // 订单类型
    std::string timeInForce;        // 有效方式 ("GTC", "IOC", "FOK")
    
    // 价格和数量
    std::string originalQuantity;   // 订单原始数量
    std::string originalPrice;      // 订单原始价格
    std::string averagePrice;       // 订单平均价格
    std::string stopPrice;          // 条件订单触发价格
    
    // 执行信息
    std::string executionType;      // 本次事件的具体执行类型
    std::string orderStatus;        // 订单的当前状态
    std::string lastExecutedQuantity;   // 订单末次成交量
    std::string cumulativeFilledQuantity; // 订单累计已成交量
    std::string lastExecutedPrice;  // 订单末次成交价格
    
    // 手续费信息
    std::string commissionAsset;    // 手续费资产类型
    std::string commissionAmount;   // 手续费数量
    
    // 其他信息
    int64_t tradeTime;              // 成交时间
    int64_t tradeId;                // 成交ID
    std::string buyerOrderValue;    // 买单净值
    std::string sellerOrderValue;   // 卖单净值
    bool isMakerSide;               // 该成交是作为挂单成交吗？
    bool isReduceOnly;              // 是否是只减仓单
    std::string workingType;        // 触发价类型
    std::string originalOrderType;  // 原始订单类型
    std::string positionSide;       // 持仓方向
    bool isClosePosition;           // 是否为触发平仓单
    std::string activationPrice;    // 追踪止损激活价格
    std::string callbackRate;       // 追踪止损回调比例
    std::string realizedProfit;     // 该交易实现盈亏
    std::string selfTradePreventionMode; // 自成交防止模式
    std::string priceMatchMode;     // 价格匹配模式
    int64_t goodTillDate;           // TIF为GTD的订单自动取消时间
    
    OrderUpdate() : eventTime(0), transactionTime(0), tradeTime(0), tradeId(0), 
                   isMakerSide(false), isReduceOnly(false), isClosePosition(false),
                   goodTillDate(0) {}
};

/**
 * @brief WebSocket消息类型
 */
enum class MessageType {
    ACCOUNT_UPDATE,
    POSITION_UPDATE,
    ORDER_UPDATE,
    BALANCE_UPDATE,
    ERROR,
    HEARTBEAT
};

/**
 * @brief 通用WebSocket消息
 */
struct WebSocketMessage {
    MessageType type;
    std::string rawData;
    int64_t timestamp;
    
    WebSocketMessage() : type(MessageType::HEARTBEAT), 
                        timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()) {}
};

/**
 * @brief 账户资产信息 (用于账户信息查询)
 */
struct AccountAsset {
    std::string asset;                      // 资产
    std::string walletBalance;              // 余额
    std::string unrealizedProfit;           // 未实现盈亏
    std::string marginBalance;              // 保证金余额
    std::string maintMargin;                // 维持保证金
    std::string initialMargin;              // 当前所需起始保证金
    std::string positionInitialMargin;      // 持仓所需起始保证金(基于最新标记价格)
    std::string openOrderInitialMargin;     // 当前挂单所需起始保证金(基于最新标记价格)
    std::string crossWalletBalance;         // 全仓账户余额
    std::string crossUnPnl;                 // 全仓持仓未实现盈亏
    std::string availableBalance;           // 可用余额
    std::string maxWithdrawAmount;          // 最大可转出余额
    int64_t updateTime;                     // 更新时间
    
    AccountAsset() : updateTime(0) {}
};

/**
 * @brief 账户持仓信息 (用于账户信息查询)
 */
struct AccountPosition {
    std::string symbol;                     // 交易对
    std::string positionSide;               // 持仓方向
    std::string positionAmt;                // 持仓数量
    std::string unrealizedProfit;           // 持仓未实现盈亏
    std::string isolatedMargin;             // 逐仓保证金
    std::string notional;                   // 名义价值
    std::string isolatedWallet;             // 逐仓钱包余额
    std::string initialMargin;              // 持仓所需起始保证金(基于最新标记价格)
    std::string maintMargin;                // 当前杠杆下用户可用的最大名义价值
    int64_t updateTime;                     // 更新时间
    
    // 新增字段以匹配main.cpp中的使用
    std::string entryPrice;                 // 入仓价格
    std::string breakEvenPrice;             // 盈亏平衡价
    std::string markPrice;                  // 标记价格
    std::string liquidationPrice;           // 强平价格
    std::string leverage;                   // 杠杆倍数
    std::string maxNotionalValue;           // 最大名义价值
    std::string marginType;                 // 保证金模式
    bool isAutoAddMargin;                   // 是否自动追加保证金
    std::string bidNotional;                // 买盘名义价值
    std::string askNotional;                // 卖盘名义价值
    
    AccountPosition() : updateTime(0), isAutoAddMargin(false) {}
};

/**
 * @brief 账户余额响应 (v2/account.balance)
 */
struct AccountBalanceResponse {
    std::string id;                         // 请求ID
    int status;                             // 状态码
    std::vector<AccountAsset> result;       // 账户资产列表
    
    AccountBalanceResponse() : status(0) {}
};

/**
 * @brief 账户信息响应 (v2/account.status)
 */
struct AccountInfoResponse {
    std::string id;                         // 请求ID
    int status;                             // 状态码
    
    // 总计信息
    std::string totalInitialMargin;         // 当前所需起始保证金总额
    std::string totalMaintMargin;           // 维持保证金总额
    std::string totalWalletBalance;         // 账户总余额
    std::string totalUnrealizedProfit;      // 持仓未实现盈亏总额
    std::string totalMarginBalance;         // 保证金总余额
    std::string totalPositionInitialMargin; // 持仓所需起始保证金
    std::string totalOpenOrderInitialMargin;// 当前挂单所需起始保证金
    std::string totalCrossWalletBalance;    // 全仓账户余额
    std::string totalCrossUnPnl;           // 全仓持仓未实现盈亏总额
    std::string availableBalance;           // 可用余额
    std::string maxWithdrawAmount;          // 最大可转出余额
    
    // 新增字段以匹配main.cpp中的使用
    int feeTier;                            // 手续费等级
    bool canTrade;                          // 是否可以交易
    bool canDeposit;                        // 是否可以充值
    bool canWithdraw;                       // 是否可以提现
    int64_t updateTime;                     // 更新时间
    bool multiAssetsMargin;                 // 是否启用多资产保证金
    int64_t tradeGroupId;                   // 交易组ID
    
    // 详细信息
    std::vector<AccountAsset> assets;       // 资产列表
    std::vector<AccountPosition> positions; // 持仓列表
    
    AccountInfoResponse() : status(0), feeTier(0), canTrade(false), canDeposit(false), 
                           canWithdraw(false), updateTime(0), multiAssetsMargin(false), 
                           tradeGroupId(0) {}
};

/**
 * @brief 订单请求参数
 */
struct OrderRequest {
    std::string symbol;                     // 交易对
    std::string side;                       // 买卖方向 BUY, SELL
    std::string positionSide;               // 持仓方向 BOTH, LONG, SHORT
    std::string type;                       // 订单类型
    std::string reduceOnly;                 // 只减仓 true, false
    std::string quantity;                   // 下单数量
    std::string price;                      // 委托价格
    std::string newClientOrderId;           // 用户自定义订单号
    std::string stopPrice;                  // 触发价
    std::string closePosition;              // 触发后全部平仓
    std::string activationPrice;            // 追踪止损激活价格
    std::string callbackRate;               // 追踪止损回调比例
    std::string timeInForce;                // 有效方法 GTC, IOC, FOK, GTD
    std::string workingType;                // 触发价类型
    std::string priceProtect;               // 条件单触发保护
    std::string newOrderRespType;           // 响应类型 ACK, RESULT
    std::string priceMatch;                 // 价格匹配模式
    std::string selfTradePreventionMode;    // 自成交防止模式
    int64_t goodTillDate;                   // GTD订单自动取消时间
    
    OrderRequest() : goodTillDate(0) {}
};

/**
 * @brief 订单响应
 */
struct OrderResponse {
    std::string id;                         // 请求ID
    int status;                             // 状态码
    
    // 订单信息
    std::string clientOrderId;              // 客户端订单ID
    std::string cumQty;                     // 累计成交量
    std::string cumQuote;                   // 累计成交金额
    std::string cummulativeQuoteQty;        // 累计成交金额（备用字段）
    std::string executedQty;                // 已成交数量
    int64_t orderId;                        // 订单ID
    std::string origQty;                    // 原始数量
    std::string origType;                   // 原始订单类型
    std::string price;                      // 价格
    bool reduceOnly;                        // 只减仓
    std::string side;                       // 买卖方向
    std::string positionSide;               // 持仓方向
    std::string status_str;                 // 订单状态
    std::string stopPrice;                  // 触发价
    bool closePosition;                     // 全部平仓
    std::string symbol;                     // 交易对
    std::string timeInForce;                // 有效方法
    std::string type;                       // 订单类型
    std::string activatePrice;              // 激活价格
    std::string priceRate;                  // 价格比率
    int64_t updateTime;                     // 更新时间
    std::string workingType;                // 工作类型
    bool priceProtect;                      // 价格保护
    std::string priceMatch;                 // 价格匹配
    std::string selfTradePreventionMode;    // 自成交防止模式
    int64_t goodTillDate;                   // 有效期
    
    // 新增错误处理字段
    bool success;                           // 是否成功
    int errorCode;                          // 错误代码
    std::string errorMessage;               // 错误消息
    
    OrderResponse() : status(0), orderId(0), reduceOnly(false), closePosition(false), 
                     updateTime(0), priceProtect(false), goodTillDate(0),
                     success(true), errorCode(0) {}
};

/**
 * @brief 撤单请求参数
 */
struct CancelOrderRequest {
    std::string symbol;                     // 交易对
    int64_t orderId;                        // 订单ID (orderId 和 origClientOrderId 必须至少提供一个)
    std::string origClientOrderId;          // 客户端订单ID
    
    CancelOrderRequest() : orderId(0) {}
};

/**
 * @brief 深度信息中的价格档位
 */
struct PriceLevel {
    std::string price;                      // 价格
    std::string quantity;                   // 数量
    
    PriceLevel() = default;
    PriceLevel(const std::string& p, const std::string& q) : price(p), quantity(q) {}
};

/**
 * @brief 深度信息更新
 */
struct DepthUpdate {
    std::string eventType;                  // 事件类型 "depthUpdate"
    int64_t eventTime;                      // 事件时间
    int64_t transactionTime;                // 交易时间
    std::string symbol;                     // 交易对
    int64_t firstUpdateId;                  // 从上次推送至今新增的第一个update Id
    int64_t finalUpdateId;                  // 从上次推送至今新增的最后一个update Id
    int64_t prevFinalUpdateId;              // 上次推送的最后一个update Id
    std::vector<PriceLevel> bids;           // 买方价格档位
    std::vector<PriceLevel> asks;           // 卖方价格档位
    
    DepthUpdate() : eventTime(0), transactionTime(0), firstUpdateId(0), 
                   finalUpdateId(0), prevFinalUpdateId(0) {}
};

/**
 * @brief 精简交易推送
 */
struct TradeLite {
    std::string eventType;                  // 事件类型 "TRADE_LITE"
    int64_t eventTime;                      // 事件时间
    int64_t transactionTime;                // 交易时间
    std::string symbol;                     // 交易对
    std::string quantity;                   // 订单原始数量
    std::string price;                      // 订单原始价格
    bool isMakerSide;                       // 该成交是作为挂单成交吗？
    std::string clientOrderId;              // 客户端自定订单ID
    std::string side;                       // 订单方向
    std::string lastPrice;                  // 订单末次成交价格
    std::string lastQuantity;               // 订单末次成交量
    int64_t tradeId;                        // 成交ID
    int64_t orderId;                        // 订单ID
    
    // 新增字段以匹配main.cpp中的使用
    std::string orderStatus;                // 订单状态
    std::string lastFilledQuantity;         // 订单末次成交量
    std::string cumulativeFilledQuantity;   // 订单累计已成交量
    std::string lastFilledPrice;            // 订单末次成交价格
    std::string commissionAsset;            // 手续费资产类型
    std::string commission;                 // 手续费数量
    int64_t orderTradeTime;                 // 订单成交时间
    std::string buyerOrderId;               // 买单订单ID
    std::string sellerOrderId;              // 卖单订单ID
    bool isMarkerSide;                      // 是否为挂单方
    bool isReduceOnly;                      // 是否为只减仓单
    std::string stopPriceWorkingType;       // 触发价类型
    std::string originalOrderType;          // 原始订单类型
    std::string positionSide;               // 持仓方向
    bool isClosePosition;                   // 是否为触发平仓单
    std::string activationPrice;            // 追踪止损激活价格
    std::string callbackRate;               // 追踪止损回调比例
    std::string realizedPnl;                // 该交易实现盈亏
    bool priceProtect;                      // 条件单触发保护
    int64_t stopPriceId;                    // 止损价格ID
    int64_t strategyId;                     // 策略ID
    
    TradeLite() : eventTime(0), transactionTime(0), isMakerSide(false), 
                 tradeId(0), orderId(0), orderTradeTime(0), isMarkerSide(false),
                 isReduceOnly(false), isClosePosition(false), priceProtect(false),
                 stopPriceId(0), strategyId(0) {}
};

} // namespace trading