#include "crypto_modified.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cout << "用法: " << argv[0] << " <-Encrypt|-Decrypt> <密钥> <数据>" << std::endl;
        return 1;
    }

    std::string operation = argv[1];
    std::string key = argv[2];
    std::string data = argv[3];

    try {
        if (operation == "-Encrypt") {
            std::string result = crypto::Cryptor::Encrypt(key, data);
            std::cout << "加密结果: " << result << std::endl;
        } else if (operation == "-Decrypt") {
            std::string result = crypto::Cryptor::Decrypt(key, data);
            std::cout << "解密结果: " << result << std::endl;
        } else {
            std::cout << "无效操作: " << operation << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}