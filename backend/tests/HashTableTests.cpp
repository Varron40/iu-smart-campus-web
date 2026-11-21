#include "data_structures/HashTable.h"
#include <cassert>
#include <iostream>
#include <string>

int main() {
    HashTable<std::string, int> table(4);

    table.insert("alpha", 1);
    table.insert("beta", 2);
    table.insert("gamma", 3);
    table.insert("alpha", 99);
    int value = 0;
    assert(table.find("alpha", value) && value == 99);
    assert(!table.find("missing", value));

    assert(table.erase("beta"));
    assert(!table.erase("beta"));
    assert(table.size() == 2);

    std::size_t initialBuckets = table.bucketCount();
    for (int i = 0; i < 50; ++i) table.insert("collision-key-" + std::to_string(i), i);
    assert(table.bucketCount() > initialBuckets);
    for (int i = 0; i < 50; ++i) {
        assert(table.find("collision-key-" + std::to_string(i), value));
    }
    assert(table.empty() == false);

    table.clear();
    assert(table.empty());
    assert(!table.find("alpha", value));
    std::cout << "HashTable tests passed\n";
    return 0;
}
