#pragma once
#include "data_structures/Graph.h"
#include "models/CampusRecords.h"
#include <string>
#include <vector>

struct MapPoint {
    double x = 0;
    double y = 0;
};

struct MapBuilding {
    Building building;
    int floorCount = 0;
    int mappedFloorCount = 0;
};

struct MapFloorInfo {
    int id = 0;
    int buildingId = 0;
    std::string label;
    std::string description;
    bool hasMap = false;
    std::string coordinateSystem = "LOCAL_FLOOR_X_RIGHT_Y_DOWN";
    std::string unitLabel = "local units";
    double width = 0;
    double height = 0;
    double metersPerUnit = 0;
    bool scaleKnown = false;
    double elevation = 0;
    double floorHeight = 3;
    std::string verificationStatus = "NO_MAP";
    std::string sourceReference;
};

struct MapFeature {
    int id = 0;
    int floorId = 0;
    int buildingId = 0;
    int classroomId = 0;
    int navigationLocationId = -1;
    int edgeFromId = -1;
    int edgeToId = -1;
    std::string key;
    std::string type;
    std::string label;
    std::string geometryType;
    std::vector<MapPoint> points;
    double elevation = 0;
    double height = 0;
    double thickness = 0;
    bool isDemo = false;
    bool isActive = true;
};

struct MapFloorData {
    MapFloorInfo floor;
    std::vector<MapFeature> features;
    std::vector<GraphVertex> navigationLocations;
};

struct MapSeatSection {
    int id = 0;
    std::string courseCode;
    std::string courseName;
    std::string sectionCode;
    std::string term;
    std::string day;
    std::string startTime;
    std::string endTime;
    int capacity = 0;
    int enrolledCount = 0;
    int remainingSeats = 0;
};

struct MapRoomDetails {
    Classroom room;
    std::vector<MapSeatSection> sections;
};
