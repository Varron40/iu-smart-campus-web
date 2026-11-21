#include "managers/NavigationManager.h"

NavigationManager::NavigationManager() : graph(5) {
    // DEMO ONLY: replace with verified campus nodes and connections.
    graph.addUndirectedEdge(0, 1, 20);
    graph.addUndirectedEdge(1, 2, 15);
    graph.addUndirectedEdge(0, 3, 10);
    graph.addUndirectedEdge(3, 2, 12);
    graph.addUndirectedEdge(2, 4, 8);
}
PathResult NavigationManager::findRoute(int from, int to) const {
    return dijkstra(graph, from, to);
}
