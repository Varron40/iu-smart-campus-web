#include "api/ApiHelpers.h"
#include "api/ModuleRoutes.h"
#include "data_access/NavigationRepository.h"
#include <cmath>
#include <regex>

namespace {
using Api::Json;

int routeId(const httplib::Request& request, std::size_t capture = 1) {
    try {
        std::string value = request.matches[capture].str();
        std::size_t parsed = 0;
        int id = std::stoi(value, &parsed);
        return parsed == value.size() && id >= 0 ? id : -1;
    } catch (...) {
        return -1;
    }
}

Json locationJson(const GraphVertex& location) {
    Json result = {
        {"id", location.id}, {"key", location.locationKey}, {"type", location.locationType},
        {"label", location.label}, {"buildingId", location.buildingId ? Json(location.buildingId) : Json(nullptr)},
        {"floorId", location.floorId ? Json(location.floorId) : Json(nullptr)},
        {"isDemo", location.isDemo}, {"isActive", location.isActive}
    };
    result["coordinates"] = location.hasCoordinates ? Json{{"x", location.x}, {"y", location.y}} : Json(nullptr);
    return result;
}

Json edgeJson(const NavigationEdgeRecord& edge) {
    return {{"fromId", edge.fromId}, {"toId", edge.toId}, {"weight", edge.weight},
            {"type", edge.edgeType}, {"accessibilityStatus", edge.accessibilityStatus},
            {"isActive", edge.isActive}};
}

Json routeJson(int from, int to, const NavigationPath& path) {
    Json locations = Json::array();
    for (const auto& location : path.locations) locations.push_back(locationJson(location));
    Json edges = Json::array();
    for (std::size_t index = 0; index < path.edges.size(); ++index) {
        const auto& edge = path.edges[index];
        edges.push_back({{"fromId", path.locations[index].id}, {"toId", edge.to},
                         {"weight", edge.weight}, {"type", edge.edgeType},
                         {"accessibilityStatus", edge.accessibilityStatus}});
    }
    Json pathIds = Json::array();
    for (const auto& location : path.locations) pathIds.push_back(location.id);
    Json warnings = Json::array();
    if (path.containsDemoData) warnings.push_back("This route uses synthetic demo data and is not a verified campus route.");
    if (!path.accessibilityVerified)
        warnings.push_back("Accessibility is unknown or unverified for one or more route segments.");
    return {
        {"from", from}, {"to", to}, {"distance", path.totalCost}, {"path", pathIds},
        {"totalCost", path.totalCost}, {"costUnit", "distance units"}, {"locations", locations},
        {"edges", edges}, {"isDemo", path.containsDemoData},
        {"accessibilityVerified", path.accessibilityVerified}, {"warnings", warnings}
    };
}

void sendNavigationError(httplib::Response& response, const std::string& error) {
    if (error == "location_not_found")
        return Api::sendJson(response, 404, {{"error", "Active navigation location not found"}});
    if (error == "route_unavailable")
        return Api::sendJson(response, 404, {{"error", "No active route connects these locations"}});
    if (error == "invalid_location")
        return Api::sendJson(response, 400, {{"error", "Location key, type, and label must be valid; coordinates must be finite"}});
    if (error == "duplicate_or_invalid_location")
        return Api::sendJson(response, 409, {{"error", "Location key is already in use or its building/floor is invalid"}});
    if (error == "invalid_edge")
        return Api::sendJson(response, 400, {{"error", "Edge weight must be non-negative and edge metadata must be valid"}});
    if (error == "edge_exists")
        return Api::sendJson(response, 409, {{"error", "An edge already connects these locations"}});
    if (error == "edge_not_found")
        return Api::sendJson(response, 404, {{"error", "Navigation edge not found"}});
    if (error == "not_found")
        return Api::sendJson(response, 404, {{"error", "Navigation location not found"}});
    Api::sendJson(response, 500, {{"error", "Navigation data could not be saved"}});
}

int authorizeAdmin(const httplib::Request& request, httplib::Response& response,
                   StudentRepository& students, SessionStore& sessions, int& adminId) {
    int status = Api::authorize(request, sessions, students, adminId, true);
    if (status) Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
    return status;
}
}

void registerNavigationRoutes(httplib::Server& server, NavigationRepository& navigation,
                              StudentRepository& students, SessionStore& sessions) {
    server.Get("/api/navigation/locations", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId);
        if (status) return Api::sendJson(response, status, {{"error", "Unauthenticated"}});
        bool includeInactive = false;
        if (request.has_param("includeInactive") && request.get_param_value("includeInactive") == "true") {
            Student viewer;
            if (!students.findById(studentId, viewer) || viewer.role != "admin")
                return Api::sendJson(response, 403, {{"error", "Admin role required"}});
            includeInactive = true;
        }
        std::vector<GraphVertex> locations;
        if (!navigation.listLocations(includeInactive, locations))
            return Api::sendJson(response, 500, {{"error", "Navigation locations could not be loaded"}});
        Json result = Json::array();
        for (const auto& location : locations) result.push_back(locationJson(location));
        Api::sendJson(response, 200, {{"locations", result}, {"syntheticDataMayBePresent", true}});
    });

    server.Get("/api/admin/navigation/edges", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        if (authorizeAdmin(request, response, students, sessions, adminId)) return;
        std::vector<NavigationEdgeRecord> edges;
        if (!navigation.listEdges(edges))
            return Api::sendJson(response, 500, {{"error", "Navigation edges could not be loaded"}});
        Json result = Json::array();
        for (const auto& edge : edges) result.push_back(edgeJson(edge));
        Api::sendJson(response, 200, {{"edges", result}});
    });

    server.Post("/api/admin/navigation/locations", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        if (authorizeAdmin(request, response, students, sessions, adminId)) return;
        try {
            Json body = Json::parse(request.body);
            GraphVertex location;
            location.id = -1;
            location.locationKey = body.value("key", "");
            location.locationType = body.value("type", "");
            location.label = body.value("label", "");
            location.buildingId = body.value("buildingId", 0);
            location.floorId = body.value("floorId", 0);
            if (body.contains("coordinates") && !body["coordinates"].is_null()) {
                if (!body["coordinates"].is_object() || !body["coordinates"].contains("x") ||
                    !body["coordinates"].contains("y") || !body["coordinates"]["x"].is_number() ||
                    !body["coordinates"]["y"].is_number())
                    return Api::sendJson(response, 400, {{"error", "Coordinates must contain numeric x and y values"}});
                location.hasCoordinates = true;
                location.x = body["coordinates"]["x"].get<double>();
                location.y = body["coordinates"]["y"].get<double>();
            }
            if (location.buildingId < 0 || location.floorId < 0 || (location.floorId > 0 && location.buildingId == 0))
                return Api::sendJson(response, 400, {{"error", "A floor location requires its building ID"}});
            GraphVertex saved;
            std::string error;
            if (!navigation.saveLocation(location, error, saved)) return sendNavigationError(response, error);
            Api::sendJson(response, 201, {{"location", locationJson(saved)}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid navigation location request"}});
        }
    });

    server.Put(R"(/api/admin/navigation/locations/([0-9]+))", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        if (authorizeAdmin(request, response, students, sessions, adminId)) return;
        int id = routeId(request);
        if (id < 0) return Api::sendJson(response, 400, {{"error", "Invalid location ID"}});
        try {
            Json body = Json::parse(request.body);
            GraphVertex location;
            location.id = id;
            location.locationKey = body.value("key", "");
            location.locationType = body.value("type", "");
            location.label = body.value("label", "");
            location.buildingId = body.value("buildingId", 0);
            location.floorId = body.value("floorId", 0);
            if (body.contains("coordinates") && !body["coordinates"].is_null()) {
                if (!body["coordinates"].is_object() || !body["coordinates"].contains("x") ||
                    !body["coordinates"].contains("y") || !body["coordinates"]["x"].is_number() ||
                    !body["coordinates"]["y"].is_number())
                    return Api::sendJson(response, 400, {{"error", "Coordinates must contain numeric x and y values"}});
                location.hasCoordinates = true;
                location.x = body["coordinates"]["x"].get<double>();
                location.y = body["coordinates"]["y"].get<double>();
            }
            if (location.buildingId < 0 || location.floorId < 0 || (location.floorId > 0 && location.buildingId == 0))
                return Api::sendJson(response, 400, {{"error", "A floor location requires its building ID"}});
            GraphVertex saved;
            std::string error;
            if (!navigation.saveLocation(location, error, saved)) return sendNavigationError(response, error);
            Api::sendJson(response, 200, {{"location", locationJson(saved)}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid navigation location request"}});
        }
    });

    server.Patch(R"(/api/admin/navigation/locations/([0-9]+)/status)", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        if (authorizeAdmin(request, response, students, sessions, adminId)) return;
        int id = routeId(request);
        if (id < 0) return Api::sendJson(response, 400, {{"error", "Invalid location ID"}});
        try {
            Json body = Json::parse(request.body);
            if (!body.contains("isActive") || !body["isActive"].is_boolean())
                return Api::sendJson(response, 400, {{"error", "isActive must be a boolean"}});
            std::string error;
            if (!navigation.setLocationActive(id, body["isActive"].get<bool>(), error))
                return sendNavigationError(response, error);
            Api::sendJson(response, 200, {{"message", "Navigation location status updated"}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid navigation status request"}});
        }
    });

    server.Post("/api/admin/navigation/edges", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        if (authorizeAdmin(request, response, students, sessions, adminId)) return;
        try {
            Json body = Json::parse(request.body);
            if (!body.contains("fromId") || !body["fromId"].is_number_integer() ||
                !body.contains("toId") || !body["toId"].is_number_integer() ||
                !body.contains("weight") || !body["weight"].is_number())
                return Api::sendJson(response, 400, {{"error", "fromId, toId, and numeric weight are required"}});
            std::string edgeType = body.value("type", "CORRIDOR");
            std::string accessibility = body.value("accessibilityStatus", "UNKNOWN");
            std::string error;
            if (!navigation.saveEdge(body["fromId"].get<int>(), body["toId"].get<int>(),
                                     body["weight"].get<double>(), edgeType, accessibility, error))
                return sendNavigationError(response, error);
            Api::sendJson(response, 201, {{"message", "Navigation edge created"}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid navigation edge request"}});
        }
    });

    server.Patch(R"(/api/admin/navigation/edges/([0-9]+)/([0-9]+))", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        if (authorizeAdmin(request, response, students, sessions, adminId)) return;
        int from = routeId(request, 1), to = routeId(request, 2);
        if (from < 0 || to < 0) return Api::sendJson(response, 400, {{"error", "Invalid edge endpoints"}});
        try {
            Json body = Json::parse(request.body);
            if (!body.contains("weight") || !body["weight"].is_number() ||
                !body.contains("isActive") || !body["isActive"].is_boolean())
                return Api::sendJson(response, 400, {{"error", "Numeric weight and boolean isActive are required"}});
            std::string edgeType = body.value("type", "CORRIDOR");
            std::string accessibility = body.value("accessibilityStatus", "UNKNOWN");
            std::string error;
            if (!navigation.updateEdge(from, to, body["weight"].get<double>(), edgeType,
                                       accessibility, body["isActive"].get<bool>(), error))
                return sendNavigationError(response, error);
            Api::sendJson(response, 200, {{"message", "Navigation edge updated"}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid navigation edge request"}});
        }
    });

    server.Delete(R"(/api/admin/navigation/edges/([0-9]+)/([0-9]+))", [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        if (authorizeAdmin(request, response, students, sessions, adminId)) return;
        int from = routeId(request, 1), to = routeId(request, 2);
        if (from < 0 || to < 0) return Api::sendJson(response, 400, {{"error", "Invalid edge endpoints"}});
        std::string error;
        if (!navigation.removeEdge(from, to, error)) return sendNavigationError(response, error);
        Api::sendJson(response, 200, {{"message", "Navigation edge removed"}});
    });

    auto routeHandler = [&navigation, &students, &sessions](const httplib::Request& request, httplib::Response& response, int from, int to) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId);
        if (status) return Api::sendJson(response, status, {{"error", "Unauthenticated"}});
        NavigationPath path;
        std::string error;
        if (!navigation.findRoute(from, to, path, error)) return sendNavigationError(response, error);
        Api::sendJson(response, 200, routeJson(from, to, path));
    };

    server.Post("/api/navigation/route", [routeHandler](const httplib::Request& request, httplib::Response& response) {
        try {
            Json body = Json::parse(request.body);
            if (!body.contains("fromId") || !body["fromId"].is_number_integer() ||
                !body.contains("toId") || !body["toId"].is_number_integer())
                return Api::sendJson(response, 400, {{"error", "fromId and toId are required"}});
            routeHandler(request, response, body["fromId"].get<int>(), body["toId"].get<int>());
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid route request"}});
        }
    });

    server.Get("/api/demo/route", [routeHandler](const httplib::Request& request, httplib::Response& response) {
        if (!request.has_param("from") || !request.has_param("to"))
            return Api::sendJson(response, 400, {{"error", "Query parameters 'from' and 'to' are required"}});
        try {
            routeHandler(request, response, std::stoi(request.get_param_value("from")),
                         std::stoi(request.get_param_value("to")));
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Route endpoints must be integer location IDs"}});
        }
    });
}
