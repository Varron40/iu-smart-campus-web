#include "data_structures/Graph.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

Graph::Graph(int vertexCount) {
    if (vertexCount < 0) throw std::invalid_argument("Vertex count cannot be negative.");
    for (int id = 0; id < vertexCount; ++id) {
        GraphVertex vertex;
        vertex.id = id;
        vertex.label = "Vertex " + std::to_string(id);
        addVertex(vertex);
    }
}

bool Graph::addVertex(const GraphVertex& vertex) {
    if (vertex.id < 0 || vertex.label.empty()) throw std::invalid_argument("Vertex ID and label are required.");
    if (vertices.find(vertex.id) != vertices.end()) return false;
    vertices.emplace(vertex.id, vertex);
    adjacency.emplace(vertex.id, std::vector<GraphEdge>{});
    return true;
}

bool Graph::removeVertex(int id) {
    if (vertices.erase(id) == 0) return false;
    adjacency.erase(id);
    for (auto& entry : adjacency) {
        auto& edges = entry.second;
        edges.erase(std::remove_if(edges.begin(), edges.end(),
                                   [id](const GraphEdge& edge) { return edge.to == id; }), edges.end());
    }
    return true;
}

bool Graph::setVertexActive(int id, bool isActive) {
    auto vertex = vertices.find(id);
    if (vertex == vertices.end()) return false;
    vertex->second.isActive = isActive;
    return true;
}

bool Graph::vertexExists(int id) const {
    return vertices.find(id) != vertices.end();
}

const GraphVertex* Graph::findVertex(int id) const {
    auto vertex = vertices.find(id);
    return vertex == vertices.end() ? nullptr : &vertex->second;
}

std::vector<int> Graph::vertexIds() const {
    std::vector<int> ids;
    ids.reserve(vertices.size());
    for (const auto& entry : vertices) ids.push_back(entry.first);
    return ids;
}

bool Graph::hasEdge(int from, int to) const {
    auto adjacencyList = adjacency.find(from);
    if (adjacencyList == adjacency.end()) return false;
    return std::any_of(adjacencyList->second.begin(), adjacencyList->second.end(),
                       [to](const GraphEdge& edge) { return edge.to == to; });
}

bool Graph::addEdge(int from, int to, double weight, const std::string& edgeType,
                    const std::string& accessibilityStatus, bool isActive) {
    if (!vertexExists(from) || !vertexExists(to)) throw std::out_of_range("Graph edge endpoint does not exist.");
    if (from == to) throw std::invalid_argument("Self-edges are not supported.");
    if (!std::isfinite(weight) || weight < 0) throw std::invalid_argument("Edge weight must be finite and non-negative.");
    if (hasEdge(from, to) || hasEdge(to, from)) return false;
    GraphEdge forward{to, weight, edgeType, accessibilityStatus, isActive};
    GraphEdge reverse{from, weight, edgeType, accessibilityStatus, isActive};
    adjacency[from].push_back(forward);
    adjacency[to].push_back(reverse);
    return true;
}

bool Graph::removeEdge(int from, int to) {
    if (!vertexExists(from) || !vertexExists(to)) return false;
    auto erase = [to](std::vector<GraphEdge>& edges) {
        auto initialSize = edges.size();
        edges.erase(std::remove_if(edges.begin(), edges.end(),
                                   [to](const GraphEdge& edge) { return edge.to == to; }), edges.end());
        return initialSize != edges.size();
    };
    bool removed = erase(adjacency[from]);
    removed = erase(adjacency[to]) || removed;
    return removed;
}

bool Graph::updateEdge(int from, int to, double weight, const std::string& edgeType,
                       const std::string& accessibilityStatus, bool isActive) {
    if (!std::isfinite(weight) || weight < 0) throw std::invalid_argument("Edge weight must be finite and non-negative.");
    if (!vertexExists(from) || !vertexExists(to)) return false;
    GraphEdge* forward = nullptr;
    GraphEdge* reverse = nullptr;
    for (auto& edge : adjacency[from]) if (edge.to == to) forward = &edge;
    for (auto& edge : adjacency[to]) if (edge.to == from) reverse = &edge;
    if (!forward || !reverse) return false;
    forward->weight = reverse->weight = weight;
    forward->edgeType = reverse->edgeType = edgeType;
    forward->accessibilityStatus = reverse->accessibilityStatus = accessibilityStatus;
    forward->isActive = reverse->isActive = isActive;
    return true;
}

void Graph::addUndirectedEdge(int from, int to, double weight) {
    if (!addEdge(from, to, weight)) throw std::invalid_argument("Duplicate graph edge.");
}

const std::vector<GraphEdge>& Graph::neighbors(int vertex) const {
    auto found = adjacency.find(vertex);
    if (found == adjacency.end()) throw std::out_of_range("Graph vertex does not exist.");
    return found->second;
}

int Graph::vertexCount() const {
    return static_cast<int>(vertices.size());
}

bool Graph::validate() const {
    if (vertices.size() != adjacency.size()) return false;
    for (const auto& entry : adjacency) {
        if (!vertexExists(entry.first)) return false;
        for (const auto& edge : entry.second) {
            if (!vertexExists(edge.to) || !std::isfinite(edge.weight) || edge.weight < 0) return false;
            bool reciprocal = false;
            for (const auto& reverse : adjacency.at(edge.to)) {
                if (reverse.to == entry.first && reverse.weight == edge.weight &&
                    reverse.isActive == edge.isActive && reverse.edgeType == edge.edgeType &&
                    reverse.accessibilityStatus == edge.accessibilityStatus) {
                    reciprocal = true;
                    break;
                }
            }
            if (!reciprocal) return false;
        }
    }
    return true;
}
