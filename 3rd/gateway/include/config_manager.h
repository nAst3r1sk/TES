#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include "yyjson.h"
#include "../../../crypto/crypto_modified.hpp"

namespace trading {

/**
 * @brief 交易所配置信息
 */
struct ExchangeConfig {
    // 兼容旧格式的API密钥字段
    std::string apiKey;
    std::string apiSecret;
    
    // 新格式的API密钥字段
    std::string testnetApiKey;
    std::string testnetApiSecret;
    std::string liveApiKey;
    std::string liveApiSecret;
    
    // Ed25519和HMAC密钥字段
    std::string ed25519ApiKey;
    std::string ed25519ApiSecret;
    std::string hmacApiKey;
    std::string hmacApiSecret;
    
    std::string passphrase;         // OKX需要
    bool testnet = false;
    std::string signatureType = "hmac";  // 签名算法类型：hmac 或 ed25519
    int syncIntervalMs = 100;
    int timeoutMs = 5000;
    std::string baseUrl;
    std::map<std::string, std::map<std::string, std::string>> baseUrls; // spot/futures -> live/testnet -> url
    
    ExchangeConfig() = default;
    
    // 获取当前环境对应的API密钥
    std::string getCurrentApiKey() const {
        // 优先使用新格式的密钥
        if (signatureType == "ed25519" && !ed25519ApiKey.empty()) {
            return ed25519ApiKey;
        } else if (signatureType == "hmac" && !hmacApiKey.empty()) {
            return hmacApiKey;
        }
        
        // 回退到旧格式
        if (testnet) {
            return !testnetApiKey.empty() ? testnetApiKey : apiKey;
        } else {
            return !liveApiKey.empty() ? liveApiKey : apiKey;
        }
    }
    
    // 获取当前环境对应的API密钥
    std::string getCurrentApiSecret() const {
        // 优先使用新格式的密钥
        if (signatureType == "ed25519" && !ed25519ApiSecret.empty()) {
            return ed25519ApiSecret;
        } else if (signatureType == "hmac" && !hmacApiSecret.empty()) {
            return hmacApiSecret;
        }
        
        // 回退到旧格式
        if (testnet) {
            return !testnetApiSecret.empty() ? testnetApiSecret : apiSecret;
        } else {
            return !liveApiSecret.empty() ? liveApiSecret : apiSecret;
        }
    }
};

/**
 * @brief 系统配置管理器
 */
class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    // 配置加载
    bool loadConfig(const std::string& configPath);
    
    // 获取交易所配置
    ExchangeConfig getExchangeConfig(const std::string& exchangeName) const;
    bool hasExchangeConfig(const std::string& exchangeName) const;
    
    // 获取系统配置
    std::string getSystemName() const { return systemName_; }
    std::string getSystemVersion() const { return systemVersion_; }
    int getMaxThreads() const { return maxThreads_; }
    
    // 获取支持的交易所列表
    std::vector<std::string> getSupportedExchanges() const;
    std::vector<std::string> getTradingExchanges() const;
    
    // 日志配置
    std::string getLogFile() const { return logFile_; }
    bool isConsoleLoggingEnabled() const { return consoleLogging_; }
    
private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    // 解析JSON配置
    bool parseSystemConfig(yyjson_val* systemObj);
    bool parseExchangeConfig(yyjson_val* exchangesObj);
    bool parseLoggingConfig(yyjson_val* loggingObj);
    
    // 系统配置
    std::string systemName_;
    std::string systemVersion_;
    int maxThreads_ = 8;
    
    // 交易所配置
    std::map<std::string, ExchangeConfig> exchangeConfigs_;
    std::vector<std::string> tradingExchanges_;
    
    // 日志配置
    std::string logFile_;
    bool consoleLogging_ = true;
    
    // JSON文档
    yyjson_doc* configDoc_ = nullptr;
};

} // namespace trading