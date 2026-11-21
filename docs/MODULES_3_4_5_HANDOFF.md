# IU Smart Campus — Modules 3–5 and Level 2 Timetable Handoff

This note describes the implemented student enrollment, organization reservation, and campus navigation modules so development can continue without relying on chat history. Modules 1–2 (student authentication, admin roles, classroom/section management, and the login gate) remain the base architecture.

## 1. Run the application

### Backend

Requirements: CMake 3.20+, a C++17 compiler, SQLite, OpenSSL, and internet access on the first CMake configure (cpp-httplib and nlohmann/json are fetched by CMake).

From PowerShell:

```powershell
cd backend
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\iu_smart_campus_api.exe
```

For a single-configuration generator, the executable is normally `build\iu_smart_campus_api.exe`; run `cmake --build build` and `ctest --test-dir build --output-on-failure`. Stop a running backend normally before rebuilding if Windows reports that the executable is locked.

The API listens at `http://localhost:8080`. It initializes `backend/data/iu-smart-campus.db` at startup and creates schema tables without deleting existing student or campus records. Runtime schema is embedded in the repositories; [the SQL outline](../database/schema.sql) mirrors those tables.

Create the initial admin account by setting all four environment variables before the first backend run:

```powershell
$env:IU_ADMIN_STUDENT_ID = 'ADMIN-001'
$env:IU_ADMIN_NAME = 'Campus Administrator'
$env:IU_ADMIN_EMAIL = 'admin@example.test'
$securePassword = Read-Host 'Initial administrator password' -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
try {
  $env:IU_ADMIN_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
  .\build\Release\iu_smart_campus_api.exe
} finally {
  Remove-Item Env:\IU_ADMIN_PASSWORD -ErrorAction SilentlyContinue
  [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
  $securePassword.Dispose()
}
```

Replace the example ID/name/email with approved values. The email and password are validated, identifiers must be unused, and the password is hashed. There is no built-in admin password, and ordinary registration cannot assign the admin role. Provisioning does not promote an existing student.

### Frontend

```powershell
cd frontend
npm install
npm run dev
```

Open the Vite URL (normally `http://localhost:5173`). The frontend uses `VITE_API_BASE_URL` when set; otherwise it makes same-origin API requests. For local split frontend/backend development, Vite's existing proxy configuration or a configured API URL should point to port 8080.

## 2. Shared application architecture

- `backend/src/main.cpp` initializes the existing account and campus repositories, initializes Modules 3–5, and registers the route handlers.
- `backend/include/api/ApiHelpers.h` resolves the bearer session to a database student and performs role checks. `ModuleRoutes.h` declares the module route registrations.
- The database is the source of truth. SQLite constraints/triggers protect invariants across multiple connections; the in-memory Hash Table and MaxHeap are indexes/work queues, not durable storage.
- The frontend feature screens live in `frontend/src/FeaturePages.tsx`; API types and calls live in `frontend/src/services/api.ts`; `App.tsx` provides the authenticated navigation shell.
- Existing bearer sessions are held in memory and become invalid after a backend restart. This is a local-development design, not a production session solution.

## Level 2 timetable import

- `backend/seed/level_two_timetable.json` is the reproducible seed from the supplied workbook: 154 meetings, 14 groups, and six courses per group.
- Startup imports 84 course/group sections and physical venue labels once, tracked in `campus_schedule_imports`. The Group 10 rows match the earlier PDF marked Fall Semester 2026–2027; that term is assigned to all imported sections.
- A section owns multiple meeting rows, each retaining its activity, day, time, venue label, instructor, and optional meeting-specific enrollment limit.
- Each physical place is a shared classroom record, referenced by every course meeting held there. Case/whitespace aliases are normalized on import and existing duplicate room rows are merged at startup; schedule venue text is still preserved as provided.
- Building/floor categories are inferred from venue prefixes only. No campus coordinates, floor geometry, or room capacity are invented.
- Imported room capacities are marked unknown, and imported sections start with enrollment closed. An admin must explicitly open enrollment and enter verified room and meeting capacities first.

## 3. Module 3 — student enrollment

### Behavior and rules

- An authenticated **student** may list active sections, enroll, view enrollment history, and withdraw their own active enrollment. The backend takes the student identity from the bearer token; client-supplied student IDs are not used.
- An admin controls section enrollment availability separately from whether the section is archived. Enrollment also requires the section's physical meeting rooms to be active with verified capacity.
- Meeting-specific capacity limits apply independently. Students choosing among lab/tutorial-lab meetings count only against their chosen lab; lecture enrollment counts against the lecture limit. The section-wide cap further limits total enrollment, and each lab's available seats are shown during signup.
- Students choose a group per course. A database check prevents active enrollments in the same course and term across multiple groups.
- When a section has multiple lab meetings, enrollment requires choosing one. The enrollment stores that lab meeting ID; the personal schedule includes common lecture/tutorial meetings plus the student's selected lab only. The My Schedule screen also includes the shared group-filterable Level 2 timetable.
- Scheduled section classroom assignments are edited per meeting; the legacy single-classroom assignment route remains limited to sections without meeting rows.
- `BEGIN IMMEDIATE` serializes writes. The repository checks section status, classroom status, duplicate enrollment, and seat count inside the transaction before creating the enrollment and its audit event.
- A partial unique index allows enrollment history while preventing two active rows for the same student/section. Capacity triggers block lowering room/section capacity below active enrollment.
- Withdrawing changes the enrollment status and records an event; it does not delete history. A withdrawn student may enroll again if the section has capacity.

### Routes

| Method | Route | Permission |
|---|---|---|
| GET | `/api/sections/available?q={text}&term={term}&group={group}` | Student |
| POST | `/api/sections/{sectionId}/enroll` | Student |
| GET | `/api/students/me/enrollments` | Student |
| POST or DELETE | `/api/enrollments/{enrollmentId}/withdraw` or `/api/enrollments/{enrollmentId}` | Student owner |
| GET | `/api/students/me/schedule` | Authenticated |

`GET /api/students/me/schedule` now returns active enrollments instead of the earlier demo placeholder. Duplicate enrollment, full capacity, inactive sections, and invalid state transitions return conflict responses; transient SQLite busy states are reported as retryable 503 errors.

## 4. Module 4 — organization room reservations

### Organization and request behavior

- Admins create organizations and assign registered users as `MEMBER` or `REPRESENTATIVE`. A student may see requests belonging to their active organization memberships.
- Any active organization member may create a request. The requester ID comes from the authenticated session.
- The request body must not contain `priority`, `priorityLevel`, or `priorityCategory`. Priority comes from the organization's admin-set level (1–3), shown as an explicitly **demo** policy.
- Request date is validated as a real `YYYY-MM-DD` calendar date. Times must be 24-hour `HH:MM`, the end must be later than the start, and bookings must stay within the requested 09:00–17:30 window. No university timezone or official priority policy is assumed.
- Any active organization member may submit a room request for a project or meeting. The room/date availability view lists scheduled classes and approved bookings, then computes free windows between 09:00 and 17:30. Pending requests do not reserve rooms.
- Request submission and approval both reject overlap with an active course meeting or an approved reservation. Approval is performed by an admin inside an immediate write transaction and is protected by a database trigger; overlapping pending requests are allowed to enter the queue.
- Only the original requester may cancel a pending request. All create/approve/reject/cancel transitions are stored in `reservation_events`. Rejection requires a reason.
- The admin MaxHeap order is effective priority descending, submission timestamp ascending (FIFO), then request ID ascending. Each full 24 hours pending adds one priority level, capped at 3. Queue priorities are recalculated when the queue is loaded.

### Routes

| Method | Route | Permission |
|---|---|---|
| GET | `/api/organizations/mine` | Student |
| GET / POST | `/api/admin/organizations` | Admin |
| PUT | `/api/admin/organizations/{organizationId}/members` | Admin |
| POST | `/api/reservations` | Active organization member |
| GET | `/api/reservations/mine` | Student |
| GET | `/api/reservations/{requestId}` | Active organization member |
| POST | `/api/reservations/{requestId}/cancel` | Original requester |
| GET | `/api/admin/reservations/queue` | Admin |
| POST | `/api/admin/reservations/{requestId}/approve` | Admin |
| POST | `/api/admin/reservations/{requestId}/reject` | Admin |
| GET | `/api/rooms/{classroomId}/reservations?date=YYYY-MM-DD` | Authenticated |

Create an organization with `name`, `type`, and `priorityLevel` (1–3). Then assign the student's exact `studentId` and role `MEMBER` or `REPRESENTATIVE` using the member route. Any active member can request a room; the representative role is retained for organization administration.

## 5. Module 5 — persistent graph and route finding

### Graph behavior

- `Graph` stores stable integer location IDs and undirected adjacency lists, validates reciprocal edges, and supports vertex/edge add, update, removal, and active-state changes.
- Dijkstra uses a min-priority queue (`std::priority_queue`) and returns a shortest weighted path over active vertices/edges. It handles sparse IDs and disconnected graphs and rejects invalid weights/endpoints.
- `navigation_locations` and `navigation_edges` persist graph metadata in SQLite. An edge is stored once with canonical endpoint order and loaded into the in-memory graph in both directions.
- An empty graph is seeded with synthetic IDs 0–4: Demo entrance, Demo corridor A, Demo corridor B, Demo crossing, and Demo destination. Seed edge weights are 20, 15, 10, 12, and 8; the shortest sample route from 0 to 4 costs 30 distance units. The seed is inserted only when the navigation table is empty.
- Admins can add/edit/deactivate locations, add/edit/deactivate edges, and delete edges. Student and admin users can request a shortest route. Coordinates are optional data only; the app does not claim that they are surveyed.
- Edge accessibility values are `UNKNOWN`, `UNVERIFIED`, `STEP_FREE`, and `STAIRS`. A route reports verified step-free only when every segment is explicitly `STEP_FREE`. Synthetic route results always carry a demo warning. Route weights are administrator-defined distance units, not meters.

### Routes

| Method | Route | Permission |
|---|---|---|
| GET | `/api/navigation/locations` | Authenticated |
| GET | `/api/navigation/locations?includeInactive=true` | Admin |
| POST | `/api/navigation/route` | Authenticated |
| GET | `/api/admin/navigation/edges` | Admin |
| POST / PUT | `/api/admin/navigation/locations` and `/api/admin/navigation/locations/{id}` | Admin |
| PATCH | `/api/admin/navigation/locations/{id}/status` | Admin |
| POST | `/api/admin/navigation/edges` | Admin |
| PATCH / DELETE | `/api/admin/navigation/edges/{fromId}/{toId}` | Admin |
| GET | `/api/demo/route?from=0&to=4` | Authenticated compatibility alias |

Example route request:

```json
{ "fromId": 0, "toId": 4 }
```

The response contains the ordered location IDs and labels, total cost, cost unit, edge accessibility metadata, an `isDemo` flag, and any warnings. `GET /api/demo/route` remains as a compatibility alias for the original dashboard test.

## 6. Verification performed

Build and run all backend tests:

```powershell
cmake -S backend -B backend/build
cmake --build backend/build --config Release
ctest --test-dir backend/build -C Release --output-on-failure
```

Frontend:

```powershell
cd frontend
npm run build
```

The backend tests cover:

- Hash Table behavior (existing tests).
- Sparse-ID Graph operations, Dijkstra shortest/disconnected routes, inactive nodes, MaxHeap build/order, and reservation queue tie-breaking.
- Executable `database/schema.sql`.
- Idempotent import of all 154 timetable meetings and 84 group-specific sections, room-alias migration, unknown-capacity enrollment blocking, admin enrollment toggles, independent lab capacities and choices, duplicate-course group prevention, and personalized multi-meeting schedules.
- Concurrent enrollment against fixed capacities, rejecting room/meeting capacity reductions below active enrollment, withdrawals, and reclaimed seats.
- Organization-member request authorization, class/reservation availability conflicts, reservation priority ordering, concurrent overlapping approvals, cancellation, and room schedule contents.
- Navigation seed routing, persisted location/edge changes, accessibility metadata, and disconnected route handling.

## 7. Files to continue from

- `backend/src/EnrollmentRepository.cpp` and `backend/src/EnrollmentRoutes.cpp`
- `backend/src/ReservationRepository.cpp` and `backend/src/ReservationRoutes.cpp`
- `backend/src/NavigationRepository.cpp` and `backend/src/NavigationRoutes.cpp`
- `backend/include/data_structures/Graph.h`, `backend/src/Graph.cpp`, and `backend/include/algorithms/Dijkstra.h`
- `backend/src/main.cpp` and `backend/CMakeLists.txt`
- `frontend/src/FeaturePages.tsx`, `frontend/src/services/api.ts`, and `frontend/src/App.tsx`
- `database/schema.sql`

## 8. Known limitations and recommended next work

1. Replace demo buildings, graph points, and edges with verified Innovation University data. Verify accessibility in person before setting `STEP_FREE`; no current data proves that routes are accessible.
2. Formalize university booking policies: timezone, booking horizon, hours, weekends/holidays, request duration limits, priority and fairness, approval delegation, and whether approved bookings conflict with scheduled classes.
3. Add database migration/version tracking. Repository initialization creates new tables and indexes but is not a general schema-migration framework.
4. Add organization edit/deactivation and member revocation workflows, plus richer admin request history and audit reporting if needed.
5. Improve production auth: persistent/revocable sessions or secure cookies, HTTPS, restrictive CORS, rate limiting, and operational logging.
6. Add a verified floor-plan editor/viewer and connect room/floor/building metadata to graph vertices before presenting the route finder as a map. The current coordinate fields are not a 2D/3D renderer.
7. Add browser/API integration tests for permissions and visual flows if the project adds an automated web test runner.

