#pragma once
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

template<class T>
class MaxHeap {
public:
    bool empty() const { return values.empty(); }
    std::size_t size() const { return values.size(); }
    const T& peek() const {
        if (empty()) throw std::underflow_error("MaxHeap is empty.");
        return values.front();
    }
    void push(const T& value) { values.push_back(value); siftUp(values.size() - 1); }
    void clear() { values.clear(); }
    void build(const std::vector<T>& input) {
        values = input;
        if (values.size() < 2) return;
        for (std::size_t parent = values.size() / 2; parent > 0; --parent) siftDown(parent - 1);
    }
    T pop() {
        if (empty()) throw std::underflow_error("MaxHeap is empty.");
        T result = values.front(); values.front() = values.back(); values.pop_back();
        if (!empty()) siftDown(0);
        return result;
    }
private:
    std::vector<T> values;
    void siftUp(std::size_t i) {
        while (i > 0) {
            auto p = (i - 1) / 2;
            if (!(values[i] > values[p])) break;
            std::swap(values[i], values[p]); i = p;
        }
    }
    void siftDown(std::size_t i) {
        while (true) {
            auto l = 2*i+1, r = 2*i+2, largest = i;
            if (l < values.size() && values[l] > values[largest]) largest = l;
            if (r < values.size() && values[r] > values[largest]) largest = r;
            if (largest == i) break;
            std::swap(values[i], values[largest]); i = largest;
        }
    }
};
