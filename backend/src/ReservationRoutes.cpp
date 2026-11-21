#include "api/ApiHelpers.h"
#include "api/ModuleRoutes.h"
#include "data_access/ReservationRepository.h"
#include <regex>

namespace {
using Api::Json;

int routeId(const httplib::Request& request) {
    try {
        const std::string value = request.matches[1].str();
        std::size_t parsed = 0;
        int id = std::stoi(value, &parsed);
        return parsed == value.size() && id > 0 ? id : 0;
    } catch (...) {
        return 0;
    }
}

bool validTime(const std::string& value) {
    static const std::regex pattern(R"(^([01][0-9]|2[0-3]):[0-5][0-9]$)");
    return std::regex_match(value, pattern);
}

bool validDate(const std::string& value) {
    static const std::regex pattern(R"(^([0-9]{4})-([0-9]{2})-([0-9]{2})$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) return false;
    int year = std::stoi(match[1].str());
    int month = std::stoi(match[2].str());
    int day = std::stoi(match[3].str());
    static const int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 1 || month < 1 || month > 12) return false;
    int maximum = daysInMonth[month - 1];
    bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (month == 2 && leapYear) maximum = 29;
    return day >= 1 && day <= maximum;
}

Json organizationJson(const Organization& organization) {
    return {{"id", organization.id}, {"name", organization.name}, {"type", organization.type},
            {"priorityLevel", organization.priorityLevel}, {"isActive", organization.isActive},
            {"isRepresentative", organization.isRepresentative}};
}

Json reservationJson(const ReservationRequest& request, bool includeHistory) {
    Json result = {
        {"id", request.id}, {"organizationId", request.organizationId},
        {"organizationName", request.organizationName}, {"requesterName", request.requesterName},
        {"classroomId", request.classroomId}, {"roomCode", request.roomCode},
        {"date", request.reservationDate}, {"startTime", request.startTime}, {"endTime", request.endTime},
        {"purpose", request.purpose}, {"priorityLevel", request.priorityLevel},
        {"effectivePriority", request.effectivePriority}, {"prioritySource", request.prioritySource},
        {"submittedAt", request.submittedAt}, {"status", request.status},
        {"decidedAt", request.decidedAt}, {"rejectionReason", request.rejectionReason}
    };
    if (includeHistory) {
        Json events = Json::array();
        for (const auto& event : request.history) {
            events.push_back({{"oldStatus", event.oldStatus.empty() ? Json(nullptr) : Json(event.oldStatus)},
                              {"newStatus", event.newStatus}, {"actorId", event.actorId},
                              {"reason", event.reason}, {"createdAt", event.createdAt}});
        }
        result["history"] = events;
    }
    return result;
}

void sendReservationError(httplib::Response& response, const std::string& error) {
    if (error == "not_found" || error == "classroom_not_found_or_inactive")
        return Api::sendJson(response, 404, {{"error", "Reservation, organization, member, or active classroom not found"}});
    if (error == "not_authorized_for_organization")
        return Api::sendJson(response, 403, {{"error", "An active organization member may submit requests"}});
    if (error == "duplicate_pending_request")
        return Api::sendJson(response, 409, {{"error", "An identical pending request already exists"}});
    if (error == "reservation_conflict")
        return Api::sendJson(response, 409, {{"error", "The classroom time conflicts with an approved reservation"}});
    if (error == "schedule_conflict")
        return Api::sendJson(response, 409, {{"error", "The classroom is scheduled for a class during that time"}});
    if (error == "outside_booking_hours")
        return Api::sendJson(response, 409, {{"error", "Room bookings must end within 09:00–17:30"}});
    if (error == "classroom_inactive")
        return Api::sendJson(response, 409, {{"error", "The classroom is inactive and cannot be approved"}});
    if (error == "invalid_transition")
        return Api::sendJson(response, 409, {{"error", "This request is no longer pending"}});
    if (error == "rejection_reason_required")
        return Api::sendJson(response, 400, {{"error", "A rejection reason is required"}});
    if (error == "busy")
        return Api::sendJson(response, 503, {{"error", "Reservation database is busy; please retry"}});
    if (error == "duplicate_organization")
        return Api::sendJson(response, 409, {{"error", "An organization with this name already exists"}});
    if (error == "organization_or_student_not_found")
        return Api::sendJson(response, 404, {{"error", "Organization or student identifier not found"}});
    if (error == "invalid_member_role")
        return Api::sendJson(response, 400, {{"error", "Member role must be MEMBER or REPRESENTATIVE"}});
    Api::sendJson(response, 500, {{"error", "Reservation request could not be saved"}});
}
}

void registerReservationRoutes(httplib::Server& server, ReservationRepository& reservations,
                               StudentRepository& students, SessionStore& sessions) {
    server.Get("/api/organizations/mine", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        std::vector<Organization> organizations;
        if (!reservations.listOrganizations(studentId, false, organizations))
            return Api::sendJson(response, 500, {{"error", "Organizations could not be loaded"}});
        Json result = Json::array();
        for (const auto& organization : organizations) result.push_back(organizationJson(organization));
        Api::sendJson(response, 200, {{"organizations", result}});
    });

    server.Get("/api/admin/organizations", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        int status = Api::authorize(request, sessions, students, adminId, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        std::vector<Organization> organizations;
        if (!reservations.listOrganizations(adminId, true, organizations))
            return Api::sendJson(response, 500, {{"error", "Organizations could not be loaded"}});
        Json result = Json::array();
        for (const auto& organization : organizations) result.push_back(organizationJson(organization));
        Api::sendJson(response, 200, {{"organizations", result}});
    });

    server.Post("/api/admin/organizations", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        int status = Api::authorize(request, sessions, students, adminId, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            Json body = Json::parse(request.body);
            Organization organization;
            organization.name = body.value("name", "");
            organization.type = body.value("type", "");
            organization.priorityLevel = body.value("priorityLevel", 1);
            if (organization.name.empty() || organization.type.empty() ||
                organization.priorityLevel < 1 || organization.priorityLevel > 3)
                return Api::sendJson(response, 400, {{"error", "Name, type, and priority level 1-3 are required"}});
            std::string error;
            Organization created;
            if (!reservations.createOrganization(organization, error, created))
                return sendReservationError(response, error);
            Api::sendJson(response, 201, {{"organization", organizationJson(created)}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid organization request"}});
        }
    });

    server.Put(R"(/api/admin/organizations/([0-9]+)/members)", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        int status = Api::authorize(request, sessions, students, adminId, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        int organizationId = routeId(request);
        if (!organizationId) return Api::sendJson(response, 400, {{"error", "Invalid organization ID"}});
        try {
            Json body = Json::parse(request.body);
            std::string studentId = body.value("studentId", "");
            std::string role = body.value("role", "");
            if (studentId.empty()) return Api::sendJson(response, 400, {{"error", "studentId is required"}});
            std::string error;
            if (!reservations.addOrganizationMember(organizationId, studentId, role, error))
                return sendReservationError(response, error);
            Api::sendJson(response, 200, {{"message", "Organization membership saved"}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid organization member request"}});
        }
    });

    server.Post("/api/reservations", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        try {
            Json body = Json::parse(request.body);
            if (body.contains("priority") || body.contains("priorityLevel") || body.contains("priorityCategory"))
                return Api::sendJson(response, 400, {{"error", "Priority is assigned by organization policy, not the request"}});
            if (!body.contains("organizationId") || !body["organizationId"].is_number_integer() ||
                !body.contains("classroomId") || !body["classroomId"].is_number_integer())
                return Api::sendJson(response, 400, {{"error", "Organization and classroom IDs are required"}});
            int organizationId = body["organizationId"].get<int>();
            int classroomId = body["classroomId"].get<int>();
            std::string date = body.value("date", "");
            std::string start = body.value("startTime", "");
            std::string end = body.value("endTime", "");
            std::string purpose = body.value("purpose", "");
            if (organizationId <= 0 || classroomId <= 0 || !validDate(date) ||
                !validTime(start) || !validTime(end) || end <= start ||
                start < "09:00" || end > "17:30" ||
                purpose.empty() || purpose.size() > 500)
                return Api::sendJson(response, 400, {{"error", "Provide a valid date, room, organization, purpose (max 500 characters), and time range between 09:00 and 17:30"}});
            ReservationRequest created;
            std::string error;
            if (!reservations.createRequest(studentId, organizationId, classroomId, date, start, end, purpose, created, error))
                return sendReservationError(response, error);
            Api::sendJson(response, 201, {{"reservation", reservationJson(created, true)}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid reservation request"}});
        }
    });

    server.Get("/api/reservations/mine", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        std::vector<ReservationRequest> requests;
        if (!reservations.listMine(studentId, requests))
            return Api::sendJson(response, 500, {{"error", "Reservations could not be loaded"}});
        Json result = Json::array();
        for (const auto& item : requests) result.push_back(reservationJson(item, true));
        Api::sendJson(response, 200, {{"reservations", result}});
    });

    server.Get(R"(/api/reservations/([0-9]+))", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        int requestId = routeId(request);
        if (!requestId) return Api::sendJson(response, 400, {{"error", "Invalid reservation ID"}});
        ReservationRequest reservation;
        if (!reservations.findForMember(studentId, requestId, reservation))
            return Api::sendJson(response, 404, {{"error", "Reservation not found"}});
        Api::sendJson(response, 200, {{"reservation", reservationJson(reservation, true)}});
    });

    server.Post(R"(/api/reservations/([0-9]+)/cancel)", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        int requestId = routeId(request);
        if (!requestId) return Api::sendJson(response, 400, {{"error", "Invalid reservation ID"}});
        ReservationRequest reservation;
        std::string error;
        if (!reservations.cancel(studentId, requestId, reservation, error))
            return sendReservationError(response, error);
        Api::sendJson(response, 200, {{"reservation", reservationJson(reservation, true)}});
    });

    server.Get("/api/admin/reservations/queue", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        int status = Api::authorize(request, sessions, students, adminId, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        std::vector<ReservationRequest> requests;
        if (!reservations.prioritizedQueue(requests))
            return Api::sendJson(response, 500, {{"error", "Reservation queue could not be loaded"}});
        Json result = Json::array();
        for (const auto& item : requests) result.push_back(reservationJson(item, true));
        Api::sendJson(response, 200, {{"queue", result}, {"priorityPolicy", "Organization level plus one level per 24 hours waiting, capped at level 3; FIFO then request ID breaks ties"}});
    });

    server.Post(R"(/api/admin/reservations/([0-9]+)/approve)", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        int status = Api::authorize(request, sessions, students, adminId, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        int requestId = routeId(request);
        if (!requestId) return Api::sendJson(response, 400, {{"error", "Invalid reservation ID"}});
        ReservationRequest reservation;
        std::string error;
        if (!reservations.decide(adminId, requestId, true, "", reservation, error))
            return sendReservationError(response, error);
        Api::sendJson(response, 200, {{"reservation", reservationJson(reservation, true)}});
    });

    server.Post(R"(/api/admin/reservations/([0-9]+)/reject)", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int adminId = 0;
        int status = Api::authorize(request, sessions, students, adminId, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        int requestId = routeId(request);
        if (!requestId) return Api::sendJson(response, 400, {{"error", "Invalid reservation ID"}});
        try {
            Json body = Json::parse(request.body);
            std::string reason = body.value("reason", "");
            if (reason.empty() || reason.size() > 500)
                return Api::sendJson(response, 400, {{"error", "Rejection reason is required (max 500 characters)"}});
            ReservationRequest reservation;
            std::string error;
            if (!reservations.decide(adminId, requestId, false, reason, reservation, error))
                return sendReservationError(response, error);
            Api::sendJson(response, 200, {{"reservation", reservationJson(reservation, true)}});
        } catch (...) {
            Api::sendJson(response, 400, {{"error", "Invalid rejection request"}});
        }
    });

    server.Get(R"(/api/rooms/([0-9]+)/reservations)", [&reservations, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId);
        if (status) return Api::sendJson(response, status, {{"error", "Unauthenticated"}});
        int roomId = routeId(request);
        std::string date = request.has_param("date") ? request.get_param_value("date") : "";
        if (!roomId || !validDate(date)) return Api::sendJson(response, 400, {{"error", "A valid room ID and YYYY-MM-DD date are required"}});
        std::vector<RoomScheduleBlock> blocks;
        if (!reservations.roomSchedule(roomId, date, blocks))
            return Api::sendJson(response, 500, {{"error", "Room schedule could not be loaded"}});
        Json items = Json::array();
        for (const auto& block : blocks)
            items.push_back({{"kind", block.kind}, {"title", block.title}, {"subtitle", block.subtitle},
                             {"startTime", block.startTime}, {"endTime", block.endTime}});
        Api::sendJson(response, 200, {{"roomId", roomId}, {"date", date},
                                      {"availabilityStart", "09:00"}, {"availabilityEnd", "17:30"},
                                      {"pendingBlocksAvailability", false}, {"blocks", items}});
    });
}
