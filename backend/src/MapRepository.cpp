#include "data_access/MapRepository.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
using Json = nlohmann::json;

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

bool execute(sqlite3* database, const char* sql) {
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::string textColumn(sqlite3_stmt* statement, int column) {
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
    return value ? value : "";
}

bool hasColumn(sqlite3* database, const char* table, const char* column) {
    std::string sql = "PRAGMA table_info(" + std::string(table) + ")";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (textColumn(statement, 1) == column) { found = true; break; }
    }
    sqlite3_finalize(statement);
    return found;
}

const char* validFeatureTypes[] = {"ROOM", "CORRIDOR", "STAIRS", "ELEVATOR", "ENTRANCE", "RAMP", "WALL", "OTHER"};
const char* validGeometryTypes[] = {"POLYGON", "LINE", "POINT"};
const char* validVerificationStatuses[] = {"DEMO", "UNVERIFIED", "VERIFIED"};

bool hasValue(const std::string& value, const char* const* values, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index)
        if (value == values[index]) return true;
    return false;
}

void readFloorInfo(sqlite3_stmt* statement, MapFloorInfo& floor) {
    floor.id = sqlite3_column_int(statement, 0);
    floor.buildingId = sqlite3_column_int(statement, 1);
    floor.label = textColumn(statement, 2);
    floor.description = textColumn(statement, 3);
    floor.hasMap = sqlite3_column_type(statement, 4) != SQLITE_NULL;
    if (!floor.hasMap) {
        floor.verificationStatus = "NO_MAP";
        return;
    }
    floor.coordinateSystem = textColumn(statement, 5);
    floor.unitLabel = textColumn(statement, 6);
    floor.width = sqlite3_column_double(statement, 7);
    floor.height = sqlite3_column_double(statement, 8);
    floor.scaleKnown = sqlite3_column_type(statement, 9) != SQLITE_NULL;
    floor.metersPerUnit = floor.scaleKnown ? sqlite3_column_double(statement, 9) : 0;
    floor.elevation = sqlite3_column_double(statement, 10);
    floor.floorHeight = sqlite3_column_double(statement, 11);
    floor.verificationStatus = textColumn(statement, 12);
    floor.sourceReference = textColumn(statement, 13);
}

void readClassroom(sqlite3_stmt* statement, Classroom& room) {
    room.id = sqlite3_column_int(statement, 0);
    room.buildingId = sqlite3_column_int(statement, 1);
    room.buildingName = textColumn(statement, 2);
    room.floorId = sqlite3_column_int(statement, 3);
    room.floorLabel = textColumn(statement, 4);
    room.roomCode = textColumn(statement, 5);
    room.roomName = textColumn(statement, 6);
    room.roomType = textColumn(statement, 7);
    room.capacity = sqlite3_column_int(statement, 8);
    room.capacityKnown = sqlite3_column_int(statement, 9) != 0;
    room.isActive = sqlite3_column_int(statement, 10) != 0;
    room.notes = textColumn(statement, 11);
}

bool floorExists(sqlite3* database, int floorId, int* buildingId = nullptr) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT building_id FROM floors WHERE id=?", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int(statement, 1, floorId);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found && buildingId) *buildingId = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return found;
}
}

MapRepository::MapRepository(sqlite3* database) : database(database) {}

bool MapRepository::initialize() {
    if (!database) return false;
    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS floor_map_metadata (
 floor_id INTEGER PRIMARY KEY,
 building_id INTEGER NOT NULL,
 coordinate_system TEXT NOT NULL DEFAULT 'LOCAL_FLOOR_X_RIGHT_Y_DOWN',
 unit_label TEXT NOT NULL DEFAULT 'local units',
 width REAL NOT NULL CHECK(width > 0),
 height REAL NOT NULL CHECK(height > 0),
 meters_per_unit REAL CHECK(meters_per_unit IS NULL OR meters_per_unit > 0),
 elevation REAL NOT NULL DEFAULT 0,
 floor_height REAL NOT NULL DEFAULT 3 CHECK(floor_height > 0),
 verification_status TEXT NOT NULL CHECK(verification_status IN ('DEMO','UNVERIFIED','VERIFIED')),
 source_reference TEXT NOT NULL DEFAULT '',
 updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
 FOREIGN KEY(floor_id,building_id) REFERENCES floors(id,building_id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS map_features (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 feature_key TEXT NOT NULL UNIQUE,
 building_id INTEGER NOT NULL,
 floor_id INTEGER NOT NULL,
 classroom_id INTEGER REFERENCES classrooms(id) ON DELETE RESTRICT,
 navigation_location_id INTEGER REFERENCES navigation_locations(id) ON DELETE RESTRICT,
 edge_from_id INTEGER,
 edge_to_id INTEGER,
 feature_type TEXT NOT NULL CHECK(feature_type IN ('ROOM','CORRIDOR','STAIRS','ELEVATOR','ENTRANCE','RAMP','WALL','OTHER')),
 label TEXT NOT NULL,
 geometry_type TEXT NOT NULL CHECK(geometry_type IN ('POLYGON','LINE','POINT')),
 geometry_json TEXT NOT NULL,
 elevation REAL NOT NULL DEFAULT 0,
 height REAL NOT NULL DEFAULT 0 CHECK(height >= 0),
 thickness REAL NOT NULL DEFAULT 0 CHECK(thickness >= 0),
 is_demo INTEGER NOT NULL DEFAULT 0 CHECK(is_demo IN (0,1)),
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),
 CHECK((edge_from_id IS NULL AND edge_to_id IS NULL) OR
       (edge_from_id IS NOT NULL AND edge_to_id IS NOT NULL AND edge_from_id < edge_to_id)),
 FOREIGN KEY(floor_id,building_id) REFERENCES floors(id,building_id) ON DELETE CASCADE,
 FOREIGN KEY(edge_from_id,edge_to_id)
   REFERENCES navigation_edges(from_location_id,to_location_id) ON DELETE RESTRICT
);
CREATE INDEX IF NOT EXISTS idx_map_features_floor_active
 ON map_features(floor_id,is_active,feature_type);
CREATE INDEX IF NOT EXISTS idx_map_features_classroom
 ON map_features(classroom_id);
CREATE TRIGGER IF NOT EXISTS validate_map_feature_room_location
BEFORE INSERT ON map_features
WHEN NEW.classroom_id IS NOT NULL AND NOT EXISTS (
 SELECT 1 FROM classrooms c
 WHERE c.id=NEW.classroom_id AND c.floor_id=NEW.floor_id AND c.building_id=NEW.building_id
)
BEGIN
 SELECT RAISE(ABORT,'map_room_location_mismatch');
END;
CREATE TRIGGER IF NOT EXISTS validate_map_feature_room_location_update
BEFORE UPDATE ON map_features
WHEN NEW.classroom_id IS NOT NULL AND NOT EXISTS (
 SELECT 1 FROM classrooms c
 WHERE c.id=NEW.classroom_id AND c.floor_id=NEW.floor_id AND c.building_id=NEW.building_id
)
BEGIN
 SELECT RAISE(ABORT,'map_room_location_mismatch');
END;
CREATE TRIGGER IF NOT EXISTS validate_map_feature_nav_location
BEFORE INSERT ON map_features
WHEN NEW.navigation_location_id IS NOT NULL AND NOT EXISTS (
 SELECT 1 FROM navigation_locations n
 WHERE n.id=NEW.navigation_location_id
   AND n.floor_id=NEW.floor_id AND n.building_id=NEW.building_id
)
BEGIN
 SELECT RAISE(ABORT,'map_nav_location_mismatch');
END;
CREATE TRIGGER IF NOT EXISTS validate_map_feature_nav_location_update
BEFORE UPDATE ON map_features
WHEN NEW.navigation_location_id IS NOT NULL AND NOT EXISTS (
 SELECT 1 FROM navigation_locations n
 WHERE n.id=NEW.navigation_location_id
   AND n.floor_id=NEW.floor_id AND n.building_id=NEW.building_id
)
BEGIN
 SELECT RAISE(ABORT,'map_nav_location_mismatch');
END;
)SQL";
    if (!execute(database, schema)) return false;
    if (!hasColumn(database, "map_features", "thickness") &&
        !execute(database, "ALTER TABLE map_features ADD COLUMN thickness REAL NOT NULL DEFAULT 0 CHECK(thickness >= 0)"))
        return false;
    return seedDemoMap() && seedCsGroundFloor();
}

bool MapRepository::seedCsGroundFloor() {
    sqlite3_stmt* statement = nullptr;
    const char* floorSql =
        "SELECT f.id,f.building_id FROM floors f JOIN buildings b ON b.id=f.building_id "
        "WHERE b.code='B' AND f.label='0' AND b.is_active=1 LIMIT 1";
    if (sqlite3_prepare_v2(database, floorSql, -1, &statement, nullptr) != SQLITE_OK) return false;
    int result = sqlite3_step(statement);
    if (result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return true;
    }
    if (result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return false;
    }
    const int floorId = sqlite3_column_int(statement, 0);
    const int buildingId = sqlite3_column_int(statement, 1);
    sqlite3_finalize(statement);

    MapFloorInfo existing;
    if (readFloorMetadata(floorId, existing) && existing.hasMap) {
        execute(database,
            "DELETE FROM map_features WHERE feature_key IN "
            "('cs0-main-entrance','cs0-side-entrance','cs0-exit','cs0-door-main','cs0-door-side',"
            "'cs0-door-b01','cs0-door-b02','cs0-door-lecture','cs0-door-womens','cs0-door-accessible')");
        return true;
    }
    if (!execute(database, "BEGIN IMMEDIATE")) return false;
    auto rollback = [this]() { execute(database, "ROLLBACK"); };

    const char* renameBuildingSql =
        "UPDATE buildings SET name='CS Building (Building B)',"
        "description='The user-provided ground-floor plan identifies this as the CS building; campus location remains unverified.' "
        "WHERE id=? AND name='Building B (timetable label; site unverified)'";
    if (sqlite3_prepare_v2(database, renameBuildingSql, -1, &statement, nullptr) != SQLITE_OK) {
        rollback();
        return false;
    }
    sqlite3_bind_int(statement, 1, buildingId);
    bool saved = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!saved) {
        rollback();
        return false;
    }

    const char* updateFloorSql =
        "UPDATE floors SET description=? WHERE id=? AND building_id=?";
    if (sqlite3_prepare_v2(database, updateFloorSql, -1, &statement, nullptr) != SQLITE_OK) {
        rollback();
        return false;
    }
    bindText(statement, 1, "CS building ground floor traced from the user-provided plan; dimensions and scale are unverified.");
    sqlite3_bind_int(statement, 2, floorId);
    sqlite3_bind_int(statement, 3, buildingId);
    saved = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!saved) {
        rollback();
        return false;
    }

    const char* metadataSql =
        "INSERT INTO floor_map_metadata(floor_id,building_id,coordinate_system,unit_label,width,height,"
        "meters_per_unit,elevation,floor_height,verification_status,source_reference) "
        "VALUES(?,?,'LOCAL_FLOOR_X_RIGHT_Y_DOWN','image units',873,522,NULL,0,3,'UNVERIFIED',?)";
    if (sqlite3_prepare_v2(database, metadataSql, -1, &statement, nullptr) != SQLITE_OK) {
        rollback();
        return false;
    }
    sqlite3_bind_int(statement, 1, floorId);
    sqlite3_bind_int(statement, 2, buildingId);
    bindText(statement, 3,
        "User-provided CS Building Floor 0 plan (received 2026-11-21); traced geometry, scale and dimensions unverified.");
    saved = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!saved) {
        rollback();
        return false;
    }

    auto classroomId = [this, buildingId, floorId](const char* normalizedCode) {
        sqlite3_stmt* room = nullptr;
        const char* sql =
            "SELECT id FROM classrooms WHERE building_id=? AND floor_id=? "
            "AND lower(replace(replace(room_code,' ',''),char(9),''))=? AND is_active=1 LIMIT 1";
        if (sqlite3_prepare_v2(database, sql, -1, &room, nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_int(room, 1, buildingId);
        sqlite3_bind_int(room, 2, floorId);
        bindText(room, 3, normalizedCode);
        int id = sqlite3_step(room) == SQLITE_ROW ? sqlite3_column_int(room, 0) : 0;
        sqlite3_finalize(room);
        return id;
    };
    const int roomB01 = classroomId("b0.1");
    const int roomB02 = classroomId("b0.2");

    struct FloorFeatureSeed {
        const char* key;
        const char* type;
        const char* label;
        const char* geometryType;
        const char* geometry;
        const char* roomCode;
        double height;
    };
    const FloorFeatureSeed features[] = {
        {"cs0-lecture-hall-01", "OTHER", "Lecture hall 0.1", "POLYGON",
         "[[248,49],[630,49],[630,186],[248,186]]", nullptr, 0.4},
        {"cs0-north-bathroom", "OTHER", "Bathroom", "POLYGON",
         "[[633,50],[754,50],[754,151],[633,151]]", nullptr, 0.2},
        {"cs0-north-stairs", "STAIRS", "Stairs", "POLYGON",
         "[[757,52],[807,52],[807,134],[757,134]]", nullptr, 0.2},
        {"cs0-womens-bathroom", "OTHER", "Women's bathroom", "POLYGON",
         "[[97,153],[207,153],[207,251],[97,251]]", nullptr, 0.2},
        {"cs0-accessible-bathroom", "OTHER", "Accessible bathroom", "POLYGON",
         "[[69,251],[209,251],[209,306],[69,306]]", nullptr, 0.2},
        {"cs0-lobby", "OTHER", "Lobby and corridor", "POLYGON",
         "[[248,187],[809,187],[809,374],[248,374]]", nullptr, 0.1},
        {"cs0-elevator", "ELEVATOR", "Elevator", "POLYGON",
         "[[656,251],[754,251],[754,334],[656,334]]", nullptr, 0.2},
        {"cs0-west-stairs", "STAIRS", "Stairs", "POLYGON",
         "[[279,375],[316,375],[316,457],[279,457]]", nullptr, 0.2},
        {"cs0-room-b01", "ROOM", "B 0.1", "POLYGON",
         "[[509,375],[585,375],[585,450],[509,450]]", "b0.1", 0.4},
        {"cs0-room-b02", "ROOM", "B 0.2", "POLYGON",
         "[[664,375],[784,375],[784,450],[664,450]]", "b0.2", 0.4},
        {"cs0-exterior-north", "WALL", "Exterior wall", "LINE",
         "[[247,48],[811,48],[811,134]]", nullptr, 0},
        {"cs0-exterior-east", "WALL", "Exterior wall", "LINE",
         "[[811,189],[811,375],[784,375]]", nullptr, 0},
        {"cs0-exterior-south-west", "WALL", "Exterior wall", "LINE",
         "[[247,306],[247,375],[509,375]]", nullptr, 0},
        {"cs0-exterior-south-center", "WALL", "Exterior wall", "LINE",
         "[[585,375],[664,375]]", nullptr, 0},
        {"cs0-exterior-south-east", "WALL", "Exterior wall", "LINE",
         "[[784,375],[811,375]]", nullptr, 0},
        {"cs0-exterior-west", "WALL", "Exterior wall", "LINE",
         "[[247,48],[247,213]]", nullptr, 0},
        {"cs0-hall-divider", "WALL", "Interior wall", "LINE",
         "[[247,186],[630,186],[630,48]]", nullptr, 0},
        {"cs0-north-divider", "WALL", "Interior wall", "LINE",
         "[[630,186],[754,186],[754,151]]", nullptr, 0},
        {"cs0-elevator-divider", "WALL", "Interior wall", "LINE",
         "[[754,251],[754,334]]", nullptr, 0}
    };

    const char* featureSql =
        "INSERT INTO map_features(feature_key,building_id,floor_id,classroom_id,feature_type,label,"
        "geometry_type,geometry_json,elevation,height,is_demo) "
        "VALUES(?,?,?,?,?,?,?,?,0,?,0)";
    if (sqlite3_prepare_v2(database, featureSql, -1, &statement, nullptr) != SQLITE_OK) {
        rollback();
        return false;
    }
    bool featuresSaved = true;
    for (const FloorFeatureSeed& feature : features) {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        bindText(statement, 1, feature.key);
        sqlite3_bind_int(statement, 2, buildingId);
        sqlite3_bind_int(statement, 3, floorId);
        const int roomId = feature.roomCode
            ? (std::string(feature.roomCode) == "b0.1" ? roomB01 : roomB02) : 0;
        if (roomId) sqlite3_bind_int(statement, 4, roomId);
        else sqlite3_bind_null(statement, 4);
        bindText(statement, 5, feature.type);
        bindText(statement, 6, feature.label);
        bindText(statement, 7, feature.geometryType);
        bindText(statement, 8, feature.geometry);
        sqlite3_bind_double(statement, 9, feature.height);
        if (sqlite3_step(statement) != SQLITE_DONE) {
            featuresSaved = false;
            break;
        }
    }
    sqlite3_finalize(statement);
    if (!featuresSaved || !execute(database, "COMMIT")) {
        rollback();
        return false;
    }
    return true;
}

bool MapRepository::seedDemoMap() {
    sqlite3_stmt* floor = nullptr;
    const char* floorSql =
        "SELECT f.id,f.building_id FROM floors f JOIN buildings b ON b.id=f.building_id "
        "WHERE b.code='DEMO' AND f.label='Demo Floor' AND b.is_active=1 LIMIT 1";
    if (sqlite3_prepare_v2(database, floorSql, -1, &floor, nullptr) != SQLITE_OK) return false;
    int result = sqlite3_step(floor);
    if (result == SQLITE_DONE) {
        sqlite3_finalize(floor);
        return true;
    }
    if (result != SQLITE_ROW) {
        sqlite3_finalize(floor);
        return false;
    }
    int floorId = sqlite3_column_int(floor, 0);
    int buildingId = sqlite3_column_int(floor, 1);
    sqlite3_finalize(floor);

    MapFloorInfo existing;
    if (readFloorMetadata(floorId, existing) && existing.hasMap) return true;
    if (!execute(database, "BEGIN IMMEDIATE")) return false;
    const char* metadataSql =
        "INSERT INTO floor_map_metadata(floor_id,building_id,coordinate_system,unit_label,width,height,"
        "meters_per_unit,elevation,floor_height,verification_status,source_reference) "
        "VALUES(?,?,'LOCAL_FLOOR_X_RIGHT_Y_DOWN','demo units',100,70,NULL,0,4,'DEMO',"
        "'Synthetic sample only; not an official or surveyed campus plan')";
    sqlite3_stmt* metadata = nullptr;
    if (sqlite3_prepare_v2(database, metadataSql, -1, &metadata, nullptr) != SQLITE_OK) {
        execute(database, "ROLLBACK");
        return false;
    }
    sqlite3_bind_int(metadata, 1, floorId);
    sqlite3_bind_int(metadata, 2, buildingId);
    bool metadataSaved = sqlite3_step(metadata) == SQLITE_DONE;
    sqlite3_finalize(metadata);
    if (!metadataSaved) {
        execute(database, "ROLLBACK");
        return false;
    }

    sqlite3_stmt* updateNode = nullptr;
    const char* updateNodeSql =
        "UPDATE navigation_locations SET building_id=?,floor_id=?,x=?,y=? "
        "WHERE location_key=? AND is_demo=1 AND (x IS NULL OR y IS NULL OR floor_id IS NULL)";
    if (sqlite3_prepare_v2(database, updateNodeSql, -1, &updateNode, nullptr) != SQLITE_OK) {
        execute(database, "ROLLBACK");
        return false;
    }
    struct DemoNode { const char* key; double x; double y; };
    const DemoNode nodes[] = {
        {"demo-entrance", 10, 55}, {"demo-corridor-a", 40, 55},
        {"demo-corridor-b", 70, 35}, {"demo-crossing", 15, 30},
        {"demo-destination", 90, 35}
    };
    bool nodesSaved = true;
    for (const auto& node : nodes) {
        sqlite3_reset(updateNode);
        sqlite3_clear_bindings(updateNode);
        sqlite3_bind_int(updateNode, 1, buildingId);
        sqlite3_bind_int(updateNode, 2, floorId);
        sqlite3_bind_double(updateNode, 3, node.x);
        sqlite3_bind_double(updateNode, 4, node.y);
        bindText(updateNode, 5, node.key);
        if (sqlite3_step(updateNode) != SQLITE_DONE) nodesSaved = false;
    }
    sqlite3_finalize(updateNode);
    if (!nodesSaved) {
        execute(database, "ROLLBACK");
        return false;
    }

    sqlite3_stmt* feature = nullptr;
    const char* featureSql =
        "INSERT INTO map_features(feature_key,building_id,floor_id,edge_from_id,edge_to_id,feature_type,label,"
        "geometry_type,geometry_json,elevation,height,is_demo) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)";
    if (sqlite3_prepare_v2(database, featureSql, -1, &feature, nullptr) != SQLITE_OK) {
        execute(database, "ROLLBACK");
        return false;
    }
    struct DemoFeature {
        const char* key; int from; int to; const char* type; const char* label;
        const char* geometryType; const char* geometryJson; double elevation; double height;
    };
    const DemoFeature features[] = {
        {"demo-footprint", -1, -1, "OTHER", "Illustrative floor extent", "POLYGON",
         "[[3,3],[97,3],[97,67],[3,67]]", 0, 0.25},
        {"demo-edge-0-1", 0, 1, "CORRIDOR", "Sample corridor segment", "LINE", "[[10,55],[40,55]]", 0.3, 0},
        {"demo-edge-1-2", 1, 2, "CORRIDOR", "Sample corridor segment", "LINE", "[[40,55],[70,35]]", 0.3, 0},
        {"demo-edge-0-3", 0, 3, "CORRIDOR", "Sample corridor segment", "LINE", "[[10,55],[15,30]]", 0.3, 0},
        {"demo-edge-2-3", 2, 3, "CORRIDOR", "Sample corridor segment", "LINE", "[[15,30],[70,35]]", 0.3, 0},
        {"demo-edge-2-4", 2, 4, "CORRIDOR", "Sample corridor segment", "LINE", "[[70,35],[90,35]]", 0.3, 0}
    };
    bool featuresSaved = true;
    for (const auto& item : features) {
        sqlite3_reset(feature);
        sqlite3_clear_bindings(feature);
        bindText(feature, 1, item.key);
        sqlite3_bind_int(feature, 2, buildingId);
        sqlite3_bind_int(feature, 3, floorId);
        if (item.from >= 0) {
            sqlite3_bind_int(feature, 4, item.from);
            sqlite3_bind_int(feature, 5, item.to);
        } else {
            sqlite3_bind_null(feature, 4);
            sqlite3_bind_null(feature, 5);
        }
        bindText(feature, 6, item.type);
        bindText(feature, 7, item.label);
        bindText(feature, 8, item.geometryType);
        bindText(feature, 9, item.geometryJson);
        sqlite3_bind_double(feature, 10, item.elevation);
        sqlite3_bind_double(feature, 11, item.height);
        if (sqlite3_step(feature) != SQLITE_DONE) featuresSaved = false;
    }
    sqlite3_finalize(feature);
    if (!featuresSaved || !execute(database, "COMMIT")) {
        execute(database, "ROLLBACK");
        return false;
    }
    return true;
}

bool MapRepository::readFloorMetadata(int floorId, MapFloorInfo& floor) {
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT f.id,f.building_id,f.label,f.description,m.floor_id,m.coordinate_system,m.unit_label,"
        "m.width,m.height,m.meters_per_unit,m.elevation,m.floor_height,m.verification_status,m.source_reference "
        "FROM floors f JOIN buildings b ON b.id=f.building_id "
        "LEFT JOIN floor_map_metadata m ON m.floor_id=f.id WHERE f.id=? AND b.is_active=1";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, floorId);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readFloorInfo(statement, floor);
    sqlite3_finalize(statement);
    return found;
}

bool MapRepository::listBuildings(bool includeInactive, std::vector<MapBuilding>& buildings) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    const char* activeSql =
        "SELECT b.id,b.code,b.name,b.description,b.is_active,COUNT(DISTINCT f.id),"
        "COUNT(DISTINCT m.floor_id) FROM buildings b "
        "LEFT JOIN floors f ON f.building_id=b.id "
        "LEFT JOIN floor_map_metadata m ON m.floor_id=f.id "
        "WHERE b.is_active=1 GROUP BY b.id ORDER BY b.name";
    const char* allSql =
        "SELECT b.id,b.code,b.name,b.description,b.is_active,COUNT(DISTINCT f.id),"
        "COUNT(DISTINCT m.floor_id) FROM buildings b "
        "LEFT JOIN floors f ON f.building_id=b.id "
        "LEFT JOIN floor_map_metadata m ON m.floor_id=f.id "
        "GROUP BY b.id ORDER BY b.name";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, includeInactive ? allSql : activeSql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        MapBuilding item;
        item.building.id = sqlite3_column_int(statement, 0);
        item.building.code = textColumn(statement, 1);
        item.building.name = textColumn(statement, 2);
        item.building.description = textColumn(statement, 3);
        item.building.isActive = sqlite3_column_int(statement, 4) != 0;
        item.floorCount = sqlite3_column_int(statement, 5);
        item.mappedFloorCount = sqlite3_column_int(statement, 6);
        buildings.push_back(item);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool MapRepository::listFloors(int buildingId, std::vector<MapFloorInfo>& floors, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    error.clear();
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT f.id,f.building_id,f.label,f.description,m.floor_id,m.coordinate_system,m.unit_label,"
        "m.width,m.height,m.meters_per_unit,m.elevation,m.floor_height,m.verification_status,m.source_reference "
        "FROM floors f JOIN buildings b ON b.id=f.building_id "
        "LEFT JOIN floor_map_metadata m ON m.floor_id=f.id "
        "WHERE f.building_id=? AND b.is_active=1 ORDER BY f.id";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, buildingId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        MapFloorInfo floor;
        readFloorInfo(statement, floor);
        floors.push_back(floor);
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        error = "database_error";
        return false;
    }
    if (floors.empty()) {
        sqlite3_stmt* building = nullptr;
        if (sqlite3_prepare_v2(database, "SELECT 1 FROM buildings WHERE id=? AND is_active=1", -1, &building, nullptr) != SQLITE_OK) {
            error = "database_error";
            return false;
        }
        sqlite3_bind_int(building, 1, buildingId);
        bool exists = sqlite3_step(building) == SQLITE_ROW;
        sqlite3_finalize(building);
        if (!exists) error = "not_found";
    }
    return error.empty();
}

bool MapRepository::getFloorFeatures(int floorId, MapFloorData& data, std::string& error, bool includeInactive) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    error.clear();
    if (!readFloorMetadata(floorId, data.floor)) {
        error = "not_found";
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const char* activeFeatureSql =
        "SELECT id,feature_key,feature_type,label,classroom_id,navigation_location_id,edge_from_id,edge_to_id,"
        "geometry_type,geometry_json,elevation,height,thickness,is_demo,is_active "
        "FROM map_features WHERE floor_id=? AND is_active=1 ORDER BY id";
    const char* allFeatureSql =
        "SELECT id,feature_key,feature_type,label,classroom_id,navigation_location_id,edge_from_id,edge_to_id,"
        "geometry_type,geometry_json,elevation,height,thickness,is_demo,is_active "
        "FROM map_features WHERE floor_id=? ORDER BY id";
    if (sqlite3_prepare_v2(database, includeInactive ? allFeatureSql : activeFeatureSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, floorId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        MapFeature feature;
        feature.id = sqlite3_column_int(statement, 0);
        feature.floorId = floorId;
        feature.buildingId = data.floor.buildingId;
        feature.key = textColumn(statement, 1);
        feature.type = textColumn(statement, 2);
        feature.label = textColumn(statement, 3);
        feature.classroomId = sqlite3_column_type(statement, 4) == SQLITE_NULL ? 0 : sqlite3_column_int(statement, 4);
        feature.navigationLocationId = sqlite3_column_type(statement, 5) == SQLITE_NULL ? -1 : sqlite3_column_int(statement, 5);
        feature.edgeFromId = sqlite3_column_type(statement, 6) == SQLITE_NULL ? -1 : sqlite3_column_int(statement, 6);
        feature.edgeToId = sqlite3_column_type(statement, 7) == SQLITE_NULL ? -1 : sqlite3_column_int(statement, 7);
        feature.geometryType = textColumn(statement, 8);
        try {
            Json geometry = Json::parse(textColumn(statement, 9));
            for (const auto& point : geometry) {
                feature.points.push_back({point.at(0).get<double>(), point.at(1).get<double>()});
            }
        } catch (...) {
            sqlite3_finalize(statement);
            error = "invalid_stored_geometry";
            return false;
        }
        feature.elevation = sqlite3_column_double(statement, 10);
        feature.height = sqlite3_column_double(statement, 11);
        feature.thickness = sqlite3_column_double(statement, 12);
        feature.isDemo = sqlite3_column_int(statement, 13) != 0;
        feature.isActive = sqlite3_column_int(statement, 14) != 0;
        data.features.push_back(std::move(feature));
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        error = "database_error";
        return false;
    }

    const char* locationSql =
        "SELECT id,location_key,location_type,label,building_id,floor_id,x,y,is_demo,is_active "
        "FROM navigation_locations WHERE floor_id=? AND is_active=1 ORDER BY id";
    if (sqlite3_prepare_v2(database, locationSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, floorId);
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        GraphVertex location;
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
        data.navigationLocations.push_back(std::move(location));
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        error = "database_error";
        return false;
    }
    return true;
}

bool MapRepository::getRoomDetails(int roomId, MapRoomDetails& details, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    error.clear();
    sqlite3_stmt* statement = nullptr;
    const char* roomSql =
        "SELECT c.id,c.building_id,b.name,c.floor_id,f.label,c.room_code,c.room_name,c.room_type,"
        "c.capacity,c.capacity_known,c.is_active,c.notes FROM classrooms c "
        "JOIN buildings b ON b.id=c.building_id JOIN floors f ON f.id=c.floor_id "
        "WHERE c.id=? AND c.is_active=1 AND b.is_active=1";
    if (sqlite3_prepare_v2(database, roomSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, roomId);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readClassroom(statement, details.room);
    sqlite3_finalize(statement);
    if (!found) {
        error = "not_found";
        return false;
    }

    const char* sectionSql =
        "SELECT s.id,s.course_code,s.course_name,s.section_code,s.term,s.day,s.start_time,s.end_time,"
        "CASE WHEN s.enrollment_capacity IS NOT NULL AND s.enrollment_capacity<COALESCE("
        "(SELECT MIN(mc.capacity) FROM academic_section_meetings cm JOIN classrooms mc ON mc.id=cm.classroom_id "
        "WHERE cm.section_id=s.id AND LOWER(cm.venue_label)<>'online' "
        "AND mc.capacity_known=1 AND mc.is_active=1),c.capacity) "
        "THEN s.enrollment_capacity ELSE COALESCE((SELECT MIN(mc.capacity) "
        "FROM academic_section_meetings cm JOIN classrooms mc ON mc.id=cm.classroom_id "
        "WHERE cm.section_id=s.id AND LOWER(cm.venue_label)<>'online' "
        "AND mc.capacity_known=1 AND mc.is_active=1),c.capacity) END,"
        "(SELECT COUNT(*) FROM enrollments e WHERE e.section_id=s.id AND e.status='ENROLLED') "
        "FROM classrooms c JOIN academic_sections s ON s.is_active=1 "
        "LEFT JOIN academic_section_meetings m ON m.section_id=s.id AND m.classroom_id=c.id "
        "WHERE c.id=? AND c.capacity_known=1 AND "
        "(m.id IS NOT NULL OR (NOT EXISTS (SELECT 1 FROM academic_section_meetings any_m "
        "WHERE any_m.section_id=s.id) AND s.classroom_id=c.id)) AND NOT EXISTS ("
        "SELECT 1 FROM academic_section_meetings unknown_m "
        "LEFT JOIN classrooms unknown_c ON unknown_c.id=unknown_m.classroom_id "
        "WHERE unknown_m.section_id=s.id AND LOWER(unknown_m.venue_label)<>'online' "
        "AND (unknown_c.id IS NULL OR unknown_c.capacity_known=0 OR unknown_c.is_active=0)) "
        "GROUP BY s.id "
        "ORDER BY s.term,s.course_code,s.section_code";
    if (sqlite3_prepare_v2(database, sectionSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, roomId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        MapSeatSection section;
        section.id = sqlite3_column_int(statement, 0);
        section.courseCode = textColumn(statement, 1);
        section.courseName = textColumn(statement, 2);
        section.sectionCode = textColumn(statement, 3);
        section.term = textColumn(statement, 4);
        section.day = textColumn(statement, 5);
        section.startTime = textColumn(statement, 6);
        section.endTime = textColumn(statement, 7);
        section.capacity = sqlite3_column_int(statement, 8);
        section.enrolledCount = sqlite3_column_int(statement, 9);
        section.remainingSeats = std::max(0, section.capacity - section.enrolledCount);
        details.sections.push_back(std::move(section));
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        error = "database_error";
        return false;
    }
    return true;
}

bool MapRepository::saveFloorMetadata(const MapFloorInfo& metadata, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    error.clear();
    int buildingId = 0;
    if (!floorExists(database, metadata.id, &buildingId)) {
        error = "not_found";
        return false;
    }
    if (metadata.buildingId != buildingId || metadata.width <= 0 || metadata.height <= 0 ||
        metadata.width > 100000 || metadata.height > 100000 ||
        !std::isfinite(metadata.width) || !std::isfinite(metadata.height) ||
        !std::isfinite(metadata.elevation) || !std::isfinite(metadata.floorHeight) ||
        metadata.floorHeight <= 0 || metadata.unitLabel.empty() || metadata.unitLabel.size() > 32 ||
        metadata.coordinateSystem != "LOCAL_FLOOR_X_RIGHT_Y_DOWN" ||
        !hasValue(metadata.verificationStatus, validVerificationStatuses,
                  sizeof(validVerificationStatuses) / sizeof(validVerificationStatuses[0])) ||
        (metadata.scaleKnown && (!std::isfinite(metadata.metersPerUnit) || metadata.metersPerUnit <= 0)) ||
        (metadata.verificationStatus == "VERIFIED" && metadata.sourceReference.empty()) ||
        metadata.sourceReference.size() > 500) {
        error = "invalid_metadata";
        return false;
    }
    if (metadata.verificationStatus == "VERIFIED") {
        sqlite3_stmt* demoData = nullptr;
        const char* sql =
            "SELECT 1 FROM map_features f WHERE f.floor_id=? AND f.is_demo=1 AND f.is_active=1 "
            "UNION ALL SELECT 1 FROM navigation_locations n WHERE n.floor_id=? AND n.is_demo=1 "
            "AND n.is_active=1 LIMIT 1";
        if (sqlite3_prepare_v2(database, sql, -1, &demoData, nullptr) != SQLITE_OK) {
            error = "database_error";
            return false;
        }
        sqlite3_bind_int(demoData, 1, metadata.id);
        sqlite3_bind_int(demoData, 2, metadata.id);
        bool containsDemo = sqlite3_step(demoData) == SQLITE_ROW;
        sqlite3_finalize(demoData);
        if (containsDemo) {
            error = "demo_data_not_verified";
            return false;
        }
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO floor_map_metadata(floor_id,building_id,coordinate_system,unit_label,width,height,"
        "meters_per_unit,elevation,floor_height,verification_status,source_reference) VALUES(?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(floor_id) DO UPDATE SET building_id=excluded.building_id,"
        "coordinate_system=excluded.coordinate_system,unit_label=excluded.unit_label,width=excluded.width,"
        "height=excluded.height,meters_per_unit=excluded.meters_per_unit,elevation=excluded.elevation,"
        "floor_height=excluded.floor_height,verification_status=excluded.verification_status,"
        "source_reference=excluded.source_reference,updated_at=CURRENT_TIMESTAMP";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, metadata.id);
    sqlite3_bind_int(statement, 2, buildingId);
    bindText(statement, 3, metadata.coordinateSystem);
    bindText(statement, 4, metadata.unitLabel);
    sqlite3_bind_double(statement, 5, metadata.width);
    sqlite3_bind_double(statement, 6, metadata.height);
    if (metadata.scaleKnown) sqlite3_bind_double(statement, 7, metadata.metersPerUnit); else sqlite3_bind_null(statement, 7);
    sqlite3_bind_double(statement, 8, metadata.elevation);
    sqlite3_bind_double(statement, 9, metadata.floorHeight);
    bindText(statement, 10, metadata.verificationStatus);
    bindText(statement, 11, metadata.sourceReference);
    bool saved = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!saved) error = "database_error";
    return saved;
}

bool MapRepository::validateFeature(const MapFeature& feature, const MapFloorInfo& floor, std::string& error) {
    error.clear();
    if (!floor.hasMap || feature.floorId != floor.id || feature.buildingId != floor.buildingId ||
        feature.key.empty() || feature.key.size() > 100 || feature.label.empty() || feature.label.size() > 200 ||
        !hasValue(feature.type, validFeatureTypes, sizeof(validFeatureTypes) / sizeof(validFeatureTypes[0])) ||
        !hasValue(feature.geometryType, validGeometryTypes, sizeof(validGeometryTypes) / sizeof(validGeometryTypes[0])) ||
        feature.points.size() > 500 || !std::isfinite(feature.elevation) ||
        !std::isfinite(feature.height) || feature.height < 0 ||
        !std::isfinite(feature.thickness) || feature.thickness < 0) {
        error = "invalid_feature";
        return false;
    }
    std::size_t minimumPoints = feature.geometryType == "POLYGON" ? 3 :
                                feature.geometryType == "LINE" ? 2 : 1;
    if (feature.points.size() < minimumPoints ||
        (feature.geometryType == "POINT" && feature.points.size() != 1)) {
        error = "invalid_feature_geometry";
        return false;
    }
    for (const auto& point : feature.points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            point.x < 0 || point.y < 0 || point.x > floor.width || point.y > floor.height) {
            error = "invalid_feature_geometry";
            return false;
        }
    }
    if (feature.type == "ROOM" &&
        (feature.classroomId <= 0 || feature.geometryType != "POLYGON")) {
        error = "room_feature_requires_classroom";
        return false;
    }
    if (feature.classroomId > 0) {
        sqlite3_stmt* room = nullptr;
        if (sqlite3_prepare_v2(database,
                "SELECT 1 FROM classrooms WHERE id=? AND floor_id=? AND building_id=?",
                -1, &room, nullptr) != SQLITE_OK) {
            error = "database_error";
            return false;
        }
        sqlite3_bind_int(room, 1, feature.classroomId);
        sqlite3_bind_int(room, 2, floor.id);
        sqlite3_bind_int(room, 3, floor.buildingId);
        bool valid = sqlite3_step(room) == SQLITE_ROW;
        sqlite3_finalize(room);
        if (!valid) {
            error = "room_location_mismatch";
            return false;
        }
    }
    if (feature.navigationLocationId >= 0) {
        sqlite3_stmt* location = nullptr;
        if (sqlite3_prepare_v2(database,
                "SELECT 1 FROM navigation_locations WHERE id=? AND floor_id=? AND building_id=?",
                -1, &location, nullptr) != SQLITE_OK) {
            error = "database_error";
            return false;
        }
        sqlite3_bind_int(location, 1, feature.navigationLocationId);
        sqlite3_bind_int(location, 2, floor.id);
        sqlite3_bind_int(location, 3, floor.buildingId);
        bool valid = sqlite3_step(location) == SQLITE_ROW;
        sqlite3_finalize(location);
        if (!valid) {
            error = "navigation_location_mismatch";
            return false;
        }
    }
    bool hasFrom = feature.edgeFromId >= 0;
    bool hasTo = feature.edgeToId >= 0;
    if (hasFrom != hasTo || (hasFrom && (feature.edgeFromId == feature.edgeToId ||
                                         feature.geometryType != "LINE"))) {
        error = "invalid_feature_edge";
        return false;
    }
    if (hasFrom) {
        int from = std::min(feature.edgeFromId, feature.edgeToId);
        int to = std::max(feature.edgeFromId, feature.edgeToId);
        sqlite3_stmt* edge = nullptr;
        if (sqlite3_prepare_v2(database,
                "SELECT 1 FROM navigation_edges e "
                "JOIN navigation_locations a ON a.id=e.from_location_id "
                "JOIN navigation_locations b ON b.id=e.to_location_id "
                "WHERE e.from_location_id=? AND e.to_location_id=? "
                "AND a.floor_id=? AND b.floor_id=?",
                -1, &edge, nullptr) != SQLITE_OK) {
            error = "database_error";
            return false;
        }
        sqlite3_bind_int(edge, 1, from);
        sqlite3_bind_int(edge, 2, to);
        sqlite3_bind_int(edge, 3, floor.id);
        sqlite3_bind_int(edge, 4, floor.id);
        bool valid = sqlite3_step(edge) == SQLITE_ROW;
        sqlite3_finalize(edge);
        if (!valid) {
            error = "navigation_edge_mismatch";
            return false;
        }
    }
    return true;
}

bool MapRepository::saveFeature(const MapFeature& feature, MapFeature& saved, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    error.clear();
    MapFloorInfo floor;
    if (!readFloorMetadata(feature.floorId, floor)) {
        error = "not_found";
        return false;
    }
    MapFeature normalized = feature;
    normalized.buildingId = floor.buildingId;
    if (feature.id == 0) {
        std::string baseKey = normalized.key.substr(0, 90);
        std::string candidate = baseKey;
        for (int suffix = 2; suffix < 10000; ++suffix) {
            sqlite3_stmt* keyCheck = nullptr;
            if (sqlite3_prepare_v2(database,
                    "SELECT 1 FROM map_features WHERE feature_key=?", -1, &keyCheck, nullptr) != SQLITE_OK) {
                error = "database_error";
                return false;
            }
            bindText(keyCheck, 1, candidate);
            bool exists = sqlite3_step(keyCheck) == SQLITE_ROW;
            sqlite3_finalize(keyCheck);
            if (!exists) break;
            candidate = baseKey + "-" + std::to_string(suffix);
        }
        normalized.key = candidate;
    }
    if (!validateFeature(normalized, floor, error)) return false;
    Json geometry = Json::array();
    for (const auto& point : normalized.points) geometry.push_back({point.x, point.y});
    sqlite3_stmt* statement = nullptr;
    const char* insertSql =
        "INSERT INTO map_features(feature_key,building_id,floor_id,classroom_id,navigation_location_id,"
        "edge_from_id,edge_to_id,feature_type,label,geometry_type,geometry_json,elevation,height,thickness,is_demo,is_active) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    const char* updateSql =
        "UPDATE map_features SET feature_key=?,building_id=?,floor_id=?,classroom_id=?,navigation_location_id=?,"
        "edge_from_id=?,edge_to_id=?,feature_type=?,label=?,geometry_type=?,geometry_json=?,elevation=?,height=?,"
        "thickness=?,is_demo=? WHERE id=?";
    const bool isUpdate = feature.id > 0;
    if (sqlite3_prepare_v2(database, isUpdate ? updateSql : insertSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    bindText(statement, 1, normalized.key);
    sqlite3_bind_int(statement, 2, floor.buildingId);
    sqlite3_bind_int(statement, 3, floor.id);
    if (normalized.classroomId > 0) sqlite3_bind_int(statement, 4, normalized.classroomId); else sqlite3_bind_null(statement, 4);
    if (normalized.navigationLocationId >= 0) sqlite3_bind_int(statement, 5, normalized.navigationLocationId); else sqlite3_bind_null(statement, 5);
    if (normalized.edgeFromId >= 0) {
        sqlite3_bind_int(statement, 6, std::min(normalized.edgeFromId, normalized.edgeToId));
        sqlite3_bind_int(statement, 7, std::max(normalized.edgeFromId, normalized.edgeToId));
    } else {
        sqlite3_bind_null(statement, 6);
        sqlite3_bind_null(statement, 7);
    }
    bindText(statement, 8, normalized.type);
    bindText(statement, 9, normalized.label);
    bindText(statement, 10, normalized.geometryType);
    bindText(statement, 11, geometry.dump());
    sqlite3_bind_double(statement, 12, normalized.elevation);
    sqlite3_bind_double(statement, 13, normalized.height);
    sqlite3_bind_double(statement, 14, normalized.thickness);
    sqlite3_bind_int(statement, 15, normalized.isDemo ? 1 : 0);
    if (isUpdate) sqlite3_bind_int(statement, 16, normalized.id);
    bool ok = sqlite3_step(statement) == SQLITE_DONE && (!isUpdate || sqlite3_changes(database) == 1);
    int resultCode = sqlite3_extended_errcode(database);
    int id = isUpdate ? feature.id : static_cast<int>(sqlite3_last_insert_rowid(database));
    sqlite3_finalize(statement);
    if (!ok) {
        if ((resultCode & 0xFF) == SQLITE_CONSTRAINT) error = "duplicate_or_invalid_feature";
        else if (isUpdate && resultCode == SQLITE_OK) error = "not_found";
        else error = "database_error";
        return false;
    }
    saved = normalized;
    saved.id = id;
    saved.buildingId = floor.buildingId;
    saved.floorId = floor.id;
    return true;
}

bool MapRepository::setFeatureActive(int featureId, bool isActive, std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    error.clear();
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "UPDATE map_features SET is_active=? WHERE id=?", -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, isActive ? 1 : 0);
    sqlite3_bind_int(statement, 2, featureId);
    bool updated = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!updated) error = "not_found";
    return updated;
}
