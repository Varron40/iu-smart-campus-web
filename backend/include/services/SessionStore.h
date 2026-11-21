#pragma once
#include <mutex>
#include <string>
#include <unordered_map>

class SessionStore {
public:
    std::string create(int studentDatabaseId);
    bool find(const std::string& token, int& studentDatabaseId) const;
    void invalidate(const std::string& token);
private:
    mutable std::mutex mutex;
    std::unordered_map<std::string, int> sessions;
};
