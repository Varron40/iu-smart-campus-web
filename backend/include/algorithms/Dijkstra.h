#pragma once
#include "data_structures/Graph.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>

struct PathResult { double distance; std::vector<int> path; };

inline PathResult dijkstra(const Graph& graph, int source, int destination) {
    if (!graph.vertexExists(source) || !graph.vertexExists(destination))
        throw std::out_of_range("Invalid route endpoint.");
    const double INF = std::numeric_limits<double>::infinity();
    const GraphVertex* sourceVertex = graph.findVertex(source);
    const GraphVertex* destinationVertex = graph.findVertex(destination);
    if (!sourceVertex || !destinationVertex || !sourceVertex->isActive || !destinationVertex->isActive)
        return {INF, {}};
    std::map<int, double> dist;
    std::map<int, int> previous;
    for (int id : graph.vertexIds()) {
        dist[id] = INF;
        previous[id] = -1;
    }
    using State = std::pair<double, int>;
    std::priority_queue<State, std::vector<State>, std::greater<State>> queue;
    dist[source] = 0;
    queue.push({0, source});
    while (!queue.empty()) {
        auto [cost, u] = queue.top(); queue.pop();
        if (cost != dist[u]) continue;
        const GraphVertex* current = graph.findVertex(u);
        if (!current || !current->isActive) continue;
        if (u == destination) break;
        for (const auto& edge : graph.neighbors(u)) {
            const GraphVertex* next = graph.findVertex(edge.to);
            if (!edge.isActive || !next || !next->isActive) continue;
            if (edge.weight > std::numeric_limits<double>::max() - cost) continue;
            double nextCost = cost + edge.weight;
            if (nextCost < dist[edge.to]) {
                dist[edge.to] = nextCost;
                previous[edge.to] = u;
                queue.push({nextCost, edge.to});
            }
        }
    }
    if (dist[destination] == INF) return {INF, {}};
    std::vector<int> path;
    for (int at = destination; at != -1; at = previous[at]) {
        path.push_back(at);
        if (at == source) break;
        if (path.size() > static_cast<std::size_t>(graph.vertexCount()))
            return {INF, {}};
    }
    if (path.empty() || path.back() != source) return {INF, {}};
    std::reverse(path.begin(), path.end());
    return {dist[destination], path};
}
