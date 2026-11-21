#include "data_access/CampusRepository.h"
#include "data_access/EnrollmentRepository.h"
#include "data_access/MapRepository.h"
#include "data_access/NavigationRepository.h"
#include "data_access/ReservationRepository.h"
#include "data_access/StudentRepository.h"
#include "models/CampusRecords.h"
#include "models/CampusMap.h"
#include "models/Navigation.h"
#include "models/Student.h"
#include <sqlite3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Student makeStudent(const std::string& id) {
    Student student;
    student.studentId = id;
    student.fullName = id;
    student.universityEmail = id + "@example.test";
    student.passwordHash = "test-hash";
    student.faculty = "Test Faculty";
    return student;
}

void runWorkflow(const std::string& path) {
    sqlite3* database = nullptr;
    require(sqlite3_open(path.c_str(), &database) == SQLITE_OK, "Could not open test database");
    std::unique_ptr<sqlite3, int(*)(sqlite3*)> databaseGuard(database, sqlite3_close);
    {
        StudentRepository students(database);
        CampusRepository campus(database);
        require(students.initialize() && campus.initialize(), "Base database initialization failed");
        std::string error;
        for (const std::string id : {"STU-1", "STU-2", "STU-3"}) {
            Student student = makeStudent(id);
            require(students.create(student, error), "Student setup failed");
        }
        Student first, second, third;
        require(students.findByStudentId("STU-1", first) &&
                students.findByStudentId("STU-2", second) &&
                students.findByStudentId("STU-3", third), "Student lookup failed");

        std::vector<Building> buildings;
        require(campus.listBuildings(buildings) && !buildings.empty(), "Demo building setup failed");
        std::vector<Floor> floors;
        require(campus.listFloors(buildings.front().id, floors) && !floors.empty(), "Demo floor setup failed");
        Classroom room;
        room.buildingId = buildings.front().id;
        room.floorId = floors.front().id;
        room.roomCode = "TEST-101";
        room.roomName = "Test Room";
        room.roomType = "Seminar room";
        room.capacity = 2;
        require(campus.saveClassroom(room, error), "Test classroom setup failed");
        Classroom normalizedDuplicate = room;
        normalizedDuplicate.id = 0;
        normalizedDuplicate.roomCode = " test - 101 ";
        require(!campus.saveClassroom(normalizedDuplicate, error) && error == "duplicate_room_code",
                "Room codes differing only by case or spacing must refer to one shared classroom");
        sqlite3_stmt* legacyAlias = nullptr;
        require(sqlite3_prepare_v2(database,
                    "INSERT INTO classrooms(building_id,floor_id,room_code,room_name,room_type,capacity,capacity_known,is_active) "
                    "VALUES(?,?,?,'Duplicate alias','Seminar room',30,1,1)",
                    -1, &legacyAlias, nullptr) == SQLITE_OK, "Could not prepare legacy duplicate-room migration fixture");
        sqlite3_bind_int(legacyAlias, 1, room.buildingId);
        sqlite3_bind_int(legacyAlias, 2, room.floorId);
        sqlite3_bind_text(legacyAlias, 3, " TEST - 101 ", -1, SQLITE_TRANSIENT);
        require(sqlite3_step(legacyAlias) == SQLITE_DONE, "Could not create legacy duplicate room");
        int legacyAliasId = static_cast<int>(sqlite3_last_insert_rowid(database));
        sqlite3_finalize(legacyAlias);
        require(campus.initialize(), "Room alias migration should complete during campus initialization");
        Classroom removedAlias;
        require(!campus.findClassroom(legacyAliasId, removedAlias),
                "Existing case/spacing duplicate rooms should merge into one shared classroom");
        AcademicSection section;
        section.courseCode = "TEST100";
        section.courseName = "Test Course";
        section.sectionCode = "A";
        section.term = "Fall 2099";
        section.day = "Monday";
        section.startTime = "10:00";
        section.endTime = "11:00";
        section.classroomId = room.id;
        section.isOpenForEnrollment = true;
        require(campus.saveSection(section, error), "Test section setup failed");

        EnrollmentRepository enrollments(path);
        EnrollmentRepository concurrentEnrollments(path);
        require(enrollments.initialize() && concurrentEnrollments.initialize(), "Enrollment schema setup failed");
        std::atomic_int enrolled{0};
        std::string firstError, secondError, thirdError;
        Enrollment firstEnrollment, secondEnrollment, thirdEnrollment;
        std::thread firstThread([&] { if (enrollments.enroll(first.id, section.id, 0, firstEnrollment, firstError)) ++enrolled; });
        std::thread secondThread([&] { if (concurrentEnrollments.enroll(second.id, section.id, 0, secondEnrollment, secondError)) ++enrolled; });
        std::thread thirdThread([&] { if (enrollments.enroll(third.id, section.id, 0, thirdEnrollment, thirdError)) ++enrolled; });
        firstThread.join();
        secondThread.join();
        thirdThread.join();
        require(enrolled == 2, "Concurrent enrollments must not exceed the section capacity");
        require(firstError == "section_full" || secondError == "section_full" || thirdError == "section_full",
                "Capacity overflow should return section_full");

        Classroom smallerRoom;
        require(campus.findClassroom(room.id, smallerRoom), "Classroom reload failed");
        smallerRoom.capacity = 1;
        require(!campus.saveClassroom(smallerRoom, error) && error == "active_enrollments_exceed_capacity",
                "Room capacity must not be reduced below active enrollments");

        std::vector<Enrollment> activeEnrollments;
        require(enrollments.listForStudent(first.id, true, activeEnrollments), "Enrollment listing failed");
        if (!activeEnrollments.empty()) {
            Enrollment withdrawn;
            require(enrollments.withdraw(first.id, activeEnrollments.front().id, withdrawn, error),
                    "Student withdrawal failed");
            require(withdrawn.status == "WITHDRAWN" && !withdrawn.updatedAt.empty(),
                    "Withdrawal status and update timestamp should be returned");
        } else {
            require(enrollments.listForStudent(second.id, true, activeEnrollments) && !activeEnrollments.empty(),
                    "Expected an enrolled student");
            Enrollment withdrawn;
            require(enrollments.withdraw(second.id, activeEnrollments.front().id, withdrawn, error),
                    "Student withdrawal failed");
        }
        Enrollment replacement;
        int replacementStudent = firstError == "section_full" ? first.id :
                                 secondError == "section_full" ? second.id : third.id;
        require(enrollments.enroll(replacementStudent, section.id, 0, replacement, error),
                "A withdrawn seat should become available");

        ReservationRepository reservations(path);
        require(reservations.initialize(), "Reservation schema setup failed");
        Organization firstOrganization;
        firstOrganization.name = "Test Organization A";
        firstOrganization.type = "Student club";
        firstOrganization.priorityLevel = 2;
        Organization createdOrganization;
        require(reservations.createOrganization(firstOrganization, error, createdOrganization),
                "Organization creation failed");
        require(reservations.addOrganizationMember(createdOrganization.id, "STU-1", "REPRESENTATIVE", error),
                "Organization representative assignment failed");
        require(reservations.addOrganizationMember(createdOrganization.id, "STU-2", "MEMBER", error),
                "Organization member assignment failed");
        Organization secondOrganization;
        secondOrganization.name = "Test Organization B";
        secondOrganization.type = "Student club";
        secondOrganization.priorityLevel = 1;
        Organization secondCreated;
        require(reservations.createOrganization(secondOrganization, error, secondCreated),
                "Second organization creation failed");
        require(reservations.addOrganizationMember(secondCreated.id, "STU-1", "REPRESENTATIVE", error),
                "Second representative assignment failed");
        ReservationRequest unauthorizedRequest;
        require(!reservations.createRequest(third.id, createdOrganization.id, room.id, "2099-10-09",
                    "10:00", "11:00", "Unauthorized request", unauthorizedRequest, error) &&
                error == "not_authorized_for_organization",
                "A user outside an organization must not submit a request");

        ReservationRequest memberRequest;
        require(reservations.createRequest(second.id, createdOrganization.id, room.id, "2099-10-12",
                    "12:00", "13:00", "Member project meeting", memberRequest, error),
                "Any active organization member should be able to request a room outside class hours");
        std::vector<RoomScheduleBlock> weekdaySchedule;
        require(reservations.roomSchedule(room.id, "2099-10-12", weekdaySchedule) &&
                weekdaySchedule.size() == 1 && weekdaySchedule.front().kind == "CLASS" &&
                weekdaySchedule.front().startTime == "10:00" && weekdaySchedule.front().endTime == "11:00",
                "Room availability should include scheduled classes but ignore pending booking requests");
        ReservationRequest classConflict;
        require(!reservations.createRequest(second.id, createdOrganization.id, room.id, "2099-10-12",
                    "10:30", "11:30", "Conflicts with lecture", classConflict, error) &&
                error == "schedule_conflict",
                "Room requests must not overlap a scheduled lecture");

        ReservationRequest firstRequest, secondRequest;
        require(reservations.createRequest(first.id, createdOrganization.id, room.id, "2099-10-10",
                    "10:00", "12:00", "Club meeting", firstRequest, error),
                "Reservation request creation failed");
        require(reservations.createRequest(first.id, secondCreated.id, room.id, "2099-10-10",
                    "11:00", "13:00", "Overlapping event", secondRequest, error),
                "Overlapping request should be pending until approval");
        std::vector<ReservationRequest> queue;
        require(reservations.prioritizedQueue(queue) && queue.size() == 2 &&
                queue.front().id == firstRequest.id && queue.front().effectivePriority == 2,
                "Reservation queue should apply organization priority");
        ReservationRequest approved;
        require(reservations.decide(first.id, firstRequest.id, true, "", approved, error),
                "Non-overlapping reservation approval failed");
        ReservationRequest rejected;
        require(!reservations.decide(second.id, secondRequest.id, true, "", rejected, error) &&
                error == "reservation_conflict", "Overlapping approval must be rejected");
        require(reservations.cancel(first.id, secondRequest.id, rejected, error) &&
                rejected.status == "CANCELLED" && rejected.history.size() == 2,
                "Requester cancellation and audit history failed");
        std::vector<RoomScheduleBlock> schedule;
        require(reservations.roomSchedule(room.id, "2099-10-10", schedule) &&
                schedule.size() == 1 && schedule.front().kind == "RESERVATION" &&
                schedule.front().startTime == firstRequest.startTime,
                "Only approved reservations should block room availability");

        ReservationRepository concurrentReservations(path);
        require(concurrentReservations.initialize(), "Second reservation connection setup failed");
        ReservationRequest concurrentFirst, concurrentSecond;
        require(reservations.createRequest(first.id, createdOrganization.id, room.id, "2099-10-11",
                    "10:00", "11:00", "Concurrent request A", concurrentFirst, error) &&
                reservations.createRequest(first.id, secondCreated.id, room.id, "2099-10-11",
                    "10:30", "11:30", "Concurrent request B", concurrentSecond, error),
                "Concurrent reservation request setup failed");
        std::atomic_int approvals{0};
        std::string firstDecisionError, secondDecisionError;
        ReservationRequest firstDecision, secondDecision;
        std::thread firstApproval([&] {
            if (reservations.decide(first.id, concurrentFirst.id, true, "", firstDecision, firstDecisionError))
                ++approvals;
        });
        std::thread secondApproval([&] {
            if (concurrentReservations.decide(second.id, concurrentSecond.id, true, "", secondDecision, secondDecisionError))
                ++approvals;
        });
        firstApproval.join();
        secondApproval.join();
        require(approvals == 1, "Concurrent overlapping approvals must not both succeed");
        int pendingRequestId = firstDecision.status == "APPROVED" ? concurrentSecond.id : concurrentFirst.id;
        ReservationRequest pendingRequest;
        require(reservations.cancel(first.id, pendingRequestId, pendingRequest, error) &&
                pendingRequest.status == "CANCELLED",
                "The losing concurrent reservation request should remain cancellable");

        NavigationRepository navigation(database);
        require(navigation.initialize(), "Navigation schema setup failed");
        MapRepository maps(database);
        require(maps.initialize(), "Map schema and demo seed setup failed");
        require(navigation.refresh(), "Navigation coordinates should refresh after map seeding");
        std::vector<MapFloorInfo> mapFloors;
        require(maps.listFloors(buildings.front().id, mapFloors, error) && !mapFloors.empty() &&
                mapFloors.front().hasMap && mapFloors.front().verificationStatus == "DEMO",
                "The seeded floor should expose explicit demo map metadata");
        MapFloorData mapData;
        require(maps.getFloorFeatures(mapFloors.front().id, mapData, error) &&
                mapData.features.size() == 6 && mapData.navigationLocations.size() == 5,
                "The seeded map should contain illustrative geometry and located graph points");
        require(mapData.features.front().isDemo, "Synthetic map features must retain their demo marker");
        require(maps.setFeatureActive(mapData.features.front().id, false, error),
                "Map features should be deactivatable without deletion");
        MapFloorData activeMapData, adminMapData;
        require(maps.getFloorFeatures(mapFloors.front().id, activeMapData, error) &&
                activeMapData.features.size() == 5,
                "Normal map reads should omit inactive features");
        require(maps.getFloorFeatures(mapFloors.front().id, adminMapData, error, true) &&
                adminMapData.features.size() == 6 &&
                !adminMapData.features.front().isActive,
                "Administrative map reads should include inactive features for restoration");
        require(maps.setFeatureActive(mapData.features.front().id, true, error),
                "Map features should be reactivatable");
        MapFeature invalidFeature;
        invalidFeature.floorId = mapFloors.front().id;
        invalidFeature.buildingId = buildings.front().id;
        invalidFeature.key = "out-of-bounds";
        invalidFeature.type = "OTHER";
        invalidFeature.label = "Out of bounds";
        invalidFeature.geometryType = "LINE";
        invalidFeature.points = {{200, 10}, {210, 10}};
        MapFeature savedFeature;
        require(!maps.saveFeature(invalidFeature, savedFeature, error) &&
                error == "invalid_feature_geometry",
                "Map geometry outside floor bounds must be rejected");
        MapFloorInfo falselyVerified = mapFloors.front();
        falselyVerified.verificationStatus = "VERIFIED";
        falselyVerified.sourceReference = "test source";
        require(!maps.saveFloorMetadata(falselyVerified, error) &&
                error == "demo_data_not_verified",
                "A floor with active synthetic features must not become verified");
        MapRoomDetails mapRoom;
        require(maps.getRoomDetails(room.id, mapRoom, error) &&
                mapRoom.sections.size() == 1 && mapRoom.sections.front().enrolledCount == 2 &&
                mapRoom.sections.front().remainingSeats == 0,
                "Map room details should report per-section seat summaries without student identities");
        NavigationPath demoRoute;
        require(navigation.findRoute(0, 4, demoRoute, error) && demoRoute.totalCost == 30 &&
                demoRoute.containsDemoData && !demoRoute.accessibilityVerified,
                "Synthetic navigation seed and route metadata failed");
        GraphVertex location;
        location.id = -1;
        location.locationKey = "test-terminal";
        location.locationType = "ENTRANCE";
        location.label = "Test terminal";
        GraphVertex savedLocation;
        require(navigation.saveLocation(location, error, savedLocation) && savedLocation.id > 4,
                "Navigation location persistence failed");
        require(navigation.saveEdge(4, savedLocation.id, 5, "CORRIDOR", "STEP_FREE", error),
                "Navigation edge creation failed");
        NavigationPath extendedRoute;
        require(navigation.findRoute(0, savedLocation.id, extendedRoute, error) &&
                extendedRoute.totalCost == 35 && extendedRoute.accessibilityVerified == false,
                "Route should include demo-path accessibility metadata");
        require(navigation.removeEdge(4, savedLocation.id, error), "Navigation edge deletion failed");
        require(!navigation.findRoute(0, savedLocation.id, extendedRoute, error) &&
                error == "route_unavailable", "Disconnected graph should return no route");
    }
}

void runTimetableImportWorkflow(const std::string& path) {
    sqlite3* database = nullptr;
    require(sqlite3_open(path.c_str(), &database) == SQLITE_OK, "Could not open timetable test database");
    std::unique_ptr<sqlite3, int(*)(sqlite3*)> databaseGuard(database, sqlite3_close);
    StudentRepository students(database);
    CampusRepository campus(database);
    require(students.initialize() && campus.initialize(), "Timetable database initialization failed");
    EnrollmentRepository enrollments(path);
    require(enrollments.initialize(), "Timetable enrollment schema setup failed");

    std::string error;
    require(campus.importSchedule(IU_SCHEDULE_SEED_PATH, error),
            "Timetable seed import failed: " + error);
    std::vector<AcademicSection> sections;
    require(campus.listSections("", "Fall 2026-2027", "", 0, false, sections) &&
            sections.size() == 84, "Timetable import should create 84 group-specific course sections");
    std::size_t meetingCount = 0;
    for (const AcademicSection& section : sections) meetingCount += section.meetings.size();
    require(meetingCount == 154, "Timetable import should preserve all 154 meetings");
    int spacedRoomId = 0;
    int compactRoomId = 0;
    for (const AcademicSection& section : sections) {
        for (const SectionMeeting& meeting : section.meetings) {
            if (meeting.venueLabel == "B 2.1") spacedRoomId = meeting.classroomId;
            if (meeting.venueLabel == "B2.1") compactRoomId = meeting.classroomId;
        }
    }
    require(spacedRoomId > 0 && spacedRoomId == compactRoomId,
            "Venue labels that differ only by case/spacing should link to the same classroom");
    std::vector<Classroom> rooms;
    require(campus.listClassrooms("", 0, 0, "", false, rooms) && rooms.size() == 18,
            "Timetable import should create one record per physical venue after case/spacing alias normalization");
    require(std::all_of(rooms.begin(), rooms.end(), [](const Classroom& room) {
        return !room.capacityKnown;
    }), "Imported venue capacities must remain explicitly unverified");

    require(campus.importSchedule(IU_SCHEDULE_SEED_PATH, error), "Repeated timetable import should be idempotent");
    std::vector<AcademicSection> repeatedSections;
    require(campus.listSections("", "Fall 2026-2027", "", 0, false, repeatedSections) &&
            repeatedSections.size() == 84, "Repeated import must not duplicate course sections");

    Student student = makeStudent("TIMETABLE-1");
    require(students.create(student, error), "Timetable student setup failed");
    Student timetableStudent;
    require(students.findByStudentId("TIMETABLE-1", timetableStudent), "Could not reload timetable test student");
    std::vector<AvailableSection> available;
    require(enrollments.listAvailable("", "Fall 2026-2027", "G10", available) &&
            available.size() == 6, "Group filter should return six courses for Group 10");
    require(std::none_of(available.begin(), available.end(), [](const AvailableSection& section) {
        return section.capacityKnown || section.openForEnrollment;
    }), "Sections must stay closed while physical room capacities are unknown");

    AvailableSection blockedSection = *std::find_if(available.begin(), available.end(),
        [](const AvailableSection& section) { return section.courseCode == "BS212"; });
    require(!blockedSection.openForEnrollment,
            "Imported sections must remain closed until the administrator opens enrollment");
    require(campus.setSectionEnrollmentOpen(blockedSection.sectionId, true, error),
            "Admin should be able to open a section even before room capacity is verified");
    Enrollment blockedEnrollment;
    require(!enrollments.enroll(timetableStudent.id, blockedSection.sectionId, 0, blockedEnrollment, error) &&
            error == "capacity_unknown", "Enrollment must be rejected until room capacities are verified");

    for (Classroom& room : rooms) {
        room.capacity = 40;
        require(campus.saveClassroom(room, error), "Admin-entered classroom capacity should be saved");
    }
    AcademicSection mathematics;
    require(campus.findSection(blockedSection.sectionId, mathematics), "Could not load Group 10 mathematics section");
    mathematics.enrollmentCapacity = 30;
    mathematics.isOpenForEnrollment = true;
    require(campus.saveSection(mathematics, error), "Section enrollment capacity override should save");
    AcademicSection onlineCourse;
    auto online = std::find_if(sections.begin(), sections.end(), [](const AcademicSection& section) {
        return section.courseCode == "UR211" && section.sectionCode == "G10";
    });
    require(online != sections.end() && campus.findSection(online->id, onlineCourse),
            "Could not load online Group 10 section");
    onlineCourse.enrollmentCapacity = 25;
    onlineCourse.isOpenForEnrollment = true;
    require(campus.saveSection(onlineCourse, error), "Online section capacity override should save");

    require(enrollments.listAvailable("", "Fall 2026-2027", "G10", available) &&
            std::all_of(available.begin(), available.end(), [](const AvailableSection& section) {
                return section.capacityKnown && section.openForEnrollment;
            }), "Verified room and section capacities should open all Group 10 courses");
    auto mathematicsOffering = std::find_if(available.begin(), available.end(),
        [](const AvailableSection& section) { return section.courseCode == "BS212"; });
    auto onlineOffering = std::find_if(available.begin(), available.end(),
        [](const AvailableSection& section) { return section.courseCode == "UR211"; });
    require(mathematicsOffering != available.end() && mathematicsOffering->capacity == 30 &&
            onlineOffering != available.end() && onlineOffering->capacity == 25,
            "Room limits and online section overrides should produce the effective capacity");

    auto secondaryRoom = std::find_if(rooms.begin(), rooms.end(), [](const Classroom& room) {
        return room.roomCode == "B.Class room 2";
    });
    require(secondaryRoom != rooms.end(), "Could not locate a secondary timetable classroom");
    NavigationRepository navigation(database);
    require(navigation.initialize(), "Timetable navigation schema setup failed");
    MapRepository maps(database);
    require(maps.initialize(), "Timetable map details setup failed");
    std::vector<MapBuilding> mapBuildings;
    require(maps.listBuildings(false, mapBuildings), "Map building listing failed");
    auto csBuilding = std::find_if(mapBuildings.begin(), mapBuildings.end(), [](const MapBuilding& building) {
        return building.building.code == "B";
    });
    require(csBuilding != mapBuildings.end(), "The timetable should include Building B");
    std::vector<MapFloorInfo> csFloors;
    require(maps.listFloors(csBuilding->building.id, csFloors, error), "CS building floor listing failed");
    auto groundFloor = std::find_if(csFloors.begin(), csFloors.end(), [](const MapFloorInfo& floor) {
        return floor.label == "0";
    });
    require(groundFloor != csFloors.end() && groundFloor->hasMap &&
            groundFloor->verificationStatus == "UNVERIFIED" && !groundFloor->scaleKnown,
            "The supplied CS ground-floor plan should be present without claiming a verified scale");
    MapFloorData csGroundMap;
    require(maps.getFloorFeatures(groundFloor->id, csGroundMap, error) &&
            !csGroundMap.features.empty() && csGroundMap.navigationLocations.empty(),
            "The supplied plan should render its traced features without inventing route nodes");
    auto roomB01 = std::find_if(rooms.begin(), rooms.end(), [](const Classroom& room) {
        return room.roomCode == "B 0.1";
    });
    auto roomB02 = std::find_if(rooms.begin(), rooms.end(), [](const Classroom& room) {
        return room.roomCode == "B 0.2";
    });
    auto featureB01 = std::find_if(csGroundMap.features.begin(), csGroundMap.features.end(), [](const MapFeature& feature) {
        return feature.key == "cs0-room-b01";
    });
    auto featureB02 = std::find_if(csGroundMap.features.begin(), csGroundMap.features.end(), [](const MapFeature& feature) {
        return feature.key == "cs0-room-b02";
    });
    require(roomB01 != rooms.end() && roomB02 != rooms.end() &&
            featureB01 != csGroundMap.features.end() && featureB01->classroomId == roomB01->id &&
            featureB02 != csGroundMap.features.end() && featureB02->classroomId == roomB02->id,
            "The B 0.1 and B 0.2 plan areas should open their corresponding classroom records");
    MapRoomDetails roomDetails;
    require(maps.getRoomDetails(secondaryRoom->id, roomDetails, error) &&
            std::any_of(roomDetails.sections.begin(), roomDetails.sections.end(), [](const MapSeatSection& section) {
                return section.courseCode == "BS212" && section.sectionCode == "G10" && section.capacity == 30;
            }), "Room details should include sections meeting in secondary rooms and use effective capacity");

    Enrollment enrollment;
    require(enrollments.enroll(timetableStudent.id, mathematicsOffering->sectionId, 0, enrollment, error),
            "Student should be able to enroll after capacities are entered");
    std::vector<Enrollment> studentSchedule;
    require(enrollments.listForStudent(timetableStudent.id, true, studentSchedule) &&
            studentSchedule.size() == 1 && studentSchedule.front().meetings.size() == 2,
            "Student schedule should return every weekly meeting for an enrolled course");

    Student labStudents[4];
    for (int index = 0; index < 4; ++index) {
        labStudents[index] = makeStudent("LAB-STUDENT-" + std::to_string(index + 1));
        require(students.create(labStudents[index], error), "Lab enrollment student setup failed");
        Student reloaded;
        require(students.findByStudentId(labStudents[index].studentId, reloaded),
                "Could not reload a lab enrollment student");
        labStudents[index] = reloaded;
    }
    AcademicSection labSection;
    labSection.courseCode = "LAB999";
    labSection.courseName = "Activity-specific capacity test";
    labSection.sectionCode = "LAB-A";
    labSection.term = "Fall 2099";
    labSection.enrollmentCapacity = 10;
    labSection.isOpenForEnrollment = true;
    SectionMeeting lecture;
    lecture.activityType = "Lecture";
    lecture.day = "Monday";
    lecture.startTime = "09:00";
    lecture.endTime = "10:00";
    lecture.classroomId = rooms[0].id;
    lecture.venueLabel = rooms[0].roomCode;
    lecture.enrollmentCapacity = 10;
    SectionMeeting labOne;
    labOne.activityType = "Lab";
    labOne.day = "Tuesday";
    labOne.startTime = "10:00";
    labOne.endTime = "11:00";
    labOne.classroomId = rooms[1].id;
    labOne.venueLabel = rooms[1].roomCode;
    labOne.enrollmentCapacity = 1;
    SectionMeeting labTwo = labOne;
    labTwo.id = 0;
    labTwo.startTime = "11:00";
    labTwo.endTime = "12:00";
    labTwo.classroomId = rooms[2].id;
    labTwo.venueLabel = rooms[2].roomCode;
    labTwo.enrollmentCapacity = 2;
    labSection.meetings = {lecture, labOne, labTwo};
    require(campus.saveSection(labSection, error), "Activity-specific section capacity setup failed");
    require(labSection.meetings.size() == 3 && labSection.meetings[1].id > 0 &&
            labSection.meetings[2].id > 0, "Lab option meeting IDs should be assigned");
    std::vector<AvailableSection> labOfferings;
    require(enrollments.listAvailable("LAB999", "Fall 2099", "", labOfferings) &&
            labOfferings.size() == 1 && labOfferings.front().capacity == 3,
            "Section seat count should sum separate lab capacities and respect lecture and section caps");
    Enrollment firstLabEnrollment, secondLabEnrollment, thirdLabEnrollment, fullLabEnrollment;
    require(enrollments.enroll(labStudents[0].id, labSection.id, labSection.meetings[1].id, firstLabEnrollment, error),
            "Student should enroll in the first lab option");
    require(!enrollments.enroll(labStudents[1].id, labSection.id, labSection.meetings[1].id, fullLabEnrollment, error) &&
            error == "section_full", "One lab's seat limit must not be shared with another lab");
    require(enrollments.enroll(labStudents[1].id, labSection.id, labSection.meetings[2].id, secondLabEnrollment, error) &&
            enrollments.enroll(labStudents[2].id, labSection.id, labSection.meetings[2].id, thirdLabEnrollment, error),
            "Students should use the independent second lab capacity");
    require(!enrollments.enroll(labStudents[3].id, labSection.id, labSection.meetings[2].id, fullLabEnrollment, error) &&
            error == "section_full", "Enrollment should stop when the selected lab group reaches capacity");
    AcademicSection reducedLabCapacity = labSection;
    reducedLabCapacity.meetings[2].enrollmentCapacity = 1;
    require(!campus.saveSection(reducedLabCapacity, error) &&
            error == "active_enrollments_exceed_capacity",
            "Admin cannot reduce a lab limit below its current enrollment");
    require(campus.setSectionEnrollmentOpen(labSection.id, false, error) &&
            !enrollments.enroll(labStudents[3].id, labSection.id, labSection.meetings[2].id, fullLabEnrollment, error) &&
            error == "section_inactive",
            "Admin can close enrollment independently of section activity");
    require(campus.setSectionEnrollmentOpen(labSection.id, true, error),
            "Admin should be able to reopen lab enrollment");
    studentSchedule.clear();
    require(enrollments.listForStudent(labStudents[0].id, true, studentSchedule) &&
            studentSchedule.size() == 1 && studentSchedule.front().labMeetingId == labSection.meetings[1].id &&
            std::count_if(studentSchedule.front().meetings.begin(), studentSchedule.front().meetings.end(),
                [](const SectionMeeting& meeting) { return meeting.isLabOption; }) == 1,
            "Student schedule should include only the selected lab option");

    auto otherGroup = std::find_if(sections.begin(), sections.end(), [](const AcademicSection& section) {
        return section.courseCode == "BS212" && section.sectionCode == "G9";
    });
    require(otherGroup != sections.end() && campus.setSectionEnrollmentOpen(otherGroup->id, true, error),
            "Admin should be able to open the parallel course section for duplicate-course protection testing");
    require(otherGroup != sections.end() &&
            !enrollments.enroll(timetableStudent.id, otherGroup->id, 0, blockedEnrollment, error) &&
            error == "course_already_enrolled",
            "A student must not enroll in the same course in another group for the same term");
}
}

int main() {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("iu-smart-campus-workflow-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
    std::filesystem::path timetablePath = path.string() + "-timetable";
    try {
        std::ifstream schemaFile(CAMPUS_SCHEMA_PATH);
        require(schemaFile.good(), "Could not read database/schema.sql");
        std::string schema((std::istreambuf_iterator<char>(schemaFile)), std::istreambuf_iterator<char>());
        sqlite3* schemaDatabase = nullptr;
        require(sqlite3_open(":memory:", &schemaDatabase) == SQLITE_OK, "Could not open schema validation database");
        char* schemaError = nullptr;
        int schemaResult = sqlite3_exec(schemaDatabase, schema.c_str(), nullptr, nullptr, &schemaError);
        if (schemaError) sqlite3_free(schemaError);
        sqlite3_close(schemaDatabase);
        require(schemaResult == SQLITE_OK, "database/schema.sql failed to execute");
        runWorkflow(path.string());
        runTimetableImportWorkflow(timetablePath.string());
        std::cout << "Enrollment, timetable import, reservation, and navigation workflow tests passed.\n";
        std::filesystem::remove(path);
        std::filesystem::remove(path.string() + "-wal");
        std::filesystem::remove(path.string() + "-shm");
        std::filesystem::remove(timetablePath);
        std::filesystem::remove(timetablePath.string() + "-wal");
        std::filesystem::remove(timetablePath.string() + "-shm");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Workflow tests failed: " << error.what() << '\n';
        std::filesystem::remove(path);
        std::filesystem::remove(path.string() + "-wal");
        std::filesystem::remove(path.string() + "-shm");
        std::filesystem::remove(timetablePath);
        std::filesystem::remove(timetablePath.string() + "-wal");
        std::filesystem::remove(timetablePath.string() + "-shm");
        return 1;
    }
}
