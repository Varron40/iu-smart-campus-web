#pragma once
#include "algorithms/Dijkstra.h"

class NavigationManager {
public:
    NavigationManager();
    PathResult findRoute(int from, int to) const;
private:
    Graph graph;
};
