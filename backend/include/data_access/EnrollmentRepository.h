#pragma once
#include "models/Enrollment.h"
#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>

class EnrollmentRepository {
public:
    explicit EnrollmentRepository(const std::string& databasePath);
    ~EnrollmentRepository();
    EnrollmentRepository(const EnrollmentRepository&) = delete;
    EnrollmentRepository& operator=(const EnrollmentRepository&) = delete;
    bool initialize();
    bool listAvailable(const std::string& query, const std::string& term, const std::string& group,
                       std::vector<AvailableSection>& sections);
    bool enroll(int studentId, int sectionId, int labMeetingId,
                Enrollment& enrollment, std::string& error);
    bool withdraw(int studentId, int enrollmentId, Enrollment& enrollment, std::string& error);
    bool listForStudent(int studentId, bool activeOnly, std::vector<Enrollment>& enrollments);
private:
    sqlite3* database = nullptr;
    std::mutex transactionMutex;
};
