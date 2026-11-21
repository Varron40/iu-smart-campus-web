#pragma once
#include <functional>
#include <list>
#include <stdexcept>
#include <utility>
#include <vector>

// Custom hash table with separate chaining.
template<class K, class V>
class HashTable {
public:
    explicit HashTable(std::size_t capacity = 101, double maxLoadFactor = 0.75)
        : buckets(capacity), maxLoadFactor(maxLoadFactor) {
        if (!capacity) throw std::invalid_argument("Capacity must be positive.");
        if (maxLoadFactor <= 0.0) throw std::invalid_argument("Load factor must be positive.");
    }
    void insert(const K& key, const V& value) {
        auto& b = buckets[index(key)];
        for (auto& item : b) if (item.first == key) { item.second = value; return; }
        b.push_back({key, value}); ++items;
        if (loadFactor() > maxLoadFactor) rehash(buckets.size() * 2);
    }
    bool find(const K& key, V& out) const {
        for (const auto& item : buckets[index(key)])
            if (item.first == key) { out = item.second; return true; }
        return false;
    }
    bool erase(const K& key) {
        auto& b = buckets[index(key)];
        for (auto it = b.begin(); it != b.end(); ++it)
            if (it->first == key) { b.erase(it); --items; return true; }
        return false;
    }
    std::size_t size() const { return items; }
    bool empty() const { return items == 0; }
    std::size_t bucketCount() const { return buckets.size(); }
    double loadFactor() const { return buckets.empty() ? 0.0 : static_cast<double>(items) / buckets.size(); }
    void clear() {
        for (auto& bucket : buckets) bucket.clear();
        items = 0;
    }
private:
    std::vector<std::list<std::pair<K,V>>> buckets;
    std::size_t items{0};
    double maxLoadFactor;
    std::size_t index(const K& key) const { return std::hash<K>{}(key) % buckets.size(); }
    void rehash(std::size_t newCapacity) {
        std::vector<std::list<std::pair<K, V>>> old = std::move(buckets);
        buckets = std::vector<std::list<std::pair<K, V>>>(newCapacity ? newCapacity : 1);
        items = 0;
        for (auto& bucket : old) {
            for (auto& item : bucket) {
                buckets[std::hash<K>{}(item.first) % buckets.size()].push_back(item);
                ++items;
            }
        }
    }
};
