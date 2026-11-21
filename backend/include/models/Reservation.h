#pragma once
#include <string>
#include <vector>

struct Organization {
    int id = 0;
    std::string name;
    std::string type;
    int priorityLevel = 1;
    bool isActive = true;
    bool isRepresentative = false;
};

struct ReservationEvent {
    int id = 0;
    std::string oldStatus;
    std::string newStatus;
    int actorId = 0;
    std::string reason;
    std::string createdAt;
};

struct ReservationRequest {
    int id = 0;
    int organizationId = 0;
    std::string organizationName;
    int requesterId = 0;
    std::string requesterName;
    int classroomId = 0;
    std::string roomCode;
    std::string reservationDate;
    std::string startTime;
    std::string endTime;
    std::string purpose;
    int priorityLevel = 1;
    int effectivePriority = 1;
    std::string prioritySource;
    std::string submittedAt;
    std::string status;
    int decidedBy = 0;
    std::string decidedAt;
    std::string rejectionReason;
    std::vector<ReservationEvent> history;
};

struct RoomScheduleBlock {
    std::string kind;
    std::string title;
    std::string subtitle;
    std::string startTime;
    std::string endTime;
};

struct ReservationQueueItem {
    int requestId = 0;
    int basePriority = 1;
    int effectivePriority = 1;
    std::string submittedAt;

    bool operator>(const ReservationQueueItem& other) const {
        if (effectivePriority != other.effectivePriority)
            return effectivePriority > other.effectivePriority;
        if (submittedAt != other.submittedAt)
            return submittedAt < other.submittedAt;
        return requestId < other.requestId;
    }

    bool operator==(const ReservationQueueItem& other) const {
        return requestId == other.requestId;
    }
};
