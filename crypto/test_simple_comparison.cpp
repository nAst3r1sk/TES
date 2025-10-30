#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

// 包含非boost版本的实现
#include "crypto.hpp"

int main() {
    std::string test_key = "BINANCE";
    std::string test_data = "OccPINwmRzJQIMhj1rCwtDuuYxCs1zDlcJnU2IGEjfIbhm7tEeifTYDXYqDbg2Of";
    
    std::cout << "=== 代码逻辑对比分析 ===" << std::endl;
    std::cout << "测试密钥: " << test_key << std::endl;
    std::cout << "测试数据: " << test_data << std::endl;
    std::cout << std::endl;
    
    try {
        // 测试非boost版本 (crypto.hpp)
        std::cout << "=== 非boost版本 (crypto.hpp) ===" << std::endl;
        std::string encrypted_new = crypto::Cryptor::Encrypt(test_key, test_data);
        std::cout << "加密结果: " << encrypted_new << std::endl;
        
        std::string decrypted_new = crypto::Cryptor::Decrypt(test_key, encrypted_new);
        std::cout << "解密结果: " << decrypted_new << std::endl;
        std::cout << "解密匹配: " << (decrypted_new == test_data ? "✓" : "✗") << std::endl;
        std::cout << std::endl;
        
        // 分析加密算法差异
        std::cout << "=== 代码逻辑分析 ===" << std::endl;
        std::cout << "非boost版本特点:" << std::endl;
        std::cout << "1. 使用TwoFish算法进行加密" << std::endl;
        std::cout << "2. 使用SHA256哈希处理密钥" << std::endl;
        std::cout << "3. 使用CBC模式" << std::endl;
        std::cout << "4. 使用PKCS7填充" << std::endl;
        std::cout << "5. 使用自定义Base64编码" << std::endl;
        std::cout << std::endl;
        
        std::cout << "原始版本特点 (从encode_orgi.cpp分析):" << std::endl;
        std::cout << "1. 主要使用BlowFish算法进行加密" << std::endl;
        std::cout << "2. BlowFish不使用哈希处理密钥 (Hash::kNoHash)" << std::endl;
        std::cout << "3. 使用ECB模式" << std::endl;
        std::cout << "4. 使用空格填充" << std::endl;
        std::cout << "5. 使用Boost Base64编码" << std::endl;
        std::cout << std::endl;
        
        std::cout << "=== 主要差异总结 ===" << std::endl;
        std::cout << "1. 加密算法: TwoFish vs BlowFish" << std::endl;
        std::cout << "2. 密钥处理: SHA256哈希 vs 直接使用" << std::endl;
        std::cout << "3. 加密模式: CBC vs ECB" << std::endl;
        std::cout << "4. 填充方式: PKCS7 vs 空格填充" << std::endl;
        std::cout << "5. Base64实现: 自定义 vs Boost" << std::endl;
        std::cout << std::endl;
        
        std::cout << "由于这些根本性差异，两个版本的加密结果不会相同。" << std::endl;
        std::cout << "但是，main.cpp中的调用方式是一致的:" << std::endl;
        std::cout << "- crypto::Cryptor::Encrypt(exchange, data)" << std::endl;
        std::cout << "- crypto::Cryptor::Decrypt(exchange, data)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}