#pragma once
#include "data_structures/Graph.h"
#include <vector>

struct NavigationPath {
    double totalCost = 0;
    std::vector<GraphVertex> locations;
    std::vector<GraphEdge> edges;
    bool containsDemoData = false;
    bool accessibilityVerified = true;
};

struct NavigationEdgeRecord {
    int fromId = 0;
    int toId = 0;
    double weight = 0;
    std::string edgeType;
    std::string accessibilityStatus;
    bool isActive = true;
};
