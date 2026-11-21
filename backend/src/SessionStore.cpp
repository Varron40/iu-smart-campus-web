#include "services/SessionStore.h"
#include <openssl/rand.h>
#include <iomanip>
#include <sstream>

std::string SessionStore::create(int studentDatabaseId) {
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) throw std::runtime_error("Unable to generate session token");
    std::ostringstream token;
    token << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) token << std::setw(2) << static_cast<unsigned int>(byte);
    std::lock_guard<std::mutex> lock(mutex);
    sessions[token.str()] = studentDatabaseId;
    return token.str();
}

bool SessionStore::find(const std::string& token, int& studentDatabaseId) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto found = sessions.find(token);
    if (found == sessions.end()) return false;
    studentDatabaseId = found->second;
    return true;
}

void SessionStore::invalidate(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex);
    sessions.erase(token);
}
