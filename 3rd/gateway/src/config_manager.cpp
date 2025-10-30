#include "config_manager.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace trading {

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configPath) {
    // 读取配置文件
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << configPath << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();
    file.close();
    
    // 解析JSON
    configDoc_ = yyjson_read(jsonStr.c_str(), jsonStr.length(), 0);
    if (!configDoc_) {
        std::cerr << "Failed to parse JSON config" << std::endl;
        return false;
    }
    
    yyjson_val* root = yyjson_doc_get_root(configDoc_);
    if (!root || !yyjson_is_obj(root)) {
        std::cerr << "Invalid JSON config: root is not an object" << std::endl;
        return false;
    }
    
    // 解析各个配置段
    yyjson_val* systemObj = yyjson_obj_get(root, "system");
    if (systemObj && !parseSystemConfig(systemObj)) {
        std::cerr << "Failed to parse system config" << std::endl;
        return false;
    }
    
    yyjson_val* exchangesObj = yyjson_obj_get(root, "exchanges");
    if (exchangesObj && !parseExchangeConfig(exchangesObj)) {
        std::cerr << "Failed to parse exchanges config" << std::endl;
        return false;
    }
    
    yyjson_val* loggingObj = yyjson_obj_get(root, "logging");
    if (loggingObj && !parseLoggingConfig(loggingObj)) {
        std::cerr << "Failed to parse logging config" << std::endl;
        return false;
    }
    
    // 解析gateway配置获取支持的交易所
    yyjson_val* gatewayObj = yyjson_obj_get(root, "gateway");
    if (gatewayObj) {
        yyjson_val* tradingExchangesArr = yyjson_obj_get(gatewayObj, "trading_exchanges");
        if (tradingExchangesArr && yyjson_is_arr(tradingExchangesArr)) {
            size_t idx, max;
            yyjson_val* val;
            yyjson_arr_foreach(tradingExchangesArr, idx, max, val) {
                if (yyjson_is_str(val)) {
                    tradingExchanges_.push_back(yyjson_get_str(val));
                }
            }
        }
    }
    
    return true;
}

bool ConfigManager::parseSystemConfig(yyjson_val* systemObj) {
    yyjson_val* nameVal = yyjson_obj_get(systemObj, "name");
    if (nameVal && yyjson_is_str(nameVal)) {
        systemName_ = yyjson_get_str(nameVal);
    }
    
    yyjson_val* versionVal = yyjson_obj_get(systemObj, "version");
    if (versionVal && yyjson_is_str(versionVal)) {
        systemVersion_ = yyjson_get_str(versionVal);
    }
    
    yyjson_val* threadsVal = yyjson_obj_get(systemObj, "max_threads");
    if (threadsVal && yyjson_is_int(threadsVal)) {
        maxThreads_ = yyjson_get_int(threadsVal);
    }
    
    return true;
}

bool ConfigManager::parseExchangeConfig(yyjson_val* exchangesObj) {
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(exchangesObj, &iter);
    yyjson_val* key, *val;
    
    while ((key = yyjson_obj_iter_next(&iter))) {
        val = yyjson_obj_iter_get_val(key);
        if (!yyjson_is_obj(val)) continue;
        
        std::string exchangeName = yyjson_get_str(key);
        ExchangeConfig config;
        
        // 解析API密钥 - 兼容新旧格式
        yyjson_val* apiKeyVal = yyjson_obj_get(val, "api_key");
        if (apiKeyVal && yyjson_is_str(apiKeyVal)) {
            config.apiKey = yyjson_get_str(apiKeyVal);
        }
        
        yyjson_val* apiSecretVal = yyjson_obj_get(val, "api_secret");
        if (apiSecretVal && yyjson_is_str(apiSecretVal)) {
            config.apiSecret = yyjson_get_str(apiSecretVal);
        }
        
        // 解析新格式的API密钥
        yyjson_val* testnetApiKeyVal = yyjson_obj_get(val, "testnet_api_key");
        if (testnetApiKeyVal && yyjson_is_str(testnetApiKeyVal)) {
            config.testnetApiKey = yyjson_get_str(testnetApiKeyVal);
        }
        
        yyjson_val* testnetApiSecretVal = yyjson_obj_get(val, "testnet_secret_key");
        if (testnetApiSecretVal && yyjson_is_str(testnetApiSecretVal)) {
            config.testnetApiSecret = yyjson_get_str(testnetApiSecretVal);
        }
        
        yyjson_val* liveApiKeyVal = yyjson_obj_get(val, "live_api_key");
        if (liveApiKeyVal && yyjson_is_str(liveApiKeyVal)) {
            config.liveApiKey = yyjson_get_str(liveApiKeyVal);
        }
        
        yyjson_val* liveApiSecretVal = yyjson_obj_get(val, "live_secret_key");
        if (liveApiSecretVal && yyjson_is_str(liveApiSecretVal)) {
            config.liveApiSecret = yyjson_get_str(liveApiSecretVal);
        }
        
        // 解析Ed25519和HMAC密钥
        // 将交易所名称转换为大写用于解密（匹配加密时使用的密钥格式）
        std::string decryptionKey = exchangeName;
        std::transform(decryptionKey.begin(), decryptionKey.end(), decryptionKey.begin(), ::toupper);
        
        yyjson_val* ed25519ApiKeyVal = yyjson_obj_get(val, "ed25519_api_key");
        if (ed25519ApiKeyVal && yyjson_is_str(ed25519ApiKeyVal)) {
            std::string encryptedKey = yyjson_get_str(ed25519ApiKeyVal);
            config.ed25519ApiKey = crypto::Cryptor::Decrypt(decryptionKey, encryptedKey);
        }
        
        yyjson_val* ed25519ApiSecretVal = yyjson_obj_get(val, "ed25519_api_secret");
        if (ed25519ApiSecretVal && yyjson_is_str(ed25519ApiSecretVal)) {
            std::string encryptedSecret = yyjson_get_str(ed25519ApiSecretVal);
            config.ed25519ApiSecret = crypto::Cryptor::Decrypt(decryptionKey, encryptedSecret);
        }
        
        yyjson_val* hmacApiKeyVal = yyjson_obj_get(val, "hmac_api_key");
        if (hmacApiKeyVal && yyjson_is_str(hmacApiKeyVal)) {
            std::string encryptedKey = yyjson_get_str(hmacApiKeyVal);
            config.hmacApiKey = crypto::Cryptor::Decrypt(decryptionKey, encryptedKey);
        }
        
        yyjson_val* hmacApiSecretVal = yyjson_obj_get(val, "hmac_api_secret");
        if (hmacApiSecretVal && yyjson_is_str(hmacApiSecretVal)) {
            std::string encryptedSecret = yyjson_get_str(hmacApiSecretVal);
            config.hmacApiSecret = crypto::Cryptor::Decrypt(decryptionKey, encryptedSecret);
        }

        // OKX特有的passphrase
        yyjson_val* passphraseVal = yyjson_obj_get(val, "passphrase");
        if (passphraseVal && yyjson_is_str(passphraseVal)) {
            config.passphrase = yyjson_get_str(passphraseVal);
        }
        
        // 测试网配置
        yyjson_val* testnetVal = yyjson_obj_get(val, "testnet");
        if (testnetVal && yyjson_is_bool(testnetVal)) {
            config.testnet = yyjson_get_bool(testnetVal);
        }
        
        // 签名算法类型配置
        yyjson_val* signatureTypeVal = yyjson_obj_get(val, "signature_type");
        if (signatureTypeVal && yyjson_is_str(signatureTypeVal)) {
            config.signatureType = yyjson_get_str(signatureTypeVal);
        }
        
        // 超时配置
        yyjson_val* timeoutVal = yyjson_obj_get(val, "timeout_ms");
        if (timeoutVal && yyjson_is_int(timeoutVal)) {
            config.timeoutMs = yyjson_get_int(timeoutVal);
        }
        
        yyjson_val* syncIntervalVal = yyjson_obj_get(val, "sync_interval_ms");
        if (syncIntervalVal && yyjson_is_int(syncIntervalVal)) {
            config.syncIntervalMs = yyjson_get_int(syncIntervalVal);
        }
        
        // 基础URL配置
        yyjson_val* baseUrlVal = yyjson_obj_get(val, "base_url");
        if (baseUrlVal && yyjson_is_str(baseUrlVal)) {
            config.baseUrl = yyjson_get_str(baseUrlVal);
        }
        
        // 解析base_urls对象
        yyjson_val* baseUrlsObj = yyjson_obj_get(val, "base_urls");
        if (baseUrlsObj && yyjson_is_obj(baseUrlsObj)) {
            yyjson_obj_iter urlIter;
            yyjson_obj_iter_init(baseUrlsObj, &urlIter);
            yyjson_val* typeKey, *typeVal;
            
            while ((typeKey = yyjson_obj_iter_next(&urlIter))) {
                typeVal = yyjson_obj_iter_get_val(typeKey);
                if (!yyjson_is_obj(typeVal)) continue;
                
                std::string type = yyjson_get_str(typeKey); // "spot" or "futures"
                std::map<std::string, std::string> urls;
                
                yyjson_val* liveVal = yyjson_obj_get(typeVal, "live");
                if (liveVal && yyjson_is_str(liveVal)) {
                    urls["live"] = yyjson_get_str(liveVal);
                }
                
                yyjson_val* testnetUrlVal = yyjson_obj_get(typeVal, "testnet");
                if (testnetUrlVal && yyjson_is_str(testnetUrlVal)) {
                    urls["testnet"] = yyjson_get_str(testnetUrlVal);
                }
                
                config.baseUrls[type] = urls;
            }
        }
        
        exchangeConfigs_[exchangeName] = config;
    }
    
    return true;
}

bool ConfigManager::parseLoggingConfig(yyjson_val* loggingObj) {
    yyjson_val* fileVal = yyjson_obj_get(loggingObj, "file");
    if (fileVal && yyjson_is_str(fileVal)) {
        logFile_ = yyjson_get_str(fileVal);
    }
    
    yyjson_val* consoleVal = yyjson_obj_get(loggingObj, "console");
    if (consoleVal && yyjson_is_bool(consoleVal)) {
        consoleLogging_ = yyjson_get_bool(consoleVal);
    }
    
    return true;
}

ExchangeConfig ConfigManager::getExchangeConfig(const std::string& exchangeName) const {
    auto it = exchangeConfigs_.find(exchangeName);
    if (it != exchangeConfigs_.end()) {
        return it->second;
    }
    return ExchangeConfig();
}

bool ConfigManager::hasExchangeConfig(const std::string& exchangeName) const {
    return exchangeConfigs_.find(exchangeName) != exchangeConfigs_.end();
}

std::vector<std::string> ConfigManager::getSupportedExchanges() const {
    std::vector<std::string> exchanges;
    for (const auto& pair : exchangeConfigs_) {
        exchanges.push_back(pair.first);
    }
    return exchanges;
}

std::vector<std::string> ConfigManager::getTradingExchanges() const {
    return tradingExchanges_;
}

} // namespace trading