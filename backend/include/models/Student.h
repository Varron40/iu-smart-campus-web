#pragma once
#include <string>

struct Student {
    int id = 0;
    std::string studentId;
    std::string fullName;
    std::string universityEmail;
    std::string passwordHash;
    std::string faculty;
    std::string department;
    std::string academicYear;
    std::string createdAt;
    std::string role = "student";
};
