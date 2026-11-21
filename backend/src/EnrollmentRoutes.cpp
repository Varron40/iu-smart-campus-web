#include "api/ApiHelpers.h"
#include "api/ModuleRoutes.h"
#include "data_access/EnrollmentRepository.h"
#include <string>

namespace {
using Api::Json;

Json meetingJson(const SectionMeeting& meeting) {
    return {
        {"id", meeting.id}, {"activityType", meeting.activityType}, {"day", meeting.day},
        {"startTime", meeting.startTime}, {"endTime", meeting.endTime},
        {"enrollmentCapacity", meeting.enrollmentCapacity ? Json(meeting.enrollmentCapacity) : Json(nullptr)},
        {"capacity", meeting.capacityKnown ? Json(meeting.capacity) : Json(nullptr)},
        {"capacityKnown", meeting.capacityKnown}, {"enrolledCount", meeting.enrolledCount},
        {"remainingSeats", meeting.capacityKnown ? Json(meeting.remainingSeats) : Json(nullptr)},
        {"isLabOption", meeting.isLabOption},
        {"classroomId", meeting.classroomId ? Json(meeting.classroomId) : Json(nullptr)},
        {"venueLabel", meeting.venueLabel},
        {"classroomCode", meeting.classroomCode.empty() ? Json(nullptr) : Json(meeting.classroomCode)},
        {"buildingName", meeting.buildingName.empty() ? Json(nullptr) : Json(meeting.buildingName)},
        {"instructor", meeting.instructor}
    };
}

Json availableJson(const AvailableSection& section) {
    Json meetings = Json::array();
    for (const SectionMeeting& meeting : section.meetings) meetings.push_back(meetingJson(meeting));
    return {
        {"sectionId", section.sectionId}, {"courseCode", section.courseCode},
        {"courseName", section.courseName}, {"sectionCode", section.sectionCode},
        {"term", section.term}, {"day", section.day},
        {"startTime", section.startTime}, {"endTime", section.endTime},
        {"classroomId", section.classroomId ? Json(section.classroomId) : Json(nullptr)},
        {"classroomCode", section.classroomCode.empty() ? Json(nullptr) : Json(section.classroomCode)},
        {"buildingName", section.buildingName.empty() ? Json(nullptr) : Json(section.buildingName)},
        {"capacity", section.capacityKnown ? Json(section.capacity) : Json(nullptr)},
        {"enrolledCount", section.capacityKnown ? Json(section.enrolledCount) : Json(nullptr)},
        {"remainingSeats", section.capacityKnown ? Json(section.remainingSeats) : Json(nullptr)},
        {"capacityKnown", section.capacityKnown},
        {"openForEnrollment", section.openForEnrollment},
        {"isFull", section.isFull},
        {"meetings", meetings}
    };
}

Json enrollmentJson(const Enrollment& enrollment) {
    Json meetings = Json::array();
    for (const SectionMeeting& meeting : enrollment.meetings) meetings.push_back(meetingJson(meeting));
    return {
        {"id", enrollment.id}, {"sectionId", enrollment.sectionId},
        {"labMeetingId", enrollment.labMeetingId ? Json(enrollment.labMeetingId) : Json(nullptr)},
        {"status", enrollment.status},
        {"createdAt", enrollment.createdAt}, {"updatedAt", enrollment.updatedAt},
        {"courseCode", enrollment.courseCode}, {"courseName", enrollment.courseName},
        {"sectionCode", enrollment.sectionCode}, {"term", enrollment.term},
        {"day", enrollment.day}, {"startTime", enrollment.startTime}, {"endTime", enrollment.endTime},
        {"classroomCode", enrollment.classroomCode.empty() ? Json(nullptr) : Json(enrollment.classroomCode)},
        {"buildingName", enrollment.buildingName.empty() ? Json(nullptr) : Json(enrollment.buildingName)},
        {"meetings", meetings}
    };
}

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

void sendRepositoryError(httplib::Response& response, const std::string& error) {
    if (error == "section_not_found" || error == "not_found")
        return Api::sendJson(response, 404, {{"error", "Section or enrollment not found"}});
    if (error == "already_enrolled")
        return Api::sendJson(response, 409, {{"error", "You are already enrolled in this section"}});
    if (error == "course_already_enrolled")
        return Api::sendJson(response, 409, {{"error", "You are already enrolled in this course for the selected term"}});
    if (error == "section_full")
        return Api::sendJson(response, 409, {{"error", "This section is full"}});
    if (error == "section_inactive")
        return Api::sendJson(response, 409, {{"error", "This section is not open for enrollment"}});
    if (error == "lab_choice_required")
        return Api::sendJson(response, 400, {{"error", "Choose one available lab meeting before enrolling"}});
    if (error == "invalid_lab_choice")
        return Api::sendJson(response, 400, {{"error", "The selected lab meeting does not belong to this section"}});
    if (error == "capacity_unknown")
        return Api::sendJson(response, 409, {{"error", "Verified room and/or enrollment capacity is required before enrollment"}});
    if (error == "invalid_transition")
        return Api::sendJson(response, 409, {{"error", "This enrollment has already been withdrawn"}});
    if (error == "busy")
        return Api::sendJson(response, 503, {{"error", "Enrollment is busy; please retry"}});
    Api::sendJson(response, 500, {{"error", "Enrollment could not be saved"}});
}
}

void registerEnrollmentRoutes(httplib::Server& server, EnrollmentRepository& enrollments,
                              StudentRepository& students, SessionStore& sessions) {
    server.Get("/api/sections/available", [&enrollments, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        std::vector<AvailableSection> sections;
        const std::string query = request.has_param("q") ? request.get_param_value("q") : "";
        const std::string term = request.has_param("term") ? request.get_param_value("term") : "";
        const std::string group = request.has_param("group") ? request.get_param_value("group") : "";
        if (!enrollments.listAvailable(query, term, group, sections))
            return Api::sendJson(response, 500, {{"error", "Available sections could not be loaded"}});
        Json result = Json::array();
        for (const auto& section : sections) result.push_back(availableJson(section));
        Api::sendJson(response, 200, {{"sections", result}});
    });

    server.Post(R"(/api/sections/([0-9]+)/enroll)", [&enrollments, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        int sectionId = routeId(request);
        if (!sectionId) return Api::sendJson(response, 400, {{"error", "Invalid section ID"}});
        int labMeetingId = 0;
        try {
            Json body = request.body.empty() ? Json::object() : Json::parse(request.body);
            if (!body.is_object()) return Api::sendJson(response, 400, {{"error", "Invalid enrollment request"}});
            if (body.contains("labMeetingId") && !body["labMeetingId"].is_null()) {
                if (!body["labMeetingId"].is_number_integer() || body["labMeetingId"].get<int>() <= 0)
                    return Api::sendJson(response, 400, {{"error", "labMeetingId must be a positive meeting ID or null"}});
                labMeetingId = body["labMeetingId"].get<int>();
            }
        } catch (...) {
            return Api::sendJson(response, 400, {{"error", "Invalid enrollment request"}});
        }
        Enrollment enrollment;
        std::string error;
        if (!enrollments.enroll(studentId, sectionId, labMeetingId, enrollment, error))
            return sendRepositoryError(response, error);
        Api::sendJson(response, 201, {{"enrollment", enrollmentJson(enrollment)}});
    });

    server.Get("/api/students/me/enrollments", [&enrollments, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        std::vector<Enrollment> records;
        if (!enrollments.listForStudent(studentId, false, records))
            return Api::sendJson(response, 500, {{"error", "Enrollments could not be loaded"}});
        Json result = Json::array();
        for (const auto& enrollment : records) result.push_back(enrollmentJson(enrollment));
        Api::sendJson(response, 200, {{"enrollments", result}});
    });

    auto withdraw = [&enrollments, &students, &sessions](const httplib::Request& request, httplib::Response& response) {
        int studentId = 0;
        int status = Api::authorize(request, sessions, students, studentId, false, true);
        if (status) return Api::sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Student role required"}});
        int enrollmentId = routeId(request);
        if (!enrollmentId) return Api::sendJson(response, 400, {{"error", "Invalid enrollment ID"}});
        Enrollment enrollment;
        std::string error;
        if (!enrollments.withdraw(studentId, enrollmentId, enrollment, error))
            return sendRepositoryError(response, error);
        Api::sendJson(response, 200, {{"enrollment", enrollmentJson(enrollment)}});
    };
    server.Post(R"(/api/enrollments/([0-9]+)/withdraw)", withdraw);
    server.Delete(R"(/api/enrollments/([0-9]+))", withdraw);
}
