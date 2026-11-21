#include "services/PasswordHash.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstdlib>
#include <openssl/crypto.h>

namespace {
constexpr int kIterations = 120000;
constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;

std::string hexEncode(const unsigned char* data, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) output << std::setw(2) << static_cast<unsigned int>(data[i]);
    return output.str();
}

bool hexDecode(const std::string& input, std::vector<unsigned char>& output) {
    if (input.size() % 2 != 0) return false;
    output.resize(input.size() / 2);
    for (std::size_t i = 0; i < output.size(); ++i) {
        unsigned int value = 0;
        std::string pair = input.substr(i * 2, 2);
        char* end = nullptr;
        value = std::strtoul(pair.c_str(), &end, 16);
        if (*end != '\0' || value > 255) return false;
        output[i] = static_cast<unsigned char>(value);
    }
    return true;
}
}

std::string PasswordHash::hashPassword(const std::string& password) {
    unsigned char salt[kSaltBytes];
    unsigned char hash[kHashBytes];
    if (RAND_bytes(salt, kSaltBytes) != 1) return {};
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt, kSaltBytes,
                           kIterations, EVP_sha256(), kHashBytes, hash) != 1) return {};
    return "pbkdf2-sha256$" + std::to_string(kIterations) + "$" + hexEncode(salt, kSaltBytes) + "$" + hexEncode(hash, kHashBytes);
}

bool PasswordHash::verifyPassword(const std::string& password, const std::string& encoded) {
    std::istringstream parts(encoded);
    std::string algorithm, iterationsText, saltText, hashText;
    std::getline(parts, algorithm, '$');
    std::getline(parts, iterationsText, '$');
    std::getline(parts, saltText, '$');
    std::getline(parts, hashText);
    if (algorithm != "pbkdf2-sha256") return false;
    int iterations = std::stoi(iterationsText);
    std::vector<unsigned char> salt, expected;
    if (!hexDecode(saltText, salt) || !hexDecode(hashText, expected)) return false;
    std::vector<unsigned char> actual(expected.size());
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(),
                           static_cast<int>(salt.size()), iterations, EVP_sha256(),
                           static_cast<int>(actual.size()), actual.data()) != 1) return false;
    return CRYPTO_memcmp(expected.data(), actual.data(), actual.size()) == 0;
}
