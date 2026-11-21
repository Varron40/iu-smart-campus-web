#include "models/Student.h"
#include "models/CampusRecords.h"
#include "data_structures/HashTable.h"
#include "data_access/EnrollmentRepository.h"
#include "data_access/MapRepository.h"
#include "data_access/NavigationRepository.h"
#include "data_access/ReservationRepository.h"
#include "data_access/StudentRepository.h"
#include "data_access/CampusRepository.h"
#include "api/ModuleRoutes.h"
#include "services/PasswordHash.h"
#include "services/SessionStore.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {
void setCors(httplib::Response& response) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    response.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
}

void sendJson(httplib::Response& response, int status, const json& body) {
    setCors(response);
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

json publicStudent(const Student& student) {
    return {
        {"id", student.studentId},
        {"studentId", student.studentId},
        {"fullName", student.fullName},
        {"universityEmail", student.universityEmail},
        {"faculty", student.faculty},
        {"department", student.department},
        {"academicYear", student.academicYear},
        {"createdAt", student.createdAt},
        {"role", student.role}
    };
}

bool validEmail(const std::string& email) {
    static const std::regex emailPattern(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
    return std::regex_match(email, emailPattern);
}

std::string trimText(std::string value) {
    const std::string whitespace = " \t\r\n";
    std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

bool validPassword(const std::string& password) {
    return password.size() >= 8;
}

bool authenticated(const httplib::Request& request, const SessionStore& sessions, int& databaseId) {
    auto authorization = request.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) != 0) return false;
    return sessions.find(authorization.substr(prefix.size()), databaseId);
}

int accessStatus(const httplib::Request& request, const SessionStore& sessions,
                 StudentRepository& repository, int& databaseId, bool requireAdmin) {
    if (!authenticated(request, sessions, databaseId)) return 401;
    Student student;
    if (!repository.findById(databaseId, student)) return 401;
    if (requireAdmin && student.role != "admin") return 403;
    return 0;
}

int queryId(const httplib::Request& request, const std::string& name) {
    if (!request.has_param(name)) return 0;
    try {
        std::string value = request.get_param_value(name);
        std::size_t parsed = 0;
        int id = std::stoi(value, &parsed);
        return parsed == value.size() && id > 0 ? id : -1;
    } catch (...) {
        return -1;
    }
}

json publicBuilding(const Building& building) {
    return {{"id", building.id}, {"code", building.code}, {"name", building.name},
            {"description", building.description}, {"isActive", building.isActive}};
}

json publicFloor(const Floor& floor) {
    return {{"id", floor.id}, {"buildingId", floor.buildingId}, {"label", floor.label},
            {"description", floor.description}};
}

json publicClassroom(const Classroom& room) {
    return {{"id", room.id}, {"buildingId", room.buildingId}, {"buildingName", room.buildingName},
            {"floorId", room.floorId}, {"floorLabel", room.floorLabel}, {"roomCode", room.roomCode},
            {"roomName", room.roomName}, {"roomType", room.roomType},
            {"capacity", room.capacityKnown ? json(room.capacity) : json(nullptr)},
            {"capacityKnown", room.capacityKnown},
            {"isActive", room.isActive}, {"notes", room.notes}, {"occupancy", nullptr},
            {"remainingSeats", nullptr}};
}

json publicMeeting(const SectionMeeting& meeting) {
    return {{"id", meeting.id}, {"activityType", meeting.activityType}, {"day", meeting.day},
            {"startTime", meeting.startTime}, {"endTime", meeting.endTime},
            {"enrollmentCapacity", meeting.enrollmentCapacity ? json(meeting.enrollmentCapacity) : json(nullptr)},
            {"classroomId", meeting.classroomId ? json(meeting.classroomId) : json(nullptr)},
            {"venueLabel", meeting.venueLabel},
            {"classroomCode", meeting.classroomCode.empty() ? json(nullptr) : json(meeting.classroomCode)},
            {"buildingName", meeting.buildingName.empty() ? json(nullptr) : json(meeting.buildingName)},
            {"instructor", meeting.instructor}};
}

json publicSection(const AcademicSection& section) {
    json meetings = json::array();
    for (const SectionMeeting& meeting : section.meetings) meetings.push_back(publicMeeting(meeting));
    return {{"id", section.id}, {"courseCode", section.courseCode}, {"courseName", section.courseName},
            {"sectionCode", section.sectionCode}, {"instructor", section.instructor}, {"term", section.term},
            {"day", section.day}, {"startTime", section.startTime}, {"endTime", section.endTime},
            {"classroomId", section.classroomId ? json(section.classroomId) : json(nullptr)},
            {"classroomCode", section.classroomCode.empty() ? json(nullptr) : json(section.classroomCode)},
            {"buildingName", section.buildingName.empty() ? json(nullptr) : json(section.buildingName)},
            {"enrollmentCapacity", section.enrollmentCapacity ? json(section.enrollmentCapacity) : json(nullptr)},
            {"meetings", meetings},
            {"isActive", section.isActive},
            {"isOpenForEnrollment", section.isOpenForEnrollment}};
}

bool validTime(const std::string& time) {
    static const std::regex pattern(R"(^([01][0-9]|2[0-3]):[0-5][0-9]$)");
    return std::regex_match(time, pattern);
}

bool validDay(const std::string& day) {
    static const std::vector<std::string> days = {
        "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
    };
    return day.empty() || std::find(days.begin(), days.end(), day) != days.end();
}

bool readClassroomBody(const json& body, Classroom& room) {
    room.buildingId = body.value("buildingId", 0);
    room.floorId = body.value("floorId", 0);
    room.roomCode = trimText(body.value("roomCode", ""));
    room.roomName = trimText(body.value("roomName", ""));
    room.roomType = trimText(body.value("roomType", ""));
    room.notes = trimText(body.value("notes", ""));
    if (!body.contains("capacity") || !body["capacity"].is_number_integer()) return false;
    room.capacity = body["capacity"].get<int>();
    if (body.contains("isActive")) {
        if (!body["isActive"].is_boolean()) return false;
        room.isActive = body["isActive"].get<bool>();
    }
    return room.buildingId > 0 && room.floorId > 0 && !room.roomCode.empty() &&
           !room.roomName.empty() && !room.roomType.empty() && room.capacity > 0;
}

bool readSectionBody(const json& body, AcademicSection& section, bool preserveStatus = false) {
    section.courseCode = trimText(body.value("courseCode", ""));
    section.courseName = trimText(body.value("courseName", ""));
    section.sectionCode = trimText(body.value("sectionCode", ""));
    section.instructor = trimText(body.value("instructor", ""));
    section.term = trimText(body.value("term", ""));
    section.day = trimText(body.value("day", ""));
    section.startTime = trimText(body.value("startTime", ""));
    section.endTime = trimText(body.value("endTime", ""));
    if (body.contains("enrollmentCapacity")) {
        if (body["enrollmentCapacity"].is_null()) section.enrollmentCapacity = 0;
        else if (body["enrollmentCapacity"].is_number_integer() &&
                 body["enrollmentCapacity"].get<int>() > 0)
            section.enrollmentCapacity = body["enrollmentCapacity"].get<int>();
        else return false;
    }
    if (!preserveStatus || body.contains("isActive")) {
        if (body.contains("isActive") && !body["isActive"].is_boolean()) return false;
        section.isActive = body.value("isActive", true);
    }
    if (body.contains("isOpenForEnrollment")) {
        if (!body["isOpenForEnrollment"].is_boolean()) return false;
        section.isOpenForEnrollment = body["isOpenForEnrollment"].get<bool>();
    }
    if (body.contains("classroomId") && !body["classroomId"].is_null()) {
        if (!body["classroomId"].is_number_integer()) return false;
        section.classroomId = body["classroomId"].get<int>();
        if (section.classroomId <= 0) return false;
    } else {
        section.classroomId = 0;
    }
    if (body.contains("meetings")) {
        if (!body["meetings"].is_array()) return false;
        section.meetings.clear();
        for (const json& item : body["meetings"]) {
            if (!item.is_object() || !item.contains("activityType") || !item["activityType"].is_string() ||
                !item.contains("day") || !item["day"].is_string() ||
                !item.contains("startTime") || !item["startTime"].is_string() ||
                !item.contains("endTime") || !item["endTime"].is_string())
                return false;
            SectionMeeting meeting;
            if (item.contains("id")) {
                if (!item["id"].is_number_integer() || item["id"].get<int>() < 0) return false;
                meeting.id = item["id"].get<int>();
            }
            meeting.activityType = trimText(item["activityType"].get<std::string>());
            meeting.day = trimText(item["day"].get<std::string>());
            meeting.startTime = trimText(item["startTime"].get<std::string>());
            meeting.endTime = trimText(item["endTime"].get<std::string>());
            meeting.venueLabel = item.value("venueLabel", "");
            meeting.instructor = item.value("instructor", "");
            if (item.contains("enrollmentCapacity")) {
                if (item["enrollmentCapacity"].is_null()) meeting.enrollmentCapacity = 0;
                else if (item["enrollmentCapacity"].is_number_integer() &&
                         item["enrollmentCapacity"].get<int>() > 0)
                    meeting.enrollmentCapacity = item["enrollmentCapacity"].get<int>();
                else return false;
            }
            if (item.contains("classroomId") && !item["classroomId"].is_null()) {
                if (!item["classroomId"].is_number_integer() || item["classroomId"].get<int>() <= 0)
                    return false;
                meeting.classroomId = item["classroomId"].get<int>();
            }
            if (meeting.activityType.empty() || meeting.day.empty() || !validDay(meeting.day) ||
                !validTime(meeting.startTime) || !validTime(meeting.endTime) ||
                meeting.endTime <= meeting.startTime)
                return false;
            section.meetings.push_back(std::move(meeting));
        }
        if (!section.meetings.empty()) {
            const SectionMeeting& first = section.meetings.front();
            section.day = first.day;
            section.startTime = first.startTime;
            section.endTime = first.endTime;
            section.classroomId = first.classroomId;
            section.instructor = first.instructor;
        }
    }
    bool hasAnyTime = !section.startTime.empty() || !section.endTime.empty();
    return !section.courseCode.empty() && !section.courseName.empty() &&
           !section.sectionCode.empty() && !section.term.empty() &&
           validDay(section.day) && (!hasAnyTime ||
           (!section.day.empty() && validTime(section.startTime) && validTime(section.endTime) &&
            section.endTime > section.startTime));
}
}

int main() {
    httplib::Server server;
    const char* configuredDatabasePath = std::getenv("IU_DATABASE_PATH");
    const std::string databasePath = configuredDatabasePath && configuredDatabasePath[0]
        ? configuredDatabasePath : "data/iu-smart-campus.db";
    std::filesystem::path databaseFile(databasePath);
    if (databaseFile.has_parent_path())
        std::filesystem::create_directories(databaseFile.parent_path());
    sqlite3* database = nullptr;
    if (sqlite3_open(databasePath.c_str(), &database) != SQLITE_OK) {
        std::cerr << "Unable to open SQLite database\n";
        return 1;
    }
    sqlite3_busy_timeout(database, 5000);
    sqlite3_extended_result_codes(database, 1);
    StudentRepository repository(database);
    if (!repository.initialize()) {
        std::cerr << "Unable to initialize SQLite database\n";
        sqlite3_close(database);
        return 1;
    }
    CampusRepository campusRepository(database);
    std::string campusInitializationError;
    if (!campusRepository.initialize(&campusInitializationError)) {
        std::cerr << "Unable to initialize classroom and section data";
        if (!campusInitializationError.empty()) std::cerr << ": " << campusInitializationError;
        std::cerr << "\n";
        sqlite3_close(database);
        return 1;
    }
    EnrollmentRepository enrollmentRepository(databasePath);
    if (!enrollmentRepository.initialize()) {
        std::cerr << "Unable to initialize enrollment data\n";
        sqlite3_close(database);
        return 1;
    }
    std::string scheduleImportError;
    if (!campusRepository.importSchedule(IU_SCHEDULE_SEED_PATH, scheduleImportError)) {
        std::cerr << "Unable to import Level 2 timetable: " << scheduleImportError << "\n";
        sqlite3_close(database);
        return 1;
    }
    ReservationRepository reservationRepository(databasePath);
    if (!reservationRepository.initialize()) {
        std::cerr << "Unable to initialize reservation data\n";
        sqlite3_close(database);
        return 1;
    }
    NavigationRepository navigationRepository(database);
    if (!navigationRepository.initialize()) {
        std::cerr << "Unable to initialize navigation data\n";
        sqlite3_close(database);
        return 1;
    }
    MapRepository mapRepository(database);
    if (!mapRepository.initialize() || !navigationRepository.refresh()) {
        std::cerr << "Unable to initialize campus map data\n";
        sqlite3_close(database);
        return 1;
    }

    const char* adminId = std::getenv("IU_ADMIN_STUDENT_ID");
    const char* adminName = std::getenv("IU_ADMIN_NAME");
    const char* adminEmail = std::getenv("IU_ADMIN_EMAIL");
    const char* adminPassword = std::getenv("IU_ADMIN_PASSWORD");
    if (adminId && *adminId && adminName && *adminName && adminEmail && *adminEmail && adminPassword && *adminPassword) {
        Student existingAdmin;
        if (!repository.findByEmail(adminEmail, existingAdmin) &&
            !repository.findByStudentId(adminId, existingAdmin) && validEmail(adminEmail) &&
            validPassword(adminPassword)) {
            Student admin;
            admin.studentId = adminId;
            admin.fullName = adminName;
            admin.universityEmail = adminEmail;
            admin.passwordHash = PasswordHash::hashPassword(adminPassword);
            admin.faculty = "Administration";
            admin.role = "admin";
            std::string adminError;
            if (admin.passwordHash.empty() || !repository.create(admin, adminError))
                std::cerr << "Admin account provisioning failed; check the configured identifiers and password.\n";
            else
                std::cout << "Configured initial administrator account is ready.\n";
        } else {
            std::cerr << "Admin account was not provisioned: configuration is invalid or an identifier is already in use.\n";
        }
    }
    SessionStore sessions;
    HashTable<std::string, Student> students;

    sqlite3_stmt* statement = nullptr;
    const char* loadSql = "SELECT id,student_id,full_name,university_email,password_hash,faculty,department,academic_year,created_at,role FROM students";
    if (sqlite3_prepare_v2(database, loadSql, -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            Student student;
            student.id = sqlite3_column_int(statement, 0);
            student.studentId = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            student.fullName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
            student.universityEmail = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
            student.passwordHash = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
            student.faculty = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
            const char* department = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
            const char* academicYear = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
            const char* createdAt = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
            const char* role = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
            student.department = department ? department : "";
            student.academicYear = academicYear ? academicYear : "";
            student.createdAt = createdAt ? createdAt : "";
            student.role = role ? role : "student";
            students.insert(student.studentId, student);
        }
        sqlite3_finalize(statement);
    }

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& response) {
        setCors(response);
        response.set_content(json{{"status", "ok"}, {"service", "iu-smart-campus-api"}}.dump(), "application/json");
    });

    server.Options(".*", [](const httplib::Request&, httplib::Response& response) {
        setCors(response);
        response.status = 204;
    });

    server.Post("/api/auth/register", [&repository, &students](const httplib::Request& request, httplib::Response& response) {
        try {
            json body = json::parse(request.body);
            std::string studentId = body.value("studentId", "");
            std::string fullName = body.value("fullName", "");
            std::string email = body.value("universityEmail", "");
            std::string password = body.value("password", "");
            std::string faculty = body.value("faculty", "");
            std::string department = body.value("department", "");
            std::string academicYear = body.value("academicYear", "");
            if (studentId.empty() || fullName.empty() || email.empty() || password.empty() || faculty.empty())
                return sendJson(response, 400, {{"error", "Missing required registration fields"}});
            if (!validEmail(email)) return sendJson(response, 400, {{"error", "Invalid university email format"}});
            if (!validPassword(password)) return sendJson(response, 400, {{"error", "Password must be at least 8 characters"}});
            Student existing;
            if (students.find(studentId, existing)) return sendJson(response, 409, {{"error", "Student ID already exists"}});
            Student student;
            student.studentId = studentId;
            student.fullName = fullName;
            student.universityEmail = email;
            student.passwordHash = PasswordHash::hashPassword(password);
            if (student.passwordHash.empty()) return sendJson(response, 500, {{"error", "Unable to secure password"}});
            student.faculty = faculty;
            student.department = department;
            student.academicYear = academicYear;
            std::string error;
            if (!repository.create(student, error)) {
                if (error == "student_id_or_email_exists")
                    return sendJson(response, 409, {{"error", "Student ID or university email already exists"}});
                return sendJson(response, 500, {{"error", "Account could not be created"}});
            }
            repository.findByEmail(email, student);
            students.insert(student.studentId, student);
            sendJson(response, 201, {{"student", publicStudent(student)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid JSON request"}});
        }
    });

    server.Post("/api/auth/login", [&repository, &sessions](const httplib::Request& request, httplib::Response& response) {
        try {
            json body = json::parse(request.body);
            std::string email = body.value("universityEmail", "");
            std::string password = body.value("password", "");
            if (email.empty() || password.empty()) return sendJson(response, 400, {{"error", "Missing login fields"}});
            Student student;
            if (!repository.findByEmail(email, student) || !PasswordHash::verifyPassword(password, student.passwordHash))
                return sendJson(response, 401, {{"error", "Invalid email or password"}});
            sendJson(response, 200, {{"token", sessions.create(student.id)}, {"student", publicStudent(student)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid JSON request"}});
        }
    });

    server.Post("/api/auth/logout", [&sessions](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        if (!authenticated(request, sessions, databaseId)) return sendJson(response, 401, {{"error", "Unauthenticated"}});
        auto authorization = request.get_header_value("Authorization");
        sessions.invalidate(authorization.substr(7));
        sendJson(response, 200, {{"message", "Logged out"}});
    });

    server.Get("/api/students/me", [&repository, &sessions](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        if (!authenticated(request, sessions, databaseId)) return sendJson(response, 401, {{"error", "Unauthenticated"}});
        Student student;
        if (!repository.findById(databaseId, student)) return sendJson(response, 404, {{"error", "Student not found"}});
        sendJson(response, 200, {{"student", publicStudent(student)}});
    });

    server.Get("/api/admin/users", [&repository, &sessions, &database](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        if (!authenticated(request, sessions, databaseId))
            return sendJson(response, 401, {{"error", "Unauthenticated"}});
        Student viewer;
        if (!repository.findById(databaseId, viewer) || viewer.role != "admin")
            return sendJson(response, 403, {{"error", "Admin role required"}});

        json users = json::array();
        sqlite3_stmt* statement = nullptr;
        const char* sql =
            "SELECT id,student_id,full_name,university_email,faculty,department,academic_year,created_at,role "
            "FROM students ORDER BY created_at DESC,id DESC";
        if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
            return sendJson(response, 500, {{"error", "Registered users could not be loaded"}});
        while (sqlite3_step(statement) == SQLITE_ROW) {
            int id = sqlite3_column_int(statement, 0);
            auto text = [statement](int column) {
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
                return value ? std::string(value) : std::string();
            };
            json enrollments = json::array();
            sqlite3_stmt* enrollmentStatement = nullptr;
            const char* enrollmentSql =
                "SELECT e.id,e.status,e.lab_meeting_id,e.created_at,"
                "s.id,s.course_code,s.course_name,s.section_code,s.term,"
                "s.instructor,s.enrollment_capacity "
                "FROM enrollments e JOIN academic_sections s ON s.id=e.section_id "
                "WHERE e.student_id=? ORDER BY e.created_at DESC,e.id DESC";
            if (sqlite3_prepare_v2(database, enrollmentSql, -1, &enrollmentStatement, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(enrollmentStatement, 1, id);
                while (sqlite3_step(enrollmentStatement) == SQLITE_ROW) {
                    auto enrollmentText = [enrollmentStatement](int column) {
                        const char* value = reinterpret_cast<const char*>(sqlite3_column_text(enrollmentStatement, column));
                        return value ? std::string(value) : std::string();
                    };
                    enrollments.push_back({
                        {"id", sqlite3_column_int(enrollmentStatement, 0)},
                        {"status", enrollmentText(1)},
                        {"labMeetingId", sqlite3_column_type(enrollmentStatement, 2) == SQLITE_NULL
                            ? json(nullptr) : json(sqlite3_column_int(enrollmentStatement, 2))},
                        {"enrolledAt", enrollmentText(3)},
                        {"sectionId", sqlite3_column_int(enrollmentStatement, 4)},
                        {"courseCode", enrollmentText(5)},
                        {"courseName", enrollmentText(6)},
                        {"sectionCode", enrollmentText(7)},
                        {"term", enrollmentText(8)},
                        {"instructor", enrollmentText(9)},
                        {"sectionCapacity", sqlite3_column_type(enrollmentStatement, 10) == SQLITE_NULL
                            ? json(nullptr) : json(sqlite3_column_int(enrollmentStatement, 10))}
                    });
                }
                sqlite3_finalize(enrollmentStatement);
            }
            users.push_back({
                {"id", id},
                {"studentId", text(1)},
                {"fullName", text(2)},
                {"universityEmail", text(3)},
                {"faculty", text(4)},
                {"department", text(5)},
                {"academicYear", text(6)},
                {"createdAt", text(7)},
                {"role", text(8)},
                {"enrollments", enrollments}
            });
        }
        sqlite3_finalize(statement);
        sendJson(response, 200, {{"users", users}});
    });

    server.Put("/api/students/me", [&repository, &sessions, &students](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        if (!authenticated(request, sessions, databaseId)) return sendJson(response, 401, {{"error", "Unauthenticated"}});
        try {
            Student student;
            if (!repository.findById(databaseId, student)) return sendJson(response, 404, {{"error", "Student not found"}});
            json body = json::parse(request.body);
            std::string fullName = body.value("fullName", student.fullName);
            std::string faculty = body.value("faculty", student.faculty);
            std::string department = body.value("department", student.department);
            std::string academicYear = body.value("academicYear", student.academicYear);
            if (fullName.empty() || faculty.empty()) return sendJson(response, 400, {{"error", "Full name and faculty are required"}});
            std::string error;
            if (!repository.updateProfile(databaseId, fullName, faculty, department, academicYear, error))
                return sendJson(response, 500, {{"error", "Profile could not be updated"}});
            repository.findById(databaseId, student);
            students.insert(student.studentId, student);
            sendJson(response, 200, {{"student", publicStudent(student)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid JSON request"}});
        }
    });

    server.Get("/api/students/me/schedule", [&repository, &enrollmentRepository, &sessions](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        if (!authenticated(request, sessions, databaseId)) return sendJson(response, 401, {{"error", "Unauthenticated"}});
        Student student;
        if (!repository.findById(databaseId, student)) return sendJson(response, 404, {{"error", "Student not found"}});
        std::vector<Enrollment> enrollments;
        if (!enrollmentRepository.listForStudent(databaseId, true, enrollments))
            return sendJson(response, 500, {{"error", "Schedule could not be loaded"}});
        json schedule = json::array();
        for (const auto& enrollment : enrollments) {
            json meetings = json::array();
            for (const SectionMeeting& meeting : enrollment.meetings) meetings.push_back(publicMeeting(meeting));
            schedule.push_back({
                {"enrollmentId", enrollment.id}, {"sectionId", enrollment.sectionId},
                {"labMeetingId", enrollment.labMeetingId ? json(enrollment.labMeetingId) : json(nullptr)},
                {"courseCode", enrollment.courseCode}, {"courseName", enrollment.courseName},
                {"sectionCode", enrollment.sectionCode}, {"term", enrollment.term},
                {"day", enrollment.day}, {"startTime", enrollment.startTime}, {"endTime", enrollment.endTime},
                {"classroomCode", enrollment.classroomCode.empty() ? json(nullptr) : json(enrollment.classroomCode)},
                {"buildingName", enrollment.buildingName.empty() ? json(nullptr) : json(enrollment.buildingName)},
                {"meetings", meetings}
            });
        }
        sendJson(response, 200, {{"studentId", student.studentId}, {"schedule", schedule}});
    });

    server.Get("/api/buildings", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, false);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Forbidden"}});
        std::vector<Building> buildings;
        if (!campusRepository.listBuildings(buildings)) return sendJson(response, 500, {{"error", "Buildings could not be loaded"}});
        json result = json::array();
        for (const auto& building : buildings) result.push_back(publicBuilding(building));
        sendJson(response, 200, {{"buildings", result}});
    });

    server.Get(R"(/api/buildings/([0-9]+)/floors)", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, false);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Forbidden"}});
        int buildingId = 0;
        try { buildingId = std::stoi(request.matches[1].str()); } catch (...) {
            return sendJson(response, 400, {{"error", "Invalid building ID"}});
        }
        if (!campusRepository.buildingExists(buildingId)) return sendJson(response, 404, {{"error", "Building not found"}});
        std::vector<Floor> floors;
        if (!campusRepository.listFloors(buildingId, floors)) return sendJson(response, 500, {{"error", "Floors could not be loaded"}});
        json result = json::array();
        for (const auto& floor : floors) result.push_back(publicFloor(floor));
        sendJson(response, 200, {{"floors", result}});
    });

    server.Get("/api/rooms", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, false);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Forbidden"}});
        int buildingId = queryId(request, "buildingId");
        int floorId = queryId(request, "floorId");
        if (buildingId < 0 || floorId < 0) return sendJson(response, 400, {{"error", "Invalid building or floor filter"}});
        bool includeInactive = false;
        if (request.has_param("includeInactive")) {
            std::string value = request.get_param_value("includeInactive");
            if (value != "true" && value != "false") return sendJson(response, 400, {{"error", "includeInactive must be true or false"}});
            Student viewer;
            includeInactive = value == "true" && repository.findById(databaseId, viewer) && viewer.role == "admin";
        }
        std::vector<Classroom> rooms;
        if (!campusRepository.listClassrooms(
                request.has_param("q") ? request.get_param_value("q") : "",
                buildingId, floorId,
                request.has_param("roomType") ? request.get_param_value("roomType") : "",
                includeInactive, rooms))
            return sendJson(response, 500, {{"error", "Classrooms could not be loaded"}});
        json result = json::array();
        for (const auto& room : rooms) result.push_back(publicClassroom(room));
        sendJson(response, 200, {{"rooms", result}, {"occupancyAvailable", false}});
    });

    server.Get(R"(/api/rooms/([0-9]+))", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, false);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Forbidden"}});
        int id = 0;
        try { id = std::stoi(request.matches[1].str()); } catch (...) { return sendJson(response, 400, {{"error", "Invalid classroom ID"}}); }
        Classroom room;
        if (!campusRepository.findClassroom(id, room) || (!room.isActive && accessStatus(request, sessions, repository, databaseId, true) != 0))
            return sendJson(response, 404, {{"error", "Classroom not found"}});
        sendJson(response, 200, {{"room", publicClassroom(room)}});
    });

    server.Post("/api/rooms", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            json body = json::parse(request.body);
            Classroom room;
            if (!readClassroomBody(body, room)) return sendJson(response, 400, {{"error", "Required classroom fields are invalid"}});
            std::string error;
            if (!campusRepository.saveClassroom(room, error)) {
                if (error == "duplicate_room_code") return sendJson(response, 409, {{"error", "Room code already exists in this building"}});
                if (error == "active_enrollments_exceed_capacity") return sendJson(response, 409, {{"error", "Room capacity cannot be less than current section enrollments"}});
                if (error == "invalid_building_or_floor") return sendJson(response, 400, {{"error", "Selected floor does not belong to an active building"}});
                return sendJson(response, 500, {{"error", "Classroom could not be created"}});
            }
            sendJson(response, 201, {{"room", publicClassroom(room)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid classroom request"}});
        }
    });

    server.Put(R"(/api/rooms/([0-9]+))", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            int id = std::stoi(request.matches[1].str());
            Classroom room;
            if (!campusRepository.findClassroom(id, room)) return sendJson(response, 404, {{"error", "Classroom not found"}});
            json body = json::parse(request.body);
            if (!readClassroomBody(body, room)) return sendJson(response, 400, {{"error", "Required classroom fields are invalid"}});
            room.id = id;
            std::string error;
            if (!campusRepository.saveClassroom(room, error)) {
                if (error == "duplicate_room_code") return sendJson(response, 409, {{"error", "Room code already exists in this building"}});
                if (error == "active_enrollments_exceed_capacity") return sendJson(response, 409, {{"error", "Room capacity cannot be less than current section enrollments"}});
                if (error == "invalid_building_or_floor") return sendJson(response, 400, {{"error", "Selected floor does not belong to an active building"}});
                return sendJson(response, 500, {{"error", "Classroom could not be updated"}});
            }
            sendJson(response, 200, {{"room", publicClassroom(room)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid classroom request"}});
        }
    });

    server.Patch(R"(/api/rooms/([0-9]+)/status)", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            int id = std::stoi(request.matches[1].str());
            json body = json::parse(request.body);
            if (!body.contains("isActive") || !body["isActive"].is_boolean())
                return sendJson(response, 400, {{"error", "isActive must be a boolean"}});
            std::string error;
            if (!campusRepository.setClassroomActive(id, body["isActive"].get<bool>(), error)) {
                if (error == "not_found") return sendJson(response, 404, {{"error", "Classroom not found"}});
                return sendJson(response, 500, {{"error", "Classroom status could not be updated"}});
            }
            Classroom room;
            if (!campusRepository.findClassroom(id, room)) return sendJson(response, 404, {{"error", "Classroom not found"}});
            sendJson(response, 200, {{"room", publicClassroom(room)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid classroom status request"}});
        }
    });

    server.Get("/api/sections", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, false);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Forbidden"}});
        int buildingId = queryId(request, "buildingId");
        if (buildingId < 0) return sendJson(response, 400, {{"error", "Invalid building filter"}});
        bool includeInactive = false;
        if (request.has_param("includeInactive")) {
            std::string value = request.get_param_value("includeInactive");
            if (value != "true" && value != "false") return sendJson(response, 400, {{"error", "includeInactive must be true or false"}});
            Student viewer;
            includeInactive = value == "true" && repository.findById(databaseId, viewer) && viewer.role == "admin";
        }
        std::vector<AcademicSection> sections;
        if (!campusRepository.listSections(
                request.has_param("q") ? request.get_param_value("q") : "",
                request.has_param("term") ? request.get_param_value("term") : "",
                request.has_param("day") ? request.get_param_value("day") : "",
                buildingId, includeInactive, sections))
            return sendJson(response, 500, {{"error", "Sections could not be loaded"}});
        json result = json::array();
        for (const auto& section : sections) result.push_back(publicSection(section));
        sendJson(response, 200, {{"sections", result}});
    });

    server.Get(R"(/api/sections/([0-9]+))", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, false);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Forbidden"}});
        int id = 0;
        try { id = std::stoi(request.matches[1].str()); } catch (...) { return sendJson(response, 400, {{"error", "Invalid section ID"}}); }
        AcademicSection section;
        if (!campusRepository.findSection(id, section) || (!section.isActive && accessStatus(request, sessions, repository, databaseId, true) != 0))
            return sendJson(response, 404, {{"error", "Section not found"}});
        sendJson(response, 200, {{"section", publicSection(section)}});
    });

    server.Post("/api/sections", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            json body = json::parse(request.body);
            AcademicSection section;
            if (!readSectionBody(body, section)) return sendJson(response, 400, {{"error", "Required section fields or time range are invalid"}});
            std::string error;
            if (!campusRepository.saveSection(section, error)) {
                if (error == "duplicate_section") return sendJson(response, 409, {{"error", "This course section already exists for the term"}});
                if (error == "invalid_meeting" || error == "invalid_capacity") return sendJson(response, 400, {{"error", "Meeting details and capacities must be valid"}});
                if (error == "active_enrollments_exceed_capacity") return sendJson(response, 409, {{"error", "Room or meeting capacity cannot be less than its current enrollment"}});
                if (error == "meeting_has_enrollments") return sendJson(response, 409, {{"error", "A lab meeting with enrolled students cannot be removed or changed to a non-lab activity"}});
                if (error == "classroom_not_active") return sendJson(response, 400, {{"error", "Assigned classroom does not exist or is inactive"}});
                return sendJson(response, 500, {{"error", "Section could not be created"}});
            }
            sendJson(response, 201, {{"section", publicSection(section)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid section request"}});
        }
    });

    server.Put(R"(/api/sections/([0-9]+))", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            int id = std::stoi(request.matches[1].str());
            AcademicSection section;
            if (!campusRepository.findSection(id, section)) return sendJson(response, 404, {{"error", "Section not found"}});
            json body = json::parse(request.body);
            if (!readSectionBody(body, section, true)) return sendJson(response, 400, {{"error", "Required section fields or time range are invalid"}});
            section.id = id;
            std::string error;
            if (!campusRepository.saveSection(section, error)) {
                if (error == "duplicate_section") return sendJson(response, 409, {{"error", "This course section already exists for the term"}});
                if (error == "invalid_meeting" || error == "invalid_capacity") return sendJson(response, 400, {{"error", "Meeting details and capacities must be valid"}});
                if (error == "active_enrollments_exceed_capacity") return sendJson(response, 409, {{"error", "Room or meeting capacity cannot be less than its current enrollment"}});
                if (error == "meeting_has_enrollments") return sendJson(response, 409, {{"error", "A lab meeting with enrolled students cannot be removed or changed to a non-lab activity"}});
                if (error == "classroom_not_active") return sendJson(response, 400, {{"error", "Assigned classroom does not exist or is inactive"}});
                return sendJson(response, 500, {{"error", "Section could not be updated"}});
            }
            sendJson(response, 200, {{"section", publicSection(section)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid section request"}});
        }
    });

    server.Patch(R"(/api/sections/([0-9]+)/classroom)", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            int id = std::stoi(request.matches[1].str());
            AcademicSection section;
            if (!campusRepository.findSection(id, section)) return sendJson(response, 404, {{"error", "Section not found"}});
            if (!section.meetings.empty())
                return sendJson(response, 400, {{"error", "Scheduled sections require classroom assignments on their individual meetings"}});
            json body = json::parse(request.body);
            if (!body.contains("classroomId") || (!body["classroomId"].is_null() &&
                (!body["classroomId"].is_number_integer() || body["classroomId"].get<int>() <= 0)))
                return sendJson(response, 400, {{"error", "classroomId must be a room ID or null"}});
            section.classroomId = body["classroomId"].is_null() ? 0 : body["classroomId"].get<int>();
            std::string error;
            if (!campusRepository.saveSection(section, error)) {
                if (error == "active_enrollments_exceed_capacity") return sendJson(response, 409, {{"error", "Classroom capacity cannot be less than active section enrollments"}});
                if (error == "meeting_has_enrollments") return sendJson(response, 409, {{"error", "A lab meeting with enrolled students cannot be removed or changed to a non-lab activity"}});
                if (error == "classroom_not_active") return sendJson(response, 400, {{"error", "Assigned classroom does not exist or is inactive"}});
                return sendJson(response, 500, {{"error", "Classroom assignment could not be updated"}});
            }
            sendJson(response, 200, {{"section", publicSection(section)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid classroom assignment request"}});
        }
    });

    server.Patch(R"(/api/sections/([0-9]+)/status)", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            int id = std::stoi(request.matches[1].str());
            json body = json::parse(request.body);
            if (!body.contains("isActive") || !body["isActive"].is_boolean())
                return sendJson(response, 400, {{"error", "isActive must be a boolean"}});
            std::string error;
            if (!campusRepository.setSectionActive(id, body["isActive"].get<bool>(), error)) {
                if (error == "not_found") return sendJson(response, 404, {{"error", "Section not found"}});
                return sendJson(response, 500, {{"error", "Section status could not be updated"}});
            }
            AcademicSection section;
            if (!campusRepository.findSection(id, section)) return sendJson(response, 404, {{"error", "Section not found"}});
            sendJson(response, 200, {{"section", publicSection(section)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid section status request"}});
        }
    });

    server.Patch(R"(/api/sections/([0-9]+)/enrollment-status)", [&sessions, &repository, &campusRepository](const httplib::Request& request, httplib::Response& response) {
        int databaseId = 0;
        int status = accessStatus(request, sessions, repository, databaseId, true);
        if (status) return sendJson(response, status, {{"error", status == 401 ? "Unauthenticated" : "Admin role required"}});
        try {
            int id = std::stoi(request.matches[1].str());
            json body = json::parse(request.body);
            if (!body.contains("isOpenForEnrollment") || !body["isOpenForEnrollment"].is_boolean())
                return sendJson(response, 400, {{"error", "isOpenForEnrollment must be a boolean"}});
            std::string error;
            if (!campusRepository.setSectionEnrollmentOpen(id, body["isOpenForEnrollment"].get<bool>(), error)) {
                if (error == "not_found") return sendJson(response, 404, {{"error", "Section not found"}});
                return sendJson(response, 500, {{"error", "Section enrollment status could not be updated"}});
            }
            AcademicSection section;
            if (!campusRepository.findSection(id, section)) return sendJson(response, 404, {{"error", "Section not found"}});
            sendJson(response, 200, {{"section", publicSection(section)}});
        } catch (...) {
            sendJson(response, 400, {{"error", "Invalid section enrollment status request"}});
        }
    });

    registerEnrollmentRoutes(server, enrollmentRepository, repository, sessions);
    registerReservationRoutes(server, reservationRepository, repository, sessions);
    registerNavigationRoutes(server, navigationRepository, repository, sessions);
    registerMapRoutes(server, mapRepository, repository, sessions);

    int port = 8080;
    if (const char* configuredPort = std::getenv("PORT")) {
        if (*configuredPort) {
            char* end = nullptr;
            const long parsedPort = std::strtol(configuredPort, &end, 10);
            if (!end || *end || parsedPort < 1 || parsedPort > 65535) {
                std::cerr << "PORT must be an integer between 1 and 65535\n";
                sqlite3_close(database);
                return 1;
            }
            port = static_cast<int>(parsedPort);
        }
    }

    std::cout << "IU Smart Campus API listening on 0.0.0.0:" << port << "\n";
    std::cout << "GET /api/health\nGET /api/navigation/locations\nPOST /api/navigation/route\n";
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Unable to start HTTP server on port " << port << "\n";
        sqlite3_close(database);
        return 1;
    }
    sqlite3_close(database);
}
