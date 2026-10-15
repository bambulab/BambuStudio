#include "BBLUtil.hpp"
#include "libslic3r/Desensitize.hpp"

#include <random>

#include <openssl/aes.h>
#include "libslic3r/Log.hpp"

using namespace std;

namespace Slic3r
{

std::string BBLCrossTalk::Crosstalk_DevId(const std::string& dev_id)
{
    return Desensitize::dev_id(dev_id);
}

std::string BBLCrossTalk::Crosstalk_DevIP(const std::string& str)
{
    return Desensitize::dev_ip(str);
}

std::string BBLCrossTalk::Crosstalk_DevName(const std::string& dev_name)
{
    return Desensitize::dev_name(dev_name);
}

std::string BBLCrossTalk::Crosstalk_JsonLog(const nlohmann::json& json)
{
    return Desensitize::json_log(json);
}

std::string BBLCrossTalk::Crosstalk_UsrId(const std::string& uid)
{
    return Desensitize::user_id(uid);
}

std::string BBLCrossTalk::Encode_DevIp(const std::string& str, const std::string& rand)
{
    if (str.empty() || rand.empty()) {
        return str;
    }

    uint32_t seed = 2166136261u;
    for (unsigned char ch : rand) {
        seed ^= ch;
        seed *= 16777619u;
    }
    std::mt19937 rng(seed);

    std::string obfuscated;
    obfuscated.resize(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char k = static_cast<unsigned char>(rng() & 0xFF);
        obfuscated[i] = static_cast<char>(static_cast<unsigned char>(str[i]) ^ k);
    }

    static const char hexmap[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(obfuscated.size() * 2);
    for (char* p = obfuscated.data(); p < obfuscated.data() + obfuscated.size(); ++p) {
        unsigned char v = static_cast<unsigned char>(*p);
        if (v >> 4 > 16) {
            BOOST_LOG_TRIVIAL(error) << "error: invalid byte value.";
            return "";
        }

        if ((v & 0x0F) > 16) {
            BOOST_LOG_TRIVIAL(error) << "error: invalid byte value.";
            return "";
        }

        hex.push_back(hexmap[v >> 4]);
        hex.push_back(hexmap[v & 0x0F]);
    }

    return hex;
}

std::string BBLCrossTalk::Decode_DevIp(const std::string& str, const std::string& rand)
{
    if (str.empty() || rand.empty()) {
        return str;
    }

    if ((str.size() % 2) != 0) {
        return str;
    }

    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string bytes;
    bytes.reserve(str.size() / 2);
    for (size_t i = 0; i < str.size(); i += 2) {
        int hi = hexval(str[i]);
        int lo = hexval(str[i + 1]);
        if (hi < 0 || lo < 0) {
            return str;
        }
        unsigned char v = static_cast<unsigned char>((hi << 4) | lo);
        bytes.push_back(static_cast<char>(v));
    }

    uint32_t seed = 2166136261u;
    for (unsigned char ch : rand) {
        seed ^= ch;
        seed *= 16777619u;
    }
    std::mt19937 rng(seed);

    std::string plain;
    plain.resize(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) {
        unsigned char k = static_cast<unsigned char>(rng() & 0xFF);
        plain[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ k);
    }

    return plain;
}

bool BBL_Encrypt::AESEncrypt(unsigned char* src, unsigned src_len, unsigned char* encrypt,
                             unsigned& out_len, const std::string& fixed_key)
{
    unsigned char user_key[AES_BLOCK_SIZE];
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        user_key[i] = (unsigned char)fixed_key[i % fixed_key.length()];
    }

    AES_KEY key;
    if (AES_set_encrypt_key(user_key, 128, &key) != 0) {
        return false;
    };

    unsigned len = 0;
    while (len < src_len) {
        AES_encrypt(src + len, encrypt + len, &key);
        len += AES_BLOCK_SIZE;
        out_len = len;
    }

    return true;
}

bool BBL_Encrypt::AESDecrypt(unsigned char* encrypt,
                             unsigned encrypt_len,
                             unsigned char* src,
                             unsigned& out_len,
                             const std::string& fixed_key)
{
    if (encrypt_len % AES_BLOCK_SIZE != 0) {
        return false;
    }

    unsigned char user_key[AES_BLOCK_SIZE];
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        user_key[i] = (unsigned char)fixed_key[i % fixed_key.length()];
    }

    AES_KEY key;
    if (AES_set_decrypt_key(user_key, 128, &key) != 0) {
        return false;
    };

    unsigned len = 0;
    while (len < encrypt_len) {
        AES_decrypt(encrypt + len, src + len, &key);
        len += AES_BLOCK_SIZE;
        out_len = len;
    }

    return true;
}


static inline void s_bbl_fill_key_32(unsigned char(&key_out)[32], const std::string& key)
{
    for (size_t i = 0; i < 32; ++i) {
        key_out[i] = static_cast<unsigned char>(key[i % key.size()]);
    }
}

static inline void s_bbl_fill_iv_16(unsigned char(&iv_out)[AES_BLOCK_SIZE], const std::string& iv)
{
    for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
        iv_out[i] = static_cast<unsigned char>(iv[i % iv.size()]);
    }
}


bool BBL_Encrypt::AES256CBC_Encrypt(unsigned char* src,
                                    unsigned src_len,
                                    unsigned char* encrypt,
                                    unsigned& out_len,
                                    const std::string& key,
                                    const std::string& iv)
{
    if (src == nullptr || encrypt == nullptr) {
        return false;
    }
    if (key.empty() || iv.empty()) {
        return false;
    }
    // No padding: require input length to be a multiple of AES block size.
    if (src_len == 0 || (src_len % AES_BLOCK_SIZE) != 0) {
        return false;
    }

    unsigned char key_bytes[32];
    s_bbl_fill_key_32(key_bytes, key);

    AES_KEY aes_key;
    if (AES_set_encrypt_key(key_bytes, 256, &aes_key) != 0) {
        return false;
    }

    unsigned char iv_bytes[AES_BLOCK_SIZE];
    s_bbl_fill_iv_16(iv_bytes, iv);

    unsigned char ivec[AES_BLOCK_SIZE];
    std::memcpy(ivec, iv_bytes, AES_BLOCK_SIZE);
        
    AES_cbc_encrypt(src, encrypt, src_len, &aes_key, ivec, AES_ENCRYPT);

    out_len = src_len;
    return true;
}

bool BBL_Encrypt::AES256CBC_Decrypt(unsigned char* encrypt,
                                    unsigned encrypt_len,
                                    unsigned char* src,
                                    unsigned& out_len,
                                    const std::string& key,
                                    const std::string& iv)
{
    if (encrypt == nullptr || src == nullptr) {
        return false;
    }
    if (key.empty() || iv.empty()) {
        return false;
    }
    // No padding: require ciphertext length to be a multiple of AES block size.
    if (encrypt_len == 0 || (encrypt_len % AES_BLOCK_SIZE) != 0) {
        return false;
    }

    unsigned char key_bytes[32];
    s_bbl_fill_key_32(key_bytes, key);

    AES_KEY aes_key;
    if (AES_set_decrypt_key(key_bytes, 256, &aes_key) != 0) {
        return false;
    }

    unsigned char iv_bytes[AES_BLOCK_SIZE];
    s_bbl_fill_iv_16(iv_bytes, iv);

    unsigned char ivec[AES_BLOCK_SIZE];
    std::memcpy(ivec, iv_bytes, AES_BLOCK_SIZE);

    AES_cbc_encrypt(encrypt, src, encrypt_len, &aes_key, ivec, AES_DECRYPT);

    out_len = encrypt_len;
    return true;
}

}// End of namespace Slic3r

