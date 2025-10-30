#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

// 包含非boost版本的实现
#include "crypto.hpp"

// 原始版本的实现（从encode_orgi.cpp复制）
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <gcrypt.h>

namespace crypto_original {
    constexpr char kBasePaddingChar = '=';
    constexpr int kBase64InputChunkSize = 3;
    constexpr const char* kBase64Prefix = "Base64:";

    inline std::string CharVectorToBase64(const std::vector<unsigned char>& vchar)
    {
        std::stringstream ss;
        ss << kBase64Prefix;
        using binary_to_base64 = boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<const unsigned char*, 6, 8>>;
        std::copy(binary_to_base64(vchar.data()), binary_to_base64(vchar.data()+vchar.size()), std::ostream_iterator<char>(ss));
        std::string result = ss.str();
        int padding_chars_num = (kBase64InputChunkSize-vchar.size()%kBase64InputChunkSize)%kBase64InputChunkSize;
        result.append(padding_chars_num, kBasePaddingChar);
        return result;
    }

    inline std::vector<unsigned char> Base64ToCharVector(const std::string& str)
    {
        using base64_to_binary = boost::archive::iterators::transform_width<boost::archive::iterators::binary_from_base64<std::string::const_iterator>, 8, 6>;
        int padding_chars_num = str.size() - int(str.find_last_not_of(kBasePaddingChar)) - 1;
        std::vector<unsigned char> out;
        out.reserve(str.size() - padding_chars_num);
        const char* str_start = str.data();
        if (boost::starts_with(str, kBase64Prefix))
            str_start += strlen(kBase64Prefix);
        std::copy(base64_to_binary(str_start), base64_to_binary(str.data() + str.size() - padding_chars_num), std::back_inserter(out));
        return out;
    }

    class Cryptor
    {
    private:
        enum class Mode { kEncrypt, kDecrypt };
        enum class Cipher { kBlowFish, kTwoFish };
        enum class Hash { kNoHash, kSha256 };

    public:
        static void EncryptBlowFish(const std::string& keystr, std::vector<unsigned char>& vbuf)
        {
            Init();
            CallCipher(Mode::kEncrypt, Cipher::kBlowFish, PrepareKey(Hash::kNoHash, Cipher::kBlowFish, keystr), vbuf);
        }

        static void DecryptBlowFish(const std::string& keystr, std::vector<unsigned char>& vbuf)
        {
            Init();
            CallCipher(Mode::kDecrypt, Cipher::kBlowFish, PrepareKey(Hash::kNoHash, Cipher::kBlowFish, keystr), vbuf);
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
            return std::unordered_map<Cipher, int>{
                {Cipher::kBlowFish, GCRY_CIPHER_BLOWFISH},
                {Cipher::kTwoFish, GCRY_CIPHER_TWOFISH},
            }.at(cipher);
        }

        static std::vector<unsigned char> PrepareKey(Hash hashmode, Cipher cipher, const std::string& keystr)
        {
            const int algo = GetAlgoCode(cipher);
            switch (hashmode)
            {
            case Hash::kNoHash:
                {
                    const size_t slen = keystr.size();
                    const size_t klen = gcry_cipher_get_algo_keylen(algo);
                    std::vector<unsigned char> key(klen, 0);
                    memcpy(key.data(), keystr.data(), std::min(slen, klen));
                    return key;
                }
            case Hash::kSha256:
                {
                    gcry_md_hd_t hd;
                    gcry_md_open(&hd, GCRY_MD_SHA256, 0);
                    const size_t klen = gcry_md_get_algo_dlen(GCRY_MD_SHA256);
                    std::vector<unsigned char> key(klen, 0);
                    gcry_md_write(hd, keystr.data(), keystr.size());
                    unsigned char* result = gcry_md_read(hd, GCRY_MD_SHA256);
                    memcpy(key.data(), result, klen);
                    gcry_md_close(hd);
                    return key;
                }
            default:
                __builtin_unreachable();
            }
        }

        static void CallCipher(Mode encrypt_mode, Cipher cipher, const std::vector<unsigned char>& key, std::vector<unsigned char>& vbuf)
        {
            const int algo = GetAlgoCode(cipher);
            const int mode = GCRY_CIPHER_MODE_ECB;
            const size_t klen = gcry_cipher_get_algo_keylen(algo);
            const size_t blklen = gcry_cipher_get_algo_blklen(algo);
            size_t vlen = vbuf.size();
            if (const size_t rem=vlen%blklen; rem)
            {
                vlen += blklen-rem;
                vbuf.resize(vlen, ' ');//pad buffer with spaces
            }
            gcry_cipher_hd_t hd;
            gcry_cipher_open(&hd, algo, mode, 0);
            gcry_cipher_setkey(hd, key.data(), klen);
            switch (encrypt_mode)
            {
            case Mode::kEncrypt:
                gcry_cipher_encrypt(hd, vbuf.data(), vlen, nullptr, 0);
                break;
            case Mode::kDecrypt:
                gcry_cipher_decrypt(hd, vbuf.data(), vlen, nullptr, 0);
                break;
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

    inline std::string Decrypt(const std::string& keystr, const std::string& ciphertext)
    {
        auto vbuf = Base64ToCharVector(ciphertext);
        Cryptor::DecryptBlowFish(keystr, vbuf);
        std::string tmp(vbuf.begin(), vbuf.end());
        boost::algorithm::trim_right(tmp);
        return tmp;
    }
}

int main() {
    std::string test_key = "BINANCE";
    std::string test_data = "OccPINwmRzJQIMhj1rCwtDuuYxCs1zDlcJnU2IGEjfIbhm7tEeifTYDXYqDbg2Of";
    
    std::cout << "=== 代码逻辑对比测试 ===" << std::endl;
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
        
        // 测试原始版本 (encode_orgi.cpp)
        std::cout << "=== 原始版本 (encode_orgi.cpp) ===" << std::endl;
        std::string encrypted_old = crypto_original::Encrypt(test_key, test_data);
        std::cout << "加密结果: " << encrypted_old << std::endl;
        
        std::string decrypted_old = crypto_original::Decrypt(test_key, encrypted_old);
        std::cout << "解密结果: " << decrypted_old << std::endl;
        std::cout << "解密匹配: " << (decrypted_old == test_data ? "✓" : "✗") << std::endl;
        std::cout << std::endl;
        
        // 对比结果
        std::cout << "=== 结果对比 ===" << std::endl;
        std::cout << "加密结果一致: " << (encrypted_new == encrypted_old ? "✓" : "✗") << std::endl;
        std::cout << "解密结果一致: " << (decrypted_new == decrypted_old ? "✓" : "✗") << std::endl;
        
        if (encrypted_new != encrypted_old) {
            std::cout << std::endl << "加密结果差异分析:" << std::endl;
            std::cout << "非boost版本长度: " << encrypted_new.length() << std::endl;
            std::cout << "原始版本长度: " << encrypted_old.length() << std::endl;
        }
        
        // 交叉解密测试
        std::cout << std::endl << "=== 交叉解密测试 ===" << std::endl;
        try {
            std::string cross_decrypt1 = crypto::Cryptor::Decrypt(test_key, encrypted_old);
            std::cout << "非boost版本解密原始版本加密: " << (cross_decrypt1 == test_data ? "✓" : "✗") << std::endl;
        } catch (const std::exception& e) {
            std::cout << "非boost版本解密原始版本加密: ✗ (异常: " << e.what() << ")" << std::endl;
        }
        
        try {
            std::string cross_decrypt2 = crypto_original::Decrypt(test_key, encrypted_new);
            std::cout << "原始版本解密非boost版本加密: " << (cross_decrypt2 == test_data ? "✓" : "✗") << std::endl;
        } catch (const std::exception& e) {
            std::cout << "原始版本解密非boost版本加密: ✗ (异常: " << e.what() << ")" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}