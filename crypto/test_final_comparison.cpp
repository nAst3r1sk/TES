#include "crypto_modified.hpp"
#include <iostream>
#include <string>

// 从原始版本复制的BlowFish加密逻辑
namespace original_crypto {
    constexpr char kBasePaddingChar = '=';
    constexpr int kBase64InputChunkSize = 3;
    constexpr const char* kBase64Prefix = "Base64:";

    // 简化的Base64编码函数
    inline std::string base64_encode(const std::vector<unsigned char>& data)
    {
        static const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        std::string ret;
        int i = 0;
        int j = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];

        for (size_t idx = 0; idx < data.size(); idx++) {
            char_array_3[i++] = data[idx];
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for(i = 0; i < 4; i++)
                    ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i) {
            for(j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (j = 0; j < i + 1; j++)
                ret += base64_chars[char_array_4[j]];

            while(i++ < 3)
                ret += '=';
        }

        return ret;
    }

    inline std::string CharVectorToBase64(const std::vector<unsigned char>& vchar)
    {
        return std::string(kBase64Prefix) + base64_encode(vchar);
    }

    class Cryptor
    {
    private:
        enum class Mode { kEncrypt, kDecrypt };
        enum class Cipher { kBlowFish };
        enum class Hash { kNoHash };

    public:
        static void EncryptBlowFish(const std::string& keystr, std::vector<unsigned char>& vbuf)
        {
            Init();
            CallCipher(Mode::kEncrypt, Cipher::kBlowFish, PrepareKey(Hash::kNoHash, Cipher::kBlowFish, keystr), vbuf);
        }

    private:
        static void Init()
        {
            gcry_check_version(GCRYPT_VERSION);
            gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
            gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
            gcry_control(GCRYCTL_ENABLE_QUICK_RANDOM, 0);
        }

        static int GetAlgoCode(Cipher cipher)
        {
            return GCRY_CIPHER_BLOWFISH;
        }

        static std::vector<unsigned char> PrepareKey(Hash hashmode, Cipher cipher, const std::string& keystr)
        {
            const int algo = GetAlgoCode(cipher);
            const size_t slen = keystr.size();
            const size_t klen = gcry_cipher_get_algo_keylen(algo);
            std::vector<unsigned char> key(klen, 0);
            memcpy(key.data(), keystr.data(), std::min(slen, klen));
            return key;
        }

        static void CallCipher(Mode encrypt_mode, Cipher cipher, const std::vector<unsigned char>& key, std::vector<unsigned char>& vbuf)
        {
            const int algo = GetAlgoCode(cipher);
            const int mode = GCRY_CIPHER_MODE_ECB;
            const size_t klen = gcry_cipher_get_algo_keylen(algo);
            const size_t blklen = gcry_cipher_get_algo_blklen(algo);
            size_t vlen = vbuf.size();
            
            // 原始版本的填充方式：用空格填充
            if (const size_t rem=vlen%blklen; rem)
            {
                vlen += blklen-rem;
                vbuf.resize(vlen, ' ');
            }
            
            gcry_cipher_hd_t hd;
            gcry_cipher_open(&hd, algo, mode, 0);
            gcry_cipher_setkey(hd, key.data(), klen);
            
            if (encrypt_mode == Mode::kEncrypt) {
                gcry_cipher_encrypt(hd, vbuf.data(), vlen, nullptr, 0);
            }
            
            gcry_cipher_close(hd);
        }
    };

    inline std::string Encrypt(const std::string& keystr, const std::string& plaintext)
    {
        std::vector<unsigned char> vbuf(plaintext.size());
        memcpy(vbuf.data(), plaintext.data(), plaintext.size());
        Cryptor::EncryptBlowFish(keystr, vbuf);
        return CharVectorToBase64(vbuf);
    }
}

int main() {
    std::string test_key = "BINANCE";
    std::string test_data = "OccPINwmRzJQIMhj1rCwtDuuYxCs1zDlcJnU2IGEjfIbhm7tEeifTYDXYqDbg2Of";
    
    std::cout << "=== 最终加密结果对比测试 ===" << std::endl;
    std::cout << "测试密钥: " << test_key << std::endl;
    std::cout << "测试数据: " << test_data << std::endl;
    std::cout << std::endl;
    
    try {
        // 测试修改后的非boost版本
        std::cout << "=== 修改后的非boost版本 (BlowFish, ECB, 空格填充, 直接密钥) ===" << std::endl;
        std::string encrypted_modified = crypto::Cryptor::Encrypt(test_key, test_data);
        std::cout << "加密结果: " << encrypted_modified << std::endl;
        std::cout << std::endl;
        
        // 测试原始版本
        std::cout << "=== 原始版本 (BlowFish, ECB, 空格填充, 直接密钥) ===" << std::endl;
        std::string encrypted_original = original_crypto::Encrypt(test_key, test_data);
        std::cout << "加密结果: " << encrypted_original << std::endl;
        std::cout << std::endl;
        
        // 对比结果
        std::cout << "=== 结果对比 ===" << std::endl;
        std::cout << "修改后版本: " << encrypted_modified << std::endl;
        std::cout << "原始版本:   " << encrypted_original << std::endl;
        std::cout << "结果一致: " << (encrypted_modified == encrypted_original ? "✓ 是" : "✗ 否") << std::endl;
        std::cout << std::endl;
        
        // 测试解密
        if (encrypted_modified == encrypted_original) {
            std::cout << "=== 解密验证 ===" << std::endl;
            std::string decrypted = crypto::Cryptor::Decrypt(test_key, encrypted_modified);
            std::cout << "解密结果: " << decrypted << std::endl;
            std::cout << "解密正确: " << (decrypted == test_data ? "✓ 是" : "✗ 否") << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}