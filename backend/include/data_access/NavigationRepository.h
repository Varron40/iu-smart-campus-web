#pragma once
#include "data_structures/Graph.h"
#include "models/Navigation.h"
#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>

class NavigationRepository {
public:
    explicit NavigationRepository(sqlite3* database);
    bool initialize();
    bool refresh();
    bool listLocations(bool includeInactive, std::vector<GraphVertex>& locations);
    bool listEdges(std::vector<NavigationEdgeRecord>& edges);
    bool saveLocation(const GraphVertex& location, std::string& error, GraphVertex& saved);
    bool setLocationActive(int locationId, bool isActive, std::string& error);
    bool saveEdge(int from, int to, double weight, const std::string& edgeType,
                  const std::string& accessibilityStatus, std::string& error);
    bool updateEdge(int from, int to, double weight, const std::string& edgeType,
                    const std::string& accessibilityStatus, bool isActive, std::string& error);
    bool removeEdge(int from, int to, std::string& error);
    bool findRoute(int from, int to, NavigationPath& path, std::string& error);
private:
    sqlite3* database;
    Graph graph;
    std::mutex repositoryMutex;
    bool loadGraph();
};
