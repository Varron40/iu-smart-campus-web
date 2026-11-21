export type HealthResponse = { status: string; service: string };
export type RouteResponse = {
  from: number;
  to: number;
  distance: number | null;
  path: number[];
  totalCost?: number;
  costUnit?: string;
  locations?: NavigationLocation[];
  edges?: Array<NavigationEdge & { fromId: number }>;
  isDemo?: boolean;
  accessibilityVerified?: boolean;
  warnings?: string[];
};

const API_BASE = import.meta.env.VITE_API_BASE_URL ?? "";

export type Student = {
  id: string;
  studentId: string;
  fullName: string;
  universityEmail: string;
  faculty: string;
  department: string;
  academicYear: string;
  createdAt: string;
  role: "student" | "admin";
};

export type Building = { id: number; code: string; name: string; description: string; isActive: boolean };
export type Floor = { id: number; buildingId: number; label: string; description: string };
export type Classroom = {
  id: number; buildingId: number; buildingName: string; floorId: number; floorLabel: string;
  roomCode: string; roomName: string; roomType: string; capacity: number | null; capacityKnown: boolean;
  isActive: boolean; notes: string; occupancy: number | null; remainingSeats: number | null;
};
export type SectionMeeting = {
  id: number; activityType: string; day: string; startTime: string; endTime: string;
  classroomId: number | null; venueLabel: string; classroomCode: string | null;
  buildingName: string | null; instructor: string; enrollmentCapacity: number | null;
  capacity?: number | null; capacityKnown?: boolean; enrolledCount?: number; remainingSeats?: number | null;
  isLabOption?: boolean;
};
export type AcademicSection = {
  id: number; courseCode: string; courseName: string; sectionCode: string; instructor: string;
  term: string; day: string; startTime: string; endTime: string; classroomId: number | null;
  classroomCode: string | null; buildingName: string | null; enrollmentCapacity: number | null;
  meetings: SectionMeeting[]; isActive: boolean; isOpenForEnrollment: boolean;
};
export type AvailableSection = {
  sectionId: number; courseCode: string; courseName: string; sectionCode: string; term: string;
  day: string; startTime: string; endTime: string; classroomId: number | null; classroomCode: string | null;
  buildingName: string | null; capacity: number | null; enrolledCount: number | null;
  remainingSeats: number | null; capacityKnown: boolean; openForEnrollment: boolean; isFull: boolean;
  meetings: SectionMeeting[];
};
export type EnrollmentRecord = {
  id: number; sectionId: number; labMeetingId: number | null; status: "ENROLLED" | "WITHDRAWN"; courseCode: string; courseName: string;
  sectionCode: string; term: string; day: string; startTime: string; endTime: string;
  classroomCode: string | null; buildingName: string | null; meetings: SectionMeeting[];
};
export type AdminUserEnrollment = {
  id: number; status: "ENROLLED" | "WITHDRAWN"; labMeetingId: number | null; enrolledAt: string;
  sectionId: number; courseCode: string; courseName: string; sectionCode: string; term: string;
  instructor: string; sectionCapacity: number | null;
};
export type AdminUser = {
  id: number; studentId: string; fullName: string; universityEmail: string;
  faculty: string; department: string; academicYear: string; createdAt: string;
  role: "student" | "admin"; enrollments: AdminUserEnrollment[];
};
export type Organization = {
  id: number; name: string; type: string; priorityLevel: number; isActive: boolean; isRepresentative: boolean;
};
export type Reservation = {
  id: number; organizationId: number; organizationName: string; requesterName: string;
  classroomId: number; roomCode: string; date: string; startTime: string; endTime: string;
  purpose: string; priorityLevel: number; effectivePriority: number; prioritySource: string;
  submittedAt: string; status: "PENDING" | "APPROVED" | "REJECTED" | "CANCELLED";
  decidedAt: string; rejectionReason: string;
  history?: Array<{ oldStatus: string | null; newStatus: string; actorId: number; reason: string; createdAt: string }>;
};
export type RoomScheduleBlock = {
  kind: "CLASS" | "RESERVATION"; title: string; subtitle: string;
  startTime: string; endTime: string;
};
export type NavigationLocation = {
  id: number; key: string; type: string; label: string; buildingId: number | null; floorId: number | null;
  coordinates: { x: number; y: number } | null; isDemo: boolean; isActive: boolean;
};
export type NavigationEdge = {
  fromId: number; toId: number; weight: number; type: string; accessibilityStatus: string; isActive: boolean;
};
export type MapPoint = { x: number; y: number };
export type MapBuilding = Building & { floorCount: number; mappedFloorCount: number };
export type MapFloor = {
  id: number; buildingId: number; label: string; description: string; hasMap: boolean;
  coordinateSystem: string; unitLabel: string; width: number | null; height: number | null;
  metersPerUnit: number | null; elevation: number | null; floorHeight: number | null;
  verificationStatus: "NO_MAP" | "DEMO" | "UNVERIFIED" | "VERIFIED";
  sourceReference: string; isDemo: boolean;
};
export type MapFeature = {
  id: number; key: string; type: "ROOM" | "CORRIDOR" | "STAIRS" | "ELEVATOR" | "ENTRANCE" | "RAMP" | "WALL" | "OTHER";
  label: string; floorId: number; buildingId: number; classroomId: number | null;
  navigationLocationId: number | null; edgeFromId: number | null; edgeToId: number | null;
  geometryType: "POLYGON" | "LINE" | "POINT"; points: MapPoint[];
  elevation: number; height: number; thickness: number; isDemo: boolean; isActive: boolean;
};
export type MapFloorData = {
  floor: MapFloor; features: MapFeature[]; navigationLocations: NavigationLocation[];
};
export type MapRoomDetails = Classroom & {
  seatAvailabilityAvailable: boolean;
  sections: Array<{
    id: number; courseCode: string; courseName: string; sectionCode: string; term: string;
    day: string; startTime: string; endTime: string;
    capacity: number; enrolledCount: number; remainingSeats: number;
  }>;
};

export type ApiError = Error & { status?: number };

async function request<T>(path: string, options: RequestInit = {}): Promise<T> {
  const token = localStorage.getItem("iu_session_token");
  const headers = new Headers(options.headers);
  headers.set("Content-Type", "application/json");
  if (token) headers.set("Authorization", `Bearer ${token}`);
  const response = await fetch(`${API_BASE}${path}`, { ...options, headers });
  const body = await response.json().catch(() => ({ error: "Unexpected server response" }));
  if (!response.ok) {
    const error = new Error(body.error ?? `API returned ${response.status}`) as ApiError;
    error.status = response.status;
    throw error;
  }
  return body as T;
}

export function registerStudent(input: {
  studentId: string; fullName: string; universityEmail: string; password: string;
  faculty: string; department: string; academicYear: string;
}) {
  return request<{ student: Student }>("/api/auth/register", { method: "POST", body: JSON.stringify(input) });
}

export async function loginStudent(universityEmail: string, password: string) {
  const result = await request<{ token: string; student: Student }>("/api/auth/login", {
    method: "POST", body: JSON.stringify({ universityEmail, password })
  });
  localStorage.setItem("iu_session_token", result.token);
  return result.student;
}

export function logoutStudent() {
  return request<{ message: string }>("/api/auth/logout", { method: "POST", body: "{}" })
    .finally(() => localStorage.removeItem("iu_session_token"));
}

export function getMyProfile() {
  return request<{ student: Student }>("/api/students/me");
}

export function getAdminUsers() {
  return request<{ users: AdminUser[] }>("/api/admin/users");
}

export function updateMyProfile(input: Partial<Pick<Student, "fullName" | "faculty" | "department" | "academicYear">>) {
  return request<{ student: Student }>("/api/students/me", { method: "PUT", body: JSON.stringify(input) });
}

export function getMySchedule() {
  return request<{ studentId: string; schedule: Array<{
    enrollmentId: number; sectionId: number; labMeetingId: number | null; courseCode: string; courseName: string; sectionCode: string;
    term: string; day: string; startTime: string; endTime: string; classroomCode: string | null; buildingName: string | null;
    meetings: SectionMeeting[];
  }> }>("/api/students/me/schedule");
}

export function getBuildings() {
  return request<{ buildings: Building[] }>("/api/buildings");
}

export function getFloors(buildingId: number) {
  return request<{ floors: Floor[] }>(`/api/buildings/${buildingId}/floors`);
}

export function getClassrooms(filters: {
  q?: string; buildingId?: number; floorId?: number; roomType?: string; includeInactive?: boolean;
} = {}) {
  const query = new URLSearchParams();
  if (filters.q) query.set("q", filters.q);
  if (filters.buildingId) query.set("buildingId", String(filters.buildingId));
  if (filters.floorId) query.set("floorId", String(filters.floorId));
  if (filters.roomType) query.set("roomType", filters.roomType);
  if (filters.includeInactive) query.set("includeInactive", "true");
  return request<{ rooms: Classroom[]; occupancyAvailable: boolean }>(`/api/rooms${query.size ? `?${query}` : ""}`);
}

export function saveClassroom(room: Omit<Classroom, "id" | "buildingName" | "floorLabel" | "capacityKnown" | "occupancy" | "remainingSeats" | "capacity"> & { capacity: number }, id?: number) {
  const path = id ? `/api/rooms/${id}` : "/api/rooms";
  return request<{ room: Classroom }>(path, { method: id ? "PUT" : "POST", body: JSON.stringify(room) });
}

export function setClassroomActive(id: number, isActive: boolean) {
  return request<{ room: Classroom }>(`/api/rooms/${id}/status`, {
    method: "PATCH", body: JSON.stringify({ isActive })
  });
}

export function getSections(filters: { q?: string; term?: string; day?: string; buildingId?: number; includeInactive?: boolean } = {}) {
  const query = new URLSearchParams();
  if (filters.q) query.set("q", filters.q);
  if (filters.term) query.set("term", filters.term);
  if (filters.day) query.set("day", filters.day);
  if (filters.buildingId) query.set("buildingId", String(filters.buildingId));
  if (filters.includeInactive) query.set("includeInactive", "true");
  return request<{ sections: AcademicSection[] }>(`/api/sections${query.size ? `?${query}` : ""}`);
}

export function saveSection(section: Omit<AcademicSection, "id" | "classroomCode" | "buildingName">, id?: number) {
  const path = id ? `/api/sections/${id}` : "/api/sections";
  return request<{ section: AcademicSection }>(path, { method: id ? "PUT" : "POST", body: JSON.stringify(section) });
}

export function assignSectionClassroom(id: number, classroomId: number | null) {
  return request<{ section: AcademicSection }>(`/api/sections/${id}/classroom`, {
    method: "PATCH", body: JSON.stringify({ classroomId })
  });
}

export function setSectionActive(id: number, isActive: boolean) {
  return request<{ section: AcademicSection }>(`/api/sections/${id}/status`, {
    method: "PATCH", body: JSON.stringify({ isActive })
  });
}

export function setSectionEnrollmentOpen(id: number, isOpenForEnrollment: boolean) {
  return request<{ section: AcademicSection }>(`/api/sections/${id}/enrollment-status`, {
    method: "PATCH", body: JSON.stringify({ isOpenForEnrollment })
  });
}

export async function getHealth(): Promise<HealthResponse> {
  const response = await fetch(`${API_BASE}/api/health`);
  if (!response.ok) throw new Error(`API returned ${response.status}`);
  return response.json();
}

export async function getDemoRoute(from: number, to: number): Promise<RouteResponse> {
  return request<RouteResponse>(`/api/demo/route?from=${from}&to=${to}`);
}

export function getAvailableSections(filters: { q?: string; term?: string; group?: string } = {}) {
  const query = new URLSearchParams();
  if (filters.q) query.set("q", filters.q);
  if (filters.term) query.set("term", filters.term);
  if (filters.group) query.set("group", filters.group);
  return request<{ sections: AvailableSection[] }>(`/api/sections/available${query.size ? `?${query}` : ""}`);
}

export function enrollInSection(sectionId: number, labMeetingId?: number) {
  return request<{ enrollment: EnrollmentRecord }>(`/api/sections/${sectionId}/enroll`, {
    method: "POST", body: JSON.stringify({ labMeetingId: labMeetingId ?? null })
  });
}

export function getMyEnrollments() {
  return request<{ enrollments: EnrollmentRecord[] }>("/api/students/me/enrollments");
}

export function withdrawFromSection(enrollmentId: number) {
  return request<{ enrollment: EnrollmentRecord }>(`/api/enrollments/${enrollmentId}/withdraw`, { method: "POST", body: "{}" });
}

export function getMyOrganizations() {
  return request<{ organizations: Organization[] }>("/api/organizations/mine");
}

export function getOrganizationsForAdmin() {
  return request<{ organizations: Organization[] }>("/api/admin/organizations");
}

export function createOrganization(input: { name: string; type: string; priorityLevel: number }) {
  return request<{ organization: Organization }>("/api/admin/organizations", { method: "POST", body: JSON.stringify(input) });
}

export function setOrganizationMember(organizationId: number, studentId: string, role: "MEMBER" | "REPRESENTATIVE") {
  return request<{ message: string }>(`/api/admin/organizations/${organizationId}/members`, {
    method: "PUT", body: JSON.stringify({ studentId, role })
  });
}

export function createReservation(input: {
  organizationId: number; classroomId: number; date: string; startTime: string; endTime: string; purpose: string;
}) {
  return request<{ reservation: Reservation }>("/api/reservations", { method: "POST", body: JSON.stringify(input) });
}

export function getRoomReservations(roomId: number, date: string) {
  return request<{
    roomId: number; date: string; availabilityStart: string; availabilityEnd: string;
    pendingBlocksAvailability: boolean; blocks: RoomScheduleBlock[];
  }>(`/api/rooms/${roomId}/reservations?date=${encodeURIComponent(date)}`);
}

export function getMyReservations() {
  return request<{ reservations: Reservation[] }>("/api/reservations/mine");
}

export function cancelReservation(id: number) {
  return request<{ reservation: Reservation }>(`/api/reservations/${id}/cancel`, { method: "POST", body: "{}" });
}

export function getReservationQueue() {
  return request<{ queue: Reservation[]; priorityPolicy: string }>("/api/admin/reservations/queue");
}

export function approveReservation(id: number) {
  return request<{ reservation: Reservation }>(`/api/admin/reservations/${id}/approve`, { method: "POST", body: "{}" });
}

export function rejectReservation(id: number, reason: string) {
  return request<{ reservation: Reservation }>(`/api/admin/reservations/${id}/reject`, { method: "POST", body: JSON.stringify({ reason }) });
}

export function getNavigationLocations(includeInactive = false) {
  return request<{ locations: NavigationLocation[]; syntheticDataMayBePresent: boolean }>(
    `/api/navigation/locations${includeInactive ? "?includeInactive=true" : ""}`
  );
}

export function getNavigationEdges() {
  return request<{ edges: NavigationEdge[] }>("/api/admin/navigation/edges");
}

export function saveNavigationLocation(input: {
  key: string; type: string; label: string; buildingId?: number; floorId?: number;
  coordinates?: { x: number; y: number } | null;
}, id?: number) {
  const path = id === undefined ? "/api/admin/navigation/locations" : `/api/admin/navigation/locations/${id}`;
  return request<{ location: NavigationLocation }>(path, { method: id === undefined ? "POST" : "PUT", body: JSON.stringify(input) });
}

export function setNavigationLocationActive(id: number, isActive: boolean) {
  return request<{ message: string }>(`/api/admin/navigation/locations/${id}/status`, {
    method: "PATCH", body: JSON.stringify({ isActive })
  });
}

export function saveNavigationEdge(input: Omit<NavigationEdge, "isActive">) {
  return request<{ message: string }>("/api/admin/navigation/edges", { method: "POST", body: JSON.stringify(input) });
}

export function updateNavigationEdge(input: NavigationEdge) {
  return request<{ message: string }>(`/api/admin/navigation/edges/${input.fromId}/${input.toId}`, {
    method: "PATCH", body: JSON.stringify({
      weight: input.weight, type: input.type, accessibilityStatus: input.accessibilityStatus, isActive: input.isActive
    })
  });
}

export function deleteNavigationEdge(fromId: number, toId: number) {
  return request<{ message: string }>(`/api/admin/navigation/edges/${fromId}/${toId}`, { method: "DELETE" });
}

export function findNavigationRoute(fromId: number, toId: number) {
  return request<RouteResponse>("/api/navigation/route", {
    method: "POST", body: JSON.stringify({ fromId, toId })
  });
}

export function getMapBuildings(includeInactive = false) {
  return request<{ buildings: MapBuilding[] }>(`/api/map/buildings${includeInactive ? "?includeInactive=true" : ""}`);
}

export function getMapFloors(buildingId: number) {
  return request<{ floors: MapFloor[] }>(`/api/map/buildings/${buildingId}/floors`);
}

export function getMapFloorFeatures(floorId: number, includeInactive = false) {
  return request<MapFloorData>(`/api/map/floors/${floorId}/features${includeInactive ? "?includeInactive=true" : ""}`);
}

export function getMapRoomDetails(roomId: number) {
  return request<{ room: MapRoomDetails }>(`/api/map/rooms/${roomId}`);
}

export function saveFloorMapMetadata(input: {
  buildingId: number; width: number; height: number; coordinateSystem: string; unitLabel: string;
  metersPerUnit: number | null; elevation: number; floorHeight: number;
  verificationStatus: "DEMO" | "UNVERIFIED" | "VERIFIED"; sourceReference: string;
}, floorId: number) {
  return request<{ floor: MapFloor }>(`/api/admin/map/floors/${floorId}/metadata`, {
    method: "PUT", body: JSON.stringify(input)
  });
}

export function saveMapFeature(input: {
  key: string; type: MapFeature["type"]; label: string; geometryType: MapFeature["geometryType"];
  points: MapPoint[]; classroomId?: number | null; navigationLocationId?: number | null;
  edgeFromId?: number | null; edgeToId?: number | null; elevation?: number; height?: number; thickness?: number; isDemo?: boolean;
}, floorId: number, featureId?: number) {
  const path = featureId === undefined
    ? `/api/admin/map/floors/${floorId}/features`
    : `/api/admin/map/floors/${floorId}/features/${featureId}`;
  return request<{ feature: MapFeature }>(path, {
    method: featureId === undefined ? "POST" : "PUT", body: JSON.stringify(input)
  });
}

export function setMapFeatureActive(id: number, isActive: boolean) {
  return request<{ message: string }>(`/api/admin/map/features/${id}/status`, {
    method: "PATCH", body: JSON.stringify({ isActive })
  });
}
