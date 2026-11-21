#pragma once
#include "models/CampusRecords.h"
#include <string>
#include <vector>

struct AvailableSection {
    int sectionId = 0;
    std::string courseCode;
    std::string courseName;
    std::string sectionCode;
    std::string term;
    std::string day;
    std::string startTime;
    std::string endTime;
    int classroomId = 0;
    std::string classroomCode;
    std::string buildingName;
    int capacity = 0;
    int enrolledCount = 0;
    int remainingSeats = 0;
    bool capacityKnown = false;
    bool openForEnrollment = false;
    bool isFull = false;
    std::vector<SectionMeeting> meetings;
};

struct Enrollment {
    int id = 0;
    int studentId = 0;
    int sectionId = 0;
    int labMeetingId = 0;
    std::string status;
    std::string createdAt;
    std::string updatedAt;
    std::string courseCode;
    std::string courseName;
    std::string sectionCode;
    std::string term;
    std::string day;
    std::string startTime;
    std::string endTime;
    std::string classroomCode;
    std::string buildingName;
    std::vector<SectionMeeting> meetings;
};
