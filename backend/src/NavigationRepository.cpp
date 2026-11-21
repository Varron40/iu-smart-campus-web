#include "data_access/NavigationRepository.h"
#include "algorithms/Dijkstra.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

bool execute(sqlite3* database, const char* sql) {
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

const char* validTypes[] = {"BUILDING", "FLOOR", "ROOM", "JUNCTION", "ENTRANCE", "OTHER"};
const char* validAccessibility[] = {"UNKNOWN", "UNVERIFIED", "STEP_FREE", "STAIRS"};

bool hasValue(const char* value, const char* const* values, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index)
        if (value == std::string(values[index])) return true;
    return false;
}

std::string textColumn(sqlite3_stmt* statement, int column) {
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
    return value ? value : "";
}

void readLocation(sqlite3_stmt* statement, GraphVertex& location) {
    location.id = sqlite3_column_int(statement, 0);
    location.locationKey = textColumn(statement, 1);
    location.locationType = textColumn(statement, 2);
    location.label = textColumn(statement, 3);
    location.buildingId = sqlite3_column_int(statement, 4);
    location.floorId = sqlite3_column_int(statement, 5);
    location.hasCoordinates = sqlite3_column_type(statement, 6) != SQLITE_NULL &&
                              sqlite3_column_type(statement, 7) != SQLITE_NULL;
    location.x = location.hasCoordinates ? sqlite3_column_double(statement, 6) : 0;
    location.y = location.hasCoordinates ? sqlite3_column_double(statement, 7) : 0;
    location.isDemo = sqlite3_column_int(statement, 8) != 0;
    location.isActive = sqlite3_column_int(statement, 9) != 0;
}

bool loadLocation(sqlite3* database, int id, GraphVertex& location) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT id,location_key,location_type,label,building_id,floor_id,x,y,is_demo,is_active FROM navigation_locations WHERE id=?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, id);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readLocation(statement, location);
    sqlite3_finalize(statement);
    return found;
}
}

NavigationRepository::NavigationRepository(sqlite3* database) : database(database) {}

bool NavigationRepository::initialize() {
    if (!database) return false;
    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS navigation_locations (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 location_key TEXT NOT NULL UNIQUE,
 location_type TEXT NOT NULL,
 label TEXT NOT NULL,
 building_id INTEGER REFERENCES buildings(id) ON DELETE RESTRICT,
 floor_id INTEGER,
 x REAL,
 y REAL,
 is_demo INTEGER NOT NULL DEFAULT 0 CHECK(is_demo IN (0,1)),
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),
 CHECK((x IS NULL AND y IS NULL) OR (x IS NOT NULL AND y IS NOT NULL)),
 FOREIGN KEY(floor_id,building_id) REFERENCES floors(id,building_id) ON DELETE RESTRICT
);
CREATE TABLE IF NOT EXISTS navigation_edges (
 from_location_id INTEGER NOT NULL REFERENCES navigation_locations(id) ON DELETE RESTRICT,
 to_location_id INTEGER NOT NULL REFERENCES navigation_locations(id) ON DELETE RESTRICT,
 weight REAL NOT NULL CHECK(weight >= 0),
 edge_type TEXT NOT NULL,
 accessibility_status TEXT NOT NULL DEFAULT 'UNKNOWN'
   CHECK(accessibility_status IN ('UNKNOWN','UNVERIFIED','STEP_FREE','STAIRS')),
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),
 PRIMARY KEY(from_location_id,to_location_id),
 CHECK(from_location_id < to_location_id)
);
)SQL";
    if (!execute(database, schema)) return false;

    sqlite3_stmt* count = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT COUNT(*) FROM navigation_locations", -1, &count, nullptr) != SQLITE_OK)
        return false;
    bool empty = sqlite3_step(count) == SQLITE_ROW && sqlite3_column_int(count, 0) == 0;
    sqlite3_finalize(count);
    if (empty) {
        const char* seed = R"SQL(
BEGIN IMMEDIATE;
INSERT INTO navigation_locations(id,location_key,location_type,label,is_demo)
 VALUES(0,'demo-entrance','ENTRANCE','Demo entrance',1),
       (1,'demo-corridor-a','JUNCTION','Demo corridor A',1),
       (2,'demo-corridor-b','JUNCTION','Demo corridor B',1),
       (3,'demo-crossing','JUNCTION','Demo crossing',1),
       (4,'demo-destination','ROOM','Demo destination',1);
INSERT INTO navigation_edges(from_location_id,to_location_id,weight,edge_type,accessibility_status)
 VALUES(0,1,20,'DEMO_PATH','UNKNOWN'),(1,2,15,'DEMO_PATH','UNKNOWN'),
       (0,3,10,'DEMO_PATH','UNKNOWN'),(2,3,12,'DEMO_PATH','UNKNOWN'),
       (2,4,8,'DEMO_PATH','UNKNOWN');
COMMIT;
)SQL";
        if (!execute(database, seed)) {
            execute(database, "ROLLBACK");
            return false;
        }
    }
    return loadGraph();
}

bool NavigationRepository::refresh() {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    return loadGraph();
}

bool NavigationRepository::loadGraph() {
    Graph loaded;
    sqlite3_stmt* locations = nullptr;
    const char* locationSql =
        "SELECT id,location_key,location_type,label,building_id,floor_id,x,y,is_demo,is_active "
        "FROM navigation_locations ORDER BY id";
    if (sqlite3_prepare_v2(database, locationSql, -1, &locations, nullptr) != SQLITE_OK) return false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(locations)) == SQLITE_ROW) {
        GraphVertex location;
        readLocation(locations, location);
        if (!loaded.addVertex(location)) {
            sqlite3_finalize(locations);
            return false;
        }
    }
    sqlite3_finalize(locations);
    if (result != SQLITE_DONE) return false;

    sqlite3_stmt* edges = nullptr;
    const char* edgeSql =
        "SELECT from_location_id,to_location_id,weight,edge_type,accessibility_status,is_active "
        "FROM navigation_edges ORDER BY from_location_id,to_location_id";
    if (sqlite3_prepare_v2(database, edgeSql, -1, &edges, nullptr) != SQLITE_OK) return false;
    while ((result = sqlite3_step(edges)) == SQLITE_ROW) {
        try {
            if (!loaded.addEdge(sqlite3_column_int(edges, 0), sqlite3_column_int(edges, 1),
                                sqlite3_column_double(edges, 2), textColumn(edges, 3),
                                textColumn(edges, 4), sqlite3_column_int(edges, 5) != 0)) {
                sqlite3_finalize(edges);
                return false;
            }
        } catch (...) {
            sqlite3_finalize(edges);
            return false;
        }
    }
    sqlite3_finalize(edges);
    if (result != SQLITE_DONE || !loaded.validate()) return false;
    graph = std::move(loaded);
    return true;
}

bool NavigationRepository::listLocations(bool includeInactive, std::vector<GraphVertex>& locations) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    sqlite3_stmt* statement = nullptr;
    const char* activeSql =
        "SELECT id,location_key,location_type,label,building_id,floor_id,x,y,is_demo,is_active "
        "FROM navigation_locations WHERE is_active=1 ORDER BY label";
    const char* allSql =
        "SELECT id,location_key,location_type,label,building_id,floor_id,x,y,is_demo,is_active "
        "FROM navigation_locations ORDER BY label";
    if (sqlite3_prepare_v2(database, includeInactive ? allSql : activeSql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        GraphVertex location;
        readLocation(statement, location);
        locations.push_back(location);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool NavigationRepository::listEdges(std::vector<NavigationEdgeRecord>& edges) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT from_location_id,to_location_id,weight,edge_type,accessibility_status,is_active "
        "FROM navigation_edges ORDER BY from_location_id,to_location_id";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        NavigationEdgeRecord edge;
        edge.fromId = sqlite3_column_int(statement, 0);
        edge.toId = sqlite3_column_int(statement, 1);
        edge.weight = sqlite3_column_double(statement, 2);
        edge.edgeType = textColumn(statement, 3);
        edge.accessibilityStatus = textColumn(statement, 4);
        edge.isActive = sqlite3_column_int(statement, 5) != 0;
        edges.push_back(edge);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool NavigationRepository::saveLocation(const GraphVertex& location, std::string& error, GraphVertex& saved) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (location.locationKey.empty() || location.label.empty() ||
        !hasValue(location.locationType.c_str(), validTypes, sizeof(validTypes) / sizeof(validTypes[0])) ||
        (location.hasCoordinates && (!std::isfinite(location.x) || !std::isfinite(location.y)))) {
        error = "invalid_location";
        return false;
    }
    const bool isUpdate = location.id >= 0;
    sqlite3_stmt* statement = nullptr;
    const char* insertSql =
        "INSERT INTO navigation_locations(location_key,location_type,label,building_id,floor_id,x,y,is_demo,is_active) "
        "VALUES(?,?,?,?,?,?,?,?,?)";
    const char* updateSql =
        "UPDATE navigation_locations SET location_key=?,location_type=?,label=?,building_id=?,floor_id=?,x=?,y=? "
        "WHERE id=?";
    const char* sql = isUpdate ? updateSql : insertSql;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    bindText(statement, 1, location.locationKey);
    bindText(statement, 2, location.locationType);
    bindText(statement, 3, location.label);
    if (location.buildingId > 0) sqlite3_bind_int(statement, 4, location.buildingId); else sqlite3_bind_null(statement, 4);
    if (location.floorId > 0) sqlite3_bind_int(statement, 5, location.floorId); else sqlite3_bind_null(statement, 5);
    if (location.hasCoordinates) {
        sqlite3_bind_double(statement, 6, location.x);
        sqlite3_bind_double(statement, 7, location.y);
    } else {
        sqlite3_bind_null(statement, 6);
        sqlite3_bind_null(statement, 7);
    }
    if (isUpdate) {
        sqlite3_bind_int(statement, 8, location.id);
    } else {
        sqlite3_bind_int(statement, 8, location.isDemo ? 1 : 0);
        sqlite3_bind_int(statement, 9, location.isActive ? 1 : 0);
    }
    bool inserted = sqlite3_step(statement) == SQLITE_DONE &&
                    (!isUpdate || sqlite3_changes(database) == 1);
    int resultCode = sqlite3_extended_errcode(database);
    int newId = isUpdate ? location.id : static_cast<int>(sqlite3_last_insert_rowid(database));
    sqlite3_finalize(statement);
    if (!inserted) {
        error = (resultCode & 0xFF) == SQLITE_CONSTRAINT ? "duplicate_or_invalid_location" :
                isUpdate && resultCode == SQLITE_OK ? "not_found" : "database_error";
        return false;
    }
    if (!loadLocation(database, newId, saved) || !loadGraph()) {
        error = "database_error";
        return false;
    }
    return true;
}

bool NavigationRepository::setLocationActive(int locationId, bool isActive, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "UPDATE navigation_locations SET is_active=? WHERE id=?", -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, isActive ? 1 : 0);
    sqlite3_bind_int(statement, 2, locationId);
    bool updated = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!updated) {
        error = "not_found";
        return false;
    }
    if (graph.setVertexActive(locationId, isActive)) return true;
    error = "database_error";
    return false;
}

bool NavigationRepository::saveEdge(int from, int to, double weight, const std::string& edgeType,
                                    const std::string& accessibilityStatus, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (from == to || !std::isfinite(weight) || weight < 0 ||
        !hasValue(accessibilityStatus.c_str(), validAccessibility, sizeof(validAccessibility) / sizeof(validAccessibility[0])) ||
        edgeType.empty()) {
        error = "invalid_edge";
        return false;
    }
    int low = std::min(from, to), high = std::max(from, to);
    if (!graph.vertexExists(low) || !graph.vertexExists(high)) {
        error = "location_not_found";
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO navigation_edges(from_location_id,to_location_id,weight,edge_type,accessibility_status) "
        "VALUES(?,?,?,?,?)";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, low);
    sqlite3_bind_int(statement, 2, high);
    sqlite3_bind_double(statement, 3, weight);
    bindText(statement, 4, edgeType);
    bindText(statement, 5, accessibilityStatus);
    bool inserted = sqlite3_step(statement) == SQLITE_DONE;
    int resultCode = sqlite3_extended_errcode(database);
    sqlite3_finalize(statement);
    if (!inserted) {
        error = (resultCode & 0xFF) == SQLITE_CONSTRAINT ? "edge_exists" : "database_error";
        return false;
    }
    if (!graph.addEdge(low, high, weight, edgeType, accessibilityStatus)) {
        error = "graph_inconsistent";
        return false;
    }
    return true;
}

bool NavigationRepository::updateEdge(int from, int to, double weight, const std::string& edgeType,
                                      const std::string& accessibilityStatus, bool isActive, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (from == to || !std::isfinite(weight) || weight < 0 ||
        !hasValue(accessibilityStatus.c_str(), validAccessibility, sizeof(validAccessibility) / sizeof(validAccessibility[0])) ||
        edgeType.empty()) {
        error = "invalid_edge";
        return false;
    }
    int low = std::min(from, to), high = std::max(from, to);
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "UPDATE navigation_edges SET weight=?,edge_type=?,accessibility_status=?,is_active=? "
        "WHERE from_location_id=? AND to_location_id=?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_double(statement, 1, weight);
    bindText(statement, 2, edgeType);
    bindText(statement, 3, accessibilityStatus);
    sqlite3_bind_int(statement, 4, isActive ? 1 : 0);
    sqlite3_bind_int(statement, 5, low);
    sqlite3_bind_int(statement, 6, high);
    bool updated = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!updated) {
        error = "edge_not_found";
        return false;
    }
    if (!graph.updateEdge(low, high, weight, edgeType, accessibilityStatus, isActive)) {
        error = "graph_inconsistent";
        return false;
    }
    return true;
}

bool NavigationRepository::removeEdge(int from, int to, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    int low = std::min(from, to), high = std::max(from, to);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
            "DELETE FROM navigation_edges WHERE from_location_id=? AND to_location_id=?",
            -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, low);
    sqlite3_bind_int(statement, 2, high);
    bool removed = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!removed) {
        error = "edge_not_found";
        return false;
    }
    if (!graph.removeEdge(low, high)) {
        error = "graph_inconsistent";
        return false;
    }
    return true;
}

bool NavigationRepository::findRoute(int from, int to, NavigationPath& path, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    const GraphVertex* source = graph.findVertex(from);
    const GraphVertex* destination = graph.findVertex(to);
    if (!source || !destination || !source->isActive || !destination->isActive) {
        error = "location_not_found";
        return false;
    }
    PathResult result;
    try {
        result = dijkstra(graph, from, to);
    } catch (...) {
        error = "route_error";
        return false;
    }
    if (result.path.empty()) {
        error = "route_unavailable";
        return false;
    }
    path = NavigationPath{};
    path.totalCost = result.distance;
    for (int id : result.path) {
        const GraphVertex* vertex = graph.findVertex(id);
        if (!vertex) {
            error = "graph_inconsistent";
            return false;
        }
        path.locations.push_back(*vertex);
        path.containsDemoData = path.containsDemoData || vertex->isDemo;
    }
    for (std::size_t index = 1; index < result.path.size(); ++index) {
        int fromId = result.path[index - 1];
        int toId = result.path[index];
        bool found = false;
        for (const auto& edge : graph.neighbors(fromId)) {
            if (edge.to == toId) {
                path.edges.push_back(edge);
                path.containsDemoData = path.containsDemoData || edge.edgeType == "DEMO_PATH";
                path.accessibilityVerified =
                    path.accessibilityVerified && edge.accessibilityStatus == "STEP_FREE";
                found = true;
                break;
            }
        }
        if (!found) {
            error = "graph_inconsistent";
            return false;
        }
    }
    return true;
}
