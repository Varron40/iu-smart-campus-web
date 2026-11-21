#pragma once
#include "models/CampusMap.h"
#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>

class MapRepository {
public:
    explicit MapRepository(sqlite3* database);
    bool initialize();
    bool listBuildings(bool includeInactive, std::vector<MapBuilding>& buildings);
    bool listFloors(int buildingId, std::vector<MapFloorInfo>& floors, std::string& error);
    bool getFloorFeatures(int floorId, MapFloorData& data, std::string& error, bool includeInactive = false);
    bool getRoomDetails(int roomId, MapRoomDetails& details, std::string& error);
    bool saveFloorMetadata(const MapFloorInfo& metadata, std::string& error);
    bool saveFeature(const MapFeature& feature, MapFeature& saved, std::string& error);
    bool setFeatureActive(int featureId, bool isActive, std::string& error);
private:
    sqlite3* database;
    std::mutex repositoryMutex;
    bool readFloorMetadata(int floorId, MapFloorInfo& floor);
    bool validateFeature(const MapFeature& feature, const MapFloorInfo& floor, std::string& error);
    bool seedDemoMap();
    bool seedCsGroundFloor();
};
