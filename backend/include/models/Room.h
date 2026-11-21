#pragma once
#include <string>
struct Room {
    int id{};
    std::string name;
    int floor{};
    int capacity{};
    int occupiedSeats{};
    int availableSeats() const { return capacity - occupiedSeats; }
};
