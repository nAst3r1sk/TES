#include <iostream>
#include <string>
#include <ixwebsocket/IXHttpClient.h>
#include <yyjson.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <chrono>

class BinanceAPITester {
private:
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;

    std::string hmac_sha256(const std::string& key, const std::string& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        unsigned int hash_len;
        
        HMAC(EVP_sha256(), key.c_str(), key.length(),
             reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
             hash, &hash_len);
        
        std::stringstream ss;
        for (unsigned int i = 0; i < hash_len; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

    long long getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        return timestamp.count();
    }

public:
    BinanceAPITester(const std::string& api_key, const std::string& api_secret, const std::string& base_url)
        : api_key_(api_key), api_secret_(api_secret), base_url_(base_url) {}

    bool testCreateListenKey() {
        std::cout << "测试创建listenKey..." << std::endl;
        
        // 创建HTTP客户端
        ix::HttpClient httpClient;
        ix::HttpRequestArgsPtr args = httpClient.createRequest();
        
        // 设置请求参数
        long long timestamp = getCurrentTimestamp();
        std::string query_string = "timestamp=" + std::to_string(timestamp);
        std::string signature = hmac_sha256(api_secret_, query_string);
        
        std::string url = base_url_ + "/fapi/v1/listenKey?" + query_string + "&signature=" + signature;
        
        args->extraHeaders["X-MBX-APIKEY"] = api_key_;
        args->extraHeaders["Content-Type"] = "application/x-www-form-urlencoded";
        
        std::cout << "请求URL: " << url << std::endl;
        std::cout << "API Key: " << api_key_.substr(0, 10) << "..." << std::endl;
        
        // 发送POST请求
        ix::HttpResponsePtr response = httpClient.post(url, "", args);
        
        if (!response) {
            std::cout << "❌ 请求失败: 无响应" << std::endl;
            return false;
        }
        
        std::cout << "HTTP状态码: " << response->statusCode << std::endl;
        std::cout << "响应内容: " << response->body << std::endl;
        
        if (response->statusCode == 200) {
            // 解析JSON响应
            yyjson_doc* doc = yyjson_read(response->body.c_str(), response->body.length(), 0);
            if (doc) {
                yyjson_val* root = yyjson_doc_get_root(doc);
                yyjson_val* listen_key = yyjson_obj_get(root, "listenKey");
                
                if (listen_key && yyjson_is_str(listen_key)) {
                    std::string key = yyjson_get_str(listen_key);
                    std::cout << "✅ 成功创建listenKey: " << key.substr(0, 20) << "..." << std::endl;
                    yyjson_doc_free(doc);
                    return true;
                } else {
                    std::cout << "❌ 响应中未找到listenKey" << std::endl;
                }
                yyjson_doc_free(doc);
            } else {
                std::cout << "❌ JSON解析失败" << std::endl;
            }
        } else {
            std::cout << "❌ API请求失败，状态码: " << response->statusCode << std::endl;
        }
        
        return false;
    }
};

int main() {
    std::cout << "=== Binance API密钥验证测试 ===" << std::endl;
    
    // 从配置文件读取的API密钥
    std::string api_key = "OccPINwmRzJQIMhj1rCwtDuuYxCs1zDlcJnU2IGEjfIbhm7tEeifTYDXYqDbg2Of";
    std::string api_secret = "EUU6jztuQmt5mynxpL6R7e0ip00P9omtfMOYUEUeFSnw7uIXcAC7OmxbjUwRz37K";
    std::string base_url = "https://fapi.binance.com";
    
    BinanceAPITester tester(api_key, api_secret, base_url);
    
    if (tester.testCreateListenKey()) {
        std::cout << "\n🎉 API密钥验证成功！可以创建WebSocket连接。" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ API密钥验证失败！请检查密钥配置。" << std::endl;
        return 1;
    }
}