#pragma once
#include <string>
#include <vector>

struct Building {
    int id = 0;
    std::string code;
    std::string name;
    std::string description;
    bool isActive = true;
};

struct Floor {
    int id = 0;
    int buildingId = 0;
    std::string label;
    std::string description;
};

struct Classroom {
    int id = 0;
    int buildingId = 0;
    std::string buildingName;
    int floorId = 0;
    std::string floorLabel;
    std::string roomCode;
    std::string roomName;
    std::string roomType;
    int capacity = 0;
    bool capacityKnown = true;
    bool isActive = true;
    std::string notes;
};

struct SectionMeeting {
    int id = 0;
    std::string activityType;
    std::string day;
    std::string startTime;
    std::string endTime;
    int classroomId = 0;
    std::string venueLabel;
    std::string instructor;
    std::string classroomCode;
    std::string buildingName;
    int enrollmentCapacity = 0;
    int capacity = 0;
    int enrolledCount = 0;
    int remainingSeats = 0;
    bool capacityKnown = false;
    bool isLabOption = false;
};

struct AcademicSection {
    int id = 0;
    std::string courseCode;
    std::string courseName;
    std::string sectionCode;
    std::string instructor;
    std::string term;
    std::string day;
    std::string startTime;
    std::string endTime;
    int classroomId = 0;
    std::string classroomCode;
    std::string buildingName;
    int enrollmentCapacity = 0;
    std::vector<SectionMeeting> meetings;
    bool isActive = true;
    bool isOpenForEnrollment = false;
};
