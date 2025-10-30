#include <iostream>
#include <string>
#include <vector>

// 直接包含原始实现，但去掉最后的示例代码部分
#include <bits/stdc++.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <gcrypt.h>
#include <fmt/core.h>
#include <fstream>

namespace crypto{ 
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
    enum class Mode
    {
        kEncrypt,
        kDecrypt,
    };
    enum class Cipher
    {
        kBlowFish,
        kTwoFish,
    };
    enum class Hash
    {
        kNoHash,
        kSha256
    };

public:
    static void EncryptTwoFish(const std::string& keystr, std::vector<unsigned char>& vbuf)
    {
        Init();
        CallCipher(Mode::kEncrypt, Cipher::kTwoFish, PrepareKey(Hash::kSha256, Cipher::kTwoFish, keystr), vbuf);
    }

    static void EncryptBlowFish(const std::string& keystr, std::vector<unsigned char>& vbuf)
    {
        Init();
        CallCipher(Mode::kEncrypt, Cipher::kBlowFish, PrepareKey(Hash::kNoHash, Cipher::kBlowFish, keystr), vbuf);
    }

    static void DecryptTwoFish(const std::string& keystr, std::vector<unsigned char>& vbuf)
    {
        Init();
        CallCipher(Mode::kDecrypt, Cipher::kTwoFish, PrepareKey(Hash::kSha256, Cipher::kTwoFish, keystr), vbuf);
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
                if (slen > klen)
                    std::cerr << fmt::format("User-specified key is too long for the cipher! It will be truncated to {} unsigned chars!", klen) <<std::endl;
                std::vector<unsigned char> key(klen, 0);
                memcpy(key.data(), keystr.data(), std::min(slen, klen));
                return key;
            }
        case Hash::kSha256:
            {
                gcry_md_hd_t hd;
                if (gcry_error_t err=gcry_md_open(&hd, GCRY_MD_SHA256, 0); err!=0)
                    throw std::runtime_error(fmt::format("gcry_md_open failed: {}", gcry_strerror(err)));
                else
                {
                    const size_t klen = gcry_md_get_algo_dlen(GCRY_MD_SHA256);
                    std::vector<unsigned char> key(klen, 0);
                    gcry_md_write(hd, keystr.data(), keystr.size());
                    unsigned char* result = gcry_md_read(hd, GCRY_MD_SHA256);
                    memcpy(key.data(), result, klen);
                    gcry_md_close(hd);
                    return key;
                }
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
        if (klen != key.size())
            throw std::runtime_error("Inconsistency bwteen key length and allowed key length!");
        size_t vlen = vbuf.size();
        if (const size_t rem=vlen%blklen; rem)
        {
            vlen += blklen-rem;
            vbuf.resize(vlen, ' ');//pad buffer with spaces
        }
        gcry_cipher_hd_t hd;
        if (gcry_error_t err=gcry_cipher_open(&hd, algo, mode, 0); err)
            throw std::runtime_error(fmt::format("gcry_cipher_open failed: {}", gcry_strerror(err)));
        if (gcry_error_t err=gcry_cipher_setkey(hd, key.data(), klen); err)
            throw std::runtime_error(fmt::format("gcry_cipher_setkey failed: {}", gcry_strerror(err)));
        switch (encrypt_mode)
        {
        case Mode::kEncrypt:
            if (gcry_error_t err=gcry_cipher_encrypt(hd, vbuf.data(), vlen, nullptr, 0); err)
                throw std::runtime_error(fmt::format("gcry_cipher_encrypt failed: {}", gcry_strerror(err)));
            break;
        case Mode::kDecrypt:
            if (gcry_error_t err=gcry_cipher_decrypt(hd, vbuf.data(), vlen, nullptr, 0); err)
                throw std::runtime_error(fmt::format("gcry_cipher_decrypt failed: {}", gcry_strerror(err)));
            break;
        default:
            __builtin_unreachable();
        }
        gcry_cipher_close(hd);
    }

public:
    Cryptor() = delete;
    Cryptor(const Cryptor&) = delete;
};//endcls Cryptor

inline std::string Encrypt(const std::string& keystr, const std::string& plaintext)
{
    std::vector<unsigned char> vbuf(plaintext.size());
    memcpy(vbuf.data(), plaintext.data(), plaintext.size());
    Cryptor::EncryptBlowFish(keystr, vbuf);
    return CharVectorToBase64(vbuf);
}

inline std::string Decrypt(const std::string& keystr, const std::string& ciphertext)
{
    std::vector<unsigned char> vbuf = Base64ToCharVector(ciphertext);
    Cryptor::DecryptBlowFish(keystr, vbuf);
    std::string tmp(vbuf.begin(), vbuf.end());
    boost::algorithm::trim_right(tmp); //remove trailing spaces as they could be padded into plaintxt
    return tmp;
}
}

int main() {
    std::cout << "=== 原始版本加解密测试 ===" << std::endl;
    
    // 测试样本API密钥
    std::string api_key = "vmPUZE6mv9SD5VNHk4HlWFsOr6aKE2zvsw0MuIgwCIPy6utIco14y7Ju91duEh8A";
    std::string api_secret = "NhqPtmdSJYdKjVHjA7PZj4Mge3R5YNiP1e3UZjInClVN65XAbvqqM6A7H5fATj0j";
    std::string key = "binance";
    
    std::cout << "原始API Key: " << api_key << std::endl;
    std::cout << "原始API Secret: " << api_secret << std::endl;
    std::cout << "加密密钥: " << key << std::endl;
    
    try {
        // 测试BlowFish加解密
        std::cout << "\n--- BlowFish 加解密测试 ---" << std::endl;
        std::string encrypted_key = crypto::Encrypt(key, api_key);
        std::string encrypted_secret = crypto::Encrypt(key, api_secret);
        
        std::cout << "加密后的API Key: " << encrypted_key << std::endl;
        std::cout << "加密后的API Secret: " << encrypted_secret << std::endl;
        
        std::string decrypted_key = crypto::Decrypt(key, encrypted_key);
        std::string decrypted_secret = crypto::Decrypt(key, encrypted_secret);
        
        std::cout << "解密后的API Key: " << decrypted_key << std::endl;
        std::cout << "解密后的API Secret: " << decrypted_secret << std::endl;
        
        // 验证
        bool key_match = (decrypted_key == api_key);
        bool secret_match = (decrypted_secret == api_secret);
        
        std::cout << "API Key 验证: " << (key_match ? "通过" : "失败") << std::endl;
        std::cout << "API Secret 验证: " << (secret_match ? "通过" : "失败") << std::endl;
        
        // 测试TwoFish加解密
        std::cout << "\n--- TwoFish 加解密测试 ---" << std::endl;
        std::vector<unsigned char> key_buf(api_key.begin(), api_key.end());
        std::vector<unsigned char> secret_buf(api_secret.begin(), api_secret.end());
        
        crypto::Cryptor::EncryptTwoFish(key, key_buf);
        crypto::Cryptor::EncryptTwoFish(key, secret_buf);
        
        std::string twofish_encrypted_key = crypto::CharVectorToBase64(key_buf);
        std::string twofish_encrypted_secret = crypto::CharVectorToBase64(secret_buf);
        
        std::cout << "TwoFish加密后的API Key: " << twofish_encrypted_key << std::endl;
        std::cout << "TwoFish加密后的API Secret: " << twofish_encrypted_secret << std::endl;
        
        // 解密
        std::vector<unsigned char> decrypt_key_buf = crypto::Base64ToCharVector(twofish_encrypted_key);
        std::vector<unsigned char> decrypt_secret_buf = crypto::Base64ToCharVector(twofish_encrypted_secret);
        
        crypto::Cryptor::DecryptTwoFish(key, decrypt_key_buf);
        crypto::Cryptor::DecryptTwoFish(key, decrypt_secret_buf);
        
        std::string twofish_decrypted_key(decrypt_key_buf.begin(), decrypt_key_buf.end());
        std::string twofish_decrypted_secret(decrypt_secret_buf.begin(), decrypt_secret_buf.end());
        
        // 去除填充的空格
        boost::algorithm::trim_right(twofish_decrypted_key);
        boost::algorithm::trim_right(twofish_decrypted_secret);
        
        std::cout << "TwoFish解密后的API Key: " << twofish_decrypted_key << std::endl;
        std::cout << "TwoFish解密后的API Secret: " << twofish_decrypted_secret << std::endl;
        
        bool twofish_key_match = (twofish_decrypted_key == api_key);
        bool twofish_secret_match = (twofish_decrypted_secret == api_secret);
        
        std::cout << "TwoFish API Key 验证: " << (twofish_key_match ? "通过" : "失败") << std::endl;
        std::cout << "TwoFish API Secret 验证: " << (twofish_secret_match ? "通过" : "失败") << std::endl;
        
        // 输出总结
        if (key_match && secret_match && twofish_key_match && twofish_secret_match) {
            std::cout << "\n✅ 所有测试通过！原始版本加解密工作正常。" << std::endl;
        } else {
            std::cout << "\n❌ 部分测试失败！" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}