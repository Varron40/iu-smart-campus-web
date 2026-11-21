#pragma once
#include "data_access/StudentRepository.h"
#include "services/SessionStore.h"
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace Api {
using Json = nlohmann::json;

inline void setCors(httplib::Response& response) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    response.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
}

inline void sendJson(httplib::Response& response, int status, const Json& body) {
    setCors(response);
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

inline int authorize(const httplib::Request& request, const SessionStore& sessions,
                     StudentRepository& repository, int& studentId,
                     bool adminOnly = false, bool studentOnly = false) {
    const std::string authorization = request.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) != 0 ||
        !sessions.find(authorization.substr(prefix.size()), studentId)) return 401;
    Student student;
    if (!repository.findById(studentId, student)) return 401;
    if (adminOnly && student.role != "admin") return 403;
    if (studentOnly && student.role != "student") return 403;
    return 0;
}

}
