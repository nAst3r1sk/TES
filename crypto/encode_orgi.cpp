#pragma once
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
// const std::string kPublicKey = "qwe123"s;

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
    std::vector<unsigned char> vbuf(ciphertext.size());
    memcpy(vbuf.data(), ciphertext.data(), ciphertext.size());
    Cryptor::DecryptBlowFish(keystr, vbuf);
    std::string tmp(vbuf.begin(), vbuf.end());
    boost::algorithm::trim_right(tmp); //remove trailing spaces as they could be padded into plaintxt
    return tmp;
}
}

// 解密：
const std::string &encrypt_api_key = account["apiKey"].GetString();
const std::string &encrypt_api_secret = account["apiSecret"].GetString();
const auto vchar1 = crypto::Base64ToCharVector(encrypt_api_key);
std::string raw_api_key = crypto::Decrypt(exchId, std::string(vchar1.begin(), vchar1.end()));
const auto vchar2 = crypto::Base64ToCharVector(encrypt_api_secret);
std::string raw_api_secret = crypto::Decrypt(exchId, std::string(vchar2.begin(), vchar2.end()));
    

// 加密：key是对应的交易所，比如币安的是BINANCE，gate是GATEIO；text是需要加密的key，就是原始的api_key，api_secret
const std::string encrypted = crypto::Encrypt(key, text);
std::cout << std::endl;
std::cout << encrypted << std::endl;
