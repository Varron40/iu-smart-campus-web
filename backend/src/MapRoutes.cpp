#include "api/ApiHelpers.h"
#include "api/ModuleRoutes.h"
#include "data_access/MapRepository.h"
#include <cstddef>

namespace {
using Api::Json;

int capturedId(const httplib::Request& request, std::size_t capture = 1) {
    try {
        std::string value = request.matches[capture].str();
        std::size_t parsed = 0;
        int id = std::stoi(value, &parsed);
        return parsed == value.size() && id > 0 ? id : 0;
    } catch (...) {
        return 0;
    }
}

Json buildingJson(const MapBuilding& item) {
    return {
        {"id", item.building.id}, {"code", item.building.code}, {"name", item.building.name},
        {"description", item.building.description}, {"isActive", item.building.isActive},
        {"floorCount", item.floorCount}, {"mappedFloorCount", item.mappedFloorCount}
    };
}

Json floorJson(const MapFloorInfo& floor) {
    return {
        {"id", floor.id}, {"buildingId", floor.buildingId}, {"label", floor.label},
        {"description", floor.description}, {"hasMap", floor.hasMap},
        {"coordinateSystem", floor.coordinateSystem},
        {"unitLabel", floor.scaleKnown ? floor.unitLabel : "local units"},
        {"width", floor.hasMap ? Json(floor.width) : Json(nullptr)},
        {"height", floor.hasMap ? Json(floor.height) : Json(nullptr)},
        {"metersPerUnit", floor.scaleKnown ? Json(floor.metersPerUnit) : Json(nullptr)},
        {"elevation", floor.hasMap ? Json(floor.elevation) : Json(nullptr)},
        {"floorHeight", floor.hasMap ? Json(floor.floorHeight) : Json(nullptr)},
        {"verificationStatus", floor.verificationStatus}, {"sourceReference", floor.sourceReference},
        {"isDemo", floor.verificationStatus == "DEMO"}
    };
}

Json featureJson(const MapFeature& feature) {
    Json points = Json::array();
    for (const auto& point : feature.points) points.push_back({{"x", point.x}, {"y", point.y}});
    return {
        {"id", feature.id}, {"key", feature.key}, {"type", feature.type}, {"label", feature.label},
        {"floorId", feature.floorId}, {"buildingId", feature.buildingId},
        {"classroomId", feature.classroomId ? Json(feature.classroomId) : Json(nullptr)},
        {"navigationLocationId", feature.navigationLocationId >= 0 ? Json(feature.navigationLocationId) : Json(nullptr)},
        {"edgeFromId", feature.edgeFromId >= 0 ? Json(feature.edgeFromId) : Json(nullptr)},
        {"edgeToId", feature.edgeToId >= 0 ? Json(feature.edgeToId) : Json(nullptr)},
        {"geometryType", feature.geometryType}, {"points", points},
        {"elevation", feature.elevation}, {"height", feature.height}, {"thickness", feature.thickness},
        {"isDemo", feature.isDemo}, {"isActive", feature.isActive}
    };
}

Json locationJson(const GraphVertex& location) {
    Json coordinates = location.hasCoordinates ? Json{{"x", location.x}, {"y", location.y}} : Json(nullptr);
    return {
        {"id", location.id}, {"key", location.locationKey}, {"type", location.locationType},
        {"label", location.label}, {"buildingId", location.buildingId ? Json(location.buildingId) : Json(nullptr)},
        {"floorId", location.floorId ? Json(location.floorId) : Json(nullptr)},
        {"coordinates", coordinates}, {"isDemo", location.isDemo}, {"isActive", location.isActive}
    };
}

Json roomJson(const MapRoomDetails& details) {
    const Classroom& room = details.room;
    Json sections = Json::array();
    for (const auto& section : details.sections) {
        sections.push_back({
            {"id", section.id}, {"courseCode", section.courseCode}, {"courseName", section.courseName},
            {"sectionCode", section.sectionCode}, {"term", section.term}, {"day", section.day},
            {"startTime", section.startTime}, {"endTime", section.endTime},
            {"capacity", section.capacity}, {"enrolledCount", section.enrolledCount},
            {"remainingSeats", section.remainingSeats}
        });
    }
    return {
        {"id", room.id}, {"buildingId", room.buildingId}, {"buildingName", room.buildingName},
        {"floorId", room.floorId}, {"floorLabel", room.floorLabel}, {"roomCode", room.roomCode},
        {"roomName", room.roomName}, {"roomType", room.roomType},
        {"capacity", room.capacityKnown ? Json(room.capacity) : Json(nullptr)},
        {"capacityKnown", room.capacityKnown},
        {"isActive", room.isActive}, {"notes", room.notes}, {"occupancy", nullptr},
        {"seatAvailabilityAvailable", !details.sections.empty()}, {"sections", sections}
    };
}

void sendMapError(httplib::Response& response, const std::string& error) {
    if (error == "not_found") return Api::sendJson(response, 404, {{"error", "Map, floor, or active room not found"}});
    if (error == "invalid_metadata")
        return Api::sendJson(response, 400, {{"error", "Map dimensions, coordinate system, scale, or source metadata are invalid"}});
    if (error == "invalid_feature" || error == "invalid_feature_geometry" ||
        error == "invalid_feature_edge" || error == "room_feature_requires_classroom")
        return Api::sendJson(response, 400, {{"error", "Feature geometry or linked entity is invalid"}});
    if (error == "room_location_mismatch" || error == "navigation_location_mismatch" ||
        error == "navigation_edge_mismatch" || error == "duplicate_or_invalid_feature")
        return Api::sendJson(response, 409, {{"error", "Feature key or linked room/navigation data conflicts with this floor"}});
    if (error == "demo_data_not_verified")
        return Api::sendJson(response, 409, {{"error", "Replace or deactivate all synthetic map and navigation data before verifying this floor"}});
    if (error == "invalid_stored_geometry")
        return Api::sendJson(response, 500, {{"error", "Stored map geometry is invalid and requires administrator repair"}});
    Api::sendJson(response, 500, {{"error", "Map data could not be loaded or saved"}});
}

int authorizeAdmin(const httplib::Request& request, httplib::Response& response,
                   StudentRepository& students, SessionStore& sessions) {
    int userId = 0;
    int status = Api::authorize(request, sessions, students, userId, true);
    if (status) Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
    return status;
}

bool readFeatureBody(const Json& body, int floorId, MapFeature& feature) {
    if (!body.is_object() || !body.contains("key") || !body["key"].is_string() ||
        !body.contains("type") || !body["type"].is_string() ||
        !body.contains("label") || !body["label"].is_string() ||
        !body.contains("geometryType") || !body["geometryType"].is_string() ||
        !body.contains("points") || !body["points"].is_array()) return false;
    feature.floorId = floorId;
    feature.key = body["key"].get<std::string>();
    feature.type = body["type"].get<std::string>();
    feature.label = body["label"].get<std::string>();
    feature.geometryType = body["geometryType"].get<std::string>();
    for (const auto& point : body["points"]) {
        if (!point.is_object() || !point.contains("x") || !point.contains("y") ||
            !point["x"].is_number() || !point["y"].is_number()) return false;
        feature.points.push_back({point["x"].get<double>(), point["y"].get<double>()});
    }
    auto optionalId = [&body](const char* name, int& value) {
        if (!body.contains(name) || body[name].is_null()) return true;
        if (!body[name].is_number_integer()) return false;
        value = body[name].get<int>();
        return value >= 0;
    };
    if (!optionalId("classroomId", feature.classroomId) ||
        !optionalId("navigationLocationId", feature.navigationLocationId) ||
        !optionalId("edgeFromId", feature.edgeFromId) ||
        !optionalId("edgeToId", feature.edgeToId)) return false;
    if (body.contains("elevation") && !body["elevation"].is_number()) return false;
    if (body.contains("height") && !body["height"].is_number()) return false;
    if (body.contains("thickness") && !body["thickness"].is_number()) return false;
    feature.elevation = body.value("elevation", 0.0);
    feature.height = body.value("height", 0.0);
    feature.thickness = body.value("thickness", 0.0);
    if (body.contains("isDemo") && !body["isDemo"].is_boolean()) return false;
    feature.isDemo = body.value("isDemo", true);
    return true;
}
}

void registerMapRoutes(httplib::Server& server, MapRepository& maps,
                       StudentRepository& students, SessionStore& sessions) {
    server.Get("/api/map/buildings", [&maps, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int userId = 0;
        int status = Api::authorize(request, sessions, students, userId);
        if (status) return Api::sendJson(response, status, {{"error", "Unauthenticated"}});
        bool includeInactive = false;
        if (request.has_param("includeInactive") && request.get_param_value("includeInactive") == "true") {
            Student viewer;
            if (!students.findById(userId, viewer) || viewer.role != "admin")
                return Api::sendJson(response, 403, {{"error", "Admin role required"}});
            includeInactive = true;
        }
        std::vector<MapBuilding> buildings;
        if (!maps.listBuildings(includeInactive, buildings))
            return Api::sendJson(response, 500, {{"error", "Buildings could not be loaded"}});
        Json result = Json::array();
        for (const auto& building : buildings) result.push_back(buildingJson(building));
        Api::sendJson(response, 200, {{"buildings", result}});
    });

    server.Get(R"(/api/map/buildings/([0-9]+)/floors)", [&maps, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int userId = 0;
        int status = Api::authorize(request, sessions, students, userId);
        if (status) return Api::sendJson(response, status, {{"error", "Unauthenticated"}});
        int buildingId = capturedId(request);
        if (!buildingId) return Api::sendJson(response, 400, {{"error", "Invalid building ID"}});
        std::vector<MapFloorInfo> floors;
        std::string error;
        if (!maps.listFloors(buildingId, floors, error)) return sendMapError(response, error);
        Json result = Json::array();
        for (const auto& floor : floors) result.push_back(floorJson(floor));
        Api::sendJson(response, 200, {{"floors", result}});
    });

    server.Get(R"(/api/map/floors/([0-9]+)/features)", [&maps, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int userId = 0;
        int status = Api::authorize(request, sessions, students, userId);
        if (status) return Api::sendJson(response, status, {{"error", "Unauthenticated"}});
        int floorId = capturedId(request);
        if (!floorId) return Api::sendJson(response, 400, {{"error", "Invalid floor ID"}});
        bool includeInactive = request.has_param("includeInactive") &&
                               request.get_param_value("includeInactive") == "true";
        if (includeInactive && authorizeAdmin(request, response, students, sessions)) return;
        MapFloorData data;
        std::string error;
        if (!maps.getFloorFeatures(floorId, data, error, includeInactive)) return sendMapError(response, error);
        Json features = Json::array();
        for (const auto& feature : data.features) features.push_back(featureJson(feature));
        Json locations = Json::array();
        for (const auto& location : data.navigationLocations) locations.push_back(locationJson(location));
        Api::sendJson(response, 200, {{"floor", floorJson(data.floor)}, {"features", features},
                                     {"navigationLocations", locations}});
    });

    server.Get(R"(/api/map/rooms/([0-9]+))", [&maps, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int userId = 0;
        int status = Api::authorize(request, sessions, students, userId);
        if (status) return Api::sendJson(response, status, {{"error", "Unauthenticated"}});
        int roomId = capturedId(request);
        if (!roomId) return Api::sendJson(response, 400, {{"error", "Invalid classroom ID"}});
        MapRoomDetails details;
        std::string error;
        if (!maps.getRoomDetails(roomId, details, error)) return sendMapError(response, error);
        Api::sendJson(response, 200, {{"room", roomJson(details)}});
    });

    server.Put(R"(/api/admin/map/floors/([0-9]+)/metadata)", [&maps, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        if (authorizeAdmin(request, response, students, sessions)) return;
        int floorId = capturedId(request);
        if (!floorId) return Api::sendJson(response, 400, {{"error", "Invalid floor ID"}});
        try {
            Json body = Json::parse(request.body);
            if (!body.is_object() || !body.contains("width") || !body["width"].is_number() ||
                !body.contains("height") || !body["height"].is_number() ||
                !body.contains("buildingId") || !body["buildingId"].is_number_integer())
                return Api::sendJson(response, 400, {{"error", "buildingId, width, and height are required"}});
            MapFloorInfo metadata;
            metadata.id = floorId;
            metadata.buildingId = body["buildingId"].get<int>();
            metadata.width = body["width"].get<double>();
            metadata.height = body["height"].get<double>();
            metadata.coordinateSystem = body.value("coordinateSystem", "LOCAL_FLOOR_X_RIGHT_Y_DOWN");
            metadata.unitLabel = body.value("unitLabel", "local units");
            metadata.elevation = body.value("elevation", 0.0);
            metadata.floorHeight = body.value("floorHeight", 3.0);
            metadata.verificationStatus = body.value("verificationStatus", "UNVERIFIED");
            metadata.sourceReference = body.value("sourceReference", "");
            if (body.contains("metersPerUnit") && !body["metersPerUnit"].is_null()) {
                if (!body["metersPerUnit"].is_number())
                    return Api::sendJson(response, 400, {{"error", "metersPerUnit must be numeric or null"}});
                metadata.scaleKnown = true;
                metadata.metersPerUnit = body["metersPerUnit"].get<double>();
            }
            metadata.hasMap = true;
            std::string error;
            if (!maps.saveFloorMetadata(metadata, error)) return sendMapError(response, error);
            Api::sendJson(response, 200, {{"floor", floorJson(metadata)}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid floor map metadata"}});
        }
    });

    auto saveFeature = [&maps, &students, &sessions](const httplib::Request& request,
                                                    httplib::Response& response, int floorId, int featureId) {
        if (authorizeAdmin(request, response, students, sessions)) return;
        if (floorId <= 0) return Api::sendJson(response, 400, {{"error", "Invalid floor ID"}});
        try {
            Json body = Json::parse(request.body);
            MapFeature feature;
            feature.id = featureId;
            if (!readFeatureBody(body, floorId, feature))
                return Api::sendJson(response, 400, {{"error", "Feature requires valid key, label, type, geometry type, and point coordinates"}});
            MapFeature saved;
            std::string error;
            if (!maps.saveFeature(feature, saved, error)) return sendMapError(response, error);
            Api::sendJson(response, featureId ? 200 : 201, {{"feature", featureJson(saved)}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid map feature request"}});
        }
    };

    server.Post(R"(/api/admin/map/floors/([0-9]+)/features)", [saveFeature](const httplib::Request& request, httplib::Response& response) {
        saveFeature(request, response, capturedId(request), 0);
    });

    server.Put(R"(/api/admin/map/floors/([0-9]+)/features/([0-9]+))", [saveFeature](const httplib::Request& request, httplib::Response& response) {
        int floorId = capturedId(request);
        int featureId = capturedId(request, 2);
        if (!floorId || !featureId) return Api::sendJson(response, 400, {{"error", "Invalid floor or feature ID"}});
        saveFeature(request, response, floorId, featureId);
    });

    server.Patch(R"(/api/admin/map/features/([0-9]+)/status)", [&maps, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        if (authorizeAdmin(request, response, students, sessions)) return;
        int featureId = capturedId(request);
        if (!featureId) return Api::sendJson(response, 400, {{"error", "Invalid feature ID"}});
        try {
            Json body = Json::parse(request.body);
            if (!body.contains("isActive") || !body["isActive"].is_boolean())
                return Api::sendJson(response, 400, {{"error", "isActive must be boolean"}});
            std::string error;
            if (!maps.setFeatureActive(featureId, body["isActive"].get<bool>(), error))
                return sendMapError(response, error);
            Api::sendJson(response, 200, {{"message", "Map feature status updated"}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid map feature status request"}});
        }
    });
}
