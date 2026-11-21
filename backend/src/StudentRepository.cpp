#include "data_access\StudentRepository.h"
#include <string>

StudentRepository::StudentRepository(sqlite3* database) : database(database) {}

bool StudentRepository::initialize() {
    const char* schema = R"SQL(
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS students (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 student_id TEXT NOT NULL UNIQUE,
 full_name TEXT NOT NULL,
 university_email TEXT NOT NULL UNIQUE,
 password_hash TEXT NOT NULL,
 faculty TEXT NOT NULL,
 department TEXT,
 academic_year TEXT,
 role TEXT NOT NULL DEFAULT 'student' CHECK(role IN ('student','admin')),
 created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)SQL";
    char* error = nullptr;
    bool ok = sqlite3_exec(database, schema, nullptr, nullptr, &error) == SQLITE_OK;
    if (error) sqlite3_free(error);
    if (!ok) return false;

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "PRAGMA table_info(students)", -1, &statement, nullptr) != SQLITE_OK) return false;
    bool hasRole = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name && std::string(name) == "role") hasRole = true;
    }
    sqlite3_finalize(statement);
    if (hasRole) return true;
    return sqlite3_exec(database,
        "ALTER TABLE students ADD COLUMN role TEXT NOT NULL DEFAULT 'student' CHECK(role IN ('student','admin'))",
        nullptr, nullptr, nullptr) == SQLITE_OK;
}

static void readStudent(sqlite3_stmt* statement, Student& student) {
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
}

bool StudentRepository::create(const Student& student, std::string& error) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO students(student_id,full_name,university_email,password_hash,faculty,department,academic_year,role) VALUES(?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, student.studentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, student.fullName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, student.universityEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, student.passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, student.faculty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, student.department.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 7, student.academicYear.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 8, student.role.c_str(), -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        const char* message = sqlite3_errmsg(database);
        if (std::string(message).find("UNIQUE constraint failed") != std::string::npos) {
            error = "student_id_or_email_exists";
        } else error = "database_error";
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool StudentRepository::findByEmail(const std::string& email, Student& student) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT id,student_id,full_name,university_email,password_hash,faculty,department,academic_year,created_at,role FROM students WHERE university_email=? COLLATE NOCASE";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readStudent(statement, student);
    sqlite3_finalize(statement);
    return found;
}

bool StudentRepository::findById(int id, Student& student) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT id,student_id,full_name,university_email,password_hash,faculty,department,academic_year,created_at,role FROM students WHERE id=?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, id);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readStudent(statement, student);
    sqlite3_finalize(statement);
    return found;
}

bool StudentRepository::findByStudentId(const std::string& studentId, Student& student) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT id,student_id,full_name,university_email,password_hash,faculty,department,academic_year,created_at,role FROM students WHERE student_id=?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, studentId.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readStudent(statement, student);
    sqlite3_finalize(statement);
    return found;
}

bool StudentRepository::updateProfile(int id, const std::string& fullName, const std::string& faculty,
                                      const std::string& department, const std::string& academicYear, std::string& error) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE students SET full_name=?,faculty=?,department=?,academic_year=? WHERE id=?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) { error = "database_error"; return false; }
    sqlite3_bind_text(statement, 1, fullName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, faculty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, department.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, academicYear.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 5, id);
    bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) error = "database_error";
    sqlite3_finalize(statement);
    return ok;
}
