cmake_minimum_required(VERSION 3.20)
project(IUSmartCampusAPI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
    GIT_SHALLOW TRUE
)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib json)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/seed")
configure_file(seed/level_two_timetable.json "${CMAKE_BINARY_DIR}/seed/level_two_timetable.json" COPYONLY)

add_executable(iu_smart_campus_api
    src/main.cpp
    src/Graph.cpp
    src/NavigationManager.cpp
    src/NavigationRepository.cpp
    src/NavigationRoutes.cpp
    src/MapRepository.cpp
    src/MapRoutes.cpp
    src/CampusRepository.cpp
    src/EnrollmentRepository.cpp
    src/EnrollmentRoutes.cpp
    src/ReservationRepository.cpp
    src/ReservationRoutes.cpp
    src/PasswordHash.cpp
    src/SessionStore.cpp
    src/StudentRepository.cpp
)
target_include_directories(iu_smart_campus_api PRIVATE include)
target_link_libraries(iu_smart_campus_api PRIVATE httplib::httplib nlohmann_json::nlohmann_json SQLite3::SQLite3 OpenSSL::Crypto)
target_compile_definitions(iu_smart_campus_api PRIVATE IU_SCHEDULE_SEED_PATH="${CMAKE_CURRENT_SOURCE_DIR}/seed/level_two_timetable.json")
if(WIN32)
    target_link_libraries(iu_smart_campus_api PRIVATE ws2_32)
endif()

enable_testing()
add_executable(hash_table_tests tests/HashTableTests.cpp)
target_include_directories(hash_table_tests PRIVATE include)
add_test(NAME hash_table_tests COMMAND hash_table_tests)

add_executable(graph_heap_tests tests/GraphHeapTests.cpp src/Graph.cpp)
target_include_directories(graph_heap_tests PRIVATE include)
add_test(NAME graph_heap_tests COMMAND graph_heap_tests)

add_executable(module_workflow_tests
    tests/ModuleWorkflowTests.cpp
    src/Graph.cpp
    src/CampusRepository.cpp
    src/EnrollmentRepository.cpp
    src/MapRepository.cpp
    src/NavigationRepository.cpp
    src/ReservationRepository.cpp
    src/StudentRepository.cpp
)
target_include_directories(module_workflow_tests PRIVATE include)
target_link_libraries(module_workflow_tests PRIVATE SQLite3::SQLite3 nlohmann_json::nlohmann_json)
target_compile_definitions(module_workflow_tests PRIVATE
    CAMPUS_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/schema.sql"
    IU_SCHEDULE_SEED_PATH="${CMAKE_CURRENT_SOURCE_DIR}/seed/level_two_timetable.json")
add_test(NAME module_workflow_tests COMMAND module_workflow_tests)
