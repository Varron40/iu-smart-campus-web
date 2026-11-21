# IU Smart Campus — Web Starter

A local-first web application starter for the Smart 3D University Building Navigation & Classroom Reservation System.

## Stack
- Frontend: React + TypeScript + Vite
- 2D/3D: accessible SVG floor maps + Three.js views
- Backend: C++17 REST API using cpp-httplib and nlohmann/json
- Academic core: custom Graph, Dijkstra, Max Binary Heap, Hash Table
- Deployment: frontend and backend can be deployed separately

> The included synthetic campus graph and `DEMO` room are placeholders. Imported timetable venues are unverified labels, not verified floor-plan information.

## Requirements
- Node.js 20+ and npm
- CMake 3.20+
- C++17 compiler (Visual Studio 2022 Build Tools/MSVC recommended on Windows, or GCC/Clang)
- Internet connection for first CMake configure (FetchContent downloads C++ dependencies)

## 1. Run frontend (VS Code terminal)
```bash
cd frontend
npm install
npm run dev
```
Open the URL printed by Vite (normally http://localhost:5173).

## 2. Run C++ backend (second VS Code terminal)
Windows PowerShell:
```powershell
cd backend
cmake -S . -B build
cmake --build build --config Release
.\build\Release\iu_smart_campus_api.exe
```
If using a single-configuration generator such as MinGW:
```bash
cd backend
cmake -S . -B build
cmake --build build
./build/iu_smart_campus_api
```
The API listens on `http://localhost:8080`.

## Base API endpoints
- `GET /api/health` — service status; public
- `GET /api/navigation/locations` and `POST /api/navigation/route` — authenticated campus graph and routes
- `GET /api/demo/route?from=0&to=4` — compatibility alias for the synthetic shortest route; requires a bearer token
- `GET /api/map/buildings`, `/api/map/buildings/{id}/floors`, and `/api/map/floors/{id}/features` — authenticated map data

## Module 6 — Interactive campus map

The authenticated workspace now includes an interactive campus map with responsive 2D pan/zoom, a Three.js orbit view, floor selection, room details and per-section seat summaries. Route calculations still come from the C++ Dijkstra API; a route is drawn only for graph edges with matching stored floor geometry. Missing geometry never hides the backend's text route.

The current map is explicitly synthetic: the `DEMO` / “Demo Floor” contains a 100 × 70 local-unit sample extent and illustrative graph segments, but no invented classroom positions or official IU campus geometry. Its scale is unknown. The project contains no verified source plans; replace this seed only after acquiring approved floor plans and room-position records. The UI shows unplaced active rooms separately and falls back to 2D if WebGL is unavailable.

Administrators can edit floor metadata and map feature geometry in the map screen. Coordinates use `LOCAL_FLOOR_X_RIGHT_Y_DOWN`; stored points are JSON arrays such as `[{"x":10,"y":12},{"x":24,"y":12}]`. A map is not marked verified while active synthetic features or navigation locations remain. API routes, validation rules, data provenance guidance, and the admin editor are documented in [the Module 6 handoff](docs/MODULE_6_INTERACTIVE_MAP.md).

The backend initializes the Module 6 tables automatically alongside the other modules. `database/schema.sql` includes the matching schema outline. Run the existing backend build and CTest commands above to validate the map repository and workflows.

## Campus reference and Level 2 timetable

The authenticated dashboard links to the Innovation University campus pin at `30.320492, 31.7391875`. This is a campus-level reference only; it is not assigned to the Computers & IT building. The **Level 2 timetable** is imported from `backend/seed/level_two_timetable.json`, generated from the supplied all-groups workbook. It contains 154 meetings, 14 groups (1–10, 12–14, and 16), and six courses per group. The workbook's Group 10 schedule matches the earlier PDF marked Fall Semester 2026–2027; that term is assigned to these rows.

Venue labels on meeting rows are preserved verbatim. Physical places are shared classroom records; labels differing only by case/spacing merge at import and existing duplicates are consolidated during startup. If that consolidation would overlap approved reservations, startup reports the conflict and rolls back the alias migration; resolve the bookings before restarting. Building/floor categories are inferred from room-label prefixes; no building coordinates or room capacities have been verified. No campus pin or map coordinates are assigned to these buildings. The user-provided CS Building ground-floor diagram is shown on a white background in 2D; traced geometry links B 0.1 and B 0.2 in both views. The plan is `UNVERIFIED`, its scale is unknown, and 3D wall height is illustrative. The one-time import creates multi-meeting course sections without inventing capacities. Imported sections start with enrollment closed; admins must explicitly open them and set the verified room/meeting limits.

## VS Code
Open this folder in VS Code. Recommended extensions:
- C/C++ (Microsoft)
- CMake Tools
- ESLint
- Prettier

## Recommended next milestones
1. Replace synthetic graph/building data with verified university records, including accessibility metadata.
2. Formalize booking timezone, booking window, priority, and class-schedule conflict policies.
3. Add versioned database migrations and organization/member administration history.
4. Replace synthetic map/graph geometry with approved, verified campus plans and room positions.
5. Harden authentication, CORS, deployment, logging, and browser/API integration tests for production.

# Module 1 — Student Accounts and Authentication

## MSYS2/UCRT64 setup
Install the required native libraries:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sqlite3 mingw-w64-ucrt-x86_64-openssl
```

Open an MSYS2 UCRT64 terminal or enable those tools in VS Code. Build and run:

```bash
cd /e/Programming/IU-Smart-Campus-Web/backend
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build -C Release --output-on-failure
./build/iu_smart_campus_api.exe
```

Frontend:

```bash
cd /e/Programming/IU-Smart-Campus-Web/frontend
npm install
npm run dev
```

The API listens on `http://localhost:8080`; Vite uses `http://localhost:5173` by default.

## SQLite database
The backend initializes `backend/data/iu-smart-campus.db` automatically. The schema is embedded in `StudentRepository::initialize` and mirrored in `database/schema.sql`. SQLite files are ignored by Git. Passwords are stored only as PBKDF2-HMAC-SHA256 strings (`pbkdf2-sha256$iterations$salt$hash`) using OpenSSL and a fresh random 16-byte salt.

## Authentication
Login returns an opaque random bearer token. Tokens are held in memory and are invalidated on logout or backend restart. This is suitable for local development, not distributed production. For production, use HTTPS, persistent/revocable sessions or signed short-lived tokens, secure cookies or a hardened token policy, CORS restrictions, rate limiting, and CSRF protections. The frontend stores the token in localStorage only for this local module.

## Student routes

| Method | Route | Auth | Request | Success |
|---|---|---:|---|---|
| POST | `/api/auth/register` | No | `studentId`, `fullName`, `universityEmail`, `password`, `faculty`, `department`, `academicYear` | 201 student |
| POST | `/api/auth/login` | No | `universityEmail`, `password` | 200 token and student |
| POST | `/api/auth/logout` | Bearer | Empty `{}` | 200 |
| GET | `/api/students/me` | Bearer | None | 200 student |
| PUT | `/api/students/me` | Bearer | Permitted fields: `fullName`, `faculty`, `department`, `academicYear` | 200 student |
| GET | `/api/students/me/schedule` | Bearer | None | 200 schedule from active enrollments |

Errors use JSON: `{"error":"message"}`. Registration and login validate required fields; login failures use a generic 401. Duplicate IDs/emails return 409 where identifiable. Unauthenticated protected endpoints return 401.

Example:

```bash
curl -X POST http://localhost:8080/api/auth/register -H "Content-Type: application/json" -d "{\"studentId\":\"DEMO-1001\",\"fullName\":\"Demo Student\",\"universityEmail\":\"demo.student@example.test\",\"password\":\"DemoPass123\",\"faculty\":\"Demo Faculty\",\"department\":\"Demo Department\",\"academicYear\":\"Year 1\"}"
curl -X POST http://localhost:8080/api/auth/login -H "Content-Type: application/json" -d "{\"universityEmail\":\"demo.student@example.test\",\"password\":\"DemoPass123\"}"
curl http://localhost:8080/api/students/me -H "Authorization: Bearer <token>"
```

## Custom Hash Table
`HashTable<K,V>` in `backend/include/data_structures/HashTable.h` is used for student lookup by unique `studentId`. It uses `std::hash<K>` modulo the bucket count, separate chaining with `std::list`, duplicate-key replacement, and rehashing at load factor 0.75.

- Insert: average O(1), worst O(n)
- Lookup: average O(1), worst O(n)
- Delete: average O(1), worst O(n)
- Rehash: O(n)

Separate chaining keeps colliding keys in the same bucket's linked list. SQLite remains the persistent source of truth. Startup loads student records into the table; registration and profile updates keep matching entries consistent. A restart recreates the cache from SQLite. The Hash Table test is `backend/tests/HashTableTests.cpp`.

## Deployment note
For production, deploy the frontend as a static site and the C++ API as a separate service/container. Configure `VITE_API_BASE_URL` to the public API URL. Do not expose database credentials or secrets in frontend environment variables.

# Module 2 — Classroom and Section Management

## Roles and initial administrator
Student registration always creates a `student` role; the client cannot request a role. Students can view active classroom/section data. Only `admin` accounts can add, edit, assign, archive, or reactivate those records. The API checks the role on every administrative request.

The initial administrator is provisioned at backend startup using process environment variables. Set all four before the first run; the password is hashed and never logged or stored in source:

```powershell
$env:IU_ADMIN_STUDENT_ID = 'ADMIN-001'
$env:IU_ADMIN_NAME = 'Campus Administrator'
$env:IU_ADMIN_EMAIL = 'admin@example.test'
$securePassword = Read-Host 'Initial administrator password' -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
try {
  $env:IU_ADMIN_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
  .\build\iu_smart_campus_api.exe
} finally {
  Remove-Item Env:\IU_ADMIN_PASSWORD -ErrorAction SilentlyContinue
  [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
  $securePassword.Dispose()
}
```

Use your approved administrator identifier and email instead of the example values. The identifiers must not already belong to another account, the email must pass validation, and the password must be at least eight characters. Provisioning is one-time: the account persists in SQLite after the environment variables are removed. A duplicate/invalid setup is reported by the backend and does not promote an existing student. Keep the environment password private; do not put it in `.env`, source control, or frontend configuration. The admin role is never accepted from the registration API.

## Campus records and demo placeholders
SQLite creates the `buildings`, `floors`, `classrooms`, `academic_sections`, and `academic_section_meetings` tables without dropping Module 1 data. It keeps the `DEMO` map placeholder and imports the provided Level 2 workbook once, idempotently. Imported A/B/C building and floor labels are tentative classifications from venue strings only; room capacities remain explicitly unknown. Administrators enter verified room capacities in **Classrooms**. Occupancy remains unavailable until enrollment data exists.

Classroom codes are unique within a building after case/whitespace normalization. The database enforces that a classroom's floor belongs to its building, and the API also validates this before writing. Classroom/section archival is a status change, not a deletion. Sections can have multiple weekly meeting rows, each with its own activity, day, time, venue label, instructor, optional enrollment cap, and classroom link.

## Module 2 API
All read endpoints below require authentication. `Student` can read active records. `Admin` can also request archived records with `includeInactive=true`. Create/update/archive/assignment routes require `Admin`. Errors use `{"error":"message"}`.

| Method | Route | Permission | Request / query |
|---|---|---|---|
| GET | `/api/buildings` | Student/Admin | — |
| GET | `/api/buildings/{id}/floors` | Student/Admin | — |
| GET | `/api/rooms` | Student/Admin | `q`, `buildingId`, `floorId`, `roomType`; Admin only: `includeInactive=true` |
| GET | `/api/rooms/{id}` | Student/Admin | — |
| POST | `/api/rooms` | Admin | `buildingId`, `floorId`, `roomCode`, `roomName`, `roomType`, positive `capacity`, optional `notes` |
| PUT | `/api/rooms/{id}` | Admin | Same classroom fields |
| PATCH | `/api/rooms/{id}/status` | Admin | `{"isActive":false}` to archive or `true` to reactivate |
| GET | `/api/sections` | Student/Admin | `q`, `term`, `day`, `buildingId`; Admin only: `includeInactive=true` |
| GET | `/api/sections/{id}` | Student/Admin | — |
| POST | `/api/sections` | Admin | `courseCode`, `courseName`, `sectionCode`, `term`; optional legacy schedule fields, `enrollmentCapacity`, `isOpenForEnrollment`, and `meetings[]` with meeting-specific `enrollmentCapacity` |
| PUT | `/api/sections/{id}` | Admin | Same section fields |
| PATCH | `/api/sections/{id}/classroom` | Admin | Legacy single-schedule sections only: `{"classroomId":123}` or `{"classroomId":null}` |
| PATCH | `/api/sections/{id}/status` | Admin | `{"isActive":false}` or `true` |
| PATCH | `/api/sections/{id}/enrollment-status` | Admin | `{"isOpenForEnrollment":true}` or `false` |

Successful create routes return 201; reads and updates return 200. Validation errors return 400, duplicate room/section keys return 409, missing records return 404, unauthenticated requests return 401, and student attempts at admin actions return 403. Meeting times must use `HH:MM`; each meeting requires a valid day and an end time later than its start time. A section capacity override is optional for room-based sections and required for online-only sections.

The custom Hash Table is reused as an in-memory classroom index keyed by `buildingId:roomCode`; SQLite remains authoritative and enforces uniqueness. Startup rebuilds the index from SQLite, and room create/edit/archive operations update it. In-memory key lookup is average O(1), worst O(n); SQL search and wildcard text filtering are not guaranteed O(1).

The provided white Innovation University logo is stored at `frontend/public/innovation-university-logo.png` and is used on the sign-in and campus screens.

# Module 3 — Student Enrollment

Students can browse active academic sections, choose a group per course, enroll, view enrollment history, withdraw from an active enrollment, and see their schedule alongside the shared Level 2 timetable in **My Schedule**. Enrollment identity is always derived from the authenticated bearer token. A section is open only when an admin has enabled enrollment and required room capacities are verified. Meeting limits apply independently: students count against lecture meetings and only their selected lab/tutorial-lab meeting; the section-wide override caps the total. Online-only sections require a section capacity. A student cannot enroll in the same course twice in the same term, even in a different group.

The API uses `BEGIN IMMEDIATE` to serialize seat allocation, rechecks effective and selected-lab capacity within the transaction, records lab assignment and enrollment events, and enforces one active enrollment per student/section with a partial unique index. Database triggers prevent duplicate active enrollment in the same course/term and prevent admins from reducing classroom, meeting, or section capacities below current enrollment.

| Method | Route | Permission | Purpose |
|---|---|---|---|
| GET | `/api/sections/available?q=&term=&group=` | Student | Search active sections, optionally by group |
| POST | `/api/sections/{id}/enroll` | Student | Enroll with optional `{"labMeetingId":123}` for a selected lab |
| GET | `/api/students/me/enrollments` | Student | Read active and withdrawn enrollment records |
| POST or DELETE | `/api/enrollments/{id}/withdraw` | Student owner | Withdraw an active enrollment |
| GET | `/api/students/me/schedule` | Authenticated | Read schedule rows for active enrollments |

Duplicate course enrollment and full sections return 409. Unknown capacity, inactive sections, and withdrawn enrollment transitions are rejected. Imported sections start closed until admins verify the room/section capacities.

# Module 4 — Organization Reservations

Administrators create organizations, set their demo priority level, and assign students as `MEMBER` or `REPRESENTATIVE`. Any active organization member may request a classroom for a project or meeting. The room/date schedule shows classes and approved bookings, then finds free windows between 09:00 and 17:30. Priority is taken from the organization record; reservation requests cannot supply or override priority. Admins review a max-heap queue ordered by effective priority, then earlier submission time, then lower request ID. Each full 24 hours pending raises effective priority by one, capped at level 3.

Pending requests do not reserve a classroom. Both requests and approvals are rejected when the time overlaps an active scheduled class or an approved booking; approval is transactional and a SQLite trigger provides a second booking-overlap enforcement layer. Only the original requester may cancel a pending request. All state transitions are recorded as reservation events.

| Method | Route | Permission | Purpose |
|---|---|---|---|
| GET | `/api/organizations/mine` | Student | List active memberships and representative roles |
| GET / POST | `/api/admin/organizations` | Admin | List/create organizations and demo priority policy |
| PUT | `/api/admin/organizations/{id}/members` | Admin | Assign a student as member or representative |
| POST | `/api/reservations` | Organization member | Submit a date/time/room request |
| GET | `/api/reservations/mine` | Student | Read requests submitted by or visible to the student's organizations |
| POST | `/api/reservations/{id}/cancel` | Original requester | Cancel a pending request |
| GET | `/api/admin/reservations/queue` | Admin | Read pending requests in priority order |
| POST | `/api/admin/reservations/{id}/approve` | Admin | Approve unless the slot conflicts |
| POST | `/api/admin/reservations/{id}/reject` | Admin | Reject with a required reason |
| GET | `/api/rooms/{id}/reservations?date=YYYY-MM-DD` | Authenticated | Read class/approved-reservation blocks and free-window bounds |

Times are local `HH:MM` strings with no timezone or booking-window policy assumed. Priority is a demo policy, not an official university allocation rule.

# Module 5 — Persistent Campus Graph

Navigation locations and undirected weighted edges are stored in SQLite. Dijkstra operates on active locations and edges and handles sparse database IDs, disconnected routes, and finite non-negative weights. Administrators can add/edit/deactivate locations, add/edit/deactivate edges, and delete edges. Student and admin users can request shortest routes. Edge accessibility metadata is `UNKNOWN`, `UNVERIFIED`, `STEP_FREE`, or `STAIRS`; routes are called verified step-free only when every segment is explicitly marked `STEP_FREE`.

On an empty navigation table, startup seeds five clearly labeled synthetic demo locations and edges to make the route finder usable. They are not verified Innovation University data. Route costs are in administrator-defined **distance units**, not meters. Accessibility remains unknown until data is verified.

| Method | Route | Permission | Purpose |
|---|---|---|---|
| GET | `/api/navigation/locations` | Authenticated | List active graph locations |
| GET | `/api/navigation/locations?includeInactive=true` | Admin | Include inactive locations |
| POST | `/api/navigation/route` | Authenticated | Find a shortest route using `{ "fromId": 0, "toId": 4 }` |
| POST / PUT | `/api/admin/navigation/locations[/{id}]` | Admin | Add or edit a location |
| PATCH | `/api/admin/navigation/locations/{id}/status` | Admin | Activate/deactivate a location |
| GET | `/api/admin/navigation/edges` | Admin | List undirected graph edges |
| POST / PATCH / DELETE | `/api/admin/navigation/edges[/{fromId}/{toId}]` | Admin | Add, edit/status-toggle, or delete an edge |

## Database initialization and continuation notes

The backend initializes the Module 1–6 SQLite tables on startup and preserves existing student/campus data. The Level 2 workbook import is one-time and tracked in `campus_schedule_imports`; the JSON seed remains the reproducible source. `database/schema.sql` mirrors the runtime schema. The navigation seed is inserted only when its table is empty. See [the Module 3–5 handoff](docs/MODULES_3_4_5_HANDOFF.md) for implementation details, test instructions, example workflows, current limitations, and recommended next work.

Run the C++ backend tests after building:

```powershell
ctest --test-dir backend/build -C Release --output-on-failure
```

The suite covers the custom hash table, graph/Dijkstra/heap behavior, concurrent section capacity, the full 154-meeting timetable import, capacity-gated multi-meeting enrollment, reservation conflict/transition rules, and persistent navigation graph operations.
