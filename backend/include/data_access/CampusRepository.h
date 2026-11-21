#pragma once
#include "data_structures/HashTable.h"
#include "models/CampusRecords.h"
#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>

class CampusRepository {
public:
    explicit CampusRepository(sqlite3* database);
    bool initialize(std::string* error = nullptr);
    bool importSchedule(const std::string& path, std::string& error);
    bool listBuildings(std::vector<Building>& buildings);
    bool buildingExists(int id);
    bool listFloors(int buildingId, std::vector<Floor>& floors);
    bool listClassrooms(const std::string& query, int buildingId, int floorId,
                        const std::string& roomType, bool includeInactive,
                        std::vector<Classroom>& classrooms);
    bool findClassroom(int id, Classroom& classroom);
    bool saveClassroom(Classroom& classroom, std::string& error);
    bool setClassroomActive(int id, bool active, std::string& error);
    bool listSections(const std::string& query, const std::string& term,
                      const std::string& day, int buildingId, bool includeInactive,
                      std::vector<AcademicSection>& sections);
    bool findSection(int id, AcademicSection& section);
    bool saveSection(AcademicSection& section, std::string& error);
    bool setSectionActive(int id, bool active, std::string& error);
    bool setSectionEnrollmentOpen(int id, bool open, std::string& error);
private:
    sqlite3* database;
    HashTable<std::string, Classroom> classroomsByBuildingAndCode;
    std::mutex classroomIndexMutex;
    bool refreshClassroomIndex();
    bool validLocation(int buildingId, int floorId);
    static std::string roomKey(int buildingId, const std::string& roomCode);
};
