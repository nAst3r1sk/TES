#include "execution/execution_controller.h"
#include "execution/config_manager.h"
#include "execution/signal_transmission_manager.h"
#include "execution/json_feedback_writer.h"
#include "common/common_types.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace tes {
namespace execution {

// 类型转换函数
static OrderSide convert_order_side(shared_memory::OrderSide side)
{
    switch (side) {
        case shared_memory::OrderSide::BUY: return OrderSide::BUY;
        case shared_memory::OrderSide::SELL: return OrderSide::SELL;
        default: return OrderSide::BUY;
    }
}

static OrderType convert_order_type(shared_memory::OrderType type)
{
    switch (type) {
        case shared_memory::OrderType::MARKET: return OrderType::MARKET;
        case shared_memory::OrderType::LIMIT: return OrderType::LIMIT;
        case shared_memory::OrderType::STOP: return OrderType::STOP;
        case shared_memory::OrderType::STOP_LIMIT: return OrderType::STOP_LIMIT;
        default: return OrderType::LIMIT;
    }
}

static TimeInForce convert_time_in_force(shared_memory::TimeInForce tif)
{
    switch (tif) {
        case shared_memory::TimeInForce::DAY: return TimeInForce::DAY;
        case shared_memory::TimeInForce::GTC: return TimeInForce::GTC;
        case shared_memory::TimeInForce::IOC: return TimeInForce::IOC;
        case shared_memory::TimeInForce::FOK: return TimeInForce::FOK;
        default: return TimeInForce::DAY;
    }
}

ExecutionController::ExecutionController() 
    : initialized_(false), running_(false)
{
    // 从全局配置管理器加载配置
    auto& config_manager = GlobalConfigManager::instance();
    auto system_config = config_manager.get_system_config();
    
    config_.worker_thread_count = system_config.worker_thread_count > 0 ? system_config.worker_thread_count : std::thread::hardware_concurrency();
    config_.signal_processing_interval_ms = system_config.signal_processing_interval_ms;
    config_.heartbeat_interval_ms = system_config.heartbeat_interval_ms;
    config_.statistics_update_interval_ms = system_config.statistics_update_interval_ms;
    config_.enable_risk_checking = system_config.enable_risk_control;
    config_.enable_position_tracking = system_config.enable_position_tracking;
    config_.enable_algorithm_execution = system_config.enable_algorithm_execution;
    config_.enable_order_feedback = system_config.enable_order_feedback;
    config_.trading_exchanges = system_config.trading_exchanges;
    config_.signal_transmission_mode = static_cast<SignalTransmissionMode>(system_config.signaltrans_mode);
    config_.system_config_file = "config/system_config.json";
    config_.gateway_config.api_key = system_config.api_key;
    config_.gateway_config.api_secret = system_config.api_secret;
    config_.gateway_config.testnet = system_config.testnet;
    config_.gateway_config.trading_type = system_config.trading_type.empty() ? "futures" : system_config.trading_type[0];
    config_.gateway_config.enable_websocket = system_config.enable_websocket;
    config_.gateway_config.sync_interval_ms = system_config.sync_interval_ms;
    config_.gateway_config.timeout_ms = system_config.timeout_ms;
    
    // 加载TWAP相关配置
     config_.twap_quantity_threshold = system_config.twap_quantity_threshold;
     config_.twap_value_threshold = system_config.twap_value_threshold;
     config_.twap_market_impact_threshold = system_config.twap_market_impact_threshold;
     config_.default_twap_duration_minutes = system_config.twap_default_duration_minutes;
     config_.twap_min_slice_size = system_config.twap_min_slice_size;
     config_.max_twap_slices = system_config.twap_max_slices;
     config_.default_participation_rate = system_config.twap_default_participation_rate;
     config_.max_price_deviation_bps = system_config.twap_max_price_deviation_bps;
    
    statistics_ = {};
}

// 辅助函数：检查是否启用了指定交易所
bool ExecutionController::is_exchange_enabled(const std::string& exchange) const {
    return std::find(config_.trading_exchanges.begin(), config_.trading_exchanges.end(), exchange) != config_.trading_exchanges.end();
}

ExecutionController::~ExecutionController()
{
    stop();
    cleanup();
}

bool ExecutionController::initialize()
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    
    if (initialized_.load()) {
        return true;
    }
    
    try {
        // 初始化Gateway适配器（替代BinanceTradingInterface）
        if (is_exchange_enabled("binance")) {
            gateway_adapter_ = &GatewayAdapter::getInstance();
            if (!gateway_adapter_->initialize(config_.gateway_config)) {
                set_error("Failed to initialize GatewayAdapter");
                return false;
            }
            
            // 设置Gateway适配器的回调
            gateway_adapter_->set_order_update_callback(
                [this](const Order& order) {
                    handle_order_event(order);
                }
            );
            
            gateway_adapter_->set_trade_execution_callback(
                [this](const Trade& trade) {
                    handle_trade_event(trade);
                }
            );
        }
        
        // 移除MarketDataManager初始化，因为它依赖不存在的头文件
        // 市场数据功能将通过gateway系统提供
        
        // 初始化订单管理组件
        order_manager_ = std::unique_ptr<OrderManager>(new OrderManager());
        if (!order_manager_->initialize()) {
            set_error("Failed to initialize OrderManager");
            return false;
        }
        
        // 创建TWAP算法（不依赖BinanceTradingInterface）
        twap_algorithm_ = std::unique_ptr<TWAPAlgorithm>(new TWAPAlgorithm(
            std::shared_ptr<OrderManager>(order_manager_.get(), [](OrderManager*) {/* no-op deleter */}),
            nullptr  // 暂时不使用BinanceTradingInterface
        ));
        if (!twap_algorithm_->initialize()) {
            set_error("Failed to initialize TWAPAlgorithm");
            return false;
        }

        // 初始化信号传递管理器，获取配置
        signal_transmission_manager_ = std::unique_ptr<SignalTransmissionManager>(new SignalTransmissionManager());
        SignalTransmissionManager::Config signal_config;
        
        try {
            std::ifstream file(config_.system_config_file);
            if (file.is_open()) {
                nlohmann::json json_config;
                file >> json_config;
                
                if (json_config.contains("signal_transmission_config")) {
                    const nlohmann::json& signal_json = json_config["signal_transmission_config"];
                    
                    std::string mode_str = signal_json.value("mode", std::string("shared_memory"));
                    if (mode_str == "shared_memory") {
                        signal_config.mode = SignalTransmissionMode::SHARED_MEMORY;
                    } else if (mode_str == "file") {
                        signal_config.mode = SignalTransmissionMode::JSON_FILE;
                    } else if (mode_str == "network") {
                        signal_config.mode = SignalTransmissionMode::SHARED_MEMORY;
                    } else {
                        signal_config.mode = SignalTransmissionMode::SHARED_MEMORY;
                    }
                    
                    signal_config.json_file_path = signal_json.value("json_file_path", std::string("./signals.json"));
                    signal_config.json_update_interval_ms = signal_json.value("json_update_interval_ms", static_cast<uint32_t>(100));
                    signal_config.precision_tolerance = signal_json.value("precision_tolerance", 1e-8);
                    signal_config.max_position_diff = signal_json.value("max_position_diff", 1000.0);
                    signal_config.sync_timeout_ms = signal_json.value("sync_timeout_ms", static_cast<uint32_t>(5000));
                    signal_config.enable_auto_sync = signal_json.value("enable_auto_sync", true);
                }
            }
        } catch (const std::exception& e) {
            // 使用默认配置
            signal_config.mode = SignalTransmissionMode::SHARED_MEMORY;
            // 使用默认的JSON模式配置
            signal_config.json_file_path = "./signals.json";
            signal_config.json_update_interval_ms = 100;
            signal_config.precision_tolerance = 1e-8;
            signal_config.max_position_diff = 1000.0;
            signal_config.sync_timeout_ms = 5000;
            signal_config.enable_auto_sync = true;
        }
        
        if (!signal_transmission_manager_->initialize(signal_config)) {
            set_error("Failed to initialize SignalTransmissionManager");
            return false;
        }

        // 根据信号传递模式决定是否创建共享内存接口
        if (signal_config.mode == SignalTransmissionMode::SHARED_MEMORY) {
            shared_memory_interface_ = std::unique_ptr<SharedMemoryInterface>(new SharedMemoryInterface());
            if (!shared_memory_interface_->initialize(false)) {
                set_error("Failed to initialize SharedMemoryInterface");
                return false;
            }
        } else {
            shared_memory_interface_ = nullptr;
            
            // 在JSON模式下初始化JSON反馈写入器
            json_feedback_writer_ = std::unique_ptr<JsonFeedbackWriter>(new JsonFeedbackWriter());
            if (!json_feedback_writer_->initialize(config_.json_feedback_config)) {
                set_error("Failed to initialize JsonFeedbackWriter");
                return false;
            }
        }
        
        // Binance交易接口的回调设置已在前面的enable_binance_trading条件块中完成
        
        trading_rule_checker_ = std::unique_ptr<TradingRuleChecker>(new TradingRuleChecker());
        if (!trading_rule_checker_->initialize()) {
            set_error("Failed to initialize TradingRuleChecker");
            return false;
        }
        
        if (is_exchange_enabled("binance") && gateway_adapter_) {
            // TODO: 实现Gateway适配器的客户端获取方法
            // auto binance_client = gateway_adapter_->get_binance_client();
            // if (binance_client) {
            //     trading_rule_checker_->set_binance_client(binance_client);
            // }
        }
        
        position_manager_ = std::unique_ptr<PositionManager>(new PositionManager());
        if (!position_manager_->initialize()) {
            set_error("Failed to initialize PositionManager");
            return false;
        }
        
        // 将PositionManager集成到Gateway适配器
        if (is_exchange_enabled("binance") && gateway_adapter_) {
            // TODO: 实现Gateway适配器的PositionManager集成
            // gateway_adapter_->set_position_manager(
            //     std::shared_ptr<PositionManager>(position_manager_.get(), [](PositionManager*){/* no-op deleter */})
            // );
            
            // 设置PositionManager的交易所适配器
            // position_manager_->set_exchange_adapter(
            //     std::shared_ptr<PositionExchangeAdapter>(
            //         gateway_adapter_, [](PositionExchangeAdapter*){/* no-op deleter */})
            // );
        }
        
        // 设置信号传递管理器的依赖
        signal_transmission_manager_->set_position_manager(std::shared_ptr<PositionManager>(
            position_manager_.get(), [](PositionManager*) {/* no-op deleter */}));
        
        // 设置SharedMemoryInterface依赖
        if (shared_memory_interface_) {
            signal_transmission_manager_->set_shared_memory_interface(
                std::shared_ptr<SharedMemoryInterface>(shared_memory_interface_.get(), [](SharedMemoryInterface*){/* no-op deleter */}));
        }
        
        // 设置GatewayAdapter依赖
        if (gateway_adapter_) {
            // TODO: 设置gateway适配器的回调函数
            // signal_transmission_manager_->set_gateway_adapter(gateway_adapter_);
        }
        
        // 设置持仓同步回调函数
        signal_transmission_manager_->set_position_sync_callback(
            [this](const PositionSyncResult& result) {
                std::cout << "\n=== Position Sync Completed ===" << std::endl;
                std::cout << "Success: " << (result.success ? "Yes" : "No") << std::endl;
                if (result.success) {
                    std::cout << "Positions aligned: " << result.positions_aligned << std::endl;
                    std::cout << "Positions opened: " << result.positions_opened << std::endl;
                    std::cout << "Positions closed: " << result.positions_closed << std::endl;
                    std::cout << "Total operations: " << (result.positions_opened + result.positions_closed) << std::endl;
                } else {
                    std::cout << "Error: " << result.error_message << std::endl;
                }
                std::cout << "==============================\n" << std::endl;
            }
        );
        
        // 初始化线程池用于并发信号处理
        signal_thread_pool_ = std::unique_ptr<ThreadPool>(new ThreadPool(config_.worker_thread_count));
        
        // 初始化无锁信号队列
        signal_queue_ = std::unique_ptr<MPMCLockFreeQueue<shared_memory::TradingSignal>>(new MPMCLockFreeQueue<shared_memory::TradingSignal>());
        
        // 初始化异步回调管理器
        async_callback_manager_ = std::unique_ptr<AsyncCallbackManager>(new AsyncCallbackManager());
        AsyncCallbackManager::Config callback_config;
        callback_config.thread_pool_size = 2;
        callback_config.max_queue_size = 5000;
        callback_config.batch_size = 20;
        callback_config.flush_interval_ms = 5;
        
        if (!async_callback_manager_->initialize(callback_config) || 
            !async_callback_manager_->start()) {
            return false;
        }
        
        // 初始化性能监控器
        performance_monitor_ = std::unique_ptr<PerformanceMonitor>(new PerformanceMonitor());
        
        // 配置性能监控器
        PerformanceMonitor::Config perf_config;
        perf_config.collection_interval_ms = 1000;  // 1秒收集间隔
        perf_config.max_data_points = 1000;         // 最大数据点数
        perf_config.enable_cpu_monitoring = true;
        perf_config.enable_memory_monitoring = true;
        perf_config.enable_file_output = true;
        perf_config.output_file_path = "performance_monitor.log";
        performance_monitor_->set_config(perf_config);
        
        // 初始化并启动性能监控器
        if (!performance_monitor_->initialize(perf_config)) {
            return false;
        }
        
        if (!performance_monitor_->start()) {
            return false;
        }
        
        // 设置事件回调
        setup_event_callbacks();
        
        // 初始化统计信息
        statistics_ = {};
        statistics_.last_signal_time = std::chrono::high_resolution_clock::now();
        statistics_.last_order_time = std::chrono::high_resolution_clock::now();
        statistics_.last_trade_time = std::chrono::high_resolution_clock::now();
        
        initialized_.store(true);
        return true;
    } catch (const std::exception& e) {
        set_error("Exception in initialize: " + std::string(e.what()));
        return false;
    }
}

bool ExecutionController::start()
{
    if (!initialized_.load()) {
        if (!initialize()) {
            return false;
        }
    }
    
    if (running_.load()) {
        return true;
    }
    
    try {
        // 只在共享内存模式下连接共享内存
        if (shared_memory_interface_ && !shared_memory_interface_->connect()) {
            set_error("Failed to connect to shared memory");
            return false;
        }
        
        // 启动核心组件
        if (!order_manager_->start()) {
            set_error("Failed to start OrderManager");
            return false;
        }
        
        if (!twap_algorithm_->start()) {
            set_error("Failed to start TWAPAlgorithm");
            return false;
        }
        
        if (!position_manager_->start()) {
            set_error("Failed to start PositionManager");
            return false;
        }
        
        if (!signal_transmission_manager_->start()) {
            set_error("Failed to start SignalTransmissionManager");
            return false;
        }
        
        // 启动Gateway适配器
        if (is_exchange_enabled("binance") && gateway_adapter_) {
            if (!gateway_adapter_->start()) {
                set_error("Failed to start GatewayAdapter");
                return false;
            }
        }
        
        running_.store(true);
        
        // 启动工作线程
        worker_threads_.clear();
        
        // 信号处理线程
        for (uint32_t i = 0; i < config_.worker_thread_count; ++i) {
            worker_threads_.push_back(
                std::unique_ptr<std::thread>(new std::thread(&ExecutionController::signal_processing_worker, this)));
        }
        
        // 心跳线程
        worker_threads_.push_back(
            std::unique_ptr<std::thread>(new std::thread(&ExecutionController::heartbeat_worker, this)));
        
        // 统计线程
        worker_threads_.push_back(
            std::unique_ptr<std::thread>(new std::thread(&ExecutionController::statistics_worker, this)));
        
        // 更新执行状态
        // shared_memory_interface_->set_execution_status(true);
        
        return true;
    } catch (const std::exception& e) {
        set_error("Exception in start: " + std::string(e.what()));
        return false;
    }
}

void ExecutionController::stop()
{
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 更新执行状态
    if (shared_memory_interface_) {
        // shared_memory_interface_->set_execution_status(false);
    }
    
    // 等待工作线程结束
    for (auto& thread : worker_threads_) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
    worker_threads_.clear();
    
    // 停止核心组件
    if (position_manager_) {
        position_manager_->stop();
    }
    
    if (signal_transmission_manager_) {
        signal_transmission_manager_->stop();
    }
    
    // 停止Gateway适配器
    if (gateway_adapter_) {
        gateway_adapter_->stop();
    }
    
    if (twap_algorithm_) {
        twap_algorithm_->stop();
    }
    
    if (order_manager_) {
        order_manager_->stop();
    }
    
    if (shared_memory_interface_) {
        shared_memory_interface_->disconnect();
    }
}

void ExecutionController::cleanup()
{
    if (!initialized_.load()) {
        return;
    }
    
    stop();
    
    // 清理核心组件
    if (position_manager_) {
        position_manager_->cleanup();
        position_manager_.reset();
    }
    
    if (trading_rule_checker_) {
        trading_rule_checker_->cleanup();
        trading_rule_checker_.reset();
    }
    
    if (shared_memory_interface_) {
        // shared_memory_interface_->cleanup();
        shared_memory_interface_.reset();
    }
    
    if (twap_algorithm_) {
        twap_algorithm_->cleanup();
        twap_algorithm_.reset();
    }
    
    if (order_manager_) {
        order_manager_->cleanup();
        order_manager_.reset();
    }
    
    initialized_.store(false);
}

OrderManager* ExecutionController::get_order_manager() const
{
    return order_manager_.get();
}

TWAPAlgorithm* ExecutionController::get_twap_algorithm() const
{
    return twap_algorithm_.get();
}

SharedMemoryInterface* ExecutionController::get_shared_memory_interface() const
{
    return shared_memory_interface_.get();
}

TradingRuleChecker* ExecutionController::get_trading_rule_checker() const
{
    return trading_rule_checker_.get();
}

PositionManager* ExecutionController::get_position_manager() const
{
    return position_manager_.get();
}

SignalTransmissionManager* ExecutionController::get_signal_transmission_manager() const
{
    return signal_transmission_manager_.get();
}

GatewayAdapter* ExecutionController::get_gateway_adapter() const
{
    return gateway_adapter_;
}

void ExecutionController::process_trading_signal(const shared_memory::TradingSignal& signal)
{
    if (!running_.load()) {
        return;
    }
    
    try {
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        statistics_.signals_processed++;
        statistics_.last_signal_time = std::chrono::high_resolution_clock::now();
        
        // 创建订单
        Order order;
        order.strategy_id = signal.strategy_id;
        order.instrument_id = signal.instrument_id;
        order.side = convert_order_side(signal.side);
        order.type = convert_order_type(signal.order_type);
        order.quantity = signal.quantity;
        order.price = signal.price;
        order.time_in_force = convert_time_in_force(signal.time_in_force);
        order.timestamp = std::chrono::high_resolution_clock::now();
        
        // 交易规则检查
        if (config_.enable_risk_checking) {
            TradingRuleCheckResult rule_result = trading_rule_checker_->check_order(order);
            if (rule_result != TradingRuleCheckResult::PASS) {
                statistics_.risk_violations++;
                
                // 发送拒绝回报
                shared_memory::OrderFeedback feedback;
                feedback.set_order_id("");
                feedback.status = shared_memory::OrderStatus::REJECTED;
                feedback.set_error_message(trading_rule_checker_->get_trading_rule_result_description(rule_result));
                feedback.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                
                shared_memory_interface_->send_order_feedback(feedback);
                return;
            }
        }
        
        // 判断是否使用TWAP算法执行
        bool use_twap = should_use_twap_execution(signal);
        
        if (use_twap) {
            // 使用TWAP算法执行大额订单
            execute_with_twap(signal);
        } else {
            // 直接执行小额订单
            execute_direct_order(order);
        }
        
    } catch (const std::exception& e) {
        set_error("Exception in process_trading_signal: " + std::string(e.what()));
    }
}

void ExecutionController::process_trading_signals(const std::vector<shared_memory::TradingSignal>& signals)
{
    if (signals.empty()) {
        return;
    }
    
    // 记录信号处理开始时间
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 将信号放入无锁队列
    for (const auto& signal : signals) {
        signal_queue_->enqueue(signal);
    }
    
    // 记录队列大小
    if (performance_monitor_) {
        performance_monitor_->record_queue_size("signal_queue", signals.size());
    }
    
    // 如果信号数量较少或线程池未初始化，使用串行处理
    if (signals.size() <= 2 || !signal_thread_pool_) {
        shared_memory::TradingSignal signal;
        size_t processed_count = 0;
        while (signal_queue_->dequeue(signal)) {
            auto signal_start = std::chrono::high_resolution_clock::now();
            
            try {
                process_trading_signal(signal);
                processed_count++;
                
                // 记录成功处理
                if (performance_monitor_) {
                    performance_monitor_->record_success("signal_processing");
                }
            } catch (const std::exception& e) {
                // 记录处理错误
                if (performance_monitor_) {
                    performance_monitor_->record_error("signal_processing");
                }
            }
            
            // 记录单个信号处理延迟
            if (performance_monitor_) {
                auto signal_end = std::chrono::high_resolution_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::microseconds>(signal_end - signal_start).count();
                performance_monitor_->record_latency("signal_processing", static_cast<double>(latency));
            }
        }
        
        // 记录串行处理吞吐量
        if (performance_monitor_ && processed_count > 0) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            performance_monitor_->record_throughput("signal_processing_serial", processed_count, duration_ms);
        }
        return;
    }
    
    // 使用线程池并发处理信号队列中的信号
    std::vector<std::future<void>> futures;
    futures.reserve(signals.size());
    
    for (size_t i = 0; i < signals.size(); ++i) {
        auto future = signal_thread_pool_->enqueue([this]() {
            shared_memory::TradingSignal signal;
            if (signal_queue_->dequeue(signal)) {
                auto signal_start = std::chrono::high_resolution_clock::now();
                
                try {
                    process_trading_signal(signal);
                    
                    // 记录成功处理
                    if (performance_monitor_) {
                        performance_monitor_->record_success("signal_processing_concurrent");
                    }
                } catch (const std::exception& e) {
                    set_error("Concurrent signal processing error: " + std::string(e.what()));
                    
                    // 记录处理错误
                    if (performance_monitor_) {
                        performance_monitor_->record_error("signal_processing_concurrent");
                    }
                }
                
                // 记录单个信号处理延迟
                if (performance_monitor_) {
                    auto signal_end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(signal_end - signal_start).count();
                    performance_monitor_->record_latency("signal_processing_concurrent", static_cast<double>(latency));
                }
            }
        });
        futures.push_back(std::move(future));
    }
    
    // 等待所有任务完成
    size_t completed_count = 0;
    for (auto& future : futures) {
        try {
            future.get();
            completed_count++;
        } catch (const std::exception& e) {
            set_error("Future get error: " + std::string(e.what()));
        }
    }
    
    // 记录并发处理吞吐量
    if (performance_monitor_ && completed_count > 0) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        performance_monitor_->record_throughput("signal_processing_concurrent", completed_count, duration_ms);
    }
}

std::string ExecutionController::create_order(const Order& order)
{
    if (!running_.load()) {
        return "";
    }
    
    // 如果启用了Binance交易接口，使用gateway适配器创建订单
    if (is_exchange_enabled("binance") && gateway_adapter_) {
        return gateway_adapter_->submit_order(order);
    }
    
    // 否则使用传统的订单管理器
    if (!order_manager_) {
        return "";
    }
    
    return order_manager_->create_order(order);
}

bool ExecutionController::submit_order(const std::string& order_id)
{
    if (!running_.load() || !order_manager_) {
        return false;
    }
    
    return order_manager_->submit_order(order_id);
}

bool ExecutionController::cancel_order(const std::string& order_id)
{
    if (!running_.load()) {
        return false;
    }
    
    // 如果启用了Binance交易接口，使用gateway适配器取消订单
    if (is_exchange_enabled("binance") && gateway_adapter_) {
        return gateway_adapter_->cancel_order(order_id);
    }
    
    // 否则使用传统的订单管理器
    if (!order_manager_) {
        return false;
    }
    
    return order_manager_->cancel_order(order_id);
}

bool ExecutionController::modify_order(const std::string& order_id, const Order& new_order)
{
    if (!running_.load()) {
        return false;
    }
    
    // 如果启用了Binance交易接口，使用gateway适配器修改订单
    // 注意：当前gateway适配器不支持直接修改订单，需要先取消再重新提交
    if (is_exchange_enabled("binance") && gateway_adapter_) {
        // TODO: 实现修改订单逻辑（取消原订单并提交新订单）
        return false; // 暂时返回false，表示不支持修改
    }
    
    // 否则使用传统的订单管理器
    if (!order_manager_) {
        return false;
    }
    
    return order_manager_->modify_order(order_id, new_order.quantity, new_order.price);
}

std::string ExecutionController::start_twap_execution(const std::string& strategy_id,
                                                     const std::string& instrument_id,
                                                     OrderSide side,
                                                     const TWAPParams& params)
{
    if (!running_.load() || !twap_algorithm_) {
        return "";
    }
    
    if (!config_.enable_algorithm_execution) {
        return "";
    }
    
    std::string execution_id = twap_algorithm_->start_execution(strategy_id, instrument_id, side, params);
    if (!execution_id.empty()) {
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        statistics_.algorithm_executions++;
    }
    
    return execution_id;
}

bool ExecutionController::pause_twap_execution(const std::string& execution_id)
{
    if (!running_.load() || !twap_algorithm_) {
        return false;
    }
    
    return twap_algorithm_->pause_execution(execution_id);
}

bool ExecutionController::resume_twap_execution(const std::string& execution_id)
{
    if (!running_.load() || !twap_algorithm_) {
        return false;
    }
    
    return twap_algorithm_->resume_execution(execution_id);
}

bool ExecutionController::cancel_twap_execution(const std::string& execution_id)
{
    if (!running_.load() || !twap_algorithm_) {
        return false;
    }
    
    return twap_algorithm_->cancel_execution(execution_id);
}

void ExecutionController::set_trading_rule_limits(const std::string& /* strategy_id */, const TradingRuleLimits& limits)
{
    if (trading_rule_checker_) {
        trading_rule_checker_->set_limits(limits);
    }
}

TradingRuleLimits ExecutionController::get_trading_rule_limits(const std::string& /* strategy_id */) const
{
    if (trading_rule_checker_) {
        return trading_rule_checker_->get_limits();
    }
    return {};
}

TradingRuleCheckResult ExecutionController::check_order_trading_rules(const Order& order)
{
    if (trading_rule_checker_) {
        return trading_rule_checker_->check_order(order);
    }
    return TradingRuleCheckResult::PASS;
}

Position ExecutionController::get_position(const std::string& strategy_id, const std::string& instrument_id) const
{
    if (position_manager_) {
        return position_manager_->get_position(strategy_id, instrument_id);
    }
    return Position{};
}

std::vector<Position> ExecutionController::get_positions_by_strategy(const std::string& strategy_id) const
{
    if (position_manager_) {
        return position_manager_->get_positions_by_strategy(strategy_id);
    }
    return {};
}

double ExecutionController::calculate_total_pnl(const std::string& strategy_id) const
{
    if (position_manager_) {
        return position_manager_->calculate_total_pnl(strategy_id);
    }
    return 0.0;
}

void ExecutionController::update_market_data(const MarketData& market_data)
{
    // 直接分发给其他组件
    if (position_manager_) {
        position_manager_->update_market_data(market_data);
    }
    
    if (twap_algorithm_) {
        // TWAP算法使用execution::MarketData类型
        twap_algorithm_->update_market_data(market_data);
    }
}

void ExecutionController::update_account_info(const AccountInfo& account_info)
{
    (void)account_info; // Suppress unused parameter warning
    // 可以在这里处理账户信息更新
    // 例如更新风险限制、资金状况等
}

void ExecutionController::set_order_event_callback(OrderEventCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    order_event_callback_ = callback;
}

void ExecutionController::set_trade_event_callback(TradeEventCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    trade_event_callback_ = callback;
}

void ExecutionController::set_trading_rule_event_callback(TradingRuleEventCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    trading_rule_event_callback_ = callback;
}

void ExecutionController::set_position_event_callback(PositionEventCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    position_event_callback_ = callback;
}

void ExecutionController::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

ExecutionController::Config ExecutionController::get_config() const
{
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

ExecutionStatistics ExecutionController::get_statistics() const
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    return statistics_;
}

bool ExecutionController::is_running() const
{
    return running_.load();
}

bool ExecutionController::is_connected() const
{
    if (shared_memory_interface_) {
        return shared_memory_interface_->is_connected();
    }
    return false;
}

std::string ExecutionController::get_last_error() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void ExecutionController::signal_processing_worker()
{
    while (running_.load()) {
        try {
            std::vector<shared_memory::TradingSignal> signals;
            if (signal_transmission_manager_->receive_signals(signals, 100)) {
                process_trading_signals(signals);
            }
        } catch (const std::exception& e) {
            set_error("Signal processing error: " + std::string(e.what()));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.signal_processing_interval_ms));
    }
}

void ExecutionController::heartbeat_worker()
{
    while (running_.load()) {
        try {
            if (shared_memory_interface_) {
                shared_memory_interface_->update_heartbeat();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.heartbeat_interval_ms));
        } catch (const std::exception& e) {
            set_error("Exception in heartbeat_worker: " + std::string(e.what()));
        }
    }
}

void ExecutionController::statistics_worker()
{
    while (running_.load()) {
        try {
            update_statistics();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.statistics_update_interval_ms));
        } catch (const std::exception& e) {
            set_error("Exception in statistics_worker: " + std::string(e.what()));
        }
    }
}

void ExecutionController::setup_event_callbacks()
{
    // 设置订单管理器事件回调
    if (order_manager_) {
        order_manager_->set_order_event_callback(
            [this](const Order& order) { handle_order_event(order); });
        order_manager_->set_trade_event_callback(
            [this](const Trade& trade) { handle_trade_event(trade); });
    }
    
    // 设置持仓管理器事件回调
    if (position_manager_) {
        position_manager_->set_position_event_callback(
            [this](const PositionEvent& event) { handle_position_event(event); });
    }
}

void ExecutionController::handle_order_event(const Order& order)
{
    // 发送订单回报
    if (config_.enable_order_feedback) {
        send_order_feedback(order);
    }
    
    // 调用用户回调
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (order_event_callback_) {
        order_event_callback_(order);
    }
}

void ExecutionController::handle_trade_event(const Trade& trade)
{
    // 更新持仓
    if (config_.enable_position_tracking && position_manager_ && order_manager_) {
        // 通过order_id获取对应的订单来获取strategy_id
        auto order_ptr = order_manager_->get_order(trade.order_id);
        if (order_ptr) {
            position_manager_->process_trade(trade, order_ptr->strategy_id);
        }
    }
    
    // 更新统计
    {
        std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
        statistics_.trades_processed++;
        statistics_.last_trade_time = std::chrono::high_resolution_clock::now();
    }
    
    // 调用用户回调
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (trade_event_callback_) {
        trade_event_callback_(trade);
    }
}

void ExecutionController::handle_trading_rule_event(const TradingRuleEvent& event)
{
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (trading_rule_event_callback_) {
        trading_rule_event_callback_(event);
    }
}

void ExecutionController::handle_position_event(const PositionEvent& event)
{
    // 调用用户回调
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (position_event_callback_) {
        position_event_callback_(event);
    }
}

void ExecutionController::send_order_feedback(const Order& order)
{
    // 根据信号传递模式选择反馈方式
    if (shared_memory_interface_) {
        // 共享内存模式
        shared_memory::OrderFeedback feedback;
        feedback.set_order_id(order.order_id);
        
        // 转换OrderStatus枚举
        switch (order.status) {
            case OrderStatus::PENDING:
                feedback.status = shared_memory::OrderStatus::PENDING;
                break;
            case OrderStatus::SUBMITTED:
                feedback.status = shared_memory::OrderStatus::SUBMITTED;
                break;
            case OrderStatus::PARTIALLY_FILLED:
                feedback.status = shared_memory::OrderStatus::PARTIAL_FILLED;
                break;
            case OrderStatus::FILLED:
                feedback.status = shared_memory::OrderStatus::FILLED;
                break;
            case OrderStatus::CANCELLED:
                feedback.status = shared_memory::OrderStatus::CANCELLED;
                break;
            case OrderStatus::REJECTED:
                feedback.status = shared_memory::OrderStatus::REJECTED;
                break;
            case OrderStatus::ERROR:
                feedback.status = shared_memory::OrderStatus::REJECTED; // 映射ERROR到REJECTED
                break;
            default:
                feedback.status = shared_memory::OrderStatus::PENDING;
                break;
        }
        
        feedback.filled_volume = order.filled_quantity;
        feedback.filled_price = order.average_price;
        feedback.set_error_message("Order " + order.order_id + " status update");
        feedback.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(order.update_time.time_since_epoch()).count();
        
        shared_memory_interface_->send_order_feedback(feedback);
    } else if (json_feedback_writer_) {
        // JSON文件模式
        if (!json_feedback_writer_->write_order_feedback(order)) {
            std::cerr << "Failed to write order feedback to JSON file for order: " << order.order_id << std::endl;
        }
    }
    // 如果两者都不存在，则不进行任何操作（保持原有行为）
}

void ExecutionController::update_statistics()
{
    // 需添加更多的统计信息更新逻辑
    // 从各个组件收集统计信息并汇总
}

bool ExecutionController::should_use_twap_execution(const shared_memory::TradingSignal& signal)
{
    // TWAP算法使用条件判断
    
    // 1. 大额订单判断（数量超过阈值）
    double quantity_threshold = config_.twap_quantity_threshold; // 默认10000
    if (signal.quantity >= quantity_threshold) {
        return true;
    }
    
    // 2. 高价值订单判断（金额超过阈值）
    double order_value = signal.quantity * signal.price;
    double value_threshold = config_.twap_value_threshold; // 默认1000000
    if (order_value >= value_threshold) {
        return true;
    }
    
    // 3. 市场冲击风险判断（订单量占日均成交量比例）
    // 简化处理，实盘应该获取历史成交量数据
    double estimated_daily_volume = signal.quantity * 100; // 简化估算
    double market_impact_ratio = signal.quantity / estimated_daily_volume;
    if (market_impact_ratio > config_.twap_market_impact_threshold) { // 默认0.05 (5%)
        return true;
    }
    
    // 4. 策略配置强制使用TWAP
    std::string strategy_id = signal.get_strategy_id();
    if (strategy_id.find("TWAP") != std::string::npos || 
        strategy_id.find("twap") != std::string::npos) {
        return true;
    }
    
    return false;
}

void ExecutionController::execute_with_twap(const shared_memory::TradingSignal& signal)
{
    try {
        // 创建临时订单用于TWAP拆单检查
        Order temp_order;
        temp_order.instrument_id = signal.instrument_id;
        temp_order.side = convert_order_side(signal.side);
        temp_order.quantity = signal.quantity;
        temp_order.price = signal.price;
        temp_order.type = OrderType::LIMIT;
        temp_order.time_in_force = TimeInForce::GTC;
        temp_order.strategy_id = signal.get_strategy_id();
        
        // 进行TWAP拆单前的交易规则检查
        auto check_result = trading_rule_checker_->check_twap_slice(
            temp_order.instrument_id, temp_order.quantity, temp_order.price, false);
        if (check_result != TradingRuleCheckResult::PASS) {
            // 发送TWAP检查失败回报
            shared_memory::OrderFeedback feedback;
            feedback.set_order_id("");
            feedback.status = shared_memory::OrderStatus::REJECTED;
            feedback.set_error_message("TWAP slice check failed: " + 
                trading_rule_checker_->get_trading_rule_result_description(check_result));
            feedback.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            shared_memory_interface_->send_order_feedback(feedback);
            return;
        }
        
        // 配置TWAP参数
        TWAPParams twap_params;
        twap_params.total_quantity = signal.quantity;
        twap_params.duration_minutes = config_.default_twap_duration_minutes;
        twap_params.slice_count = std::min(static_cast<uint32_t>(
            signal.quantity / config_.twap_min_slice_size), config_.max_twap_slices);
        twap_params.participation_rate = config_.default_participation_rate;
        twap_params.price_tolerance = config_.max_price_deviation_bps / 10000.0;
        twap_params.allow_partial_fill = true;
        
        // 对TWAP参数进行精度修正
        if (trading_rule_checker_->get_binance_client()) {
            // 修正总数量精度
            twap_params.total_quantity = trading_rule_checker_->fix_quantity_precision(
                signal.instrument_id, twap_params.total_quantity);
            
            // 修正价格精度
            temp_order.price = trading_rule_checker_->fix_price_precision(
                signal.instrument_id, signal.price);
        }
        
        // 启动TWAP执行
        std::string execution_id = twap_algorithm_->start_execution(
            signal.strategy_id,
            signal.instrument_id,
            convert_order_side(signal.side),
            twap_params
        );
        
        if (!execution_id.empty()) {
            statistics_.twap_executions_started++;
            
            // 发送TWAP启动确认回报
            shared_memory::OrderFeedback feedback;
            feedback.set_order_id(execution_id);
            feedback.status = shared_memory::OrderStatus::SUBMITTED;
            feedback.set_error_message("TWAP execution started");
            feedback.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            shared_memory_interface_->send_order_feedback(feedback);
        } else {
            statistics_.twap_execution_failures++;
            
            // 发送TWAP启动失败回报
            shared_memory::OrderFeedback feedback;
            feedback.set_order_id("");
            feedback.status = shared_memory::OrderStatus::REJECTED;
            feedback.set_error_message("Failed to start TWAP execution");
            feedback.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            shared_memory_interface_->send_order_feedback(feedback);
        }
        
    } catch (const std::exception& e) {
        set_error("Exception in execute_with_twap: " + std::string(e.what()));
    }
}

void ExecutionController::execute_direct_order(const Order& order)
{
    try {
        // 使用GatewayAdapter执行订单
        if (is_exchange_enabled("binance") && gateway_adapter_) {
            std::string order_id = gateway_adapter_->submit_order(order);
            if (!order_id.empty()) {
                std::lock_guard<std::mutex> stats_lock(statistics_mutex_);
                statistics_.orders_created++;
                statistics_.orders_executed++;
                statistics_.last_order_time = std::chrono::high_resolution_clock::now();
            }
            return;
        }
        
        // 否则使用传统的订单管理器
        if (!order_manager_) {
            return;
        }
        
        // 直接创建并提交订单
        std::string order_id = order_manager_->create_order(order);
        if (!order_id.empty()) {
            statistics_.orders_created++;
            
            if (order_manager_->submit_order(order_id)) {
                statistics_.orders_executed++;
                statistics_.last_order_time = std::chrono::high_resolution_clock::now();
            }
        }
    } catch (const std::exception& e) {
        set_error("Exception in execute_direct_order: " + std::string(e.what()));
    }
}

void ExecutionController::set_error(const std::string& error)
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
    
    // 记录错误日志
    std::cerr << "ExecutionController Error: " << error << std::endl;
}



} // namespace execution
} // namespace tes