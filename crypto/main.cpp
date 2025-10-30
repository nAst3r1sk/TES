#include <iostream>
#include <string>
#include <vector>
#include "crypto.hpp"

void printUsage(const char* program_name) {
    std::cout << "用法: " << program_name << " <操作> <交易所> <数据>" << std::endl;
    std::cout << "操作:" << std::endl;
    std::cout << "  -Encrypt    加密数据" << std::endl;
    std::cout << "  -Decrypt    解密数据" << std::endl;
    std::cout << "交易所:" << std::endl;
    std::cout << "  BINANCE     币安交易所" << std::endl;
    std::cout << "  GATEIO      Gate.io交易所" << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program_name << " -Encrypt BINANCE \"binance_test_api_key_12345\"" << std::endl;
    std::cout << "  " << program_name << " -Decrypt BINANCE \"Base64:encrypted_data\"" << std::endl;
}

int main(int argc, char* argv[])
{
    try {
        // 检查参数数量
        if (argc != 4) {
            std::cerr << "错误: 参数数量不正确" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        std::string operation = argv[1];
        std::string exchange = argv[2];
        std::string data = argv[3];
        
        // 验证操作类型
        if (operation != "-Encrypt" && operation != "-Decrypt") {
            std::cerr << "错误: 无效的操作类型 '" << operation << "'" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        // 验证交易所名称
        if (exchange != "BINANCE" && exchange != "GATEIO") {
            std::cerr << "错误: 不支持的交易所 '" << exchange << "'" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        // 执行加密或解密操作
        std::string result;
        if (operation == "-Encrypt") {
            result = crypto::Cryptor::Encrypt(exchange, data);
            std::cout << "加密结果: " << result << std::endl;
        } else if (operation == "-Decrypt") {
            result = crypto::Cryptor::Decrypt(exchange, data);
            std::cout << "解密结果: " << result << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}