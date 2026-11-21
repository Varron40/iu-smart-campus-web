import { useEffect, useMemo, useState } from "react";
import {
  approveReservation, cancelReservation, createOrganization, createReservation, deleteNavigationEdge,
  enrollInSection, findNavigationRoute, getAvailableSections, getClassrooms, getMyEnrollments,
  getMyOrganizations, getMyReservations, getNavigationEdges, getNavigationLocations, getRoomReservations,
  getOrganizationsForAdmin, getReservationQueue, rejectReservation, saveNavigationEdge,
  saveNavigationLocation, setNavigationLocationActive, setOrganizationMember, updateNavigationEdge,
  withdrawFromSection, type AvailableSection, type Classroom, type EnrollmentRecord, type NavigationEdge,
  type NavigationLocation, type Organization, type Reservation, type RouteResponse
} from "./services/api";

type Props = { isAdmin: boolean };

function messageOf(error: unknown, fallback: string) {
  return error instanceof Error ? error.message : fallback;
}

function timeToMinutes(time: string) {
  const [hours, minutes] = time.split(":").map(Number);
  return hours * 60 + minutes;
}

function minutesToTime(minutes: number) {
  return `${String(Math.floor(minutes / 60)).padStart(2, "0")}:${String(minutes % 60).padStart(2, "0")}`;
}

function getFreeWindows(schedule: Awaited<ReturnType<typeof getRoomReservations>>) {
  const start = timeToMinutes(schedule.availabilityStart);
  const end = timeToMinutes(schedule.availabilityEnd);
  const blocks = schedule.blocks
    .map(block => ({ start: timeToMinutes(block.startTime), end: timeToMinutes(block.endTime) }))
    .filter(block => block.end > start && block.start < end)
    .map(block => ({ start: Math.max(start, block.start), end: Math.min(end, block.end) }))
    .sort((left, right) => left.start - right.start);
  const merged: Array<{ start: number; end: number }> = [];
  for (const block of blocks) {
    const previous = merged[merged.length - 1];
    if (previous && block.start <= previous.end) previous.end = Math.max(previous.end, block.end);
    else merged.push({ ...block });
  }
  const free: Array<{ start: string; end: string }> = [];
  let cursor = start;
  for (const block of merged) {
    if (block.start > cursor) free.push({ start: minutesToTime(cursor), end: minutesToTime(block.start) });
    cursor = Math.max(cursor, block.end);
  }
  if (cursor < end) free.push({ start: minutesToTime(cursor), end: minutesToTime(end) });
  return free;
}

function isWithinFreeWindow(startTime: string, endTime: string, schedule: Awaited<ReturnType<typeof getRoomReservations>>) {
  if (!startTime || !endTime || endTime <= startTime) return false;
  return getFreeWindows(schedule).some(window => startTime >= window.start && endTime <= window.end);
}

export function EnrollmentPage() {
  const [sections, setSections] = useState<AvailableSection[]>([]);
  const [enrollments, setEnrollments] = useState<EnrollmentRecord[]>([]);
  const [query, setQuery] = useState("");
  const [term, setTerm] = useState("");
  const [group, setGroup] = useState("");
  const [labChoices, setLabChoices] = useState<Record<number, number>>({});
  const [error, setError] = useState("");
  const [busy, setBusy] = useState<number | null>(null);

  async function load() {
    try {
      const [available, mine] = await Promise.all([
        getAvailableSections({ q: query, term }), getMyEnrollments()
      ]);
      setSections(available.sections);
      setEnrollments(mine.enrollments);
      setError("");
    } catch (caught) {
      setError(messageOf(caught, "Unable to load enrollment information."));
    }
  }

  useEffect(() => { void load(); }, [query, term]);
  const activeSectionIds = useMemo(
    () => new Set(enrollments.filter(item => item.status === "ENROLLED").map(item => item.sectionId)),
    [enrollments]
  );
  const activeCourseKeys = useMemo(
    () => new Set(enrollments.filter(item => item.status === "ENROLLED").map(item => `${item.courseCode}|${item.term}`)),
    [enrollments]
  );
  const visibleSections = useMemo(
    () => sections.filter(section => !group || section.sectionCode === group),
    [sections, group]
  );

  async function enroll(sectionId: number) {
    setBusy(sectionId);
    try { await enrollInSection(sectionId, labChoices[sectionId]); await load(); }
    catch (caught) { setError(messageOf(caught, "Enrollment failed.")); }
    finally { setBusy(null); }
  }

  async function withdraw(enrollmentId: number) {
    setBusy(enrollmentId);
    try { await withdrawFromSection(enrollmentId); await load(); }
    catch (caught) { setError(messageOf(caught, "Withdrawal failed.")); }
    finally { setBusy(null); }
  }

  return <section className="management-page">
    <div className="management-heading"><div><span className="eyebrow">MODULE 3 · STUDENT SELF-SERVICE</span><h2>Course enrollment</h2><p>Choose a group for each course, review its full weekly schedule, and manage your enrollments.</p></div></div>
    <div className="filter-bar">
      <input aria-label="Search courses" placeholder="Search course or section" value={query} onChange={event => setQuery(event.target.value)} />
      <input aria-label="Filter by term" placeholder="Term" value={term} onChange={event => setTerm(event.target.value)} />
      <select aria-label="Filter by group" value={group} onChange={event => setGroup(event.target.value)}>
        <option value="">All groups</option>{["G1","G2","G3","G4","G5","G6","G7","G8","G9","G10","G12","G13","G14","G16"].map(item => <option key={item}>{item}</option>)}
      </select>
    </div>
    {error && <p className="error-message">{error}</p>}
    <h3 className="feature-subheading">Your enrollments</h3>
    {enrollments.length ? <div className="record-grid">{enrollments.map(item => <article className="record-card" key={item.id}>
      <div className="record-card-top"><span className="record-code">{item.courseCode} · {item.sectionCode}</span><span className={`record-status ${item.status === "ENROLLED" ? "active" : ""}`}>{item.status}</span></div>
      <h3>{item.courseName}</h3>
      <p>{item.term}</p>
      {item.meetings.length ? <div className="meeting-list">{item.meetings.map(meeting => <div className="meeting-item" key={meeting.id}>
        <b>{meeting.day} · {meeting.startTime}–{meeting.endTime} · {meeting.activityType}</b>
        <span>{meeting.venueLabel || meeting.classroomCode || "Location not set"}</span>
      </div>)}</div> : <div className="record-meta"><span>Location</span><b>{[item.buildingName, item.classroomCode].filter(Boolean).join(" · ") || "Not assigned"}</b></div>}
      {item.status === "ENROLLED" && <button className="secondary-button" disabled={busy === item.id} onClick={() => void withdraw(item.id)}>{busy === item.id ? "Updating…" : "Withdraw"}</button>}
    </article>)}</div> : <div className="empty-panel">You have no enrollment history yet.</div>}
    <h3 className="feature-subheading">Available sections</h3>
    {visibleSections.length ? <div className="record-grid">{visibleSections.map(section => {
      const enrolled = activeSectionIds.has(section.sectionId);
      const courseAlreadyEnrolled = activeCourseKeys.has(`${section.courseCode}|${section.term}`);
      const labOptions = section.meetings.filter(meeting => meeting.isLabOption);
      const chosenLabId = labChoices[section.sectionId];
      const labChoiceRequired = labOptions.length > 0;
      const labChoiceValid = !labChoiceRequired || labOptions.some(meeting => meeting.id === chosenLabId);
      const available = section.openForEnrollment && !section.isFull && labChoiceValid && !enrolled && !courseAlreadyEnrolled;
      return <article className="record-card" key={section.sectionId}>
        <div className="record-card-top"><span className="record-code">{section.courseCode} · {section.sectionCode}</span><span className={`record-status ${available ? "active" : ""}`}>{enrolled ? "ENROLLED" : courseAlreadyEnrolled ? "COURSE ENROLLED" : section.isFull ? "FULL" : section.openForEnrollment ? "OPEN" : "CLOSED"}</span></div>
        <h3>{section.courseName}</h3>
        <p>{section.term}</p>
        {section.meetings.length ? <div className="meeting-list">{section.meetings.map(meeting => <div className="meeting-item" key={meeting.id}>
          <b>{meeting.day} · {meeting.startTime}–{meeting.endTime} · {meeting.activityType}</b>
          <span>{meeting.venueLabel || meeting.classroomCode || "Location not set"}</span>
          <span>{meeting.capacityKnown
            ? `${meeting.remainingSeats} of ${meeting.capacity} seats available${meeting.isLabOption ? " · lab choice" : ""}`
            : "Seat limit not configured"}</span>
          {meeting.instructor && <span>{meeting.instructor}</span>}
        </div>)}</div> : <div className="record-meta"><span>Room</span><b>{[section.buildingName, section.classroomCode].filter(Boolean).join(" · ") || "Not assigned"}</b></div>}
        {labChoiceRequired && <label className="lab-choice-field">Choose a lab group
          <select value={chosenLabId ?? ""} onChange={event => setLabChoices(current => ({
            ...current, [section.sectionId]: Number(event.target.value)
          }))}>
            <option value="">Select an available lab</option>
            {labOptions.map(meeting => <option key={meeting.id} value={meeting.id}
              disabled={!meeting.capacityKnown || meeting.remainingSeats === 0}>
              {meeting.day} · {meeting.startTime}–{meeting.endTime} · {meeting.venueLabel || meeting.classroomCode || "Location not set"} · {meeting.capacityKnown ? `${meeting.remainingSeats} seats left` : "capacity not set"}
            </option>)}
          </select>
        </label>}
        <div className="record-meta"><span>Seats remaining</span><b>{section.capacityKnown ? section.remainingSeats : "Waiting for verified capacity"}</b></div>
        <button className="secondary-button" disabled={!available || busy === section.sectionId} onClick={() => void enroll(section.sectionId)}>
          {enrolled ? "Already enrolled" : courseAlreadyEnrolled ? "Already enrolled in this course" : busy === section.sectionId ? "Enrolling…" : available ? "Enroll" : "Unavailable"}
        </button>
      </article>;
    })}</div> : <div className="empty-panel">No active sections match these filters.</div>}
  </section>;
}

export function ReservationsPage({ isAdmin }: Props) {
  const [organizations, setOrganizations] = useState<Organization[]>([]);
  const [rooms, setRooms] = useState<Classroom[]>([]);
  const [reservations, setReservations] = useState<Reservation[]>([]);
  const [roomSchedule, setRoomSchedule] = useState<Awaited<ReturnType<typeof getRoomReservations>> | null>(null);
  const [priorityPolicy, setPriorityPolicy] = useState("");
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [busy, setBusy] = useState(false);
  const [organizationForm, setOrganizationForm] = useState({ name: "", type: "", priorityLevel: 1 });
  const [memberForm, setMemberForm] = useState({ organizationId: 0, studentId: "", role: "REPRESENTATIVE" as "MEMBER" | "REPRESENTATIVE" });
  const [reservationForm, setReservationForm] = useState({
    organizationId: 0, classroomId: 0, date: "", startTime: "", endTime: "", purpose: ""
  });
  const [rejectionReasons, setRejectionReasons] = useState<Record<number, string>>({});

  async function load() {
    try {
      const roomResult = await getClassrooms();
      setRooms(roomResult.rooms.filter(room => room.isActive));
      if (isAdmin) {
        const [organizationResult, queue] = await Promise.all([getOrganizationsForAdmin(), getReservationQueue()]);
        setOrganizations(organizationResult.organizations);
        setReservations(queue.queue);
        setPriorityPolicy(queue.priorityPolicy);
      } else {
        const [organizationResult, mine] = await Promise.all([getMyOrganizations(), getMyReservations()]);
        setOrganizations(organizationResult.organizations);
        setReservations(mine.reservations);
      }
      setError("");
    } catch (caught) {
      setError(messageOf(caught, "Unable to load reservation information."));
    }
  }

  useEffect(() => { void load(); }, [isAdmin]);

  useEffect(() => {
    let active = true;
    setRoomSchedule(null);
    if (!reservationForm.classroomId || !reservationForm.date) return () => { active = false; };
    getRoomReservations(reservationForm.classroomId, reservationForm.date)
      .then(result => { if (active) setRoomSchedule(result); })
      .catch(caught => { if (active) setError(messageOf(caught, "Unable to load room availability.")); });
    return () => { active = false; };
  }, [reservationForm.classroomId, reservationForm.date]);

  async function perform(action: () => Promise<unknown>, success: string) {
    setBusy(true);
    setError("");
    setNotice("");
    try { await action(); setNotice(success); await load(); }
    catch (caught) { setError(messageOf(caught, "Request failed.")); }
    finally { setBusy(false); }
  }

  async function submitOrganization(event: React.FormEvent) {
    event.preventDefault();
    await perform(async () => {
      const result = await createOrganization(organizationForm);
      setMemberForm(current => ({ ...current, organizationId: result.organization.id }));
      setOrganizationForm({ name: "", type: "", priorityLevel: 1 });
    }, "Organization created. Add members before they request rooms.");
  }

  async function submitReservation(event: React.FormEvent) {
    event.preventDefault();
    if (reservationForm.organizationId <= 0 || reservationForm.classroomId <= 0) {
      setError("Choose an organization and classroom before submitting.");
      return;
    }
    if (!roomSchedule || !isWithinFreeWindow(reservationForm.startTime, reservationForm.endTime, roomSchedule)) {
      setError("Choose a time range inside one of the displayed free windows.");
      return;
    }
    await perform(async () => {
      await createReservation(reservationForm);
      setReservationForm(current => ({ ...current, purpose: "" }));
    }, "Reservation request submitted for admin review.");
  }

  const representatives = organizations;
  const pending = reservations.filter(item => item.status === "PENDING");
  const freeWindows = roomSchedule ? getFreeWindows(roomSchedule) : [];
  return <section className="management-page">
    <div className="management-heading"><div><span className="eyebrow">MODULE 4 · ROOM REQUESTS</span><h2>{isAdmin ? "Reservation approvals" : "Room reservations"}</h2><p>{isAdmin ? "Review the priority queue and approve or reject pending requests." : "Organization members can find a free classroom for a project or meeting and submit it for admin approval."}</p></div></div>
    {error && <p className="error-message">{error}</p>}
    {notice && <p className="success-message">{notice}</p>}

    {isAdmin ? <>
      <div className="feature-two-column">
        <form className="management-form" onSubmit={submitOrganization}>
          <h3>Create organization</h3>
          <label>Organization name<input required value={organizationForm.name} onChange={event => setOrganizationForm({ ...organizationForm, name: event.target.value })} /></label>
          <label>Type<input required value={organizationForm.type} onChange={event => setOrganizationForm({ ...organizationForm, type: event.target.value })} /></label>
          <label>Demo priority level<select value={organizationForm.priorityLevel} onChange={event => setOrganizationForm({ ...organizationForm, priorityLevel: Number(event.target.value) })}><option value={1}>Level 1</option><option value={2}>Level 2</option><option value={3}>Level 3</option></select></label>
          <button className="primary-button wide-field" disabled={busy}>Create organization</button>
          <p className="demo-label wide-field">Priority is an explicitly demo policy; do not use it as an official campus allocation rule.</p>
        </form>
        <form className="management-form" onSubmit={event => { event.preventDefault(); void perform(() => setOrganizationMember(memberForm.organizationId, memberForm.studentId, memberForm.role), "Organization membership saved."); }}>
          <h3>Assign organization member</h3>
          <label>Organization<select required value={memberForm.organizationId} onChange={event => setMemberForm({ ...memberForm, organizationId: Number(event.target.value) })}><option value={0}>Choose organization</option>{organizations.map(item => <option key={item.id} value={item.id}>{item.name}</option>)}</select></label>
          <label>Student ID<input required value={memberForm.studentId} onChange={event => setMemberForm({ ...memberForm, studentId: event.target.value })} /></label>
          <label>Role<select value={memberForm.role} onChange={event => setMemberForm({ ...memberForm, role: event.target.value as "MEMBER" | "REPRESENTATIVE" })}><option value="REPRESENTATIVE">Representative</option><option value="MEMBER">Member</option></select></label>
          <button className="primary-button wide-field" disabled={busy || !memberForm.organizationId}>Save membership</button>
        </form>
      </div>
      {priorityPolicy && <p className="demo-label">{priorityPolicy}</p>}
      <h3 className="feature-subheading">Pending requests · {pending.length}</h3>
      {pending.length ? <div className="record-grid">{pending.map(item => <article className="record-card" key={item.id}>
        <div className="record-card-top"><span className="record-code">Priority {item.effectivePriority} · #{item.id}</span><span className="record-status">PENDING</span></div>
        <h3>{item.organizationName} · {item.roomCode}</h3>
        <p>{item.date} · {item.startTime}–{item.endTime}</p>
        <div className="record-meta"><span>Requester</span><b>{item.requesterName}</b></div>
        <p>{item.purpose}</p><small className="demo-label">{item.prioritySource}</small>
        <div className="record-actions"><button className="secondary-button" disabled={busy} onClick={() => void perform(() => approveReservation(item.id), "Reservation approved.")}>Approve</button></div>
        <form className="inline-reject" onSubmit={event => { event.preventDefault(); const reason = rejectionReasons[item.id]?.trim(); if (reason) void perform(() => rejectReservation(item.id, reason), "Reservation rejected."); }}>
          <input required maxLength={500} placeholder="Reason required to reject" value={rejectionReasons[item.id] ?? ""} onChange={event => setRejectionReasons({ ...rejectionReasons, [item.id]: event.target.value })} />
          <button className="secondary-button" disabled={busy}>Reject</button>
        </form>
      </article>)}</div> : <div className="empty-panel">There are no pending reservation requests.</div>}
      <h3 className="feature-subheading">Organizations</h3>
      <div className="record-grid">{organizations.map(item => <article className="record-card" key={item.id}><span className="record-code">{item.type}</span><h3>{item.name}</h3><div className="record-meta"><span>Demo priority</span><b>{item.priorityLevel}</b></div></article>)}</div>
    </> : <>
      <form className="management-form reservation-request-form" onSubmit={submitReservation}>
        <h3>Find a free room and request it</h3>
        {!representatives.length && <p className="wide-field demo-label">An administrator must add your account to an organization before you can request a room.</p>}
        <label>Organization<select required value={reservationForm.organizationId} onChange={event => setReservationForm({ ...reservationForm, organizationId: Number(event.target.value) })}><option value={0}>Choose an organization</option>{representatives.map(item => <option key={item.id} value={item.id}>{item.name} · priority {item.priorityLevel}</option>)}</select></label>
        <label>Classroom<select required value={reservationForm.classroomId} onChange={event => setReservationForm({ ...reservationForm, classroomId: Number(event.target.value), startTime: "", endTime: "" })}><option value={0}>Choose a classroom</option>{rooms.map(room => <option key={room.id} value={room.id}>{room.roomCode} · {room.buildingName}</option>)}</select></label>
        <label>Date<input required type="date" value={reservationForm.date} onChange={event => setReservationForm({ ...reservationForm, date: event.target.value, startTime: "", endTime: "" })} /></label>
        <div className="wide-field room-availability" aria-live="polite">
          <h4>Room availability · 09:00–17:30</h4>
          {!reservationForm.classroomId || !reservationForm.date ? <p>Select a classroom and date to see its schedule.</p>
            : !roomSchedule ? <p>Loading room schedule…</p>
            : <>
              {roomSchedule.blocks.length > 0 && <div className="availability-blocks"><b>Busy during:</b>{roomSchedule.blocks.map((block, index) =>
                <span key={`${block.kind}-${index}`} className={`availability-block ${block.kind.toLowerCase()}`}>
                  {block.startTime}–{block.endTime} · {block.title}{block.subtitle ? ` · ${block.subtitle}` : ""}
                </span>)}</div>}
              <div className="availability-blocks"><b>Free windows:</b>{freeWindows.length
                ? freeWindows.map(window => <button type="button" className="availability-window" key={`${window.start}-${window.end}`}
                    onClick={() => setReservationForm(current => ({ ...current, startTime: window.start, endTime: window.end }))}>
                    {window.start}–{window.end} · Select
                  </button>)
                : <span>No free time remains in this room's 09:00–17:30 booking window.</span>}
              </div>
            </>}
        </div>
        <label>Start time<input required type="time" min="09:00" max="17:30" value={reservationForm.startTime} onChange={event => setReservationForm({ ...reservationForm, startTime: event.target.value })} /></label>
        <label>End time<input required type="time" min="09:00" max="17:30" value={reservationForm.endTime} onChange={event => setReservationForm({ ...reservationForm, endTime: event.target.value })} /></label>
        <label className="wide-field">Purpose<input required maxLength={500} value={reservationForm.purpose} onChange={event => setReservationForm({ ...reservationForm, purpose: event.target.value })} /></label>
        <p className="wide-field demo-label">Scheduled classes and approved bookings are unavailable. Pending requests do not block availability; an administrator approves or rejects each request.</p>
        <button className="primary-button wide-field" disabled={busy || !representatives.length || !freeWindows.length}>Submit request</button>
      </form>
      <h3 className="feature-subheading">Your organization requests</h3>
      {reservations.length ? <div className="record-grid">{reservations.map(item => <article className="record-card" key={item.id}>
        <div className="record-card-top"><span className="record-code">#{item.id} · {item.organizationName}</span><span className={`record-status ${item.status === "APPROVED" ? "active" : ""}`}>{item.status}</span></div>
        <h3>{item.roomCode}</h3><p>{item.date} · {item.startTime}–{item.endTime}</p><p>{item.purpose}</p>
        {item.rejectionReason && <p className="error-message">Reason: {item.rejectionReason}</p>}
        {item.status === "PENDING" && <button className="secondary-button" disabled={busy} onClick={() => void perform(() => cancelReservation(item.id), "Request cancelled.")}>Cancel request</button>}
        {item.history?.length ? <details className="reservation-history"><summary>Request history</summary>{item.history.map((entry, index) => <p key={`${entry.newStatus}-${index}`}>{entry.oldStatus ?? "Created"} → {entry.newStatus} · {entry.createdAt}{entry.reason && ` · ${entry.reason}`}</p>)}</details> : null}
      </article>)}</div> : <div className="empty-panel">No reservation requests have been submitted.</div>}
    </>}
  </section>;
}

export function NavigationPage({ isAdmin }: Props) {
  const [locations, setLocations] = useState<NavigationLocation[]>([]);
  const [edges, setEdges] = useState<NavigationEdge[]>([]);
  const [fromId, setFromId] = useState<number | "">("");
  const [toId, setToId] = useState<number | "">("");
  const [route, setRoute] = useState<RouteResponse | null>(null);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [busy, setBusy] = useState(false);
  const [locationForm, setLocationForm] = useState<{ id: number | null; key: string; type: string; label: string; buildingId: string; floorId: string; x: string; y: string }>({ id: null, key: "", type: "JUNCTION", label: "", buildingId: "", floorId: "", x: "", y: "" });
  const [edgeForm, setEdgeForm] = useState({ fromId: 0, toId: 0, weight: 1, type: "CORRIDOR", accessibilityStatus: "UNKNOWN", isActive: true });

  async function load() {
    try {
      const locationResult = await getNavigationLocations(isAdmin);
      setLocations(locationResult.locations);
      if (isAdmin) setEdges((await getNavigationEdges()).edges);
      setError("");
    } catch (caught) {
      setError(messageOf(caught, "Unable to load navigation data."));
    }
  }
  useEffect(() => { void load(); }, [isAdmin]);

  async function perform(action: () => Promise<unknown>, success: string) {
    setBusy(true); setError(""); setNotice("");
    try { await action(); setNotice(success); await load(); }
    catch (caught) { setError(messageOf(caught, "Navigation update failed.")); }
    finally { setBusy(false); }
  }

  async function findRoute(event: React.FormEvent) {
    event.preventDefault();
    if (fromId === "" || toId === "") return;
    setBusy(true); setError(""); setRoute(null);
    try { setRoute(await findNavigationRoute(fromId, toId)); }
    catch (caught) { setError(messageOf(caught, "No route is available for these locations.")); }
    finally { setBusy(false); }
  }

  async function saveLocation(event: React.FormEvent) {
    event.preventDefault();
    const coordinates = locationForm.x.trim() && locationForm.y.trim()
      ? { x: Number(locationForm.x), y: Number(locationForm.y) } : null;
    if ((locationForm.x.trim() && !locationForm.y.trim()) || (!locationForm.x.trim() && locationForm.y.trim())) {
      setError("Enter both coordinate values, or leave both blank."); return;
    }
    await perform(() => saveNavigationLocation({
      key: locationForm.key, type: locationForm.type, label: locationForm.label,
      buildingId: locationForm.buildingId ? Number(locationForm.buildingId) : undefined,
      floorId: locationForm.floorId ? Number(locationForm.floorId) : undefined, coordinates
    }, locationForm.id ?? undefined), locationForm.id !== null ? "Location updated." : "Location created.");
    setLocationForm({ id: null, key: "", type: "JUNCTION", label: "", buildingId: "", floorId: "", x: "", y: "" });
  }

  async function saveEdge(event: React.FormEvent) {
    event.preventDefault();
    await perform(async () => {
      if (edges.some(edge => edge.fromId === edgeForm.fromId && edge.toId === edgeForm.toId ||
        edge.fromId === edgeForm.toId && edge.toId === edgeForm.fromId)) {
        await updateNavigationEdge(edgeForm);
      } else {
        await saveNavigationEdge(edgeForm);
      }
    }, "Edge saved.");
    setEdgeForm({ fromId: 0, toId: 0, weight: 1, type: "CORRIDOR", accessibilityStatus: "UNKNOWN", isActive: true });
  }

  return <section className="management-page">
    <div className="management-heading"><div><span className="eyebrow">MODULE 5 · GRAPH + DIJKSTRA</span><h2>Campus navigation</h2><p>Find a shortest route through the active location graph. Distance values use administrator-defined units.</p></div></div>
    {error && <p className="error-message">{error}</p>}
    {notice && <p className="success-message">{notice}</p>}
    <form className="route-form" onSubmit={findRoute}>
      <label>From<select required value={fromId} onChange={event => setFromId(event.target.value === "" ? "" : Number(event.target.value))}><option value="">Choose start</option>{locations.filter(item => item.isActive).map(item => <option key={item.id} value={item.id}>{item.label}{item.isDemo ? " · DEMO" : ""}</option>)}</select></label>
      <label>To<select required value={toId} onChange={event => setToId(event.target.value === "" ? "" : Number(event.target.value))}><option value="">Choose destination</option>{locations.filter(item => item.isActive).map(item => <option key={item.id} value={item.id}>{item.label}{item.isDemo ? " · DEMO" : ""}</option>)}</select></label>
      <button className="primary-button" disabled={busy || fromId === "" || toId === ""}>{busy ? "Finding route…" : "Find shortest route"}</button>
    </form>
    {route && <div className="route-card">
      <div className="record-card-top"><span className="record-code">{route.isDemo ? "DEMO DATA" : "ROUTE"}</span><b>{route.totalCost ?? route.distance} {route.costUnit ?? "distance units"}</b></div>
      <div className="route-steps">{(route.locations ?? []).map((location, index) => <span key={location.id}>{index > 0 && <i>→</i>}{location.label}</span>)}</div>
      <p className={route.locations?.length === 1 ? "route-accessibility" : route.accessibilityVerified ? "route-accessibility verified" : "route-accessibility"}>{route.locations?.length === 1 ? "Start and destination are the same location; there are no segments to assess." : route.accessibilityVerified ? "Every segment is marked step-free." : "Accessibility is unknown or unverified."}</p>
      {route.warnings?.map(warning => <p key={warning} className="demo-label">{warning}</p>)}
    </div>}
    <div className="feature-subheading-row"><h3 className="feature-subheading">Locations</h3><span className="demo-label">Synthetic seed points are clearly marked; do not treat them as campus directions.</span></div>
    <div className="record-grid">{locations.map(location => <article className={`record-card ${location.isActive ? "" : "record-inactive"}`} key={location.id}>
      <div className="record-card-top"><span className="record-code">{location.type} · ID {location.id}</span><span className={`record-status ${location.isActive ? "active" : ""}`}>{location.isDemo ? "DEMO" : location.isActive ? "ACTIVE" : "INACTIVE"}</span></div>
      <h3>{location.label}</h3><p>{location.key}</p>
      {location.coordinates && <div className="record-meta"><span>Coordinates</span><b>{location.coordinates.x}, {location.coordinates.y}</b></div>}
      {isAdmin && <div className="record-actions">
        <button className="secondary-button" onClick={() => setLocationForm({ id: location.id, key: location.key, type: location.type, label: location.label, buildingId: location.buildingId ? String(location.buildingId) : "", floorId: location.floorId ? String(location.floorId) : "", x: location.coordinates ? String(location.coordinates.x) : "", y: location.coordinates ? String(location.coordinates.y) : "" })}>Edit</button>
        <button className="secondary-button" disabled={busy} onClick={() => void perform(() => setNavigationLocationActive(location.id, !location.isActive), location.isActive ? "Location deactivated." : "Location activated.")}>{location.isActive ? "Deactivate" : "Activate"}</button>
      </div>}
    </article>)}</div>
    {isAdmin && <>
      <div className="feature-two-column navigation-editors">
        <form className="management-form" onSubmit={saveLocation}>
          <h3>{locationForm.id !== null ? "Edit location" : "Add location"}</h3>
          <label>Stable key<input required value={locationForm.key} onChange={event => setLocationForm({ ...locationForm, key: event.target.value })} /></label>
          <label>Type<select value={locationForm.type} onChange={event => setLocationForm({ ...locationForm, type: event.target.value })}>{["BUILDING","FLOOR","ROOM","JUNCTION","ENTRANCE","OTHER"].map(type => <option key={type}>{type}</option>)}</select></label>
          <label className="wide-field">Label<input required value={locationForm.label} onChange={event => setLocationForm({ ...locationForm, label: event.target.value })} /></label>
          <label>Building ID<input type="number" min="1" value={locationForm.buildingId} onChange={event => setLocationForm({ ...locationForm, buildingId: event.target.value })} /></label>
          <label>Floor ID<input type="number" min="1" value={locationForm.floorId} onChange={event => setLocationForm({ ...locationForm, floorId: event.target.value })} /></label>
          <label>X coordinate<input type="number" step="any" value={locationForm.x} onChange={event => setLocationForm({ ...locationForm, x: event.target.value })} /></label>
          <label>Y coordinate<input type="number" step="any" value={locationForm.y} onChange={event => setLocationForm({ ...locationForm, y: event.target.value })} /></label>
          <p className="wide-field demo-label">Coordinates are optional data values, not a rendered or surveyed floor plan.</p>
          <button className="primary-button wide-field" disabled={busy}>{locationForm.id !== null ? "Save location" : "Add location"}</button>
        </form>
        <form className="management-form" onSubmit={saveEdge}>
          <h3>Add or edit undirected edge</h3>
          <label>From ID<input required type="number" min="0" value={edgeForm.fromId} onChange={event => setEdgeForm({ ...edgeForm, fromId: Number(event.target.value) })} /></label>
          <label>To ID<input required type="number" min="0" value={edgeForm.toId} onChange={event => setEdgeForm({ ...edgeForm, toId: Number(event.target.value) })} /></label>
          <label>Distance weight<input required type="number" min="0" step="any" value={edgeForm.weight} onChange={event => setEdgeForm({ ...edgeForm, weight: Number(event.target.value) })} /></label>
          <label>Edge type<input required value={edgeForm.type} onChange={event => setEdgeForm({ ...edgeForm, type: event.target.value })} /></label>
          <label>Accessibility<select value={edgeForm.accessibilityStatus} onChange={event => setEdgeForm({ ...edgeForm, accessibilityStatus: event.target.value })}><option>UNKNOWN</option><option>UNVERIFIED</option><option>STEP_FREE</option><option>STAIRS</option></select></label>
          <button className="primary-button wide-field" disabled={busy}>Save edge</button>
          <p className="wide-field demo-label">Only set STEP_FREE after the segment has been verified in person.</p>
        </form>
      </div>
      <h3 className="feature-subheading">Graph edges</h3>
      {edges.length ? <div className="record-grid">{edges.map(edge => <article className="record-card" key={`${edge.fromId}-${edge.toId}`}>
        <div className="record-card-top"><span className="record-code">{edge.fromId} ↔ {edge.toId}</span><span className={`record-status ${edge.isActive ? "active" : ""}`}>{edge.isActive ? "ACTIVE" : "INACTIVE"}</span></div>
        <h3>{edge.weight} units · {edge.type}</h3><p>Accessibility: {edge.accessibilityStatus}</p>
        <div className="record-actions">
          <button className="secondary-button" onClick={() => setEdgeForm({ ...edge, isActive: edge.isActive })}>Edit</button>
          <button className="secondary-button" disabled={busy} onClick={() => void perform(() => updateNavigationEdge({ ...edge, isActive: !edge.isActive }), edge.isActive ? "Edge deactivated." : "Edge activated.")}>{edge.isActive ? "Deactivate" : "Activate"}</button>
          <button className="secondary-button" disabled={busy} onClick={() => void perform(() => deleteNavigationEdge(edge.fromId, edge.toId), "Edge deleted.")}>Delete</button>
        </div>
      </article>)}</div> : <div className="empty-panel">No graph edges are configured.</div>}
    </>}
  </section>;
}
