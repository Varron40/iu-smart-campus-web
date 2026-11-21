#pragma once
#include <string>

namespace PasswordHash {
std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& encoded);
}
