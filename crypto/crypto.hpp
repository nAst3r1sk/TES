#pragma once
#include <bits/stdc++.h>
#include <gcrypt.h>
#include <fmt/core.h>
#include <fstream>

namespace crypto { 
constexpr char kBasePaddingChar = '=';
constexpr int kBase64InputChunkSize = 3;
constexpr const char* kBase64Prefix = "Base64:";

// Base64编码函数
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

// Base64解码函数
inline std::vector<unsigned char> base64_decode(const std::string& encoded_string)
{
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::vector<unsigned char> ret;

    // 去除可能的前缀
    size_t data_start = encoded_string.find("Base64:");
    size_t start_pos = (data_start == std::string::npos) ? 0 : data_start + 7;

    while (start_pos < in_len && encoded_string[start_pos] != '=') {
        if (encoded_string[start_pos] == ' ' || encoded_string[start_pos] == '\n' || encoded_string[start_pos] == '\r') {
            start_pos++;
            continue;
        }
        
        char_array_4[i++] = encoded_string[start_pos++]; 
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; j < i - 1; j++)
            ret.push_back(char_array_3[j]);
    }

    return ret;
}

inline std::string CharVectorToBase64(const std::vector<unsigned char>& vchar)
{
    return std::string(kBase64Prefix) + base64_encode(vchar);
}

inline std::vector<unsigned char> Base64ToCharVector(const std::string& str)
{
    return base64_decode(str);
}

class Cryptor {
private:
    enum class Mode {
        kEncrypt,
        kDecrypt,
    };
    enum class Cipher {
        kBlowFish,
        kTwoFish,
    };
    enum class Hash {
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

    static std::string Encrypt(const std::string& exchId, const std::string& text)
    {
        std::vector<unsigned char> data(text.begin(), text.end());
        EncryptTwoFish(exchId, data);
        return CharVectorToBase64(data);
    }

    static std::string Decrypt(const std::string& exchId, const std::string& ciphertext)
    {
        auto data = Base64ToCharVector(ciphertext);
        DecryptTwoFish(exchId, data);
        return std::string(data.begin(), data.end());
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
        static std::unordered_map<Cipher, int> cipher_map = {
            {Cipher::kBlowFish, GCRY_CIPHER_BLOWFISH},
            {Cipher::kTwoFish, GCRY_CIPHER_TWOFISH},
        };
        return cipher_map.at(cipher);
    }

    static std::vector<unsigned char> PrepareKey(Hash hashmode, Cipher cipher, const std::string& keystr)
    {
        const int algo = GetAlgoCode(cipher);
        
        if (hashmode == Hash::kSha256) {
            // 使用SHA256哈希密钥
            gcry_md_hd_t hd;
            gcry_md_open(&hd, GCRY_MD_SHA256, 0);
            gcry_md_write(hd, keystr.c_str(), keystr.length());
            unsigned char* hash = gcry_md_read(hd, GCRY_MD_SHA256);
            size_t hash_len = gcry_md_get_algo_dlen(GCRY_MD_SHA256);
            
            std::vector<unsigned char> key(hash, hash + hash_len);
            gcry_md_close(hd);
            return key;
        } else {
            // 直接使用原始密钥
            return std::vector<unsigned char>(keystr.begin(), keystr.end());
        }
    }

    static void CallCipher(Mode mode, Cipher cipher, const std::vector<unsigned char>& key, std::vector<unsigned char>& vbuf)
    {
        gcry_cipher_hd_t hd;
        int algo = GetAlgoCode(cipher);
        
        gcry_cipher_open(&hd, algo, GCRY_CIPHER_MODE_CBC, 0);
        gcry_cipher_setkey(hd, key.data(), key.size());
        
        // 使用零向量作为IV
        std::vector<unsigned char> iv(16, 0);
        gcry_cipher_setiv(hd, iv.data(), iv.size());
        
        if (mode == Mode::kEncrypt) {
            // 加密前进行PKCS7填充
            size_t block_size = gcry_cipher_get_algo_blklen(algo);
            size_t padding = block_size - (vbuf.size() % block_size);
            for (size_t i = 0; i < padding; i++) {
                vbuf.push_back(static_cast<unsigned char>(padding));
            }
            gcry_cipher_encrypt(hd, vbuf.data(), vbuf.size(), nullptr, 0);
        } else {
            gcry_cipher_decrypt(hd, vbuf.data(), vbuf.size(), nullptr, 0);
            // 解密后去除PKCS7填充
            if (!vbuf.empty()) {
                unsigned char padding = vbuf.back();
                if (padding <= vbuf.size()) {
                    vbuf.resize(vbuf.size() - padding);
                }
            }
        }
        
        gcry_cipher_close(hd);
    }
};
} // namespace crypto