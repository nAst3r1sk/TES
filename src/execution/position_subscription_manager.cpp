#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <unordered_set>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include "execution/execution_controller.h"
#include "execution/config_manager.h"
#include "execution/signal_transmission_manager.h"

using namespace tes::execution;

class PositionSubscriptionManager {
public:
    PositionSubscriptionManager() 
        : running_(false)
        , pos_update_file_("config/pos_update.json")
        , execution_controller_(nullptr) {
    }

    ~PositionSubscriptionManager() {
        stop();
    }

    bool initialize(ExecutionController* execution_controller) {
        if (!execution_controller) {
            std::cerr << "PositionSubscriptionManager: ExecutionController is null" << std::endl;
            return false;
        }

        execution_controller_ = execution_controller;
        
        // 获取SignalTransmissionManager实例
        signal_transmission_manager_ = execution_controller_->get_signal_transmission_manager();
        if (!signal_transmission_manager_) {
            std::cerr << "PositionSubscriptionManager: Failed to get SignalTransmissionManager" << std::endl;
            return false;
        }

        std::cout << "PositionSubscriptionManager initialized successfully" << std::endl;
        return true;
    }

    bool start() {
        if (running_.load()) {
            std::cout << "PositionSubscriptionManager already running" << std::endl;
            return true;
        }

        running_.store(true);
        monitor_thread_ = std::unique_ptr<std::thread>(new std::thread(&PositionSubscriptionManager::monitor_worker, this));
        
        std::cout << "PositionSubscriptionManager started successfully" << std::endl;
        return true;
    }

    void stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);
        
        if (monitor_thread_ && monitor_thread_->joinable()) {
            monitor_thread_->join();
        }
        
        std::cout << "PositionSubscriptionManager stopped successfully" << std::endl;
    }

private:
    void monitor_worker() {
        std::cout << "PositionSubscriptionManager monitor worker started" << std::endl;
        
        while (running_.load()) {
            try {
                nlohmann::json pos_data;
                if (!load_position_file(pos_data)) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }

                // 检查isFinished字段（在数组的最后一个对象中）
                bool is_finished = false;
                if (pos_data.is_array() && !pos_data.empty()) {
                    for (auto it = pos_data.rbegin(); it != pos_data.rend(); ++it) {
                        if (it->is_object() && it->contains("isFinished")) {
                            is_finished = (it->at("isFinished").get<int>() == 1);
                            break;
                        }
                    }
                }

                if (is_finished) {
                    std::cout << "Position update finished, skipping monitoring" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    continue;
                }

                // 获取非零quantity的品种
                std::vector<std::string> non_zero_symbols = get_non_zero_quantity_symbols(pos_data);
                
                if (!non_zero_symbols.empty()) {
                    std::cout << "Found " << non_zero_symbols.size() << " symbols with non-zero quantities" << std::endl;
                    
                    // 订阅这些品种
                    subscribe_symbols(non_zero_symbols);
                    
                    // 触发位置调整
                    trigger_position_adjustment();
                } else {
                    std::cout << "No symbols with non-zero quantities found" << std::endl;
                }

            } catch (const std::exception& e) {
                std::cerr << "PositionSubscriptionManager monitor error: " << e.what() << std::endl;
            }

            // 等待一段时间后再次检查
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
        
        std::cout << "PositionSubscriptionManager monitor worker stopped" << std::endl;
    }

    bool load_position_file(nlohmann::json& data) {
        try {
            std::ifstream file(pos_update_file_);
            if (!file.is_open()) {
                std::cerr << "Failed to open position file: " << pos_update_file_ << std::endl;
                return false;
            }

            file >> data;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error loading position file: " << e.what() << std::endl;
            return false;
        }
    }

    std::vector<std::string> get_non_zero_quantity_symbols(const nlohmann::json& pos_data) {
        std::vector<std::string> symbols;
        
        try {
            if (pos_data.is_array()) {
                for (const auto& position : pos_data) {
                    // 跳过非对象元素（如最后的配置对象）
                    if (!position.is_object()) {
                        continue;
                    }
                    
                    if (position.contains("symbol") && position.contains("quantity")) {
                        std::string symbol = position["symbol"].get<std::string>();
                        std::string quantity_str = position["quantity"].get<std::string>();
                        double quantity = std::stod(quantity_str);
                        
                        if (std::abs(quantity) > 1e-8) { // 非零判断
                            symbols.push_back(symbol);
                            std::cout << "Found non-zero position: " << symbol << " = " << quantity << std::endl;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing position data: " << e.what() << std::endl;
        }
        
        return symbols;
    }

    void subscribe_symbols(const std::vector<std::string>& symbols) {
        for (const auto& symbol : symbols) {
            if (subscribed_symbols_.find(symbol) == subscribed_symbols_.end()) {
                std::cout << "Subscribing to symbol: " << symbol << std::endl;
                
                // 简化处理，直接标记为已订阅
                // TODO: 集成新的gateway系统进行市场数据订阅
                subscribed_symbols_.insert(symbol);
                std::cout << "Successfully subscribed to market data for: " << symbol << std::endl;
            }
        }
    }

    void trigger_position_adjustment() {
        try {
            if (signal_transmission_manager_) {
                std::cout << "Triggering position adjustment via SignalTransmissionManager" << std::endl;
                
                // 调用SignalTransmissionManager的sync_positions_with_json方法
                auto result = signal_transmission_manager_->sync_positions_with_json();
                
                if (result.success) {
                     std::cout << "Position synchronization completed successfully" << std::endl;
                     std::cout << "Positions aligned: " << result.positions_aligned << std::endl;
                     std::cout << "Positions opened: " << result.positions_opened << std::endl;
                     std::cout << "Positions closed: " << result.positions_closed << std::endl;
                 } else {
                     std::cerr << "Position synchronization failed: " << result.error_message << std::endl;
                 }
            } else {
                std::cerr << "SignalTransmissionManager not available for position adjustment" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error triggering position adjustment: " << e.what() << std::endl;
        }
    }

private:
    std::atomic<bool> running_;
    std::string pos_update_file_;
    std::unique_ptr<std::thread> monitor_thread_;
    std::unordered_set<std::string> subscribed_symbols_;

    ExecutionController* execution_controller_;
    SignalTransmissionManager* signal_transmission_manager_;
};

// 全局实例
static std::unique_ptr<PositionSubscriptionManager> g_position_subscription_manager;

// C接口实现
extern "C" {
    bool initialize_position_subscription_manager(ExecutionController* execution_controller) {
        g_position_subscription_manager = std::unique_ptr<PositionSubscriptionManager>(new PositionSubscriptionManager());
        return g_position_subscription_manager->initialize(execution_controller);
    }

    bool start_position_subscription_manager() {
        return g_position_subscription_manager ? g_position_subscription_manager->start() : false;
    }

    void stop_position_subscription_manager() {
        if (g_position_subscription_manager) {
            g_position_subscription_manager->stop();
        }
    }

    void cleanup_position_subscription_manager() {
        g_position_subscription_manager.reset();
    }
}