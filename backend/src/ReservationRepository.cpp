#include "data_access/ReservationRepository.h"
#include <algorithm>

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

bool isUnique(sqlite3* database) {
    return sqlite3_extended_errcode(database) == SQLITE_CONSTRAINT_UNIQUE ||
           sqlite3_extended_errcode(database) == SQLITE_CONSTRAINT_PRIMARYKEY;
}

bool weekdayForDate(sqlite3* database, const std::string& date, std::string& weekday) {
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT CASE strftime('%w',?) WHEN '0' THEN 'Sunday' WHEN '1' THEN 'Monday' "
        "WHEN '2' THEN 'Tuesday' WHEN '3' THEN 'Wednesday' WHEN '4' THEN 'Thursday' "
        "WHEN '5' THEN 'Friday' WHEN '6' THEN 'Saturday' END";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    bindText(statement, 1, date);
    int result = sqlite3_step(statement);
    if (result == SQLITE_ROW) {
        const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        weekday = value ? value : "";
    }
    sqlite3_finalize(statement);
    return result == SQLITE_ROW && !weekday.empty();
}

bool hasClassConflict(sqlite3* database, int roomId, const std::string& date,
                      const std::string& startTime, const std::string& endTime,
                      bool& conflict) {
    std::string weekday;
    if (!weekdayForDate(database, date, weekday)) return false;
    const char* sql =
        "SELECT 1 FROM academic_section_meetings m JOIN academic_sections s ON s.id=m.section_id "
        "WHERE m.classroom_id=? AND s.is_active=1 AND m.day=? AND m.start_time<? AND m.end_time>? "
        "UNION ALL "
        "SELECT 1 FROM academic_sections s WHERE s.classroom_id=? AND s.is_active=1 AND s.day=? "
        "AND s.start_time<? AND s.end_time>? AND NOT EXISTS "
        "(SELECT 1 FROM academic_section_meetings m WHERE m.section_id=s.id) LIMIT 1";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, roomId);
    bindText(statement, 2, weekday);
    bindText(statement, 3, endTime);
    bindText(statement, 4, startTime);
    sqlite3_bind_int(statement, 5, roomId);
    bindText(statement, 6, weekday);
    bindText(statement, 7, endTime);
    bindText(statement, 8, startTime);
    int result = sqlite3_step(statement);
    conflict = result == SQLITE_ROW;
    sqlite3_finalize(statement);
    return result == SQLITE_ROW || result == SQLITE_DONE;
}

const char* requestSelect =
    "SELECT r.id,r.organization_id,o.name,r.requester_id,u.full_name,r.classroom_id,c.room_code,"
    "r.reservation_date,r.start_time,r.end_time,r.purpose,r.priority_level,r.priority_source,"
    "r.submitted_at,r.status,r.decided_by,r.decided_at,r.rejection_reason "
    "FROM reservation_requests r JOIN organizations o ON o.id=r.organization_id "
    "JOIN students u ON u.id=r.requester_id JOIN classrooms c ON c.id=r.classroom_id";

void readRequest(sqlite3_stmt* statement, ReservationRequest& request) {
    request.id = sqlite3_column_int(statement, 0);
    request.organizationId = sqlite3_column_int(statement, 1);
    request.organizationName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
    request.requesterId = sqlite3_column_int(statement, 3);
    request.requesterName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
    request.classroomId = sqlite3_column_int(statement, 5);
    request.roomCode = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
    request.reservationDate = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
    request.startTime = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
    request.endTime = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
    request.purpose = reinterpret_cast<const char*>(sqlite3_column_text(statement, 10));
    request.priorityLevel = sqlite3_column_int(statement, 11);
    request.prioritySource = reinterpret_cast<const char*>(sqlite3_column_text(statement, 12));
    request.submittedAt = reinterpret_cast<const char*>(sqlite3_column_text(statement, 13));
    request.status = reinterpret_cast<const char*>(sqlite3_column_text(statement, 14));
    request.decidedBy = sqlite3_column_int(statement, 15);
    const char* decidedAt = reinterpret_cast<const char*>(sqlite3_column_text(statement, 16));
    const char* rejection = reinterpret_cast<const char*>(sqlite3_column_text(statement, 17));
    request.decidedAt = decidedAt ? decidedAt : "";
    request.rejectionReason = rejection ? rejection : "";
}
}

ReservationRepository::ReservationRepository(const std::string& databasePath) {
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

ReservationRepository::~ReservationRepository() {
    if (database) sqlite3_close(database);
}

bool ReservationRepository::initialize() {
    if (!database) return false;
    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS organizations (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 name TEXT NOT NULL UNIQUE,
 organization_type TEXT NOT NULL,
 priority_level INTEGER NOT NULL DEFAULT 1 CHECK(priority_level BETWEEN 1 AND 3),
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),
 created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS organization_members (
 organization_id INTEGER NOT NULL REFERENCES organizations(id) ON DELETE RESTRICT,
 student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
 member_role TEXT NOT NULL CHECK(member_role IN ('MEMBER','REPRESENTATIVE')),
 is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),
 joined_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
 PRIMARY KEY(organization_id,student_id)
);
CREATE INDEX IF NOT EXISTS idx_organization_members_student
 ON organization_members(student_id,is_active,member_role);
CREATE TABLE IF NOT EXISTS reservation_requests (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 organization_id INTEGER NOT NULL REFERENCES organizations(id) ON DELETE RESTRICT,
 requester_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
 classroom_id INTEGER NOT NULL REFERENCES classrooms(id) ON DELETE RESTRICT,
 reservation_date TEXT NOT NULL,
 start_time TEXT NOT NULL,
 end_time TEXT NOT NULL,
 purpose TEXT NOT NULL,
 priority_level INTEGER NOT NULL CHECK(priority_level BETWEEN 1 AND 3),
 priority_source TEXT NOT NULL,
 submitted_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
 status TEXT NOT NULL CHECK(status IN ('PENDING','APPROVED','REJECTED','CANCELLED')),
 decided_by INTEGER REFERENCES students(id) ON DELETE RESTRICT,
 decided_at TEXT,
 rejection_reason TEXT NOT NULL DEFAULT '',
 CHECK(end_time > start_time)
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_duplicate_pending_request
 ON reservation_requests(organization_id,requester_id,classroom_id,reservation_date,start_time,end_time)
 WHERE status='PENDING';
CREATE INDEX IF NOT EXISTS idx_reservations_room_date_status
 ON reservation_requests(classroom_id,reservation_date,status,start_time,end_time);
CREATE INDEX IF NOT EXISTS idx_reservations_pending_priority
 ON reservation_requests(status,priority_level,submitted_at,id);
CREATE TABLE IF NOT EXISTS reservation_events (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 reservation_id INTEGER NOT NULL REFERENCES reservation_requests(id) ON DELETE RESTRICT,
 old_status TEXT,
 new_status TEXT NOT NULL,
 actor_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
 reason TEXT NOT NULL DEFAULT '',
 created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TRIGGER IF NOT EXISTS prevent_approved_reservation_overlap
BEFORE UPDATE OF status,reservation_date,start_time,end_time,classroom_id ON reservation_requests
WHEN NEW.status='APPROVED'
BEGIN
 SELECT CASE WHEN EXISTS (
  SELECT 1 FROM reservation_requests existing
  WHERE existing.classroom_id=NEW.classroom_id
    AND existing.reservation_date=NEW.reservation_date
    AND existing.status='APPROVED'
    AND existing.id<>NEW.id
    AND existing.start_time<NEW.end_time
    AND existing.end_time>NEW.start_time
 ) THEN RAISE(ABORT,'reservation_overlap') END;
END;
)SQL";
    if (!execute(database, schema)) return false;
    std::lock_guard<std::mutex> lock(repositoryMutex);
    return reloadPendingQueue();
}

bool ReservationRepository::createOrganization(const Organization& organization, std::string& error,
                                               Organization& created) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (!database) { error = "database_error"; return false; }
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO organizations(name,organization_type,priority_level) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) { error = "database_error"; return false; }
    bindText(statement, 1, organization.name);
    bindText(statement, 2, organization.type);
    sqlite3_bind_int(statement, 3, organization.priorityLevel);
    int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) error = isUnique(database) ? "duplicate_organization" : "database_error";
    if (result == SQLITE_DONE) created.id = static_cast<int>(sqlite3_last_insert_rowid(database));
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;
    created.name = organization.name;
    created.type = organization.type;
    created.priorityLevel = organization.priorityLevel;
    return true;
}

bool ReservationRepository::addOrganizationMember(int organizationId, const std::string& studentId,
                                                  const std::string& role, std::string& error) {
    if (role != "MEMBER" && role != "REPRESENTATIVE") { error = "invalid_member_role"; return false; }
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (!database) { error = "database_error"; return false; }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO organization_members(organization_id,student_id,member_role) "
        "SELECT o.id,s.id,? FROM organizations o JOIN students s ON s.student_id=? "
        "WHERE o.id=? AND o.is_active=1 "
        "ON CONFLICT(organization_id,student_id) DO UPDATE SET member_role=excluded.member_role,is_active=1";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) { error = "database_error"; return false; }
    bindText(statement, 1, role);
    bindText(statement, 2, studentId);
    sqlite3_bind_int(statement, 3, organizationId);
    bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!ok) error = "organization_or_student_not_found";
    return ok;
}

bool ReservationRepository::listOrganizations(int studentId, bool allOrganizations,
                                              std::vector<Organization>& organizations) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    const char* allSql = "SELECT id,name,organization_type,priority_level,is_active,0 FROM organizations ORDER BY name";
    const char* mineSql =
        "SELECT o.id,o.name,o.organization_type,o.priority_level,o.is_active,"
        "CASE WHEN m.member_role='REPRESENTATIVE' THEN 1 ELSE 0 END "
        "FROM organizations o JOIN organization_members m ON m.organization_id=o.id "
        "WHERE m.student_id=? AND m.is_active=1 AND o.is_active=1 ORDER BY o.name";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, allOrganizations ? allSql : mineSql, -1, &statement, nullptr) != SQLITE_OK) return false;
    if (!allOrganizations) sqlite3_bind_int(statement, 1, studentId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Organization organization;
        organization.id = sqlite3_column_int(statement, 0);
        organization.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        organization.type = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        organization.priorityLevel = sqlite3_column_int(statement, 3);
        organization.isActive = sqlite3_column_int(statement, 4) != 0;
        organization.isRepresentative = sqlite3_column_int(statement, 5) != 0;
        organizations.push_back(organization);
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool ReservationRepository::addEvent(int requestId, const std::string& oldStatus,
                                     const std::string& newStatus, int actorId, const std::string& reason) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO reservation_events(reservation_id,old_status,new_status,actor_id,reason) VALUES(?,?,?,?,?)";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, requestId);
    if (oldStatus.empty()) sqlite3_bind_null(statement, 2); else bindText(statement, 2, oldStatus);
    bindText(statement, 3, newStatus);
    sqlite3_bind_int(statement, 4, actorId);
    bindText(statement, 5, reason);
    bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ReservationRepository::createRequest(int requesterId, int organizationId, int classroomId,
                                          const std::string& reservationDate, const std::string& startTime,
                                          const std::string& endTime, const std::string& purpose,
                                          ReservationRequest& request, std::string& error) {
    if (startTime < "09:00" || endTime > "17:30" || endTime <= startTime) {
        error = "outside_booking_hours";
        return false;
    }
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (!database || !execute(database, "BEGIN IMMEDIATE")) {
        error = database && isBusy(database) ? "busy" : "database_error";
        return false;
    }
    auto rollback = [this]() { execute(database, "ROLLBACK"); };
    sqlite3_stmt* organization = nullptr;
    const char* orgSql =
        "SELECT o.priority_level FROM organizations o JOIN organization_members m ON m.organization_id=o.id "
        "WHERE o.id=? AND o.is_active=1 AND m.student_id=? AND m.is_active=1";
    if (sqlite3_prepare_v2(database, orgSql, -1, &organization, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(organization, 1, organizationId);
    sqlite3_bind_int(organization, 2, requesterId);
    int orgResult = sqlite3_step(organization);
    if (orgResult != SQLITE_ROW) {
        sqlite3_finalize(organization); rollback();
        error = orgResult == SQLITE_DONE ? "not_authorized_for_organization" : "database_error";
        return false;
    }
    int priority = sqlite3_column_int(organization, 0);
    sqlite3_finalize(organization);

    sqlite3_stmt* room = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM classrooms WHERE id=? AND is_active=1", -1, &room, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(room, 1, classroomId);
    int roomResult = sqlite3_step(room);
    sqlite3_finalize(room);
    if (roomResult != SQLITE_ROW) { rollback(); error = roomResult == SQLITE_DONE ? "classroom_not_found_or_inactive" : "database_error"; return false; }

    bool classConflict = false;
    if (!hasClassConflict(database, classroomId, reservationDate, startTime, endTime, classConflict)) {
        rollback(); error = "database_error"; return false;
    }
    if (classConflict) { rollback(); error = "schedule_conflict"; return false; }

    sqlite3_stmt* insert = nullptr;
    const char* insertSql =
        "INSERT INTO reservation_requests(organization_id,requester_id,classroom_id,reservation_date,"
        "start_time,end_time,purpose,priority_level,priority_source,status) VALUES(?,?,?,?,?,?,?,?,?,'PENDING')";
    if (sqlite3_prepare_v2(database, insertSql, -1, &insert, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    sqlite3_bind_int(insert, 1, organizationId);
    sqlite3_bind_int(insert, 2, requesterId);
    sqlite3_bind_int(insert, 3, classroomId);
    bindText(insert, 4, reservationDate);
    bindText(insert, 5, startTime);
    bindText(insert, 6, endTime);
    bindText(insert, 7, purpose);
    sqlite3_bind_int(insert, 8, priority);
    bindText(insert, 9, "Organization policy level " + std::to_string(priority) + " (demo)");
    int insertResult = sqlite3_step(insert);
    if (insertResult != SQLITE_DONE) error = isUnique(database) ? "duplicate_pending_request" : "database_error";
    int requestId = insertResult == SQLITE_DONE ? static_cast<int>(sqlite3_last_insert_rowid(database)) : 0;
    sqlite3_finalize(insert);
    if (insertResult != SQLITE_DONE) { rollback(); return false; }
    if (!addEvent(requestId, "", "PENDING", requesterId, "Request submitted")) {
        rollback(); error = "database_error"; return false;
    }
    if (!execute(database, "COMMIT")) { rollback(); error = isBusy(database) ? "busy" : "database_error"; return false; }
    if (!findRequest(requestId, request) || !reloadPendingQueue()) { error = "database_error"; return false; }
    return true;
}

bool ReservationRepository::findRequest(int requestId, ReservationRequest& request) {
    sqlite3_stmt* statement = nullptr;
    std::string sql = std::string(requestSelect) + " WHERE r.id=?";
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, requestId);
    bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) readRequest(statement, request);
    sqlite3_finalize(statement);
    if (!found) return false;

    sqlite3_stmt* history = nullptr;
    const char* historySql =
        "SELECT id,old_status,new_status,actor_id,reason,created_at FROM reservation_events "
        "WHERE reservation_id=? ORDER BY id";
    if (sqlite3_prepare_v2(database, historySql, -1, &history, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(history, 1, requestId);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(history)) == SQLITE_ROW) {
        ReservationEvent event;
        event.id = sqlite3_column_int(history, 0);
        const char* oldStatus = reinterpret_cast<const char*>(sqlite3_column_text(history, 1));
        const char* newStatus = reinterpret_cast<const char*>(sqlite3_column_text(history, 2));
        const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(history, 4));
        const char* created = reinterpret_cast<const char*>(sqlite3_column_text(history, 5));
        event.oldStatus = oldStatus ? oldStatus : "";
        event.newStatus = newStatus ? newStatus : "";
        event.actorId = sqlite3_column_int(history, 3);
        event.reason = reason ? reason : "";
        event.createdAt = created ? created : "";
        request.history.push_back(event);
    }
    sqlite3_finalize(history);
    return result == SQLITE_DONE;
}

bool ReservationRepository::listMine(int studentId, std::vector<ReservationRequest>& requests) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    const char* sql =
        "SELECT r.id FROM reservation_requests r WHERE r.requester_id=? OR EXISTS "
        "(SELECT 1 FROM organization_members m WHERE m.organization_id=r.organization_id "
        "AND m.student_id=? AND m.is_active=1) ORDER BY r.submitted_at DESC,r.id DESC";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, studentId);
    sqlite3_bind_int(statement, 2, studentId);
    std::vector<int> ids;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) ids.push_back(sqlite3_column_int(statement, 0));
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;
    for (int id : ids) {
        ReservationRequest request;
        if (!findRequest(id, request)) return false;
        requests.push_back(request);
    }
    return true;
}

bool ReservationRepository::findForMember(int studentId, int requestId, ReservationRequest& request) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT 1 FROM reservation_requests r WHERE r.id=? AND "
        "(r.requester_id=? OR EXISTS (SELECT 1 FROM organization_members m "
        "WHERE m.organization_id=r.organization_id AND m.student_id=? AND m.is_active=1))";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, requestId);
    sqlite3_bind_int(statement, 2, studentId);
    sqlite3_bind_int(statement, 3, studentId);
    bool allowed = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return allowed && findRequest(requestId, request);
}

bool ReservationRepository::cancel(int studentId, int requestId, ReservationRequest& request,
                                   std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (!database || !execute(database, "BEGIN IMMEDIATE")) {
        error = database && isBusy(database) ? "busy" : "database_error";
        return false;
    }
    auto rollback = [this]() { execute(database, "ROLLBACK"); };
    ReservationRequest current;
    if (!findRequest(requestId, current) || current.requesterId != studentId) {
        rollback(); error = "not_found"; return false;
    }
    if (current.status != "PENDING") { rollback(); error = "invalid_transition"; return false; }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
        "UPDATE reservation_requests SET status='CANCELLED',decided_by=?,decided_at=CURRENT_TIMESTAMP WHERE id=? AND status='PENDING'",
        -1, &statement, nullptr) != SQLITE_OK) { rollback(); error = "database_error"; return false; }
    sqlite3_bind_int(statement, 1, studentId);
    sqlite3_bind_int(statement, 2, requestId);
    bool updated = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    if (!updated || !addEvent(requestId, "PENDING", "CANCELLED", studentId, "Cancelled by requester") ||
        !execute(database, "COMMIT")) {
        rollback(); error = "database_error"; return false;
    }
    if (!findRequest(requestId, request) || !reloadPendingQueue()) { error = "database_error"; return false; }
    return true;
}

bool ReservationRepository::reloadPendingQueue() {
    pendingHeap.clear();
    queuedRequestIds.clear();
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT id,priority_level,submitted_at,"
        "CAST((julianday('now')-julianday(submitted_at))*24 AS INTEGER) "
        "FROM reservation_requests WHERE status='PENDING'";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        ReservationQueueItem item;
        item.requestId = sqlite3_column_int(statement, 0);
        item.basePriority = sqlite3_column_int(statement, 1);
        const char* submitted = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        item.submittedAt = submitted ? submitted : "";
        int ageHours = std::max(0, sqlite3_column_int(statement, 3));
        item.effectivePriority = std::min(3, item.basePriority + ageHours / 24);
        bool exists = false;
        if (!queuedRequestIds.find(item.requestId, exists)) {
            queuedRequestIds.insert(item.requestId, true);
            pendingHeap.push(item);
        }
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE;
}

bool ReservationRepository::prioritizedQueue(std::vector<ReservationRequest>& requests) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (!reloadPendingQueue()) return false;
    MaxHeap<ReservationQueueItem> ordered = pendingHeap;
    while (!ordered.empty()) {
        ReservationQueueItem item = ordered.pop();
        ReservationRequest request;
        if (!findRequest(item.requestId, request) || request.status != "PENDING") continue;
        request.effectivePriority = item.effectivePriority;
        requests.push_back(request);
    }
    return true;
}

bool ReservationRepository::decide(int adminId, int requestId, bool approve,
                                   const std::string& reason, ReservationRequest& request,
                                   std::string& error) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    if (!database || !execute(database, "BEGIN IMMEDIATE")) {
        error = database && isBusy(database) ? "busy" : "database_error";
        return false;
    }
    auto rollback = [this]() { execute(database, "ROLLBACK"); };
    ReservationRequest current;
    if (!findRequest(requestId, current)) { rollback(); error = "not_found"; return false; }
    if (current.status != "PENDING") { rollback(); error = "invalid_transition"; return false; }

    std::string newStatus = approve ? "APPROVED" : "REJECTED";
    if (approve) {
        if (current.startTime < "09:00" || current.endTime > "17:30" ||
            current.endTime <= current.startTime) {
            rollback(); error = "outside_booking_hours"; return false;
        }
        sqlite3_stmt* room = nullptr;
        if (sqlite3_prepare_v2(database, "SELECT 1 FROM classrooms WHERE id=? AND is_active=1", -1, &room, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        sqlite3_bind_int(room, 1, current.classroomId);
        bool activeRoom = sqlite3_step(room) == SQLITE_ROW;
        sqlite3_finalize(room);
        if (!activeRoom) { rollback(); error = "classroom_inactive"; return false; }

        bool classConflict = false;
        if (!hasClassConflict(database, current.classroomId, current.reservationDate,
                              current.startTime, current.endTime, classConflict)) {
            rollback(); error = "database_error"; return false;
        }
        if (classConflict) { rollback(); error = "schedule_conflict"; return false; }

        sqlite3_stmt* overlap = nullptr;
        const char* overlapSql =
            "SELECT 1 FROM reservation_requests WHERE classroom_id=? AND reservation_date=? "
            "AND status='APPROVED' AND id<>? AND start_time<? AND end_time>? LIMIT 1";
        if (sqlite3_prepare_v2(database, overlapSql, -1, &overlap, nullptr) != SQLITE_OK) {
            rollback(); error = "database_error"; return false;
        }
        sqlite3_bind_int(overlap, 1, current.classroomId);
        bindText(overlap, 2, current.reservationDate);
        sqlite3_bind_int(overlap, 3, requestId);
        bindText(overlap, 4, current.endTime);
        bindText(overlap, 5, current.startTime);
        bool hasOverlap = sqlite3_step(overlap) == SQLITE_ROW;
        sqlite3_finalize(overlap);
        if (hasOverlap) { rollback(); error = "reservation_conflict"; return false; }
    } else if (reason.empty()) {
        rollback(); error = "rejection_reason_required"; return false;
    }

    sqlite3_stmt* update = nullptr;
    const char* updateSql =
        "UPDATE reservation_requests SET status=?,decided_by=?,decided_at=CURRENT_TIMESTAMP,rejection_reason=? "
        "WHERE id=? AND status='PENDING'";
    if (sqlite3_prepare_v2(database, updateSql, -1, &update, nullptr) != SQLITE_OK) {
        rollback(); error = "database_error"; return false;
    }
    bindText(update, 1, newStatus);
    sqlite3_bind_int(update, 2, adminId);
    bindText(update, 3, approve ? "" : reason);
    sqlite3_bind_int(update, 4, requestId);
    bool updated = sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(database) == 1;
    if (!updated && sqlite3_errmsg(database) && std::string(sqlite3_errmsg(database)).find("reservation_overlap") != std::string::npos)
        error = "reservation_conflict";
    sqlite3_finalize(update);
    if (!updated || !addEvent(requestId, "PENDING", newStatus, adminId, reason) || !execute(database, "COMMIT")) {
        rollback();
        if (error.empty()) error = isBusy(database) ? "busy" : "database_error";
        return false;
    }
    if (!findRequest(requestId, request) || !reloadPendingQueue()) { error = "database_error"; return false; }
    return true;
}

bool ReservationRepository::roomSchedule(int roomId, const std::string& date,
                                         std::vector<RoomScheduleBlock>& blocks) {
    std::lock_guard<std::mutex> lock(repositoryMutex);
    std::string weekday;
    if (!database || !weekdayForDate(database, date, weekday)) return false;

    sqlite3_stmt* statement = nullptr;
    const char* classSql =
        "SELECT s.course_code,s.course_name,s.section_code,m.activity_type,m.start_time,m.end_time "
        "FROM academic_section_meetings m JOIN academic_sections s ON s.id=m.section_id "
        "WHERE m.classroom_id=? AND m.day=? AND s.is_active=1 "
        "UNION ALL "
        "SELECT s.course_code,s.course_name,s.section_code,'Class',s.start_time,s.end_time "
        "FROM academic_sections s WHERE s.classroom_id=? AND s.day=? AND s.is_active=1 "
        "AND NOT EXISTS (SELECT 1 FROM academic_section_meetings m WHERE m.section_id=s.id) "
        "ORDER BY 5,6";
    if (sqlite3_prepare_v2(database, classSql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, roomId);
    bindText(statement, 2, weekday);
    sqlite3_bind_int(statement, 3, roomId);
    bindText(statement, 4, weekday);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        auto text = [statement](int column) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
            return value ? std::string(value) : std::string();
        };
        RoomScheduleBlock block;
        block.kind = "CLASS";
        block.title = text(0) + " · " + text(1);
        block.subtitle = text(2) + " · " + text(3);
        block.startTime = text(4);
        block.endTime = text(5);
        blocks.push_back(std::move(block));
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;

    const char* reservationSql =
        "SELECT start_time,end_time FROM reservation_requests "
        "WHERE classroom_id=? AND reservation_date=? AND status='APPROVED' ORDER BY start_time,id";
    if (sqlite3_prepare_v2(database, reservationSql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, roomId);
    bindText(statement, 2, date);
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        RoomScheduleBlock block;
        block.kind = "RESERVATION";
        block.title = "Approved room reservation";
        block.subtitle = "Reserved";
        const char* start = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        const char* end = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        block.startTime = start ? start : "";
        block.endTime = end ? end : "";
        blocks.push_back(std::move(block));
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;
    std::sort(blocks.begin(), blocks.end(), [](const RoomScheduleBlock& left, const RoomScheduleBlock& right) {
        return left.startTime < right.startTime;
    });
    return true;
}
