#include "supermarket/models.hpp"

#include <array>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace market {
namespace {

constexpr unsigned int iterations = 600'000;
constexpr std::string_view prefix = "pbkdf2-sha256$600000$";
using Salt = std::array<unsigned char, 16>;
using Digest = std::array<unsigned char, 32>;

int hexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

template <std::size_t N>
std::string hex(const std::array<unsigned char, N>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(N * 2, '0');
    for (std::size_t i = 0; i < N; ++i) {
        result[i * 2] = digits[bytes[i] >> 4];
        result[i * 2 + 1] = digits[bytes[i] & 15];
    }
    return result;
}

template <std::size_t N>
void unhex(std::string_view text, std::array<unsigned char, N>& result) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = static_cast<unsigned char>((hexDigit(text[i * 2]) << 4) | hexDigit(text[i * 2 + 1]));
    }
}

void randomSalt(Salt& salt) {
#ifdef _WIN32
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        throw Error("系统随机数生成失败，无法创建密码。");
    }
#else
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw Error("系统随机数生成失败，无法创建密码。");
    }
#endif
}

Digest derive(std::string_view password, const Salt& salt) {
    Digest result{};
#ifdef _WIN32
    struct Algorithm {
        BCRYPT_ALG_HANDLE handle = nullptr;
        ~Algorithm() { if (handle != nullptr) BCryptCloseAlgorithmProvider(handle, 0); }
    } algorithm;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_SHA256_ALGORITHM,
                                                   nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        throw Error("无法初始化系统密码算法。");
    }
    if (!BCRYPT_SUCCESS(BCryptDeriveKeyPBKDF2(
            algorithm.handle, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
            static_cast<ULONG>(password.size()), const_cast<PUCHAR>(salt.data()),
            static_cast<ULONG>(salt.size()), iterations, result.data(),
            static_cast<ULONG>(result.size()), 0))) {
        throw Error("系统密码计算失败。");
    }
#else
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                         static_cast<int>(salt.size()), static_cast<int>(iterations),
                         EVP_sha256(), static_cast<int>(result.size()), result.data()) != 1) {
        throw Error("系统密码计算失败。");
    }
#endif
    return result;
}

bool equalDigest(const Digest& left, const Digest& right) noexcept {
#ifdef _WIN32
    volatile unsigned int difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) difference = difference | (left[i] ^ right[i]);
    return difference == 0;
#else
    return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
#endif
}

}

bool validPasswordHash(std::string_view encoded) {
    if (encoded.size() != prefix.size() + 32 + 1 + 64 || encoded.substr(0, prefix.size()) != prefix) return false;
    if (encoded[prefix.size() + 32] != '$') return false;
    for (std::size_t i = prefix.size(); i < encoded.size(); ++i) {
        if (i != prefix.size() + 32 && hexDigit(encoded[i]) < 0) return false;
    }
    return true;
}

std::string hashPassword(std::string_view password) {
    if (password.size() < 8 || password.size() > 128) throw Error("密码长度必须为 8 至 128 个 UTF-8 字节。");
    Salt salt{};
    randomSalt(salt);
    const Digest digest = derive(password, salt);
    return std::string(prefix) + hex(salt) + '$' + hex(digest);
}

bool verifyPassword(std::string_view password, std::string_view encoded) {
    if (password.size() < 8 || password.size() > 128 || !validPasswordHash(encoded)) return false;
    Salt salt{};
    Digest expected{};
    unhex(encoded.substr(prefix.size(), 32), salt);
    unhex(encoded.substr(prefix.size() + 33, 64), expected);
    return equalDigest(derive(password, salt), expected);
}

}
