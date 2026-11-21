#pragma once
#include "services/SessionStore.h"
#include <httplib.h>

class StudentRepository;
class EnrollmentRepository;
class ReservationRepository;
class NavigationRepository;
class MapRepository;

void registerEnrollmentRoutes(httplib::Server& server, EnrollmentRepository& enrollments,
                              StudentRepository& students, SessionStore& sessions);
void registerReservationRoutes(httplib::Server& server, ReservationRepository& reservations,
                               StudentRepository& students, SessionStore& sessions);
void registerNavigationRoutes(httplib::Server& server, NavigationRepository& navigation,
                              StudentRepository& students, SessionStore& sessions);
void registerMapRoutes(httplib::Server& server, MapRepository& maps,
                       StudentRepository& students, SessionStore& sessions);
