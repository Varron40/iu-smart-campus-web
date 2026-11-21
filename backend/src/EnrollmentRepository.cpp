#include "data_access/EnrollmentRepository.h"
#include <algorithm>
#include <cctype>

namespace {
void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

bool execute(sqlite3* database, const char* sql) {
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool isBusy(sqlite3* database) {
    int code = sqlite3_extended_errcode(database);
    return code == SQLITE_BUSY || code == SQLITE_LOCKED ||
           code == SQLITE_BUSY_SNAPSHOT || code == SQLITE_LOCKED_SHAREDCACHE;
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

bool isLabActivity(const std::string& activity) {
    std::string normalized;
    normalized.reserve(activity.size());
    for (unsigned char character : activity)
        normalized.push_back(static_cast<char>(std::tolower(character)));
    return normalized.find("lab") != std::string::npos ||
           normalized.find("practical") != std::string::npos;
}

bool isOnlineVenue(const std::string& venue) {
    std::string normalized;
    normalized.reserve(venue.size());
    for (unsigned char character : venue)
        normalized.push_back(static_cast<char>(std::tolower(character)));
    return normalized == "online";
}

bool loadMeetings(sqlite3* database, int sectionId, std::vector<SectionMeeting>& meetings,
                  int selectedLabMeetingId = 0, bool selectedScheduleOnly = false) {
    const char* sql =
        "SELECT m.id,m.activity_type,m.day,m.start_time,m.end_time,m.classroom_id,m.venue_label,m.instructor,"
        "m.enrollment_capacity,c.room_code,b.name,c.capacity,c.capacity_known,c.is_active,"
        "(SELECT COUNT(*) FROM enrollments e WHERE e.section_id=m.section_id AND e.status='ENROLLED' "
        " AND ((LOWER(m.activity_type) NOT LIKE '%lab%' AND LOWER(m.activity_type) NOT LIKE '%practical%') "
        " OR e.lab_meeting_id=m.id)) "
        ",s.enrollment_capacity "
        "FROM academic_section_meetings m JOIN academic_sections s ON s.id=m.section_id "
        "LEFT JOIN classrooms c ON c.id=m.classroom_id LEFT JOIN buildings b ON b.id=c.building_id "
        "WHERE m.section_id=? ORDER BY m.sort_order,m.id";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, sectionId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        SectionMeeting meeting;
        meeting.id = sqlite3_column_int(statement, 0);
        auto text = [statement](int column) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
            return value ? std::string(value) : std::string();
        };
        meeting.activityType = text(1);
        meeting.isLabOption = isLabActivity(meeting.activityType);
        meeting.day = text(2);
        meeting.startTime = text(3);
        meeting.endTime = text(4);
        meeting.classroomId = sqlite3_column_type(statement, 5) == SQLITE_NULL ? 0 : sqlite3_column_int(statement, 5);
        meeting.venueLabel = text(6);
        meeting.instructor = text(7);
        meeting.enrollmentCapacity = sqlite3_column_type(statement, 8) == SQLITE_NULL
            ? 0 : sqlite3_column_int(statement, 8);
        meeting.classroomCode = text(9);
        meeting.buildingName = text(10);
        const int roomCapacity = sqlite3_column_int(statement, 11);
        const bool roomCapacityKnown = sqlite3_column_int(statement, 12) != 0;
        const bool roomActive = sqlite3_column_int(statement, 13) != 0;
        meeting.enrolledCount = sqlite3_column_int(statement, 14);
        const int sectionCapacity = sqlite3_column_type(statement, 15) == SQLITE_NULL
            ? 0 : sqlite3_column_int(statement, 15);
        if (meeting.classroomId > 0) {
            meeting.capacityKnown = roomCapacityKnown && roomActive && roomCapacity > 0;
            if (meeting.capacityKnown)
                meeting.capacity = meeting.enrollmentCapacity > 0
                    ? std::min(roomCapacity, meeting.enrollmentCapacity) : roomCapacity;
        } else if (isOnlineVenue(meeting.venueLabel) &&
                   (meeting.enrollmentCapacity > 0 || sectionCapacity > 0)) {
            meeting.capacityKnown = true;
            meeting.capacity = meeting.enrollmentCapacity > 0
                ? meeting.enrollmentCapacity : sectionCapacity;
        }
        meeting.remainingSeats = meeting.capacityKnown
            ? std::max(0, meeting.capacity - meeting.enrolledCount) : 0;
        if (selectedScheduleOnly && meeting.isLabOption && meeting.id != selectedLabMeetingId) continue;
        meetings.push_back(std::move(meeting));
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

struct CapacityInfo {
    int capacity = 0;
    int enrolledCount = 0;
    int remainingSeats = 0;
    bool known = false;
    bool canEnroll = false;
    bool hasLabOptions = false;
    bool hasAvailableLabOption = false;
};

bool readSectionCapacity(sqlite3* database, int sectionId,
                         const std::vector<SectionMeeting>& meetings, CapacityInfo& info) {
    const char* sql =
        "SELECT s.enrollment_capacity,s.classroom_id,c.capacity,c.capacity_known,c.is_active,"
        "(SELECT COUNT(*) FROM enrollments e WHERE e.section_id=s.id AND e.status='ENROLLED'),"
        "s.is_active,s.is_open_for_enrollment "
        "FROM academic_sections s LEFT JOIN classrooms c ON c.id=s.classroom_id WHERE s.id=?";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, sectionId);
    int result = sqlite3_step(statement);
    if (result == SQLITE_ROW) {
        const int enrollmentCapacity = sqlite3_column_type(statement, 0) == SQLITE_NULL ? 0 : sqlite3_column_int(statement, 0);
        const bool hasLegacyRoom = sqlite3_column_type(statement, 1) != SQLITE_NULL;
        const int legacyCapacity = sqlite3_column_int(statement, 2);
        const bool legacyCapacityKnown = sqlite3_column_int(statement, 3) != 0;
        const bool legacyRoomActive = sqlite3_column_int(statement, 4) != 0;
        info.enrolledCount = sqlite3_column_int(statement, 5);
        const bool sectionActive = sqlite3_column_int(statement, 6) != 0;
        const bool enrollmentOpen = sqlite3_column_int(statement, 7) != 0;
        bool hasRequiredMeeting = false;
        bool requiredMeetingsKnown = true;
        int requiredCapacity = 0;
        int combinedLabCapacity = 0;
        info.hasLabOptions = false;
        info.hasAvailableLabOption = false;
        for (const SectionMeeting& meeting : meetings) {
            if (meeting.isLabOption) {
                info.hasLabOptions = true;
                if (meeting.capacityKnown) {
                    combinedLabCapacity += meeting.capacity;
                    if (meeting.remainingSeats > 0) info.hasAvailableLabOption = true;
                }
            } else if (isOnlineVenue(meeting.venueLabel) && enrollmentCapacity > 0) {
                hasRequiredMeeting = true;
                if (requiredCapacity == 0 || enrollmentCapacity < requiredCapacity)
                    requiredCapacity = enrollmentCapacity;
            } else {
                hasRequiredMeeting = true;
                if (!meeting.capacityKnown) requiredMeetingsKnown = false;
                else if (requiredCapacity == 0 || meeting.capacity < requiredCapacity)
                    requiredCapacity = meeting.capacity;
            }
        }
        if (!meetings.empty()) {
            info.known = requiredMeetingsKnown &&
                (!hasRequiredMeeting || requiredCapacity > 0) &&
                (!info.hasLabOptions || combinedLabCapacity > 0);
            if (info.known) {
                info.capacity = hasRequiredMeeting ? requiredCapacity : combinedLabCapacity;
                if (info.hasLabOptions && hasRequiredMeeting)
                    info.capacity = std::min(info.capacity, combinedLabCapacity);
            }
        } else if (!hasLegacyRoom && enrollmentCapacity > 0) {
            info.capacity = enrollmentCapacity;
            info.known = true;
        } else if (hasLegacyRoom && legacyRoomActive && legacyCapacityKnown && legacyCapacity > 0) {
            info.capacity = legacyCapacity;
            info.known = true;
        }
        if (info.known && enrollmentCapacity > 0)
            info.capacity = std::min(info.capacity, enrollmentCapacity);
        info.remainingSeats = info.known ? std::max(0, info.capacity - info.enrolledCount) : 0;
        info.canEnroll = sectionActive && enrollmentOpen && info.known &&
            info.capacity > 0 && info.remainingSeats > 0 &&
            (!info.hasLabOptions || info.hasAvailableLabOption);
    } else if (result != SQLITE_DONE) {
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

void readEnrollment(sqlite3_stmt* statement, Enrollment& enrollment) {
    enrollment.id = sqlite3_column_int(statement, 0);
    enrollment.studentId = sqlite3_column_int(statement, 1);
    enrollment.sectionId = sqlite3_column_int(statement, 2);
    enrollment.labMeetingId = sqlite3_column_type(statement, 3) == SQLITE_NULL
        ? 0 : sqlite3_column_int(statement, 3);
    enrollment.status = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
    const char* created = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
    const char* updated = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
    enrollment.createdAt = created ? created : "";
    enrollment.updatedAt = updated ? updated : "";
    const char* courseCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
    const char* courseName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
    const char* sectionCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
    const char* term = reinterpret_cast<const char*>(sqlite3_column_text(statement, 10));
    const char* day = reinterpret_cast<const char*>(sqlite3_column_text(statement, 11));
    const char* start = reinterpret_cast<const char*>(sqlite3_column_text(statement, 12));
    const char* end = reinterpret_cast<const char*>(sqlite3_column_text(statement, 13));
    const char* room = reinterpret_cast<const char*>(sqlite3_column_text(statement, 14));
    const char* building = reinterpret_cast<const char*>(sqlite3_column_text(statement, 15));
    enrollment.courseCode = courseCode ? courseCode : "";
    enrollment.courseName = courseName ? courseName : "";
    enrollment.sectionCode = sectionCode ? sectionCode : "";
    enrollment.term = term ? term : "";
    enrollment.day = day ? day : "";
    enrollment.startTime = start ? start : "";
    enrollment.endTime = end ? end : "";
    enrollment.classroomCode = room ? room : "";
    enrollment.buildingName = building ? building : "";
}

const char* enrollmentSelect =
    "SELECT e.id,e.student_id,e.section_id,e.lab_meeting_id,e.status,e.created_at,e.updated_at,"
    "s.course_code,s.course_name,s.section_code,s.term,s.day,s.start_time,s.end_time,c.room_code,b.name "
    "FROM enrollments e JOIN academic_sections s ON s.id=e.section_id "
    "LEFT JOIN classrooms c ON c.id=s.classroom_id LEFT JOIN buildings b ON b.id=c.building_id";

bool recordEvent(sqlite3* database, int enrollmentId, const char* oldStatus,
                 const char* newStatus, int actorId) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO enrollment_events(enrollment_id,old_status,new_status,actor_student_id) VALUES(?,?,?,?)";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, enrollmentId);
    if (oldStatus) bindText(statement, 2, oldStatus); else sqlite3_bind_null(statement, 2);
    bindText(statement, 3, newStatus);
    sqlite3_bind_int(statement, 4, actorId);
    bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}
}

EnrollmentRepository::EnrollmentRepository(const std::string& databasePath) {
    if (sqlite3_open_v2(databasePath.c_str(), &database,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        if (database) sqlite3_close(database);
        database = nullptr;
        return;
    }
    sqlite3_busy_timeout(database, 5000);
    sqlite3_extended_result_codes(database, 1);
    execute(database, "PRAGMA foreign_keys=ON");
    execute(database, "PRAGMA journal_mode=WAL");
}

EnrollmentRepository::~EnrollmentRepository() {
    if (database) sqlite3_close(database);
}

bool EnrollmentRepository::initialize() {
    if (!database) return false;
    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS enrollments (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
 section_id INTEGER NOT NULL REFERENCES academic_sections(id) ON DELETE RESTRICT,
 lab_meeting_id INTEGER REFERENCES academic_section_meetings(id) ON DELETE SET NULL,
 status TEXT NOT NULL CHECK(status IN ('ENROLLED','WITHDRAWN')),
 created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
 updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS enrollment_events (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 enrollment_id INTEGER NOT NULL REFERENCES enrollments(id) ON DELETE RESTRICT,
 old_status TEXT,
 new_status TEXT NOT NULL,
 actor_student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
 created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_one_active_enrollment
 ON enrollments(student_id,section_id) WHERE status='ENROLLED';
CREATE INDEX IF NOT EXISTS idx_enrollments_section_status ON enrollments(section_id,status);
CREATE INDEX IF NOT EXISTS idx_enrollments_student_updated ON enrollments(student_id,updated_at);
 )SQL";
    if (!execute(database, schema)) return false;
    if (!hasColumn(database, "enrollments", "lab_meeting_id") &&
        !execute(database, "ALTER TABLE enrollments ADD COLUMN lab_meeting_id INTEGER REFERENCES academic_section_meetings(id) ON DELETE SET NULL"))
        return false;

    const char* triggers = R"SQL(
DROP TRIGGER IF EXISTS prevent_capacity_below_active_enrollments;
DROP TRIGGER IF EXISTS prevent_section_room_below_enrollment_count;
DROP TRIGGER IF EXISTS prevent_section_capacity_below_active_enrollments;
DROP TRIGGER IF EXISTS prevent_meeting_room_below_enrollment_count;
DROP TRIGGER IF EXISTS prevent_meeting_room_update_below_enrollment_count;
DROP TRIGGER IF EXISTS prevent_meeting_capacity_below_active_enrollments;
DROP TRIGGER IF EXISTS validate_enrollment_lab_choice;
CREATE TRIGGER prevent_capacity_below_active_enrollments
BEFORE UPDATE OF capacity,capacity_known ON classrooms
WHEN EXISTS (
 SELECT 1 FROM academic_sections s
 WHERE s.classroom_id=NEW.id
  AND NOT EXISTS (SELECT 1 FROM academic_section_meetings m WHERE m.section_id=s.id)
  AND ((NEW.capacity_known=0) OR
       (SELECT COUNT(*) FROM enrollments e
        WHERE e.section_id=s.id AND e.status='ENROLLED') > NEW.capacity)
 UNION ALL
 SELECT 1 FROM academic_section_meetings m
 WHERE m.classroom_id=NEW.id
  AND ((NEW.capacity_known=0) OR
       (SELECT COUNT(*) FROM enrollments e WHERE e.section_id=m.section_id
        AND e.status='ENROLLED'
        AND ((LOWER(m.activity_type) NOT LIKE '%lab%' AND LOWER(m.activity_type) NOT LIKE '%practical%')
             OR e.lab_meeting_id=m.id)) > NEW.capacity)
)
BEGIN
 SELECT RAISE(ABORT,'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER prevent_section_room_below_enrollment_count
BEFORE UPDATE OF classroom_id ON academic_sections
WHEN NEW.classroom_id IS NOT NULL AND
 NOT EXISTS (SELECT 1 FROM academic_section_meetings m WHERE m.section_id=NEW.id) AND
 (SELECT COUNT(*) FROM enrollments e
  WHERE e.section_id=NEW.id AND e.status='ENROLLED') >
 (SELECT CASE WHEN c.capacity_known=1 AND c.is_active=1 THEN c.capacity ELSE 0 END
 FROM classrooms c WHERE c.id=NEW.classroom_id)
BEGIN
 SELECT RAISE(ABORT,'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER prevent_section_capacity_below_active_enrollments
BEFORE UPDATE OF enrollment_capacity ON academic_sections
WHEN NEW.enrollment_capacity IS NOT NULL AND
 (SELECT COUNT(*) FROM enrollments e
 WHERE e.section_id=NEW.id AND e.status='ENROLLED') > NEW.enrollment_capacity
BEGIN
 SELECT RAISE(ABORT,'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER prevent_meeting_room_below_enrollment_count
BEFORE INSERT ON academic_section_meetings
WHEN NEW.classroom_id IS NOT NULL AND
 (SELECT COUNT(*) FROM enrollments e
 WHERE e.section_id=NEW.section_id AND e.status='ENROLLED'
 AND ((LOWER(NEW.activity_type) NOT LIKE '%lab%' AND LOWER(NEW.activity_type) NOT LIKE '%practical%')
      OR e.lab_meeting_id=NEW.id)) >
 (SELECT CASE WHEN c.capacity_known=1 AND c.is_active=1 THEN c.capacity ELSE 0 END
 FROM classrooms c WHERE c.id=NEW.classroom_id)
BEGIN
 SELECT RAISE(ABORT,'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER prevent_meeting_room_update_below_enrollment_count
BEFORE UPDATE OF classroom_id ON academic_section_meetings
WHEN NEW.classroom_id IS NOT NULL AND
 (SELECT COUNT(*) FROM enrollments e
 WHERE e.section_id=NEW.section_id AND e.status='ENROLLED'
 AND ((LOWER(NEW.activity_type) NOT LIKE '%lab%' AND LOWER(NEW.activity_type) NOT LIKE '%practical%')
      OR e.lab_meeting_id=NEW.id)) >
 (SELECT CASE WHEN c.capacity_known=1 AND c.is_active=1 THEN c.capacity ELSE 0 END
 FROM classrooms c WHERE c.id=NEW.classroom_id)
BEGIN
 SELECT RAISE(ABORT,'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER prevent_meeting_capacity_below_active_enrollments
BEFORE UPDATE OF enrollment_capacity ON academic_section_meetings
WHEN NEW.enrollment_capacity IS NOT NULL AND
 (SELECT COUNT(*) FROM enrollments e
  WHERE e.section_id=NEW.section_id AND e.status='ENROLLED'
  AND ((LOWER(NEW.activity_type) NOT LIKE '%lab%' AND LOWER(NEW.activity_type) NOT LIKE '%practical%')
       OR e.lab_meeting_id=NEW.id)) > NEW.enrollment_capacity
BEGIN
 SELECT RAISE(ABORT,'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER validate_enrollment_lab_choice
BEFORE INSERT ON enrollments
WHEN NEW.status='ENROLLED' AND (
 NOT EXISTS (SELECT 1 FROM academic_sections s WHERE s.id=NEW.section_id
             AND s.is_active=1 AND s.is_open_for_enrollment=1)
 OR (EXISTS (SELECT 1 FROM academic_section_meetings m WHERE m.section_id=NEW.section_id
             AND (LOWER(m.activity_type) LIKE '%lab%' OR LOWER(m.activity_type) LIKE '%practical%')
             AND NEW.lab_meeting_id IS NULL))
 OR (NEW.lab_meeting_id IS NOT NULL AND NOT EXISTS (
      SELECT 1 FROM academic_section_meetings m WHERE m.id=NEW.lab_meeting_id
      AND m.section_id=NEW.section_id
      AND (LOWER(m.activity_type) LIKE '%lab%' OR LOWER(m.activity_type) LIKE '%practical%')))
 OR (NEW.lab_meeting_id IS NULL AND EXISTS (
      SELECT 1 FROM academic_section_meetings m WHERE m.section_id=NEW.section_id
      AND (LOWER(m.activity_type) LIKE '%lab%' OR LOWER(m.activity_type) LIKE '%practical%')))
)
BEGIN
 SELECT RAISE(ABORT,'invalid_lab_choice');
END;
CREATE TRIGGER IF NOT EXISTS prevent_duplicate_course_group_enrollment
BEFORE INSERT ON enrollments
WHEN NEW.status='ENROLLED' AND EXISTS (
 SELECT 1 FROM enrollments existing
 JOIN academic_sections current_section ON current_section.id=existing.section_id
 JOIN academic_sections new_section ON new_section.id=NEW.section_id
 WHERE existing.student_id=NEW.student_id
 AND existing.status='ENROLLED'
 AND current_section.course_code=new_section.course_code
 AND current_section.term=new_section.term
)
BEGIN
 SELECT RAISE(ABORT,'course_already_enrolled');
END;
)SQL";
    return execute(database, triggers);
}

bool EnrollmentRepository::listAvailable(const std::string& query, const std::string& term, const std::string& group,
                                         std::vector<AvailableSection>& sections) {
    std::string sql =
        "SELECT s.id,s.course_code,s.course_name,s.section_code,s.term,s.day,s.start_time,s.end_time,"
        "c.id,c.room_code,b.name,"
        "(SELECT COUNT(*) FROM enrollments e WHERE e.section_id=s.id AND e.status='ENROLLED') "
        "FROM academic_sections s LEFT JOIN classrooms c ON c.id=s.classroom_id AND c.is_active=1 "
        "LEFT JOIN buildings b ON b.id=c.building_id WHERE s.is_active=1";
    if (!query.empty()) sql += " AND (s.course_code LIKE ? OR s.course_name LIKE ? OR s.section_code LIKE ?)";
    if (!term.empty()) sql += " AND s.term=?";
    if (!group.empty()) sql += " AND s.section_code=?";
    sql += " ORDER BY s.term,s.course_code,CAST(SUBSTR(s.section_code,2) AS INTEGER),s.section_code";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    int index = 1;
    if (!query.empty()) {
        std::string pattern = "%" + query + "%";
        bindText(statement, index++, pattern);
        bindText(statement, index++, pattern);
        bindText(statement, index++, pattern);
    }
    if (!term.empty()) bindText(statement, index++, term);
    if (!group.empty()) bindText(statement, index++, group);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        AvailableSection section;
        section.sectionId = sqlite3_column_int(statement, 0);
        section.courseCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        section.courseName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        section.sectionCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
        section.term = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
        const char* day = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
        const char* start = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
        const char* end = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
        section.day = day ? day : "";
        section.startTime = start ? start : "";
        section.endTime = end ? end : "";
        section.classroomId = sqlite3_column_int(statement, 8);
        const char* room = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
        const char* building = reinterpret_cast<const char*>(sqlite3_column_text(statement, 10));
        section.classroomCode = room ? room : "";
        section.buildingName = building ? building : "";
        section.enrolledCount = sqlite3_column_int(statement, 11);
        sections.push_back(section);
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;
    for (AvailableSection& section : sections) {
        if (!loadMeetings(database, section.sectionId, section.meetings)) return false;
        CapacityInfo capacity;
        if (!readSectionCapacity(database, section.sectionId, section.meetings, capacity)) return false;
        section.capacityKnown = capacity.known;
        section.capacity = capacity.capacity;
        section.enrolledCount = capacity.enrolledCount;
        section.openForEnrollment = capacity.canEnroll;
        section.remainingSeats = capacity.remainingSeats;
        section.isFull = capacity.known &&
            (capacity.remainingSeats == 0 || (capacity.hasLabOptions && !capacity.hasAvailableLabOption));
    }
    return true;
}

bool EnrollmentRepository::enroll(int studentId, int sectionId, int labMeetingId,
                                  Enrollment& enrollment, std::string& error) {
    std::lock_guard<std::mutex> lock(transactionMutex);
    if (!database || !execute(database, "BEGIN IMMEDIATE")) {
        error = database && isBusy(database) ? "busy" : "database_error";
        return false;
    }
    auto rollback = [this]() { execute(database, "ROLLBACK"); };

    sqlite3_stmt* section = nullptr;
    const char* sectionSql = "SELECT is_active,is_open_for_enrollment FROM academic_sections WHERE id=?";
    if (sqlite3_prepare_v2(database, sectionSql, -1, &section, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(section, 1, sectionId);
    int result = sqlite3_step(section);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(section); rollback(); error = result == SQLITE_DONE ? "section_not_found" : "database_error"; return false;
    }
    bool active = sqlite3_column_int(section, 0) != 0;
    bool enrollmentOpen = sqlite3_column_int(section, 1) != 0;
    sqlite3_finalize(section);
    if (!active || !enrollmentOpen) { rollback(); error = "section_inactive"; return false; }
    std::vector<SectionMeeting> meetings;
    if (!loadMeetings(database, sectionId, meetings)) {
        rollback(); error = "database_error"; return false;
    }
    CapacityInfo capacityInfo;
    if (!readSectionCapacity(database, sectionId, meetings, capacityInfo)) {
        rollback(); error = "database_error"; return false;
    }
    if (!capacityInfo.known || capacityInfo.capacity <= 0) { rollback(); error = "capacity_unknown"; return false; }
    const SectionMeeting* selectedLab = nullptr;
    for (const SectionMeeting& meeting : meetings) {
        if (!meeting.isLabOption) continue;
        if (labMeetingId == meeting.id) selectedLab = &meeting;
    }
    if (capacityInfo.hasLabOptions && labMeetingId == 0) {
        rollback(); error = "lab_choice_required"; return false;
    }
    if ((capacityInfo.hasLabOptions && !selectedLab) ||
        (!capacityInfo.hasLabOptions && labMeetingId != 0)) {
        rollback(); error = "invalid_lab_choice"; return false;
    }
    if (selectedLab && !selectedLab->capacityKnown) {
        rollback(); error = "capacity_unknown"; return false;
    }
    if (capacityInfo.remainingSeats <= 0 ||
        (selectedLab && selectedLab->remainingSeats <= 0)) {
        rollback(); error = "section_full"; return false;
    }

    sqlite3_stmt* duplicate = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM enrollments WHERE student_id=? AND section_id=? AND status='ENROLLED'", -1, &duplicate, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(duplicate, 1, studentId);
    sqlite3_bind_int(duplicate, 2, sectionId);
    bool alreadyEnrolled = sqlite3_step(duplicate) == SQLITE_ROW;
    sqlite3_finalize(duplicate);
    if (alreadyEnrolled) { rollback(); error = "already_enrolled"; return false; }

    sqlite3_stmt* courseDuplicate = nullptr;
    const char* courseDuplicateSql =
        "SELECT 1 FROM enrollments e JOIN academic_sections existing ON existing.id=e.section_id "
        "JOIN academic_sections requested ON requested.id=? "
        "WHERE e.student_id=? AND e.status='ENROLLED' "
        "AND existing.course_code=requested.course_code AND existing.term=requested.term";
    if (sqlite3_prepare_v2(database, courseDuplicateSql, -1, &courseDuplicate, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(courseDuplicate, 1, sectionId);
    sqlite3_bind_int(courseDuplicate, 2, studentId);
    bool courseAlreadyEnrolled = sqlite3_step(courseDuplicate) == SQLITE_ROW;
    sqlite3_finalize(courseDuplicate);
    if (courseAlreadyEnrolled) { rollback(); error = "course_already_enrolled"; return false; }

    if (capacityInfo.enrolledCount >= capacityInfo.capacity) {
        rollback(); error = "section_full"; return false;
    }

    sqlite3_stmt* insert = nullptr;
    if (sqlite3_prepare_v2(database, "INSERT INTO enrollments(student_id,section_id,lab_meeting_id,status) VALUES(?,?,?,'ENROLLED')", -1, &insert, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(insert, 1, studentId);
    sqlite3_bind_int(insert, 2, sectionId);
    if (labMeetingId > 0) sqlite3_bind_int(insert, 3, labMeetingId);
    else sqlite3_bind_null(insert, 3);
    bool inserted = sqlite3_step(insert) == SQLITE_DONE;
    sqlite3_finalize(insert);
    if (!inserted) {
        const char* message = sqlite3_errmsg(database);
        error = message && std::string(message).find("course_already_enrolled") != std::string::npos
            ? "course_already_enrolled"
            : message && std::string(message).find("invalid_lab_choice") != std::string::npos
                ? "invalid_lab_choice"
            : sqlite3_extended_errcode(database) == SQLITE_CONSTRAINT_UNIQUE ? "already_enrolled" : "database_error";
        rollback(); return false;
    }
    int enrollmentId = static_cast<int>(sqlite3_last_insert_rowid(database));
    if (!recordEvent(database, enrollmentId, nullptr, "ENROLLED", studentId) || !execute(database, "COMMIT")) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_stmt* select = nullptr;
    std::string sql = std::string(enrollmentSelect) + " WHERE e.id=?";
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &select, nullptr) != SQLITE_OK) { error = "database_error"; return false; }
    sqlite3_bind_int(select, 1, enrollmentId);
    bool found = sqlite3_step(select) == SQLITE_ROW;
    if (found) readEnrollment(select, enrollment);
    sqlite3_finalize(select);
    return found && loadMeetings(database, enrollment.sectionId, enrollment.meetings,
                                 enrollment.labMeetingId, true);
}

bool EnrollmentRepository::withdraw(int studentId, int enrollmentId, Enrollment& enrollment, std::string& error) {
    std::lock_guard<std::mutex> lock(transactionMutex);
    if (!database || !execute(database, "BEGIN IMMEDIATE")) {
        error = database && isBusy(database) ? "busy" : "database_error";
        return false;
    }
    auto rollback = [this]() { execute(database, "ROLLBACK"); };
    sqlite3_stmt* find = nullptr;
    std::string selectSql = std::string(enrollmentSelect) + " WHERE e.id=? AND e.student_id=?";
    if (sqlite3_prepare_v2(database, selectSql.c_str(), -1, &find, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(find, 1, enrollmentId);
    sqlite3_bind_int(find, 2, studentId);
    int found = sqlite3_step(find);
    if (found != SQLITE_ROW) {
        sqlite3_finalize(find); rollback(); error = found == SQLITE_DONE ? "not_found" : "database_error"; return false;
    }
    readEnrollment(find, enrollment);
    sqlite3_finalize(find);
    if (!loadMeetings(database, enrollment.sectionId, enrollment.meetings,
                      enrollment.labMeetingId, true)) {
        rollback(); error = "database_error"; return false;
    }
    if (enrollment.status != "ENROLLED") { rollback(); error = "invalid_transition"; return false; }

    sqlite3_stmt* update = nullptr;
    if (sqlite3_prepare_v2(database, "UPDATE enrollments SET status='WITHDRAWN',updated_at=CURRENT_TIMESTAMP WHERE id=? AND student_id=? AND status='ENROLLED'", -1, &update, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(update, 1, enrollmentId);
    sqlite3_bind_int(update, 2, studentId);
    bool updated = sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(update);
    if (!updated || !recordEvent(database, enrollmentId, "ENROLLED", "WITHDRAWN", studentId) || !execute(database, "COMMIT")) {
        rollback(); error = "database_error"; return false;
    }
    enrollment.status = "WITHDRAWN";
    sqlite3_stmt* updatedAt = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT updated_at FROM enrollments WHERE id=?", -1, &updatedAt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(updatedAt, 1, enrollmentId);
        if (sqlite3_step(updatedAt) == SQLITE_ROW) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(updatedAt, 0));
            enrollment.updatedAt = value ? value : "";
        }
        sqlite3_finalize(updatedAt);
    }
    return true;
}

bool EnrollmentRepository::listForStudent(int studentId, bool activeOnly, std::vector<Enrollment>& enrollments) {
    std::string sql = std::string(enrollmentSelect) + " WHERE e.student_id=?";
    if (activeOnly) sql += " AND e.status='ENROLLED'";
    sql += " ORDER BY e.created_at DESC,e.id DESC";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, studentId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Enrollment enrollment;
        readEnrollment(statement, enrollment);
        enrollments.push_back(enrollment);
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;
    for (Enrollment& enrollment : enrollments)
        if (!loadMeetings(database, enrollment.sectionId, enrollment.meetings,
                          enrollment.labMeetingId, true)) return false;
    return true;
}
