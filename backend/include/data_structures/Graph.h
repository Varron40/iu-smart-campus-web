#pragma once
#include <map>
#include <string>
#include <vector>

struct GraphVertex {
    int id = 0;
    std::string locationKey;
    std::string locationType;
    std::string label;
    int buildingId = 0;
    int floorId = 0;
    double x = 0;
    double y = 0;
    bool hasCoordinates = false;
    bool isDemo = false;
    bool isActive = true;
};

struct GraphEdge {
    int to = 0;
    double weight = 0;
    std::string edgeType;
    std::string accessibilityStatus = "UNKNOWN";
    bool isActive = true;
};

class Graph {
public:
    Graph() = default;
    explicit Graph(int vertexCount);
    bool addVertex(const GraphVertex& vertex);
    bool removeVertex(int id);
    bool setVertexActive(int id, bool isActive);
    bool vertexExists(int id) const;
    const GraphVertex* findVertex(int id) const;
    std::vector<int> vertexIds() const;
    bool addEdge(int from, int to, double weight, const std::string& edgeType = "CORRIDOR",
                 const std::string& accessibilityStatus = "UNKNOWN", bool isActive = true);
    bool removeEdge(int from, int to);
    bool updateEdge(int from, int to, double weight, const std::string& edgeType,
                    const std::string& accessibilityStatus, bool isActive);
    void addUndirectedEdge(int from, int to, double weight);
    const std::vector<GraphEdge>& neighbors(int vertex) const;
    int vertexCount() const;
    bool validate() const;
private:
    std::map<int, GraphVertex> vertices;
    std::map<int, std::vector<GraphEdge>> adjacency;
    bool hasEdge(int from, int to) const;
};
