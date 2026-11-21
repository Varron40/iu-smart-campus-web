#pragma once
#include "data_structures/HashTable.h"
#include "data_structures/MaxHeap.h"
#include "models/Reservation.h"
#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>

class ReservationRepository {
public:
    explicit ReservationRepository(const std::string& databasePath);
    ~ReservationRepository();
    ReservationRepository(const ReservationRepository&) = delete;
    ReservationRepository& operator=(const ReservationRepository&) = delete;
    bool initialize();
    bool createOrganization(const Organization& organization, std::string& error, Organization& created);
    bool addOrganizationMember(int organizationId, const std::string& studentId,
                               const std::string& role, std::string& error);
    bool listOrganizations(int studentId, bool allOrganizations, std::vector<Organization>& organizations);
    bool createRequest(int requesterId, int organizationId, int classroomId,
                       const std::string& reservationDate, const std::string& startTime,
                       const std::string& endTime, const std::string& purpose,
                       ReservationRequest& request, std::string& error);
    bool listMine(int studentId, std::vector<ReservationRequest>& requests);
    bool findForMember(int studentId, int requestId, ReservationRequest& request);
    bool cancel(int studentId, int requestId, ReservationRequest& request, std::string& error);
    bool prioritizedQueue(std::vector<ReservationRequest>& requests);
    bool decide(int adminId, int requestId, bool approve, const std::string& reason,
                ReservationRequest& request, std::string& error);
    bool roomSchedule(int roomId, const std::string& date,
                      std::vector<RoomScheduleBlock>& blocks);
private:
    sqlite3* database = nullptr;
    std::mutex repositoryMutex;
    MaxHeap<ReservationQueueItem> pendingHeap;
    HashTable<int, bool> queuedRequestIds;
    bool reloadPendingQueue();
    bool findRequest(int requestId, ReservationRequest& request);
    bool addEvent(int requestId, const std::string& oldStatus,
                  const std::string& newStatus, int actorId, const std::string& reason);
};
