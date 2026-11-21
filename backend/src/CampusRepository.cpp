#include "data_access/CampusRepository.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>

namespace {
using Json = nlohmann::json;

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

bool sqliteExec(sqlite3* database, const char* sql) {
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

void readClassroom(sqlite3_stmt* statement, Classroom& room) {
    room.id = sqlite3_column_int(statement, 0);
    room.buildingId = sqlite3_column_int(statement, 1);
    room.buildingName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
    room.floorId = sqlite3_column_int(statement, 3);
    const char* floorLabel = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
    room.floorLabel = floorLabel ? floorLabel : "";
    room.roomCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
    room.roomName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
    room.roomType = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
    room.capacity = sqlite3_column_int(statement, 8);
    room.capacityKnown = sqlite3_column_int(statement, 9) != 0;
    room.isActive = sqlite3_column_int(statement, 10) != 0;
    const char* notes = reinterpret_cast<const char*>(sqlite3_column_text(statement, 11));
    room.notes = notes ? notes : "";
}

void readSection(sqlite3_stmt* statement, AcademicSection& section) {
    section.id = sqlite3_column_int(statement, 0);
    section.courseCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    section.courseName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
    section.sectionCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
    const char* instructor = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
    section.instructor = instructor ? instructor : "";
    section.term = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
    const char* day = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
    section.day = day ? day : "";
    const char* start = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
    section.startTime = start ? start : "";
    const char* end = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
    section.endTime = end ? end : "";
    section.classroomId = sqlite3_column_int(statement, 9);
    const char* roomCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 10));
    section.classroomCode = roomCode ? roomCode : "";
    const char* building = reinterpret_cast<const char*>(sqlite3_column_text(statement, 11));
    section.buildingName = building ? building : "";
    section.enrollmentCapacity = sqlite3_column_int(statement, 12);
    section.isActive = sqlite3_column_int(statement, 13) != 0;
    section.isOpenForEnrollment = sqlite3_column_int(statement, 14) != 0;
}

const char* classroomSelect =
    "SELECT c.id,c.building_id,b.name,c.floor_id,f.label,c.room_code,c.room_name,c.room_type,c.capacity,c.capacity_known,c.is_active,c.notes "
    "FROM classrooms c JOIN buildings b ON b.id=c.building_id JOIN floors f ON f.id=c.floor_id";
const char* sectionSelect =
    "SELECT s.id,s.course_code,s.course_name,s.section_code,s.instructor,s.term,s.day,s.start_time,s.end_time,"
    "s.classroom_id,c.room_code,b.name,s.enrollment_capacity,s.is_active,s.is_open_for_enrollment FROM academic_sections s "
    "LEFT JOIN classrooms c ON c.id=s.classroom_id LEFT JOIN buildings b ON b.id=c.building_id";

std::string normalizeVenueLabel(const std::string& value) {
    std::string normalized;
    for (unsigned char character : value) {
        if (!std::isspace(character))
            normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

bool isLabActivity(const std::string& activity) {
    std::string normalized;
    normalized.reserve(activity.size());
    for (unsigned char character : activity)
        normalized.push_back(static_cast<char>(std::tolower(character)));
    return normalized.find("lab") != std::string::npos ||
           normalized.find("practical") != std::string::npos;
}

bool hasConstraintError(sqlite3* database) {
    return sqlite3_extended_errcode(database) == SQLITE_CONSTRAINT_UNIQUE ||
           sqlite3_extended_errcode(database) == SQLITE_CONSTRAINT_PRIMARYKEY;
}

bool activeEnrollmentsExceedCapacity(sqlite3* database) {
    const char* message = sqlite3_errmsg(database);
    return message && std::string(message).find("active_enrollments_exceed_capacity") != std::string::npos;
}

bool hasColumn(sqlite3* database, const char* table, const char* column) {
    std::string sql = "PRAGMA table_info(" + std::string(table) + ")";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    bool found = false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name && column == std::string(name)) { found = true; break; }
    }
    sqlite3_finalize(statement);
    return found;
}

bool hasTable(sqlite3* database, const char* table) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bindText(statement, 1, table);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

bool hasActiveLabEnrollment(sqlite3* database, int meetingId) {
    if (!hasTable(database, "enrollments") || !hasColumn(database, "enrollments", "lab_meeting_id")) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM enrollments WHERE lab_meeting_id=? AND status='ENROLLED' LIMIT 1", -1, &statement, nullptr) != SQLITE_OK)
        return true;
    sqlite3_bind_int(statement, 1, meetingId);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

bool updateRoomReferences(sqlite3* database, int oldId, int canonicalId, std::string& error) {
    const std::pair<const char*, const char*> references[] = {
        {"academic_sections", "classroom_id"},
        {"academic_section_meetings", "classroom_id"},
        {"reservation_requests", "classroom_id"},
        {"map_features", "classroom_id"}
    };
    for (const auto& reference : references) {
        if (!hasTable(database, reference.first) ||
            !hasColumn(database, reference.first, reference.second))
            continue;
        std::string sql = "UPDATE " + std::string(reference.first) + " SET " +
            reference.second + "=? WHERE " + reference.second + "=?";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(database);
            return false;
        }
        sqlite3_bind_int(statement, 1, canonicalId);
        sqlite3_bind_int(statement, 2, oldId);
        bool ok = sqlite3_step(statement) == SQLITE_DONE;
        if (!ok) error = sqlite3_errmsg(database);
        sqlite3_finalize(statement);
        if (!ok) return false;
    }
    return true;
}

bool mergeRoomAliases(sqlite3* database, std::string& error) {
    struct RoomAlias {
        int id = 0;
        int buildingId = 0;
        std::string roomCode;
        int capacity = 0;
        bool capacityKnown = false;
        bool active = false;
    };
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT id,building_id,room_code,capacity,capacity_known,is_active "
        "FROM classrooms ORDER BY building_id,id";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        return false;
    }
    std::map<std::string, std::vector<RoomAlias>> aliases;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        RoomAlias room;
        room.id = sqlite3_column_int(statement, 0);
        room.buildingId = sqlite3_column_int(statement, 1);
        const char* code = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        room.roomCode = code ? code : "";
        room.capacity = sqlite3_column_int(statement, 3);
        room.capacityKnown = sqlite3_column_int(statement, 4) != 0;
        room.active = sqlite3_column_int(statement, 5) != 0;
        aliases[std::to_string(room.buildingId) + ":" + normalizeVenueLabel(room.roomCode)].push_back(std::move(room));
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        error = sqlite3_errmsg(database);
        return false;
    }
    if (!sqliteExec(database, "BEGIN IMMEDIATE")) {
        error = sqlite3_errmsg(database);
        return false;
    }
    auto rollback = [database]() { sqliteExec(database, "ROLLBACK"); };
    for (auto& pair : aliases) {
        std::vector<RoomAlias>& rooms = pair.second;
        if (rooms.size() < 2) continue;
        auto canonical = std::min_element(rooms.begin(), rooms.end(), [](const RoomAlias& left, const RoomAlias& right) {
            if (left.capacityKnown != right.capacityKnown) return left.capacityKnown > right.capacityKnown;
            if (left.active != right.active) return left.active > right.active;
            return left.id < right.id;
        });
        const int canonicalId = canonical->id;
        bool active = true;
        bool capacityKnown = false;
        int capacity = 0;
        for (const RoomAlias& room : rooms) {
            active = active && room.active;
            if (room.capacityKnown) {
                capacityKnown = true;
                capacity = capacity == 0 ? room.capacity : std::min(capacity, room.capacity);
            }
        }
        for (const RoomAlias& room : rooms) {
            if (room.id != canonicalId && !updateRoomReferences(database, room.id, canonicalId, error)) {
                rollback();
                return false;
            }
        }
        if (sqlite3_prepare_v2(database,
                "UPDATE classrooms SET capacity=?,capacity_known=?,is_active=? WHERE id=?",
                -1, &statement, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(database);
            rollback();
            return false;
        }
        sqlite3_bind_int(statement, 1, capacityKnown ? capacity : canonical->capacity);
        sqlite3_bind_int(statement, 2, capacityKnown ? 1 : 0);
        sqlite3_bind_int(statement, 3, active ? 1 : 0);
        sqlite3_bind_int(statement, 4, canonicalId);
        bool updated = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        if (!updated) {
            error = sqlite3_errmsg(database);
            rollback();
            return false;
        }
        for (const RoomAlias& room : rooms) {
            if (room.id == canonicalId) continue;
            if (sqlite3_prepare_v2(database, "DELETE FROM classrooms WHERE id=?", -1, &statement, nullptr) != SQLITE_OK) {
                error = sqlite3_errmsg(database);
                rollback();
                return false;
            }
            sqlite3_bind_int(statement, 1, room.id);
            bool removed = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
            sqlite3_finalize(statement);
            if (!removed) {
                error = sqlite3_errmsg(database);
                rollback();
                return false;
            }
        }
    }
    if (!sqliteExec(database, "COMMIT")) {
        error = sqlite3_errmsg(database);
        rollback();
        return false;
    }
    return true;
}

bool loadMeetings(sqlite3* database, int sectionId, std::vector<SectionMeeting>& meetings) {
    const char* sql =
        "SELECT m.id,m.activity_type,m.day,m.start_time,m.end_time,m.classroom_id,m.venue_label,m.instructor,"
        "m.enrollment_capacity,c.room_code,b.name FROM academic_section_meetings m "
        "LEFT JOIN classrooms c ON c.id=m.classroom_id LEFT JOIN buildings b ON b.id=c.building_id "
        "WHERE m.section_id=? ORDER BY m.sort_order,m.id";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, sectionId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        SectionMeeting meeting;
        meeting.id = sqlite3_column_int(statement, 0);
        const char* activity = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        const char* day = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        const char* start = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
        const char* end = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
        meeting.activityType = activity ? activity : "";
        meeting.isLabOption = isLabActivity(meeting.activityType);
        meeting.day = day ? day : "";
        meeting.startTime = start ? start : "";
        meeting.endTime = end ? end : "";
        meeting.classroomId = sqlite3_column_type(statement, 5) == SQLITE_NULL ? 0 : sqlite3_column_int(statement, 5);
        const char* venue = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
        const char* instructor = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
        meeting.enrollmentCapacity = sqlite3_column_type(statement, 8) == SQLITE_NULL
            ? 0 : sqlite3_column_int(statement, 8);
        const char* room = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
        const char* building = reinterpret_cast<const char*>(sqlite3_column_text(statement, 10));
        meeting.venueLabel = venue ? venue : "";
        meeting.instructor = instructor ? instructor : "";
        meeting.classroomCode = room ? room : "";
        meeting.buildingName = building ? building : "";
        meetings.push_back(std::move(meeting));
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

struct VenueDescriptor {
    std::string buildingCode;
    std::string buildingName;
    std::string floorLabel;
    std::string roomType;
    std::string notes;
};

VenueDescriptor describeVenue(const std::string& venue) {
    VenueDescriptor result;
    result.buildingCode = "UNVERIFIED";
    result.buildingName = "Unverified venue location";
    result.floorLabel = "Unspecified";
    result.roomType = normalizeVenueLabel(venue).find("lab") != std::string::npos
        ? "Laboratory" : "Other";
    result.notes = "Imported timetable venue; labels differing only by case or repeated spaces share one classroom record. Building/floor and geographic location are unverified; capacity must be entered by an administrator.";

    std::smatch match;
    static const std::regex hallPattern(R"(^hall\s+a([0-9])\.[0-9]+$)", std::regex_constants::icase);
    static const std::regex buildingBPattern(R"(^b\s*([0-9])\.[0-9]+$)", std::regex_constants::icase);
    if (std::regex_match(venue, match, hallPattern)) {
        result.buildingCode = "A";
        result.buildingName = "Building A (timetable label; site unverified)";
        result.floorLabel = match[1].str();
    } else if (normalizeVenueLabel(venue) == "halla01") {
        result.buildingCode = "A";
        result.buildingName = "Building A (timetable label; site unverified)";
    } else if (std::regex_match(venue, match, buildingBPattern) ||
               normalizeVenueLabel(venue).rfind("b.classroom", 0) == 0) {
        result.buildingCode = "B";
        result.buildingName = "Building B (timetable label; site unverified)";
        if (match.size() > 1 && match[1].matched) result.floorLabel = match[1].str();
    } else if (normalizeVenueLabel(venue).rfind("c", 0) == 0) {
        result.buildingCode = "C";
        result.buildingName = "Building C (timetable label; site unverified)";
    }
    if (normalizeVenueLabel(venue).rfind("hall", 0) == 0)
        result.roomType = "Lecture room";
    return result;
}
}

CampusRepository::CampusRepository(sqlite3* database) : database(database) {}

std::string CampusRepository::roomKey(int buildingId, const std::string& roomCode) {
    return std::to_string(buildingId) + ":" + roomCode;
}

bool CampusRepository::initialize(std::string* error) {
    auto setError = [error, this](const std::string& message) {
        if (error) *error = message.empty() ? sqlite3_errmsg(database) : message;
    };
    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS buildings (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 code TEXT NOT NULL UNIQUE,
 name TEXT NOT NULL,
 description TEXT NOT NULL DEFAULT '',
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1))
);
CREATE TABLE IF NOT EXISTS floors (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 building_id INTEGER NOT NULL REFERENCES buildings(id) ON DELETE RESTRICT,
 label TEXT NOT NULL,
 description TEXT NOT NULL DEFAULT '',
 UNIQUE(building_id,label),
 UNIQUE(id,building_id)
);
CREATE TABLE IF NOT EXISTS classrooms (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 building_id INTEGER NOT NULL REFERENCES buildings(id) ON DELETE RESTRICT,
 floor_id INTEGER NOT NULL,
 room_code TEXT NOT NULL,
 room_name TEXT NOT NULL,
 room_type TEXT NOT NULL,
 capacity INTEGER NOT NULL CHECK(capacity > 0),
 capacity_known INTEGER NOT NULL DEFAULT 1 CHECK(capacity_known IN (0,1)),
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),
 notes TEXT NOT NULL DEFAULT '',
 UNIQUE(building_id,room_code),
 FOREIGN KEY(floor_id,building_id) REFERENCES floors(id,building_id) ON DELETE RESTRICT
);
CREATE TABLE IF NOT EXISTS academic_sections (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 course_code TEXT NOT NULL,
 course_name TEXT NOT NULL,
 section_code TEXT NOT NULL,
 instructor TEXT NOT NULL DEFAULT '',
 term TEXT NOT NULL,
 day TEXT NOT NULL DEFAULT '',
 start_time TEXT NOT NULL DEFAULT '',
 end_time TEXT NOT NULL DEFAULT '',
 classroom_id INTEGER REFERENCES classrooms(id) ON DELETE RESTRICT,
 enrollment_capacity INTEGER CHECK(enrollment_capacity IS NULL OR enrollment_capacity > 0),
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),
 is_open_for_enrollment INTEGER NOT NULL DEFAULT 0 CHECK(is_open_for_enrollment IN (0,1)),
 UNIQUE(course_code,section_code,term),
 CHECK((start_time='' AND end_time='') OR (start_time<>'' AND end_time>start_time))
);
CREATE INDEX IF NOT EXISTS idx_classrooms_building_floor_type ON classrooms(building_id,floor_id,room_type,is_active);
CREATE INDEX IF NOT EXISTS idx_sections_term_day ON academic_sections(term,day,is_active);
CREATE TABLE IF NOT EXISTS academic_section_meetings (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 section_id INTEGER NOT NULL REFERENCES academic_sections(id) ON DELETE CASCADE,
 activity_type TEXT NOT NULL,
 day TEXT NOT NULL,
 start_time TEXT NOT NULL,
 end_time TEXT NOT NULL,
 classroom_id INTEGER REFERENCES classrooms(id) ON DELETE RESTRICT,
 venue_label TEXT NOT NULL DEFAULT '',
 instructor TEXT NOT NULL DEFAULT '',
 enrollment_capacity INTEGER CHECK(enrollment_capacity IS NULL OR enrollment_capacity > 0),
 sort_order INTEGER NOT NULL DEFAULT 0,
 CHECK(end_time > start_time)
);
CREATE INDEX IF NOT EXISTS idx_section_meetings_section ON academic_section_meetings(section_id,sort_order,id);
CREATE INDEX IF NOT EXISTS idx_section_meetings_classroom ON academic_section_meetings(classroom_id,section_id);
CREATE TABLE IF NOT EXISTS campus_schedule_imports (
 source_key TEXT PRIMARY KEY,
 imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
INSERT OR IGNORE INTO buildings(code,name,description) VALUES('DEMO','Demo Building','Placeholder building; replace with verified campus data.');
INSERT OR IGNORE INTO floors(building_id,label,description)
 SELECT id,'Demo Floor','Placeholder floor; replace with verified campus data.' FROM buildings WHERE code='DEMO';
)SQL";
    if (sqlite3_exec(database, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        setError(sqlite3_errmsg(database));
        return false;
    }
    if (!hasColumn(database, "classrooms", "capacity_known") &&
        sqlite3_exec(database, "ALTER TABLE classrooms ADD COLUMN capacity_known INTEGER NOT NULL DEFAULT 1 CHECK(capacity_known IN (0,1))", nullptr, nullptr, nullptr) != SQLITE_OK) {
        setError(sqlite3_errmsg(database));
        return false;
    }
    if (!hasColumn(database, "academic_sections", "enrollment_capacity") &&
        sqlite3_exec(database, "ALTER TABLE academic_sections ADD COLUMN enrollment_capacity INTEGER CHECK(enrollment_capacity IS NULL OR enrollment_capacity > 0)", nullptr, nullptr, nullptr) != SQLITE_OK) {
        setError(sqlite3_errmsg(database));
        return false;
    }
    if (!hasColumn(database, "academic_sections", "is_open_for_enrollment") &&
        sqlite3_exec(database, "ALTER TABLE academic_sections ADD COLUMN is_open_for_enrollment INTEGER NOT NULL DEFAULT 0 CHECK(is_open_for_enrollment IN (0,1))", nullptr, nullptr, nullptr) != SQLITE_OK) {
        setError(sqlite3_errmsg(database));
        return false;
    }
    if (!hasColumn(database, "academic_section_meetings", "enrollment_capacity") &&
        sqlite3_exec(database, "ALTER TABLE academic_section_meetings ADD COLUMN enrollment_capacity INTEGER CHECK(enrollment_capacity IS NULL OR enrollment_capacity > 0)", nullptr, nullptr, nullptr) != SQLITE_OK) {
        setError(sqlite3_errmsg(database));
        return false;
    }
    std::string migrationError;
    if (!mergeRoomAliases(database, migrationError)) {
        if (migrationError == "reservation_overlap")
            migrationError = "Approved reservations overlap after room aliases are combined; the alias migration was rolled back. Resolve the conflicting bookings and restart.";
        setError(migrationError);
        return false;
    }
    if (!refreshClassroomIndex()) {
        setError(sqlite3_errmsg(database));
        return false;
    }
    if (error) error->clear();
    return true;
}

bool CampusRepository::importSchedule(const std::string& path, std::string& error) {
    error.clear();
    Json source;
    try {
        std::ifstream input(path);
        if (!input) { error = "schedule_file_not_found"; return false; }
        input >> source;
    } catch (const std::exception&) {
        error = "invalid_schedule_file";
        return false;
    }
    if (!source.is_array() || source.empty()) { error = "invalid_schedule_file"; return false; }

    struct ScheduleRow {
        std::string group;
        std::string term;
        std::string day;
        std::string courseCode;
        std::string courseName;
        std::string activityType;
        std::string startTime;
        std::string endTime;
        std::string venueLabel;
        std::string instructor;
    };
    std::vector<ScheduleRow> rows;
    std::set<std::string> venues;
    try {
        for (const Json& item : source) {
            if (!item.is_object()) { error = "invalid_schedule_file"; return false; }
            ScheduleRow row;
            row.group = item.at("group").get<std::string>();
            row.term = item.at("term").get<std::string>();
            row.day = item.at("day").get<std::string>();
            row.courseCode = item.at("courseCode").get<std::string>();
            row.courseName = item.at("courseName").get<std::string>();
            row.activityType = item.at("activityType").get<std::string>();
            row.startTime = item.at("startTime").get<std::string>();
            row.endTime = item.at("endTime").get<std::string>();
            row.venueLabel = item.at("venueLabel").get<std::string>();
            row.instructor = item.at("instructor").get<std::string>();
            if (row.group.empty() || row.term.empty() || row.day.empty() || row.courseCode.empty() ||
                row.courseName.empty() || row.activityType.empty() ||
                row.startTime.empty() || row.endTime.empty() || row.venueLabel.empty()) {
                error = "invalid_schedule_file";
                return false;
            }
            if (normalizeVenueLabel(row.venueLabel) != "online") venues.insert(row.venueLabel);
            rows.push_back(std::move(row));
        }
    } catch (const std::exception&) {
        error = "invalid_schedule_file";
        return false;
    }

    sqlite3_stmt* imported = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM campus_schedule_imports WHERE source_key=?", -1, &imported, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    bindText(imported, 1, "level_two_timetable");
    bool alreadyImported = sqlite3_step(imported) == SQLITE_ROW;
    sqlite3_finalize(imported);
    if (alreadyImported) return true;
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    auto rollback = [this]() { sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr); };
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM campus_schedule_imports WHERE source_key=?", -1, &imported, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    bindText(imported, 1, "level_two_timetable");
    alreadyImported = sqlite3_step(imported) == SQLITE_ROW;
    sqlite3_finalize(imported);
    if (alreadyImported) {
        if (sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        return true;
    }

    auto findId = [this](const char* sql, const std::string& first, const std::string& second = std::string()) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return 0;
        bindText(statement, 1, first);
        if (!second.empty()) bindText(statement, 2, second);
        int id = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : 0;
        sqlite3_finalize(statement);
        return id;
    };
    std::map<std::string, int> venueRoomIds;
    for (const std::string& venue : venues) {
        const VenueDescriptor descriptor = describeVenue(venue);
        const std::string venueKey = descriptor.buildingCode + ":" + normalizeVenueLabel(venue);
        if (venueRoomIds.find(venueKey) != venueRoomIds.end()) continue;
        sqlite3_stmt* statement = nullptr;
        const char* buildingSql =
            "INSERT OR IGNORE INTO buildings(code,name,description) VALUES(?,?,?)";
        if (sqlite3_prepare_v2(database, buildingSql, -1, &statement, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        bindText(statement, 1, descriptor.buildingCode);
        bindText(statement, 2, descriptor.buildingName);
        bindText(statement, 3, "Building prefix inferred from timetable venue label only; site location and map are unverified.");
        int result = sqlite3_step(statement);
        sqlite3_finalize(statement);
        if (result != SQLITE_DONE) { rollback(); error = "database_error"; return false; }
        int buildingId = findId("SELECT id FROM buildings WHERE code=?", descriptor.buildingCode);
        if (!buildingId) { rollback(); error = "database_error"; return false; }

        const char* floorSql =
            "INSERT OR IGNORE INTO floors(building_id,label,description) VALUES(?,?,?)";
        if (sqlite3_prepare_v2(database, floorSql, -1, &statement, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        sqlite3_bind_int(statement, 1, buildingId);
        bindText(statement, 2, descriptor.floorLabel);
        bindText(statement, 3, descriptor.floorLabel == "Unspecified"
            ? "Floor not supplied or not reliably identifiable from the timetable label."
            : "Floor inferred from timetable room code only; floor plan and elevation are unverified.");
        result = sqlite3_step(statement);
        sqlite3_finalize(statement);
        if (result != SQLITE_DONE) { rollback(); error = "database_error"; return false; }
        sqlite3_stmt* floorLookup = nullptr;
        if (sqlite3_prepare_v2(database, "SELECT id FROM floors WHERE building_id=? AND label=?", -1, &floorLookup, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        sqlite3_bind_int(floorLookup, 1, buildingId);
        bindText(floorLookup, 2, descriptor.floorLabel);
        int floorId = sqlite3_step(floorLookup) == SQLITE_ROW ? sqlite3_column_int(floorLookup, 0) : 0;
        sqlite3_finalize(floorLookup);
        if (!floorId) { rollback(); error = "database_error"; return false; }

        const char* roomSql =
            "INSERT OR IGNORE INTO classrooms(building_id,floor_id,room_code,room_name,room_type,capacity,capacity_known,is_active,notes) "
            "VALUES(?,?,?,?,?,1,0,1,?)";
        if (sqlite3_prepare_v2(database, roomSql, -1, &statement, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        sqlite3_bind_int(statement, 1, buildingId);
        sqlite3_bind_int(statement, 2, floorId);
        bindText(statement, 3, venue);
        bindText(statement, 4, venue);
        bindText(statement, 5, descriptor.roomType);
        bindText(statement, 6, descriptor.notes);
        result = sqlite3_step(statement);
        sqlite3_finalize(statement);
        if (result != SQLITE_DONE) { rollback(); error = "database_error"; return false; }
        sqlite3_stmt* roomLookup = nullptr;
        if (sqlite3_prepare_v2(database, "SELECT id FROM classrooms WHERE building_id=? AND room_code=?", -1, &roomLookup, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        sqlite3_bind_int(roomLookup, 1, buildingId);
        bindText(roomLookup, 2, venue);
        int roomId = sqlite3_step(roomLookup) == SQLITE_ROW ? sqlite3_column_int(roomLookup, 0) : 0;
        sqlite3_finalize(roomLookup);
        if (!roomId) { rollback(); error = "database_error"; return false; }
        venueRoomIds.emplace(venueKey, roomId);
    }

    struct ImportedSection {
        AcademicSection section;
    };
    std::map<std::string, ImportedSection> sectionsByGroupAndCourse;
    for (const ScheduleRow& row : rows) {
        const std::string key = row.group + "\n" + row.courseCode;
        ImportedSection& record = sectionsByGroupAndCourse[key];
        AcademicSection& section = record.section;
        if (section.courseCode.empty()) {
            section.courseCode = row.courseCode;
            section.courseName = row.courseName;
            section.sectionCode = "G" + row.group;
            section.term = row.term;
            section.day = row.day;
            section.startTime = row.startTime;
            section.endTime = row.endTime;
            section.instructor = row.instructor;
            if (normalizeVenueLabel(row.venueLabel) != "online") {
                const VenueDescriptor descriptor = describeVenue(row.venueLabel);
                section.classroomId = venueRoomIds.at(descriptor.buildingCode + ":" + normalizeVenueLabel(row.venueLabel));
            }
            section.isActive = true;
        } else if (section.term != row.term) {
            error = "invalid_schedule_file";
            rollback();
            return false;
        }
        SectionMeeting meeting;
        meeting.activityType = row.activityType;
        meeting.day = row.day;
        meeting.startTime = row.startTime;
        meeting.endTime = row.endTime;
        meeting.venueLabel = row.venueLabel;
        meeting.instructor = row.instructor;
        if (normalizeVenueLabel(row.venueLabel) != "online") {
            const VenueDescriptor descriptor = describeVenue(row.venueLabel);
            meeting.classroomId = venueRoomIds.at(descriptor.buildingCode + ":" + normalizeVenueLabel(row.venueLabel));
        }
        section.meetings.push_back(std::move(meeting));
    }

    const char* sectionSql =
        "INSERT INTO academic_sections(course_code,course_name,section_code,instructor,term,day,start_time,end_time,classroom_id,enrollment_capacity,is_active,is_open_for_enrollment) "
        "VALUES(?,?,?,?,?,?,?,?,?,NULL,1,0)";
    const char* meetingSql =
        "INSERT INTO academic_section_meetings(section_id,activity_type,day,start_time,end_time,classroom_id,venue_label,instructor,enrollment_capacity,sort_order) "
        "VALUES(?,?,?,?,?,?,?,?,NULL,?)";
    for (auto& pair : sectionsByGroupAndCourse) {
        AcademicSection& section = pair.second.section;
        sqlite3_stmt* insertSection = nullptr;
        if (sqlite3_prepare_v2(database, sectionSql, -1, &insertSection, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        bindText(insertSection, 1, section.courseCode);
        bindText(insertSection, 2, section.courseName);
        bindText(insertSection, 3, section.sectionCode);
        bindText(insertSection, 4, section.instructor);
        bindText(insertSection, 5, section.term);
        bindText(insertSection, 6, section.day);
        bindText(insertSection, 7, section.startTime);
        bindText(insertSection, 8, section.endTime);
        if (section.classroomId) sqlite3_bind_int(insertSection, 9, section.classroomId);
        else sqlite3_bind_null(insertSection, 9);
        int result = sqlite3_step(insertSection);
        sqlite3_finalize(insertSection);
        if (result != SQLITE_DONE) {
            error = hasConstraintError(database) ? "duplicate_section" : "database_error";
            rollback();
            return false;
        }
        int sectionId = static_cast<int>(sqlite3_last_insert_rowid(database));
        for (std::size_t index = 0; index < section.meetings.size(); ++index) {
            const SectionMeeting& meeting = section.meetings[index];
            sqlite3_stmt* insertMeeting = nullptr;
            if (sqlite3_prepare_v2(database, meetingSql, -1, &insertMeeting, nullptr) != SQLITE_OK) {
                rollback(); error = "database_error"; return false;
            }
            sqlite3_bind_int(insertMeeting, 1, sectionId);
            bindText(insertMeeting, 2, meeting.activityType);
            bindText(insertMeeting, 3, meeting.day);
            bindText(insertMeeting, 4, meeting.startTime);
            bindText(insertMeeting, 5, meeting.endTime);
            if (meeting.classroomId) sqlite3_bind_int(insertMeeting, 6, meeting.classroomId);
            else sqlite3_bind_null(insertMeeting, 6);
            bindText(insertMeeting, 7, meeting.venueLabel);
            bindText(insertMeeting, 8, meeting.instructor);
            sqlite3_bind_int(insertMeeting, 9, static_cast<int>(index));
            result = sqlite3_step(insertMeeting);
            sqlite3_finalize(insertMeeting);
            if (result != SQLITE_DONE) { rollback(); error = "database_error"; return false; }
        }
    }

    sqlite3_stmt* markImported = nullptr;
    if (sqlite3_prepare_v2(database, "INSERT INTO campus_schedule_imports(source_key) VALUES(?)", -1, &markImported, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    bindText(markImported, 1, "level_two_timetable");
    bool marked = sqlite3_step(markImported) == SQLITE_DONE;
    sqlite3_finalize(markImported);
    if (!marked || sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    if (!refreshClassroomIndex()) { error = "database_error"; return false; }
    return true;
}

bool CampusRepository::refreshClassroomIndex() {
    std::lock_guard<std::mutex> lock(classroomIndexMutex);
    classroomsByBuildingAndCode.clear();
    sqlite3_stmt* statement = nullptr;
    std::string sql = std::string(classroomSelect) + " ORDER BY c.id";
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Classroom room;
        readClassroom(statement, room);
        classroomsByBuildingAndCode.insert(roomKey(room.buildingId, room.roomCode), room);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool CampusRepository::listBuildings(std::vector<Building>& buildings) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT id,code,name,description,is_active FROM buildings WHERE is_active=1 ORDER BY name";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Building building;
        building.id = sqlite3_column_int(statement, 0);
        building.code = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        building.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        const char* description = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
        building.description = description ? description : "";
        building.isActive = sqlite3_column_int(statement, 4) != 0;
        buildings.push_back(building);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool CampusRepository::buildingExists(int id) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM buildings WHERE id=? AND is_active=1", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int(statement, 1, id);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

bool CampusRepository::listFloors(int buildingId, std::vector<Floor>& floors) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT id,building_id,label,description FROM floors WHERE building_id=? ORDER BY label";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, buildingId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Floor floor;
        floor.id = sqlite3_column_int(statement, 0);
        floor.buildingId = sqlite3_column_int(statement, 1);
        floor.label = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        const char* description = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
        floor.description = description ? description : "";
        floors.push_back(floor);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool CampusRepository::listClassrooms(const std::string& query, int buildingId, int floorId,
                                      const std::string& roomType, bool includeInactive,
                                      std::vector<Classroom>& rooms) {
    std::string sql = std::string(classroomSelect) + " WHERE 1=1";
    if (!includeInactive) sql += " AND c.is_active=1";
    if (!query.empty()) sql += " AND (c.room_code LIKE ? OR c.room_name LIKE ?)";
    if (buildingId) sql += " AND c.building_id=?";
    if (floorId) sql += " AND c.floor_id=?";
    if (!roomType.empty()) sql += " AND c.room_type=?";
    sql += " ORDER BY b.name,f.label,c.room_code";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    int parameter = 1;
    if (!query.empty()) {
        std::string pattern = "%" + query + "%";
        bindText(statement, parameter++, pattern);
        bindText(statement, parameter++, pattern);
    }
    if (buildingId) sqlite3_bind_int(statement, parameter++, buildingId);
    if (floorId) sqlite3_bind_int(statement, parameter++, floorId);
    if (!roomType.empty()) bindText(statement, parameter++, roomType);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Classroom room;
        readClassroom(statement, room);
        rooms.push_back(room);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool CampusRepository::findClassroom(int id, Classroom& room) {
    sqlite3_stmt* statement = nullptr;
    std::string sql = std::string(classroomSelect) + " WHERE c.id=?";
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, id);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readClassroom(statement, room);
    sqlite3_finalize(statement);
    return found;
}

bool CampusRepository::validLocation(int buildingId, int floorId) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT 1 FROM buildings b JOIN floors f ON f.building_id=b.id WHERE b.id=? AND f.id=? AND b.is_active=1";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, buildingId);
    sqlite3_bind_int(statement, 2, floorId);
    bool valid = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return valid;
}

bool CampusRepository::saveClassroom(Classroom& room, std::string& error) {
    if (!validLocation(room.buildingId, room.floorId)) { error = "invalid_building_or_floor"; return false; }
    sqlite3_stmt* normalizedDuplicate = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT id,room_code FROM classrooms WHERE building_id=?", -1, &normalizedDuplicate, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(normalizedDuplicate, 1, room.buildingId);
    int duplicateResult = SQLITE_OK;
    while ((duplicateResult = sqlite3_step(normalizedDuplicate)) == SQLITE_ROW) {
        const char* existingCode = reinterpret_cast<const char*>(sqlite3_column_text(normalizedDuplicate, 1));
        if (sqlite3_column_int(normalizedDuplicate, 0) != room.id && existingCode &&
            normalizeVenueLabel(existingCode) == normalizeVenueLabel(room.roomCode)) {
            sqlite3_finalize(normalizedDuplicate);
            error = "duplicate_room_code";
            return false;
        }
    }
    sqlite3_finalize(normalizedDuplicate);
    if (duplicateResult != SQLITE_DONE) { error = "database_error"; return false; }
    Classroom indexedDuplicate;
    {
        std::lock_guard<std::mutex> lock(classroomIndexMutex);
        if (classroomsByBuildingAndCode.find(roomKey(room.buildingId, room.roomCode), indexedDuplicate) &&
            indexedDuplicate.id != room.id) {
            error = "duplicate_room_code";
            return false;
        }
    }
    Classroom previous;
    bool exists = room.id > 0 && findClassroom(room.id, previous);
    if (room.id > 0 && !exists) { error = "not_found"; return false; }
    sqlite3_stmt* statement = nullptr;
    const char* insertSql = "INSERT INTO classrooms(building_id,floor_id,room_code,room_name,room_type,capacity,capacity_known,is_active,notes) VALUES(?,?,?,?,?,?,?,?,?)";
    const char* updateSql = "UPDATE classrooms SET building_id=?,floor_id=?,room_code=?,room_name=?,room_type=?,capacity=?,capacity_known=1,is_active=?,notes=? WHERE id=?";
    if (sqlite3_prepare_v2(database, room.id == 0 ? insertSql : updateSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error"; return false;
    }
    sqlite3_bind_int(statement, 1, room.buildingId);
    sqlite3_bind_int(statement, 2, room.floorId);
    bindText(statement, 3, room.roomCode);
    bindText(statement, 4, room.roomName);
    bindText(statement, 5, room.roomType);
    sqlite3_bind_int(statement, 6, room.capacity);
    if (room.id == 0) {
        sqlite3_bind_int(statement, 7, 1);
        sqlite3_bind_int(statement, 8, room.isActive ? 1 : 0);
        bindText(statement, 9, room.notes);
    } else {
        sqlite3_bind_int(statement, 7, room.isActive ? 1 : 0);
        bindText(statement, 8, room.notes);
        sqlite3_bind_int(statement, 9, room.id);
    }
    int result = sqlite3_step(statement);
    if (result != SQLITE_DONE)
        error = activeEnrollmentsExceedCapacity(database) ? "active_enrollments_exceed_capacity" :
                hasConstraintError(database) ? "duplicate_room_code" : "database_error";
    if (result == SQLITE_DONE && room.id == 0) room.id = static_cast<int>(sqlite3_last_insert_rowid(database));
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;
    room.capacityKnown = true;
    std::lock_guard<std::mutex> lock(classroomIndexMutex);
    if (exists) classroomsByBuildingAndCode.erase(roomKey(previous.buildingId, previous.roomCode));
    if (!findClassroom(room.id, room)) { error = "database_error"; return false; }
    classroomsByBuildingAndCode.insert(roomKey(room.buildingId, room.roomCode), room);
    return true;
}

bool CampusRepository::setClassroomActive(int id, bool active, std::string& error) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE classrooms SET is_active=? WHERE id=?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) { error = "database_error"; return false; }
    sqlite3_bind_int(statement, 1, active ? 1 : 0);
    sqlite3_bind_int(statement, 2, id);
    bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!ok) { error = "not_found"; return false; }
    if (!refreshClassroomIndex()) { error = "database_error"; return false; }
    return true;
}

bool CampusRepository::listSections(const std::string& query, const std::string& term,
                                    const std::string& day, int buildingId, bool includeInactive,
                                    std::vector<AcademicSection>& sections) {
    std::string sql = std::string(sectionSelect) + " WHERE 1=1";
    if (!includeInactive) sql += " AND s.is_active=1";
    if (!query.empty()) sql += " AND (s.course_code LIKE ? OR s.course_name LIKE ? OR s.section_code LIKE ?)";
    if (!term.empty()) sql += " AND s.term=?";
    if (!day.empty()) sql += " AND (s.day=? OR EXISTS (SELECT 1 FROM academic_section_meetings dm WHERE dm.section_id=s.id AND dm.day=?))";
    if (buildingId) sql += " AND (b.id=? OR EXISTS (SELECT 1 FROM academic_section_meetings bm JOIN classrooms bc ON bc.id=bm.classroom_id WHERE bm.section_id=s.id AND bc.building_id=?))";
    sql += " ORDER BY s.term,s.course_code,CAST(SUBSTR(s.section_code,2) AS INTEGER),s.section_code";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    int parameter = 1;
    if (!query.empty()) {
        std::string pattern = "%" + query + "%";
        bindText(statement, parameter++, pattern);
        bindText(statement, parameter++, pattern);
        bindText(statement, parameter++, pattern);
    }
    if (!term.empty()) bindText(statement, parameter++, term);
    if (!day.empty()) {
        bindText(statement, parameter++, day);
        bindText(statement, parameter++, day);
    }
    if (buildingId) {
        sqlite3_bind_int(statement, parameter++, buildingId);
        sqlite3_bind_int(statement, parameter++, buildingId);
    }
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        AcademicSection section;
        readSection(statement, section);
        sections.push_back(section);
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;
    for (AcademicSection& section : sections)
        if (!loadMeetings(database, section.id, section.meetings)) return false;
    return true;
}

bool CampusRepository::findSection(int id, AcademicSection& section) {
    sqlite3_stmt* statement = nullptr;
    std::string sql = std::string(sectionSelect) + " WHERE s.id=?";
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, id);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readSection(statement, section);
    sqlite3_finalize(statement);
    if (found && !loadMeetings(database, section.id, section.meetings)) return false;
    return found;
}

bool CampusRepository::saveSection(AcademicSection& section, std::string& error) {
    if (section.enrollmentCapacity < 0) { error = "invalid_capacity"; return false; }
    auto activeRoomExists = [this](int roomId) {
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(database, "SELECT 1 FROM classrooms WHERE id=? AND is_active=1", -1, &check, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(check, 1, roomId);
        bool active = sqlite3_step(check) == SQLITE_ROW;
        sqlite3_finalize(check);
        return active;
    };
    if (section.classroomId > 0 && !activeRoomExists(section.classroomId)) {
        error = "classroom_not_active";
        return false;
    }
    static const std::regex timePattern(R"(^([01][0-9]|2[0-3]):[0-5][0-9]$)");
    static const std::set<std::string> validDays = {
        "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
    };
    for (const SectionMeeting& meeting : section.meetings) {
        if (meeting.activityType.empty() || !validDays.count(meeting.day) ||
            !std::regex_match(meeting.startTime, timePattern) ||
            !std::regex_match(meeting.endTime, timePattern) || meeting.enrollmentCapacity < 0 ||
            meeting.endTime <= meeting.startTime) {
            error = "invalid_meeting";
            return false;
        }
        if (meeting.classroomId > 0 && !activeRoomExists(meeting.classroomId)) {
            error = "classroom_not_active";
            return false;
        }
    }
    AcademicSection previous;
    if (section.id > 0 && !findSection(section.id, previous)) { error = "not_found"; return false; }
    for (const SectionMeeting& oldMeeting : previous.meetings) {
        const auto replacement = std::find_if(section.meetings.begin(), section.meetings.end(),
            [&oldMeeting](const SectionMeeting& meeting) { return meeting.id == oldMeeting.id; });
        if (hasActiveLabEnrollment(database, oldMeeting.id) &&
            (replacement == section.meetings.end() || !isLabActivity(replacement->activityType))) {
            error = "meeting_has_enrollments";
            return false;
        }
    }
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    auto rollback = [this]() { sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr); };
    sqlite3_stmt* statement = nullptr;
    const char* insertSql = "INSERT INTO academic_sections(course_code,course_name,section_code,instructor,term,day,start_time,end_time,classroom_id,enrollment_capacity,is_active,is_open_for_enrollment) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)";
    const char* updateSql = "UPDATE academic_sections SET course_code=?,course_name=?,section_code=?,instructor=?,term=?,day=?,start_time=?,end_time=?,classroom_id=?,enrollment_capacity=?,is_active=?,is_open_for_enrollment=? WHERE id=?";
    if (sqlite3_prepare_v2(database, section.id == 0 ? insertSql : updateSql, -1, &statement, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    bindText(statement, 1, section.courseCode);
    bindText(statement, 2, section.courseName);
    bindText(statement, 3, section.sectionCode);
    bindText(statement, 4, section.instructor);
    bindText(statement, 5, section.term);
    bindText(statement, 6, section.day);
    bindText(statement, 7, section.startTime);
    bindText(statement, 8, section.endTime);
    if (section.classroomId > 0) sqlite3_bind_int(statement, 9, section.classroomId);
    else sqlite3_bind_null(statement, 9);
    if (section.enrollmentCapacity > 0) sqlite3_bind_int(statement, 10, section.enrollmentCapacity);
    else sqlite3_bind_null(statement, 10);
    sqlite3_bind_int(statement, 11, section.isActive ? 1 : 0);
    sqlite3_bind_int(statement, 12, section.isOpenForEnrollment ? 1 : 0);
    if (section.id > 0) sqlite3_bind_int(statement, 13, section.id);
    int result = sqlite3_step(statement);
    if (result != SQLITE_DONE)
        error = activeEnrollmentsExceedCapacity(database) ? "active_enrollments_exceed_capacity" :
                hasConstraintError(database) ? "duplicate_section" : "database_error";
    if (result == SQLITE_DONE && section.id == 0) section.id = static_cast<int>(sqlite3_last_insert_rowid(database));
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) { rollback(); return false; }

    const char* insertMeetingSql =
        "INSERT INTO academic_section_meetings(section_id,activity_type,day,start_time,end_time,classroom_id,venue_label,instructor,enrollment_capacity,sort_order) "
        "VALUES(?,?,?,?,?,?,?,?,?,?)";
    const char* updateMeetingSql =
        "UPDATE academic_section_meetings SET activity_type=?,day=?,start_time=?,end_time=?,classroom_id=?,venue_label=?,instructor=?,enrollment_capacity=?,sort_order=? "
        "WHERE id=? AND section_id=?";
    for (std::size_t index = 0; index < section.meetings.size(); ++index) {
        SectionMeeting& meeting = section.meetings[index];
        sqlite3_stmt* insertMeeting = nullptr;
        const bool updateExisting = meeting.id > 0;
        if (sqlite3_prepare_v2(database, updateExisting ? updateMeetingSql : insertMeetingSql, -1, &insertMeeting, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        int parameter = 1;
        if (!updateExisting) sqlite3_bind_int(insertMeeting, parameter++, section.id);
        bindText(insertMeeting, parameter++, meeting.activityType);
        bindText(insertMeeting, parameter++, meeting.day);
        bindText(insertMeeting, parameter++, meeting.startTime);
        bindText(insertMeeting, parameter++, meeting.endTime);
        if (meeting.classroomId > 0) sqlite3_bind_int(insertMeeting, parameter++, meeting.classroomId);
        else sqlite3_bind_null(insertMeeting, parameter++);
        bindText(insertMeeting, parameter++, meeting.venueLabel);
        bindText(insertMeeting, parameter++, meeting.instructor);
        if (meeting.enrollmentCapacity > 0) sqlite3_bind_int(insertMeeting, parameter++, meeting.enrollmentCapacity);
        else sqlite3_bind_null(insertMeeting, parameter++);
        sqlite3_bind_int(insertMeeting, parameter++, static_cast<int>(index));
        if (updateExisting) {
            sqlite3_bind_int(insertMeeting, parameter++, meeting.id);
            sqlite3_bind_int(insertMeeting, parameter, section.id);
        }
        int meetingResult = sqlite3_step(insertMeeting);
        sqlite3_finalize(insertMeeting);
        if (meetingResult != SQLITE_DONE || (updateExisting && sqlite3_changes(database) != 1)) {
            error = activeEnrollmentsExceedCapacity(database) ? "active_enrollments_exceed_capacity" : "database_error";
            rollback();
            return false;
        }
        if (!updateExisting) meeting.id = static_cast<int>(sqlite3_last_insert_rowid(database));
    }

    for (const SectionMeeting& oldMeeting : previous.meetings) {
        const bool retained = std::any_of(section.meetings.begin(), section.meetings.end(),
            [&oldMeeting](const SectionMeeting& meeting) { return meeting.id == oldMeeting.id; });
        if (retained) continue;
        if (hasActiveLabEnrollment(database, oldMeeting.id)) {
            rollback(); error = "meeting_has_enrollments"; return false;
        }
        sqlite3_stmt* removeMeeting = nullptr;
        if (sqlite3_prepare_v2(database, "DELETE FROM academic_section_meetings WHERE id=? AND section_id=?", -1, &removeMeeting, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        sqlite3_bind_int(removeMeeting, 1, oldMeeting.id);
        sqlite3_bind_int(removeMeeting, 2, section.id);
        int removeResult = sqlite3_step(removeMeeting);
        sqlite3_finalize(removeMeeting);
        if (removeResult != SQLITE_DONE) {
            rollback();
            error = sqlite3_extended_errcode(database) == SQLITE_CONSTRAINT_FOREIGNKEY
                ? "meeting_has_enrollments" : "database_error";
            return false;
        }
    }
    if (sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    if (!findSection(section.id, section)) { error = "database_error"; return false; }
    return true;
}

bool CampusRepository::setSectionActive(int id, bool active, std::string& error) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE academic_sections SET is_active=? WHERE id=?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) { error = "database_error"; return false; }
    sqlite3_bind_int(statement, 1, active ? 1 : 0);
    sqlite3_bind_int(statement, 2, id);
    bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!ok) { error = sqlite3_errcode(database) == SQLITE_OK ? "not_found" : "database_error"; return false; }
    return true;
}

bool CampusRepository::setSectionEnrollmentOpen(int id, bool open, std::string& error) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "UPDATE academic_sections SET is_open_for_enrollment=? WHERE id=?", -1, &statement, nullptr) != SQLITE_OK) {
        error = "database_error";
        return false;
    }
    sqlite3_bind_int(statement, 1, open ? 1 : 0);
    sqlite3_bind_int(statement, 2, id);
    bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!ok) {
        error = sqlite3_errcode(database) == SQLITE_OK ? "not_found" : "database_error";
        return false;
    }
    return true;
}
