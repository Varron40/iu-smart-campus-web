import { lazy, Suspense, useEffect, useState } from "react";
import { Activity, Archive, Building2, CalendarDays, ChevronRight, Compass, DoorOpen, ExternalLink, LogIn, LogOut, Map, MapPin, Pencil, Plus, Search, UserRound, Users } from "lucide-react";
import { assignSectionClassroom, getAdminUsers, getBuildings, getClassrooms, getDemoRoute, getFloors, getHealth, getMyProfile, getSections, loginStudent, logoutStudent, registerStudent, saveClassroom, saveSection, setClassroomActive, setSectionActive, setSectionEnrollmentOpen, updateMyProfile, type AcademicSection, type AdminUser, type Building, type Classroom, type Floor, type RouteResponse, type SectionMeeting, type Student } from "./services/api";
import { EnrollmentPage, NavigationPage, ReservationsPage } from "./FeaturePages";
import { LevelTwoSchedulePage } from "./LevelTwoSchedulePage";

const CampusMapPage = lazy(() => import("./CampusMapPage").then(module => ({ default: module.CampusMapPage })));

type ApiState = "checking" | "online" | "offline";

export default function App() {
  const [apiState, setApiState] = useState<ApiState>("checking");
  const [route, setRoute] = useState<RouteResponse | null>(null);
  const [routeError, setRouteError] = useState("");
  const [view, setView] = useState<"dashboard" | "register" | "login" | "profile" | "schedule" | "classrooms" | "sections" | "users" | "enrollment" | "reservations" | "navigation" | "map">("login");
  const [student, setStudent] = useState<Student | null>(null);
  const [checkingSession, setCheckingSession] = useState(true);
  const [dashboardRooms, setDashboardRooms] = useState<Classroom[]>([]);
  const [dashboardRoomError, setDashboardRoomError] = useState("");

  useEffect(() => {
    getHealth()
      .then(() => setApiState("online"))
      .catch(() => setApiState("offline"));

    if (!localStorage.getItem("iu_session_token")) {
      setCheckingSession(false);
      return;
    }

    getMyProfile()
      .then(({ student: account }) => {
        setStudent(account);
        setView("dashboard");
      })
      .catch(() => {
        localStorage.removeItem("iu_session_token");
        setView("login");
      })
      .finally(() => setCheckingSession(false));
  }, []);

  useEffect(() => {
    if (!student) return;
    getClassrooms()
      .then(({ rooms }) => { setDashboardRooms(rooms); setDashboardRoomError(""); })
      .catch(caught => setDashboardRoomError(caught instanceof Error ? caught.message : "Unable to load classrooms."));
  }, [student]);

  async function findDemoRoute() {
    setRouteError("");
    try {
      setRoute(await getDemoRoute(0, 4));
    } catch {
      setRouteError("Backend unavailable. Start the C++ API using the README instructions.");
    }
  }

  async function openProfile() {
    setView("profile");
    try { setStudent((await getMyProfile()).student); } catch {
      localStorage.removeItem("iu_session_token");
      setStudent(null);
      setView("login");
    }
  }

  function openSchedule() {
    setView("schedule");
  }

  async function handleLogout() {
    try { await logoutStudent(); } catch { localStorage.removeItem("iu_session_token"); }
    setStudent(null);
    setView("login");
  }

  if (checkingSession) {
    return <div className="auth-shell"><div className="account-card"><p>Checking your account…</p></div></div>;
  }

  if (!student) {
    return <div className="auth-shell">
      <div className="auth-brand"><img src="/innovation-university-logo.png" alt="Innovation University" /><span>Smart Campus</span></div>
      <AccountForm
        key={view}
        mode={view === "register" ? "register" : "login"}
        onSuccess={(account) => { setStudent(account); setView("dashboard"); }}
        onSwitch={() => setView(view === "login" ? "register" : "login")}
      />
      <div className="auth-api-status"><span className={`status-dot ${apiState}`} />Backend {apiState}</div>
    </div>;
  }

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <img className="brand-logo" src="/innovation-university-logo.png" alt="Innovation University" />
          <div><strong>Smart Campus</strong><span>{student.role === "admin" ? "ADMINISTRATOR" : "STUDENT PORTAL"}</span></div>
        </div>
        <div className="nav-label">WORKSPACE</div>
        <button className={`nav-item ${view === "dashboard" ? "active" : ""}`} onClick={() => setView("dashboard")}><Activity size={18}/> Overview</button>
        <button className={`nav-item ${view === "classrooms" ? "active" : ""}`} onClick={() => setView("classrooms")}><DoorOpen size={18}/> Classrooms</button>
        <button className={`nav-item ${view === "sections" ? "active" : ""}`} onClick={() => setView("sections")}><CalendarDays size={18}/> Sections</button>
        {student.role === "admin" && <button className={`nav-item ${view === "users" ? "active" : ""}`} onClick={() => setView("users")}><Users size={18}/> Registered users</button>}
        {student.role === "student" && <button className={`nav-item ${view === "enrollment" ? "active" : ""}`} onClick={() => setView("enrollment")}><Users size={18}/> Enrollment</button>}
        <button className={`nav-item ${view === "map" ? "active" : ""}`} onClick={() => setView("map")}><Map size={18}/> Interactive map</button>
        <button className={`nav-item ${view === "navigation" ? "active" : ""}`} onClick={() => setView("navigation")}><Compass size={18}/> Campus navigation</button>
        <button className={`nav-item ${view === "reservations" ? "active" : ""}`} onClick={() => setView("reservations")}><CalendarDays size={18}/> Reservations</button>
        <button className={`nav-item ${view === "profile" ? "active" : ""}`} onClick={openProfile}><UserRound size={18}/> My profile</button>
        <button className={`nav-item ${view === "schedule" ? "active" : ""}`} onClick={openSchedule}><CalendarDays size={18}/> My Schedule</button>
        <div className="sidebar-bottom">
          <div className="api-card">
            <span className={`status-dot ${apiState}`} />
            <div><b>Backend API</b><small>{apiState === "checking" ? "Checking connection…" : apiState === "online" ? "Connected · localhost:8080" : "Not connected"}</small></div>
          </div>
          <small className="version">Project starter · v0.1.0</small>
        </div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div><span className="eyebrow">INNOVATION UNIVERSITY / CAMPUS SYSTEM</span><h1>Campus overview</h1></div>
          <div className="user-chip">
            <><div className="avatar">{student.fullName.slice(0, 2).toUpperCase()}</div><span>{student.role === "admin" ? "Admin" : student.fullName}</span><button className="logout-button" onClick={handleLogout}><LogOut size={14}/> Logout</button></>
          </div>
        </header>
        {view === "dashboard" ? <>
        <section className="welcome">
          <div><span className="welcome-kicker">SMART CAMPUS PLATFORM</span><h2>Find your place.<br/><em>Find your way.</em></h2><p>Explore campus spaces, discover rooms and plan your next visit.</p></div>
          <div className="welcome-art"><div className="art-orbit orbit-one"/><div className="art-orbit orbit-two"/><Building2 size={104} strokeWidth={1}/><span className="art-pin pin-a"/><span className="art-pin pin-b"/></div>
        </section>

        <section className="stats-grid">
          <Stat icon={<Building2/>} label="Buildings mapped" value="—" note="Demo IDs only"/>
          <Stat icon={<DoorOpen/>} label="Classrooms" value={dashboardRoomError ? "—" : String(dashboardRooms.length)} note="From campus records"/>
          <Stat icon={<Users/>} label="Seat occupancy" value="—" note="Not available yet"/>
          <Stat icon={<CalendarDays/>} label="Sections" value="—" note="View section directory"/>
        </section>

        <section className="content-grid">
          <div className="panel map-panel">
            <div className="panel-heading"><div><span className="eyebrow">EXPLORE</span><h3>Campus map</h3></div><button className="icon-button" aria-label="Open interactive map" onClick={() => setView("map")}><Map size={18}/></button></div>
            <div className="map-placeholder">
              <div className="map-grid"/>
              <div className="map-building building-main"><Building2/><b>Building A</b><small>Demo model</small></div>
              <div className="map-building building-side"><Building2/><b>Building B</b><small>Demo model</small></div>
              <div className="map-path path-one"/><div className="map-path path-two"/>
              <span className="map-caption">Illustrative layout · not an official campus map</span>
            </div>
            <div className="map-footer"><span><i className="legend-dot purple"/> 2D and 3D views available</span><button className="text-button" onClick={() => setView("map")}>Open interactive map <ChevronRight size={16}/></button></div>
            {route && <div className="route-result">Demo route: {route.path.join(" → ")} · Distance: {route.distance ?? "Unavailable"} units</div>}
            {routeError && <div className="error-message">{routeError}</div>}
          </div>

          <div className="panel rooms-panel">
            <div className="panel-heading"><div><span className="eyebrow">SPACE DIRECTORY</span><h3>Sample rooms</h3></div><button className="icon-button" aria-label="Search"><Search size={18}/></button></div>
            <div className="room-list">
              {dashboardRooms.slice(0, 4).map(room => <div className="room-row" key={room.id}><div className="room-icon"><DoorOpen size={19}/></div><div className="room-info"><b>{room.roomName}</b><span>{room.roomCode} · {room.buildingName} · {room.floorLabel}</span><small>{room.roomType} · {room.capacityKnown ? `Capacity ${room.capacity}` : "Capacity not set"}</small></div><div className="room-seats"><span>Occupancy</span><b>Not available yet</b></div></div>)}
              {dashboardRoomError && <p className="error-message">{dashboardRoomError}</p>}
              {!dashboardRooms.length && !dashboardRoomError && <p className="demo-note">No classroom records yet. Administrators can add rooms from Classrooms.</p>}
            </div>
            <p className="demo-note">Occupancy and remaining seats are not available until enrollment data exists.</p>
          </div>
        </section>
        <section className="content-grid campus-reference-grid">
          <div className="panel campus-reference-card">
            <div className="panel-heading"><div><span className="eyebrow">CAMPUS LOCATION</span><h3>Innovation University</h3></div><MapPin size={20} aria-hidden="true"/></div>
            <p>Second 10th of Ramadan, Sharqia</p>
            <strong className="campus-coordinates">30.320492, 31.7391875</strong>
            <p className="demo-note">Campus-level reference only. The CS building position and floor plan are not verified.</p>
            <a className="secondary-button reference-link" href="https://www.google.com/maps/search/?api=1&query=30.320492%2C31.7391875" target="_blank" rel="noreferrer">Open campus pin <ExternalLink size={14}/></a>
          </div>
          <div className="panel campus-reference-card">
            <div className="panel-heading"><div><span className="eyebrow">ACADEMIC SOURCE</span><h3>Level 2 · Fall 2026–2027</h3></div><CalendarDays size={20} aria-hidden="true"/></div>
            <p>View the supplied Computer Science timetable image for groups G1–G16.</p>
            <p className="demo-note">The source is shown as provided; individual class records and building assignments have not been inferred from it.</p>
            <button className="secondary-button reference-link" onClick={() => setView("schedule")}>View timetable <ChevronRight size={14}/></button>
          </div>
        </section>
        <footer>IU Smart Campus <span>Built for the Data Structures project · C++ core + web interface</span></footer>
        </> : view === "profile" ? (
          <ProfilePage student={student} onUpdated={setStudent} />
        ) : view === "schedule" ? <LevelTwoSchedulePage isAdmin={student.role === "admin"} />
        : view === "classrooms" ? <ClassroomManagement isAdmin={student.role === "admin"} />
        : view === "sections" ? <SectionManagement isAdmin={student.role === "admin"} />
        : view === "users" && student.role === "admin" ? <AdminUsersPage />
        : view === "enrollment" ? <EnrollmentPage />
        : view === "reservations" ? <ReservationsPage isAdmin={student.role === "admin"} />
        : view === "map" ? <Suspense fallback={<div className="empty-panel">Loading interactive map…</div>}><CampusMapPage isAdmin={student.role === "admin"} /></Suspense>
        : <NavigationPage isAdmin={student.role === "admin"} />}
      </main>
    </div>
  );
}

function AdminUsersPage() {
  const [users, setUsers] = useState<AdminUser[]>([]);
  const [query, setQuery] = useState("");
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  useEffect(() => {
    getAdminUsers()
      .then(result => {
        setUsers(result.users);
        setSelectedId(current => current ?? result.users[0]?.id ?? null);
      })
      .catch(caught => setError(caught instanceof Error ? caught.message : "Unable to load registered users"))
      .finally(() => setLoading(false));
  }, []);

  const filtered = users.filter(user => {
    const text = `${user.studentId} ${user.fullName} ${user.universityEmail} ${user.faculty} ${user.department} ${user.academicYear}`.toLowerCase();
    return text.includes(query.toLowerCase());
  });
  const selected = users.find(user => user.id === selectedId) ?? null;

  return <section className="management-page">
    <div className="management-heading">
      <div><span className="eyebrow">ADMINISTRATION · USER DIRECTORY</span><h2>Registered users</h2>
        <p>Review account details and enrollment history for every registered student or administrator.</p></div>
      <button className="secondary-button" onClick={() => void getAdminUsers().then(result => setUsers(result.users)).catch(caught => setError(caught instanceof Error ? caught.message : "Unable to refresh users"))}>Refresh</button>
    </div>
    <div className="filter-bar">
      <input aria-label="Search registered users" placeholder="Search ID, name, email, faculty or department" value={query} onChange={event => setQuery(event.target.value)} />
    </div>
    {error && <p className="error-message" role="alert">{error}</p>}
    {loading ? <div className="empty-panel">Loading registered users…</div> : !filtered.length ? <div className="empty-panel">No registered users match this search.</div> :
      <div className="admin-users-layout">
        <div className="record-grid">
          {filtered.map(user => <button
            type="button"
            key={user.id}
            className={`record-card admin-user-card ${selectedId === user.id ? "selected" : ""}`}
            onClick={() => setSelectedId(user.id)}
          >
            <div className="record-card-top">
              <span className="record-code">{user.studentId}</span>
              <span className={`record-status ${user.role === "admin" ? "active" : ""}`}>{user.role.toUpperCase()}</span>
            </div>
            <h3>{user.fullName}</h3>
            <p>{user.universityEmail}</p>
            <div className="record-meta"><span>Faculty</span><b>{user.faculty}</b></div>
            <div className="record-meta"><span>Enrollments</span><b>{user.enrollments.filter(item => item.status === "ENROLLED").length} active</b></div>
          </button>)}
        </div>
        {selected && <aside className="admin-user-details">
          <span className="eyebrow">SELECTED ACCOUNT</span>
          <h3>{selected.fullName}</h3>
          <dl>
            <div><dt>Database ID</dt><dd>{selected.id}</dd></div>
            <div><dt>University ID</dt><dd>{selected.studentId}</dd></div>
            <div><dt>Email</dt><dd>{selected.universityEmail}</dd></div>
            <div><dt>Faculty</dt><dd>{selected.faculty}</dd></div>
            <div><dt>Department</dt><dd>{selected.department || "Not set"}</dd></div>
            <div><dt>Academic year</dt><dd>{selected.academicYear || "Not set"}</dd></div>
            <div><dt>Role</dt><dd>{selected.role}</dd></div>
            <div><dt>Registered</dt><dd>{selected.createdAt}</dd></div>
          </dl>
          <h4>Enrollment history · {selected.enrollments.length}</h4>
          {selected.enrollments.length ? <div className="admin-user-enrollments">
            {selected.enrollments.map(enrollment => <article key={enrollment.id}>
              <div><b>{enrollment.courseCode} · {enrollment.sectionCode}</b><span className={`record-status ${enrollment.status === "ENROLLED" ? "active" : ""}`}>{enrollment.status}</span></div>
              <p>{enrollment.courseName} · {enrollment.term}</p>
              <small>Section {enrollment.sectionId} · Instructor: {enrollment.instructor || "Not set"} · Capacity: {enrollment.sectionCapacity ?? "Not set"} · {enrollment.enrolledAt}</small>
              {enrollment.labMeetingId != null && <small>Lab meeting ID: {enrollment.labMeetingId}</small>}
            </article>)}
          </div> : <p className="demo-note">This user has no enrollment history.</p>}
        </aside>}
      </div>}
  </section>;
}

function Stat({ icon, label, value, note }: { icon: React.ReactNode; label: string; value: string; note: string }) {
  return <div className="stat-card"><div className="stat-top"><span className="stat-icon">{icon}</span><span className="stat-note">{note}</span></div><strong>{value}</strong><span className="stat-label">{label}</span></div>;
}

function ClassroomManagement({ isAdmin }: { isAdmin: boolean }) {
  const [rooms, setRooms] = useState<Classroom[]>([]);
  const [buildings, setBuildings] = useState<Building[]>([]);
  const [floors, setFloors] = useState<Floor[]>([]);
  const [query, setQuery] = useState("");
  const [buildingId, setBuildingId] = useState(0);
  const [floorId, setFloorId] = useState(0);
  const [roomType, setRoomType] = useState("");
  const [includeInactive, setIncludeInactive] = useState(false);
  const [formRoom, setFormRoom] = useState<{
    id?: number; buildingId: number; floorId: number; roomCode: string; roomName: string;
    roomType: string; capacity: number; notes: string; isActive: boolean;
  } | null>(null);
  const [formFloors, setFormFloors] = useState<Floor[]>([]);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");
  const [message, setMessage] = useState("");

  useEffect(() => {
    let active = true;
    setLoading(true);
    Promise.all([
      getBuildings(),
      getClassrooms({ q: query, buildingId: buildingId || undefined, floorId: floorId || undefined, roomType, includeInactive })
    ]).then(([buildingResult, roomResult]) => {
      if (!active) return;
      setBuildings(buildingResult.buildings);
      setRooms(roomResult.rooms);
      setError("");
    }).catch(caught => {
      if (active) setError(caught instanceof Error ? caught.message : "Unable to load classrooms.");
    }).finally(() => { if (active) setLoading(false); });
    return () => { active = false; };
  }, [query, buildingId, floorId, roomType, includeInactive]);

  useEffect(() => {
    if (!buildingId) { setFloors([]); return; }
    getFloors(buildingId).then(result => setFloors(result.floors)).catch(caught => setError(caught instanceof Error ? caught.message : "Unable to load floors."));
  }, [buildingId]);

  useEffect(() => {
    if (!formRoom?.buildingId) { setFormFloors([]); return; }
    getFloors(formRoom.buildingId).then(result => setFormFloors(result.floors)).catch(caught => setError(caught instanceof Error ? caught.message : "Unable to load floors."));
  }, [formRoom?.buildingId]);

  async function submitRoom(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (!formRoom) return;
    setSaving(true); setError(""); setMessage("");
    try {
      const { id, ...payload } = formRoom;
      await saveClassroom(payload, id);
      setFormRoom(null);
      setMessage(id ? "Classroom updated." : "Classroom created.");
      const result = await getClassrooms({ q: query, buildingId: buildingId || undefined, floorId: floorId || undefined, roomType, includeInactive });
      setRooms(result.rooms);
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "Unable to save classroom.");
    } finally { setSaving(false); }
  }

  async function toggleActive(room: Classroom) {
    setError(""); setMessage("");
    try {
      await setClassroomActive(room.id, !room.isActive);
      setMessage(room.isActive ? "Classroom archived; related records were preserved." : "Classroom reactivated.");
      const result = await getClassrooms({ q: query, buildingId: buildingId || undefined, floorId: floorId || undefined, roomType, includeInactive });
      setRooms(result.rooms);
    } catch (caught) { setError(caught instanceof Error ? caught.message : "Unable to change classroom status."); }
  }

  return <section className="management-page">
    <div className="management-heading"><div><span className="eyebrow">CAMPUS DIRECTORY</span><h2>Classrooms</h2><p>Imported labels are kept exactly; case/spacing variants may be aliases. Verify rooms and enter real capacities before enrollment opens.</p></div>
      {isAdmin && <button className="primary-button compact-button" onClick={() => setFormRoom({ buildingId: buildings[0]?.id ?? 0, floorId: 0, roomCode: "", roomName: "", roomType: "Lecture room", capacity: 0, notes: "", isActive: true })}><Plus size={16}/> Add classroom</button>}
    </div>
    {error && <div className="error-message">{error}</div>}
    {message && <div className="success-message">{message}</div>}
    <div className="filter-bar">
      <label className="search-field"><Search size={16}/><input aria-label="Search classrooms" placeholder="Search room code or name" value={query} onChange={event => setQuery(event.target.value)} /></label>
      <select aria-label="Building filter" value={buildingId} onChange={event => { setBuildingId(Number(event.target.value)); setFloorId(0); }}><option value={0}>All buildings</option>{buildings.map(building => <option key={building.id} value={building.id}>{building.name}</option>)}</select>
      <select aria-label="Floor filter" value={floorId} onChange={event => setFloorId(Number(event.target.value))}><option value={0}>All floors</option>{floors.map(floor => <option key={floor.id} value={floor.id}>{floor.label}</option>)}</select>
      <select aria-label="Room type filter" value={roomType} onChange={event => setRoomType(event.target.value)}><option value="">All room types</option><option>Lecture room</option><option>Laboratory</option><option>Seminar room</option><option>Other</option></select>
      {isAdmin && <label className="checkbox-filter"><input type="checkbox" checked={includeInactive} onChange={event => setIncludeInactive(event.target.checked)} /> Include archived</label>}
    </div>
    {formRoom && isAdmin && <form className="management-form" onSubmit={submitRoom}>
      <h3>{formRoom.id ? "Edit classroom" : "Add classroom"}</h3>
      <label>Building<select required value={formRoom.buildingId} onChange={event => setFormRoom({ ...formRoom, buildingId: Number(event.target.value), floorId: 0 })}>{buildings.map(building => <option key={building.id} value={building.id}>{building.name}</option>)}</select></label>
      <label>Floor<select required value={formRoom.floorId} onChange={event => setFormRoom({ ...formRoom, floorId: Number(event.target.value) })}><option value={0} disabled>Select floor</option>{formFloors.map(floor => <option key={floor.id} value={floor.id}>{floor.label}</option>)}</select></label>
      <label>Room code<input required value={formRoom.roomCode} onChange={event => setFormRoom({ ...formRoom, roomCode: event.target.value })}/></label>
      <label>Room name<input required value={formRoom.roomName} onChange={event => setFormRoom({ ...formRoom, roomName: event.target.value })}/></label>
      <label>Room type<select value={formRoom.roomType} onChange={event => setFormRoom({ ...formRoom, roomType: event.target.value })}><option>Lecture room</option><option>Laboratory</option><option>Seminar room</option><option>Other</option></select></label>
      <label>Maximum capacity<input required type="number" min="1" step="1" value={formRoom.capacity} onChange={event => setFormRoom({ ...formRoom, capacity: Number(event.target.value) })}/></label>
      <label className="wide-field">Notes<input value={formRoom.notes} onChange={event => setFormRoom({ ...formRoom, notes: event.target.value })}/></label>
      <div className="form-actions"><button type="button" className="secondary-button" onClick={() => setFormRoom(null)}>Cancel</button><button className="primary-button" disabled={saving}>{saving ? "Saving…" : "Save classroom"}</button></div>
    </form>}
    {loading ? <div className="empty-panel">Loading classrooms…</div> : rooms.length ? <div className="record-grid">{rooms.map(room => <article className={`record-card ${room.isActive ? "" : "record-inactive"}`} key={room.id}>
      <div className="record-card-top"><span className="record-code">{room.roomCode}</span><span className={`record-status ${room.isActive ? "active" : ""}`}>{room.isActive ? "Active" : "Archived"}</span></div>
      <h3>{room.roomName}</h3><p>{room.buildingName} · {room.floorLabel}</p><div className="record-meta"><span>{room.roomType}</span><b>{room.capacityKnown ? `Capacity ${room.capacity}` : "Capacity not set"}</b></div>
      <div className="record-meta"><span>Occupancy</span><b>Not available yet</b></div>
      {room.notes && <p className="record-notes">{room.notes}</p>}
      {isAdmin && <div className="record-actions"><button className="secondary-button" onClick={() => setFormRoom({ id: room.id, buildingId: room.buildingId, floorId: room.floorId, roomCode: room.roomCode, roomName: room.roomName, roomType: room.roomType, capacity: room.capacity ?? 0, notes: room.notes, isActive: room.isActive })}><Pencil size={14}/> Edit</button><button className="secondary-button" onClick={() => toggleActive(room)}><Archive size={14}/>{room.isActive ? "Archive" : "Reactivate"}</button></div>}
    </article>)}</div> : <div className="empty-panel">No classrooms match these filters. {isAdmin ? "Add a classroom to get started." : ""}</div>}
    <div className="demo-label">Building and floor identifiers were inferred from timetable labels only. No campus location or floor map has been verified.</div>
  </section>;
}

function SectionManagement({ isAdmin }: { isAdmin: boolean }) {
  const [sections, setSections] = useState<AcademicSection[]>([]);
  const [buildings, setBuildings] = useState<Building[]>([]);
  const [rooms, setRooms] = useState<Classroom[]>([]);
  const [query, setQuery] = useState("");
  const [term, setTerm] = useState("");
  const [day, setDay] = useState("");
  const [buildingId, setBuildingId] = useState(0);
  const [includeInactive, setIncludeInactive] = useState(false);
  const [formSection, setFormSection] = useState<{
    id?: number; courseCode: string; courseName: string; sectionCode: string; instructor: string;
    term: string; day: string; startTime: string; endTime: string; classroomId: number | null;
    enrollmentCapacity: number | null; meetings: SectionMeeting[]; isActive: boolean;
    isOpenForEnrollment: boolean;
  } | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");
  const [message, setMessage] = useState("");

  useEffect(() => {
    let active = true;
    setLoading(true);
    Promise.all([
      getSections({ q: query, term, day, buildingId: buildingId || undefined, includeInactive }),
      getBuildings(),
      getClassrooms()
    ]).then(([sectionResult, buildingResult, roomResult]) => {
      if (!active) return;
      setSections(sectionResult.sections);
      setBuildings(buildingResult.buildings);
      setRooms(roomResult.rooms);
      setError("");
    }).catch(caught => {
      if (active) setError(caught instanceof Error ? caught.message : "Unable to load sections.");
    }).finally(() => { if (active) setLoading(false); });
    return () => { active = false; };
  }, [query, term, day, buildingId, includeInactive]);

  async function refresh() {
    const result = await getSections({ q: query, term, day, buildingId: buildingId || undefined, includeInactive });
    setSections(result.sections);
  }

  async function submitSection(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (!formSection) return;
    setSaving(true); setError(""); setMessage("");
    try {
      const { id, ...payload } = formSection;
      await saveSection(payload, id);
      setFormSection(null);
      setMessage(id ? "Section updated." : "Section created.");
      await refresh();
    } catch (caught) { setError(caught instanceof Error ? caught.message : "Unable to save section."); }
    finally { setSaving(false); }
  }

  function updateMeeting(index: number, changes: Partial<SectionMeeting>) {
    setFormSection(current => current ? {
      ...current,
      meetings: current.meetings.map((meeting, itemIndex) =>
        itemIndex === index ? { ...meeting, ...changes } : meeting)
    } : current);
  }

  function addMeeting() {
    setFormSection(current => current ? {
      ...current,
      meetings: [...current.meetings, {
        id: 0, activityType: "Lecture", day: "", startTime: "", endTime: "",
        classroomId: null, venueLabel: "", classroomCode: null, buildingName: null, instructor: "",
        enrollmentCapacity: null
      }]
    } : current);
  }

  async function changeAssignment(sectionId: number, value: string) {
    setError(""); setMessage("");
    try {
      await assignSectionClassroom(sectionId, value ? Number(value) : null);
      setMessage("Classroom assignment updated.");
      await refresh();
    } catch (caught) { setError(caught instanceof Error ? caught.message : "Unable to assign classroom."); }
  }

  async function toggleActive(section: AcademicSection) {
    setError(""); setMessage("");
    try {
      await setSectionActive(section.id, !section.isActive);
      setMessage(section.isActive ? "Section archived." : "Section reactivated.");
      await refresh();
    } catch (caught) { setError(caught instanceof Error ? caught.message : "Unable to change section status."); }
  }

  async function toggleEnrollment(section: AcademicSection) {
    setError(""); setMessage("");
    try {
      await setSectionEnrollmentOpen(section.id, !section.isOpenForEnrollment);
      setMessage(section.isOpenForEnrollment ? "Enrollment closed for this section." : "Enrollment opened for this section.");
      await refresh();
    } catch (caught) { setError(caught instanceof Error ? caught.message : "Unable to change enrollment availability."); }
  }

  return <section className="management-page">
    <div className="management-heading"><div><span className="eyebrow">ACADEMIC DIRECTORY</span><h2>Sections</h2><p>Each section can include multiple weekly meetings. Enrollment opens only after its physical room capacities are verified.</p></div>
      {isAdmin && <button className="primary-button compact-button" onClick={() => setFormSection({ courseCode: "", courseName: "", sectionCode: "", instructor: "", term: "", day: "", startTime: "", endTime: "", classroomId: null, enrollmentCapacity: null, meetings: [], isActive: true, isOpenForEnrollment: false })}><Plus size={16}/> Add section</button>}
    </div>
    {error && <div className="error-message">{error}</div>}{message && <div className="success-message">{message}</div>}
    <div className="filter-bar">
      <label className="search-field"><Search size={16}/><input aria-label="Search sections" placeholder="Course code, name, or section" value={query} onChange={event => setQuery(event.target.value)}/></label>
      <input aria-label="Term filter" placeholder="Filter by term" value={term} onChange={event => setTerm(event.target.value)}/>
      <select aria-label="Day filter" value={day} onChange={event => setDay(event.target.value)}><option value="">All days</option>{["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"].map(item => <option key={item}>{item}</option>)}</select>
      <select aria-label="Building filter" value={buildingId} onChange={event => setBuildingId(Number(event.target.value))}><option value={0}>All buildings</option>{buildings.map(building => <option key={building.id} value={building.id}>{building.name}</option>)}</select>
      {isAdmin && <label className="checkbox-filter"><input type="checkbox" checked={includeInactive} onChange={event => setIncludeInactive(event.target.checked)}/> Include archived</label>}
    </div>
    {formSection && isAdmin && <form className="management-form" onSubmit={submitSection}>
      <h3>{formSection.id ? "Edit section" : "Add section"}</h3>
      <label>Course code<input required value={formSection.courseCode} onChange={event => setFormSection({ ...formSection, courseCode: event.target.value })}/></label>
      <label>Course name<input required value={formSection.courseName} onChange={event => setFormSection({ ...formSection, courseName: event.target.value })}/></label>
      <label>Section code<input required value={formSection.sectionCode} onChange={event => setFormSection({ ...formSection, sectionCode: event.target.value })}/></label>
      <label>Instructor (optional)<input value={formSection.instructor} onChange={event => setFormSection({ ...formSection, instructor: event.target.value })}/></label>
      <label>Term<input required placeholder="Set the verified academic term" value={formSection.term} onChange={event => setFormSection({ ...formSection, term: event.target.value })}/></label>
      <label>Enrollment capacity override<input type="number" min="1" step="1" value={formSection.enrollmentCapacity ?? ""} onChange={event => setFormSection({ ...formSection, enrollmentCapacity: event.target.value ? Number(event.target.value) : null })}/></label>
      <p className="wide-field form-hint">Optional for sections with verified physical room capacities; required for online-only sections. A smaller value limits enrollment further.</p>
      <label className="section-enrollment-toggle"><input type="checkbox" checked={formSection.isOpenForEnrollment} onChange={event => setFormSection({ ...formSection, isOpenForEnrollment: event.target.checked })}/> Allow students to enroll in this section</label>
      {formSection.meetings.length === 0 && <>
        <label>Day<select value={formSection.day} onChange={event => setFormSection({ ...formSection, day: event.target.value })}><option value="">Not scheduled</option>{["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"].map(item => <option key={item}>{item}</option>)}</select></label>
        <label>Start time<input type="time" value={formSection.startTime} onChange={event => setFormSection({ ...formSection, startTime: event.target.value })}/></label>
        <label>End time<input type="time" value={formSection.endTime} onChange={event => setFormSection({ ...formSection, endTime: event.target.value })}/></label>
        <label className="wide-field">Classroom<select value={formSection.classroomId ?? ""} onChange={event => setFormSection({ ...formSection, classroomId: event.target.value ? Number(event.target.value) : null })}><option value="">Unassigned</option>{rooms.map(room => <option key={room.id} value={room.id}>{room.roomCode} · {room.buildingName} · {room.capacityKnown ? `Capacity ${room.capacity}` : "Capacity not set"}</option>)}</select></label>
      </>}
      <div className="wide-field meeting-editor">
        <div className="section-title-row"><h4>Weekly meetings</h4><button type="button" className="secondary-button" onClick={addMeeting}><Plus size={14}/> Add meeting</button></div>
        {formSection.meetings.map((meeting, index) => <div className="meeting-form-row" key={`${meeting.id}-${index}`}>
          <label>Activity<input required value={meeting.activityType} onChange={event => updateMeeting(index, { activityType: event.target.value })}/></label>
          <label>Day<select required value={meeting.day} onChange={event => updateMeeting(index, { day: event.target.value })}><option value="">Select day</option>{["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"].map(item => <option key={item}>{item}</option>)}</select></label>
          <label>Start<input required type="time" value={meeting.startTime} onChange={event => updateMeeting(index, { startTime: event.target.value })}/></label>
          <label>End<input required type="time" value={meeting.endTime} onChange={event => updateMeeting(index, { endTime: event.target.value })}/></label>
          <label>Venue label<input value={meeting.venueLabel} onChange={event => updateMeeting(index, { venueLabel: event.target.value })}/></label>
          <label>Linked classroom<select value={meeting.classroomId ?? ""} onChange={event => updateMeeting(index, { classroomId: event.target.value ? Number(event.target.value) : null })}><option value="">Online / unassigned</option>{rooms.filter(room => room.isActive).map(room => <option key={room.id} value={room.id}>{room.roomCode} · {room.buildingName}</option>)}</select></label>
          <label>Instructor<input value={meeting.instructor} onChange={event => updateMeeting(index, { instructor: event.target.value })}/></label>
          <label>Enrollment limit for this meeting<input type="number" min="1" step="1" value={meeting.enrollmentCapacity ?? ""} onChange={event => updateMeeting(index, { enrollmentCapacity: event.target.value ? Number(event.target.value) : null })}/></label>
          <small className="form-hint">This limit is specific to the meeting. Students assigned to different labs count against their selected lab only.</small>
          <button type="button" className="secondary-button remove-meeting" onClick={() => setFormSection(current => {
            if (!current) return current;
            const meetings = current.meetings.filter((_, itemIndex) => itemIndex !== index);
            return meetings.length ? { ...current, meetings } : {
              ...current, meetings, day: "", startTime: "", endTime: "", classroomId: null, instructor: ""
            };
          })}>Remove meeting</button>
        </div>)}
      </div>
      <div className="form-actions"><button type="button" className="secondary-button" onClick={() => setFormSection(null)}>Cancel</button><button className="primary-button" disabled={saving}>{saving ? "Saving…" : "Save section"}</button></div>
    </form>}
    {loading ? <div className="empty-panel">Loading sections…</div> : sections.length ? <div className="record-grid">{sections.map(section => <article className={`record-card ${section.isActive ? "" : "record-inactive"}`} key={section.id}>
      <div className="record-card-top"><span className="record-code">{section.courseCode} · {section.sectionCode}</span><span className={`record-status ${section.isOpenForEnrollment ? "active" : ""}`}>{section.isOpenForEnrollment ? "Enrollment open" : "Enrollment closed"}</span></div>
      <h3>{section.courseName}</h3><p>{section.term}{section.enrollmentCapacity ? ` · section cap ${section.enrollmentCapacity}` : " · no section cap"}</p>
      {section.meetings.length ? <div className="meeting-list">{section.meetings.map(meeting => <div className="meeting-item" key={meeting.id}>
        <b>{meeting.day} · {meeting.startTime}–{meeting.endTime} · {meeting.activityType}{meeting.enrollmentCapacity ? ` · limit ${meeting.enrollmentCapacity}` : ""}</b>
        <span>{meeting.venueLabel || meeting.classroomCode || "Location not set"}{meeting.buildingName ? ` · ${meeting.buildingName}` : ""}</span>
        {meeting.instructor && <span>{meeting.instructor}</span>}
      </div>)}</div> : <div className="record-meta"><span>{section.instructor || "Instructor not specified"}</span><b>{section.classroomCode ? `${section.classroomCode} · ${section.buildingName}` : "No classroom assigned"}</b></div>}
      {isAdmin && !section.meetings.length && <label className="assignment-field">Classroom assignment<select value={section.classroomId ?? ""} onChange={event => changeAssignment(section.id, event.target.value)}><option value="">Unassigned</option>{rooms.map(room => <option key={room.id} value={room.id}>{room.roomCode} · {room.buildingName}</option>)}</select></label>}
      {isAdmin && <div className="record-actions"><button className="secondary-button" onClick={() => setFormSection({ id: section.id, courseCode: section.courseCode, courseName: section.courseName, sectionCode: section.sectionCode, instructor: section.instructor, term: section.term, day: section.day, startTime: section.startTime, endTime: section.endTime, classroomId: section.classroomId, enrollmentCapacity: section.enrollmentCapacity, meetings: section.meetings, isActive: section.isActive, isOpenForEnrollment: section.isOpenForEnrollment })}><Pencil size={14}/> Edit</button><button className="secondary-button" onClick={() => toggleEnrollment(section)}>{section.isOpenForEnrollment ? "Close enrollment" : "Open enrollment"}</button><button className="secondary-button" onClick={() => toggleActive(section)}><Archive size={14}/>{section.isActive ? "Archive" : "Reactivate"}</button></div>}
    </article>)}</div> : <div className="empty-panel">No sections match these filters. {isAdmin ? "Add a section to get started." : ""}</div>}
  </section>;
}

function AccountForm({ mode, onSuccess, onSwitch }: {
  mode: "register" | "login";
  onSuccess: (student: Student) => void;
  onSwitch: () => void;
}) {
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const [success, setSuccess] = useState("");
  const [password, setPassword] = useState("");

  async function submit(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setLoading(true);
    setError("");
    setSuccess("");
    const form = new FormData(event.currentTarget);
    try {
      if (mode === "login") {
        const account = await loginStudent(String(form.get("universityEmail")), password);
        setSuccess("Signed in successfully.");
        onSuccess(account);
      } else {
        const registration = await registerStudent({
          studentId: String(form.get("studentId")),
          fullName: String(form.get("fullName")),
          universityEmail: String(form.get("universityEmail")),
          password,
          faculty: String(form.get("faculty")),
          department: String(form.get("department")),
          academicYear: String(form.get("academicYear"))
        });
        const account = await loginStudent(registration.student.universityEmail, password);
        onSuccess(account);
      }
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "Unable to complete the request.");
    } finally {
      setLoading(false);
    }
  }

  return <section className="account-page">
    <div className="account-card">
      <span className="welcome-kicker">STUDENT ACCOUNT</span>
      <h2>{mode === "login" ? "Welcome back" : "Create account"}</h2>
      <p>{mode === "login" ? "Sign in with your university email." : "Register using demo or university-issued student details."}</p>
      {error && <div className="error-message">{error}</div>}
      {success && <div className="success-message">{success}</div>}
      <form onSubmit={submit} className="account-form">
        {mode === "register" && <>
          <label>Student ID<input name="studentId" required /></label>
          <label>Full name<input name="fullName" required /></label>
        </>}
        <label>University email<input name="universityEmail" type="email" required /></label>
        <label>Password<input name="password" type="password" minLength={8} required value={password} onChange={e => setPassword(e.target.value)} /></label>
        {mode === "register" && <>
          <label>Faculty<input name="faculty" required /></label>
          <label>Department / program<input name="department" /></label>
          <label>Academic year / level<input name="academicYear" /></label>
        </>}
        <button className="primary-button" disabled={loading}>{loading ? "Please wait…" : mode === "login" ? "Sign in" : "Create account"}</button>
      </form>
      <button className="link-button" onClick={onSwitch}>{mode === "login" ? "Need an account? Register" : "Already registered? Sign in"}</button>
    </div>
  </section>;
}

function ProfilePage({ student, onUpdated }: { student: Student | null; onUpdated: (student: Student) => void }) {
  const [loading, setLoading] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

  if (!student) return <section className="account-page"><div className="account-card"><p>Loading profile…</p></div></section>;

  async function saveProfile(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setLoading(true); setError(""); setMessage("");
    const form = new FormData(event.currentTarget);
    try {
      const updated = await updateMyProfile({
        fullName: String(form.get("fullName")),
        faculty: String(form.get("faculty")),
        department: String(form.get("department")),
        academicYear: String(form.get("academicYear"))
      });
      onUpdated(updated.student);
      setMessage("Profile updated successfully.");
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "Unable to update profile.");
    } finally { setLoading(false); }
  }

  return <section className="account-page"><div className="account-card profile-card">
    <span className="welcome-kicker">MY ACCOUNT</span><h2>{student.fullName}</h2>
    <p>{student.studentId} · {student.universityEmail}</p>
    {error && <div className="error-message">{error}</div>}
    {message && <div className="success-message">{message}</div>}
    <form onSubmit={saveProfile} className="account-form">
      <label>Full name<input name="fullName" defaultValue={student.fullName} required /></label>
      <label>Faculty<input name="faculty" defaultValue={student.faculty} required /></label>
      <label>Department / program<input name="department" defaultValue={student.department} /></label>
      <label>Academic year / level<input name="academicYear" defaultValue={student.academicYear} /></label>
      <button className="primary-button" disabled={loading}>{loading ? "Saving…" : "Save profile"}</button>
    </form>
  </div></section>;
}

