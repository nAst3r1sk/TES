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

inline std::vector<unsigned char> base64_decode(const std::string& encoded_string)
{
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::vector<unsigned char> ret;
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in = 0;
    unsigned char char_array_4[4], char_array_3[3];

    while (in_len-- && (encoded_string[in] != '=') && 
           (isalnum(encoded_string[in]) || (encoded_string[in] == '+') || (encoded_string[in] == '/'))) {
        char_array_4[i++] = encoded_string[in]; in++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
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

        for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
    }

    return ret;
}

inline std::string CharVectorToBase64(const std::vector<unsigned char>& vchar)
{
    return std::string(kBase64Prefix) + base64_encode(vchar);
}

inline std::vector<unsigned char> Base64ToCharVector(const std::string& str)
{
    return base64_decode(str.substr(strlen(kBase64Prefix)));
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

    // 修改默认方法使用BlowFish算法（与原始版本一致）
    static std::string Encrypt(const std::string& exchId, const std::string& text)
    {
        std::vector<unsigned char> data(text.begin(), text.end());
        EncryptBlowFish(exchId, data);  // 改为使用BlowFish
        return CharVectorToBase64(data);
    }

    static std::string Decrypt(const std::string& exchId, const std::string& ciphertext)
    {
        auto data = Base64ToCharVector(ciphertext);
        DecryptBlowFish(exchId, data);  // 改为使用BlowFish
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

    // 修改密钥处理方式以匹配原始版本
    static std::vector<unsigned char> PrepareKey(Hash hashmode, Cipher cipher, const std::string& keystr)
    {
        const int algo = GetAlgoCode(cipher);
        const size_t slen = keystr.size();
        const size_t klen = gcry_cipher_get_algo_keylen(algo);
        
        // 使用原始版本的密钥处理方式：直接使用密钥，截断或填充到算法要求长度
        std::vector<unsigned char> key(klen, 0);
        memcpy(key.data(), keystr.data(), std::min(slen, klen));
        return key;
    }

    // 修改加密方法使用ECB模式和空格填充（与原始版本一致）
    static void CallCipher(Mode mode, Cipher cipher, const std::vector<unsigned char>& key, std::vector<unsigned char>& vbuf)
    {
        const int algo = GetAlgoCode(cipher);
        const int cipher_mode = GCRY_CIPHER_MODE_ECB;  // 改为ECB模式
        const size_t klen = gcry_cipher_get_algo_keylen(algo);
        const size_t blklen = gcry_cipher_get_algo_blklen(algo);
        size_t vlen = vbuf.size();
        
        if (mode == Mode::kEncrypt) {
            // 使用原始版本的填充方式：用空格填充
            if (const size_t rem = vlen % blklen; rem) {
                vlen += blklen - rem;
                vbuf.resize(vlen, ' ');  // 用空格填充
            }
        }
        
        gcry_cipher_hd_t hd;
        gcry_cipher_open(&hd, algo, cipher_mode, 0);
        gcry_cipher_setkey(hd, key.data(), klen);
        
        if (mode == Mode::kEncrypt) {
            gcry_cipher_encrypt(hd, vbuf.data(), vlen, nullptr, 0);
        } else {
            gcry_cipher_decrypt(hd, vbuf.data(), vbuf.size(), nullptr, 0);
            // 解密后去除空格填充
            while (!vbuf.empty() && vbuf.back() == ' ') {
                vbuf.pop_back();
            }
        }
        
        gcry_cipher_close(hd);
    }
};
} // namespace crypto