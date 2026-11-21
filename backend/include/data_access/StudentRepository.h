#pragma once
#include "models/Student.h"
#include <sqlite3.h>
#include <string>

class StudentRepository {
public:
    explicit StudentRepository(sqlite3* database);
    bool initialize();
    bool create(const Student& student, std::string& error);
    bool findByEmail(const std::string& email, Student& student);
    bool findById(int id, Student& student);
    bool findByStudentId(const std::string& studentId, Student& student);
    bool updateProfile(int id, const std::string& fullName, const std::string& faculty,
                       const std::string& department, const std::string& academicYear, std::string& error);
private:
    sqlite3* database;
};
