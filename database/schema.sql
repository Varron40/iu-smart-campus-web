-- Initial schema outline. Apply migrations before using in a real deployment.
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS students (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  student_id TEXT NOT NULL UNIQUE,
  full_name TEXT NOT NULL,
  university_email TEXT NOT NULL UNIQUE,
  password_hash TEXT NOT NULL,
  faculty TEXT NOT NULL,
  department TEXT,
  academic_year TEXT,
  role TEXT NOT NULL DEFAULT 'student' CHECK(role IN ('student', 'admin')),
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS buildings (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  code TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1))
);

CREATE TABLE IF NOT EXISTS floors (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  building_id INTEGER NOT NULL REFERENCES buildings(id) ON DELETE RESTRICT,
  label TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  UNIQUE(building_id, label),
  UNIQUE(id, building_id)
);

CREATE TABLE IF NOT EXISTS classrooms (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  building_id INTEGER NOT NULL REFERENCES buildings(id) ON DELETE RESTRICT,
  floor_id INTEGER NOT NULL,
  room_code TEXT NOT NULL,
  room_name TEXT NOT NULL,
  room_type TEXT NOT NULL,
  capacity INTEGER NOT NULL CHECK(capacity > 0),
  capacity_known INTEGER NOT NULL DEFAULT 1 CHECK(capacity_known IN (0, 1)),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
  notes TEXT NOT NULL DEFAULT '',
  UNIQUE(building_id, room_code),
  FOREIGN KEY(floor_id, building_id) REFERENCES floors(id, building_id) ON DELETE RESTRICT
);

CREATE TABLE IF NOT EXISTS academic_sections (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  course_code TEXT NOT NULL,
  course_name TEXT NOT NULL,
  section_code TEXT NOT NULL,
  instructor TEXT NOT NULL DEFAULT '',
  term TEXT NOT NULL,
  day TEXT NOT NULL DEFAULT '',
  start_time TEXT NOT NULL DEFAULT '',
  end_time TEXT NOT NULL DEFAULT '',
  classroom_id INTEGER REFERENCES classrooms(id) ON DELETE RESTRICT,
  enrollment_capacity INTEGER CHECK(enrollment_capacity IS NULL OR enrollment_capacity > 0),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
  is_open_for_enrollment INTEGER NOT NULL DEFAULT 0 CHECK(is_open_for_enrollment IN (0, 1)),
  UNIQUE(course_code, section_code, term),
  CHECK((start_time = '' AND end_time = '') OR (start_time <> '' AND end_time > start_time))
);

CREATE INDEX IF NOT EXISTS idx_classrooms_building_floor_type
  ON classrooms(building_id, floor_id, room_type, is_active);
CREATE INDEX IF NOT EXISTS idx_sections_term_day
  ON academic_sections(term, day, is_active);

CREATE TABLE IF NOT EXISTS academic_section_meetings (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  section_id INTEGER NOT NULL REFERENCES academic_sections(id) ON DELETE CASCADE,
  activity_type TEXT NOT NULL,
  day TEXT NOT NULL,
  start_time TEXT NOT NULL,
  end_time TEXT NOT NULL,
  classroom_id INTEGER REFERENCES classrooms(id) ON DELETE RESTRICT,
  venue_label TEXT NOT NULL DEFAULT '',
  instructor TEXT NOT NULL DEFAULT '',
  enrollment_capacity INTEGER CHECK(enrollment_capacity IS NULL OR enrollment_capacity > 0),
  sort_order INTEGER NOT NULL DEFAULT 0,
  CHECK(end_time > start_time)
);
CREATE INDEX IF NOT EXISTS idx_section_meetings_section
  ON academic_section_meetings(section_id, sort_order, id);
CREATE INDEX IF NOT EXISTS idx_section_meetings_classroom
  ON academic_section_meetings(classroom_id, section_id);
CREATE TABLE IF NOT EXISTS campus_schedule_imports (
  source_key TEXT PRIMARY KEY,
  imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS enrollments (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
  section_id INTEGER NOT NULL REFERENCES academic_sections(id) ON DELETE RESTRICT,
  lab_meeting_id INTEGER REFERENCES academic_section_meetings(id) ON DELETE SET NULL,
  status TEXT NOT NULL CHECK(status IN ('ENROLLED', 'WITHDRAWN')),
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_one_active_enrollment
  ON enrollments(student_id, section_id) WHERE status = 'ENROLLED';
CREATE TRIGGER IF NOT EXISTS prevent_duplicate_course_group_enrollment
BEFORE INSERT ON enrollments
WHEN NEW.status = 'ENROLLED' AND EXISTS (
  SELECT 1 FROM enrollments existing
  JOIN academic_sections current_section ON current_section.id = existing.section_id
  JOIN academic_sections new_section ON new_section.id = NEW.section_id
  WHERE existing.student_id = NEW.student_id
    AND existing.status = 'ENROLLED'
    AND current_section.course_code = new_section.course_code
    AND current_section.term = new_section.term
)
BEGIN
  SELECT RAISE(ABORT, 'course_already_enrolled');
END;
CREATE INDEX IF NOT EXISTS idx_enrollments_section_status
  ON enrollments(section_id, status);
CREATE INDEX IF NOT EXISTS idx_enrollments_student_updated
  ON enrollments(student_id, updated_at);
CREATE TABLE IF NOT EXISTS enrollment_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  enrollment_id INTEGER NOT NULL REFERENCES enrollments(id) ON DELETE RESTRICT,
  old_status TEXT,
  new_status TEXT NOT NULL,
  actor_student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TRIGGER IF NOT EXISTS prevent_capacity_below_active_enrollments
BEFORE UPDATE OF capacity, capacity_known ON classrooms
WHEN EXISTS (
  SELECT 1 FROM academic_sections s
  WHERE s.classroom_id = NEW.id
    AND NOT EXISTS (SELECT 1 FROM academic_section_meetings m WHERE m.section_id = s.id)
    AND (NEW.capacity_known = 0 OR
         (SELECT COUNT(*) FROM enrollments e
          WHERE e.section_id = s.id AND e.status = 'ENROLLED') > NEW.capacity)
  UNION ALL
  SELECT 1 FROM academic_section_meetings m
  WHERE m.classroom_id = NEW.id
    AND (NEW.capacity_known = 0 OR
         (SELECT COUNT(*) FROM enrollments e
          WHERE e.section_id = m.section_id AND e.status = 'ENROLLED'
            AND ((LOWER(m.activity_type) NOT LIKE '%lab%' AND LOWER(m.activity_type) NOT LIKE '%practical%')
                 OR e.lab_meeting_id = m.id)) > NEW.capacity)
)
BEGIN
  SELECT RAISE(ABORT, 'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER IF NOT EXISTS prevent_section_capacity_below_active_enrollments
BEFORE UPDATE OF enrollment_capacity ON academic_sections
WHEN NEW.enrollment_capacity IS NOT NULL AND
  (SELECT COUNT(*) FROM enrollments e
   WHERE e.section_id = NEW.id AND e.status = 'ENROLLED') > NEW.enrollment_capacity
BEGIN
  SELECT RAISE(ABORT, 'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER IF NOT EXISTS prevent_section_room_below_enrollment_count
BEFORE UPDATE OF classroom_id ON academic_sections
WHEN NEW.classroom_id IS NOT NULL AND
  NOT EXISTS (SELECT 1 FROM academic_section_meetings m WHERE m.section_id = NEW.id) AND
  (SELECT COUNT(*) FROM enrollments e
   WHERE e.section_id = NEW.id AND e.status = 'ENROLLED') >
  (SELECT CASE WHEN c.capacity_known = 1 AND c.is_active = 1 THEN c.capacity ELSE 0 END
   FROM classrooms c WHERE c.id = NEW.classroom_id)
BEGIN
  SELECT RAISE(ABORT, 'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER IF NOT EXISTS prevent_meeting_room_below_enrollment_count
BEFORE INSERT ON academic_section_meetings
WHEN NEW.classroom_id IS NOT NULL AND
  (SELECT COUNT(*) FROM enrollments e
   WHERE e.section_id = NEW.section_id AND e.status = 'ENROLLED'
     AND ((LOWER(NEW.activity_type) NOT LIKE '%lab%' AND LOWER(NEW.activity_type) NOT LIKE '%practical%')
          OR e.lab_meeting_id = NEW.id)) >
  (SELECT CASE WHEN c.capacity_known = 1 AND c.is_active = 1 THEN c.capacity ELSE 0 END
   FROM classrooms c WHERE c.id = NEW.classroom_id)
BEGIN
  SELECT RAISE(ABORT, 'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER IF NOT EXISTS prevent_meeting_room_update_below_enrollment_count
BEFORE UPDATE OF classroom_id ON academic_section_meetings
WHEN NEW.classroom_id IS NOT NULL AND
  (SELECT COUNT(*) FROM enrollments e
   WHERE e.section_id = NEW.section_id AND e.status = 'ENROLLED'
     AND ((LOWER(NEW.activity_type) NOT LIKE '%lab%' AND LOWER(NEW.activity_type) NOT LIKE '%practical%')
          OR e.lab_meeting_id = NEW.id)) >
  (SELECT CASE WHEN c.capacity_known = 1 AND c.is_active = 1 THEN c.capacity ELSE 0 END
   FROM classrooms c WHERE c.id = NEW.classroom_id)
BEGIN
  SELECT RAISE(ABORT, 'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER IF NOT EXISTS prevent_meeting_capacity_below_active_enrollments
BEFORE UPDATE OF enrollment_capacity ON academic_section_meetings
WHEN NEW.enrollment_capacity IS NOT NULL AND
  (SELECT COUNT(*) FROM enrollments e
   WHERE e.section_id = NEW.section_id AND e.status = 'ENROLLED'
     AND ((LOWER(NEW.activity_type) NOT LIKE '%lab%' AND LOWER(NEW.activity_type) NOT LIKE '%practical%')
          OR e.lab_meeting_id = NEW.id)) > NEW.enrollment_capacity
BEGIN
  SELECT RAISE(ABORT, 'active_enrollments_exceed_capacity');
END;
CREATE TRIGGER IF NOT EXISTS validate_enrollment_lab_choice
BEFORE INSERT ON enrollments
WHEN NEW.status = 'ENROLLED' AND (
  NOT EXISTS (SELECT 1 FROM academic_sections s WHERE s.id = NEW.section_id
              AND s.is_active = 1 AND s.is_open_for_enrollment = 1)
  OR (EXISTS (SELECT 1 FROM academic_section_meetings m WHERE m.section_id = NEW.section_id
              AND (LOWER(m.activity_type) LIKE '%lab%' OR LOWER(m.activity_type) LIKE '%practical%')
              AND NEW.lab_meeting_id IS NULL))
  OR (NEW.lab_meeting_id IS NOT NULL AND NOT EXISTS (
       SELECT 1 FROM academic_section_meetings m WHERE m.id = NEW.lab_meeting_id
       AND m.section_id = NEW.section_id
       AND (LOWER(m.activity_type) LIKE '%lab%' OR LOWER(m.activity_type) LIKE '%practical%')))
  OR (NEW.lab_meeting_id IS NULL AND EXISTS (
       SELECT 1 FROM academic_section_meetings m WHERE m.section_id = NEW.section_id
       AND (LOWER(m.activity_type) LIKE '%lab%' OR LOWER(m.activity_type) LIKE '%practical%')))
)
BEGIN
  SELECT RAISE(ABORT, 'invalid_lab_choice');
END;

CREATE TABLE IF NOT EXISTS organizations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  organization_type TEXT NOT NULL,
  priority_level INTEGER NOT NULL DEFAULT 1 CHECK(priority_level BETWEEN 1 AND 3),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS organization_members (
  organization_id INTEGER NOT NULL REFERENCES organizations(id) ON DELETE RESTRICT,
  student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
  member_role TEXT NOT NULL CHECK(member_role IN ('MEMBER', 'REPRESENTATIVE')),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
  joined_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY(organization_id, student_id)
);
CREATE INDEX IF NOT EXISTS idx_organization_members_student
  ON organization_members(student_id, is_active, member_role);
CREATE TABLE IF NOT EXISTS reservation_requests (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  organization_id INTEGER NOT NULL REFERENCES organizations(id) ON DELETE RESTRICT,
  requester_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
  classroom_id INTEGER NOT NULL REFERENCES classrooms(id) ON DELETE RESTRICT,
  reservation_date TEXT NOT NULL,
  start_time TEXT NOT NULL,
  end_time TEXT NOT NULL,
  purpose TEXT NOT NULL,
  priority_level INTEGER NOT NULL CHECK(priority_level BETWEEN 1 AND 3),
  priority_source TEXT NOT NULL,
  submitted_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  status TEXT NOT NULL CHECK(status IN ('PENDING', 'APPROVED', 'REJECTED', 'CANCELLED')),
  decided_by INTEGER REFERENCES students(id) ON DELETE RESTRICT,
  decided_at TEXT,
  rejection_reason TEXT NOT NULL DEFAULT '',
  CHECK(end_time > start_time)
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_duplicate_pending_request
  ON reservation_requests(organization_id, requester_id, classroom_id, reservation_date, start_time, end_time)
  WHERE status = 'PENDING';
CREATE INDEX IF NOT EXISTS idx_reservations_room_date_status
  ON reservation_requests(classroom_id, reservation_date, status, start_time, end_time);
CREATE INDEX IF NOT EXISTS idx_reservations_pending_priority
  ON reservation_requests(status, priority_level, submitted_at, id);
CREATE TABLE IF NOT EXISTS reservation_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  reservation_id INTEGER NOT NULL REFERENCES reservation_requests(id) ON DELETE RESTRICT,
  old_status TEXT,
  new_status TEXT NOT NULL,
  actor_id INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
  reason TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TRIGGER IF NOT EXISTS prevent_approved_reservation_overlap
BEFORE UPDATE OF status, reservation_date, start_time, end_time, classroom_id ON reservation_requests
WHEN NEW.status = 'APPROVED'
BEGIN
  SELECT CASE WHEN EXISTS (
    SELECT 1 FROM reservation_requests existing
    WHERE existing.classroom_id = NEW.classroom_id
      AND existing.reservation_date = NEW.reservation_date
      AND existing.status = 'APPROVED'
      AND existing.id <> NEW.id
      AND existing.start_time < NEW.end_time
      AND existing.end_time > NEW.start_time
  ) THEN RAISE(ABORT, 'reservation_overlap') END;
END;

CREATE TABLE IF NOT EXISTS navigation_locations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  location_key TEXT NOT NULL UNIQUE,
  location_type TEXT NOT NULL,
  label TEXT NOT NULL,
  building_id INTEGER REFERENCES buildings(id) ON DELETE RESTRICT,
  floor_id INTEGER,
  x REAL,
  y REAL,
  is_demo INTEGER NOT NULL DEFAULT 0 CHECK(is_demo IN (0, 1)),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
  CHECK((x IS NULL AND y IS NULL) OR (x IS NOT NULL AND y IS NOT NULL)),
  FOREIGN KEY(floor_id, building_id) REFERENCES floors(id, building_id) ON DELETE RESTRICT
);
CREATE TABLE IF NOT EXISTS navigation_edges (
  from_location_id INTEGER NOT NULL REFERENCES navigation_locations(id) ON DELETE RESTRICT,
  to_location_id INTEGER NOT NULL REFERENCES navigation_locations(id) ON DELETE RESTRICT,
  weight REAL NOT NULL CHECK(weight >= 0),
  edge_type TEXT NOT NULL,
  accessibility_status TEXT NOT NULL DEFAULT 'UNKNOWN'
    CHECK(accessibility_status IN ('UNKNOWN', 'UNVERIFIED', 'STEP_FREE', 'STAIRS')),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
  PRIMARY KEY(from_location_id, to_location_id),
  CHECK(from_location_id < to_location_id)
);

CREATE TABLE IF NOT EXISTS floor_map_metadata (
  floor_id INTEGER PRIMARY KEY,
  building_id INTEGER NOT NULL,
  coordinate_system TEXT NOT NULL DEFAULT 'LOCAL_FLOOR_X_RIGHT_Y_DOWN',
  unit_label TEXT NOT NULL DEFAULT 'local units',
  width REAL NOT NULL CHECK(width > 0),
  height REAL NOT NULL CHECK(height > 0),
  meters_per_unit REAL CHECK(meters_per_unit IS NULL OR meters_per_unit > 0),
  elevation REAL NOT NULL DEFAULT 0,
  floor_height REAL NOT NULL DEFAULT 3 CHECK(floor_height > 0),
  verification_status TEXT NOT NULL CHECK(verification_status IN ('DEMO', 'UNVERIFIED', 'VERIFIED')),
  source_reference TEXT NOT NULL DEFAULT '',
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY(floor_id, building_id) REFERENCES floors(id, building_id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS map_features (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  feature_key TEXT NOT NULL UNIQUE,
  building_id INTEGER NOT NULL,
  floor_id INTEGER NOT NULL,
  classroom_id INTEGER REFERENCES classrooms(id) ON DELETE RESTRICT,
  navigation_location_id INTEGER REFERENCES navigation_locations(id) ON DELETE RESTRICT,
  edge_from_id INTEGER,
  edge_to_id INTEGER,
  feature_type TEXT NOT NULL CHECK(feature_type IN ('ROOM', 'CORRIDOR', 'STAIRS', 'ELEVATOR', 'ENTRANCE', 'RAMP', 'WALL', 'OTHER')),
  label TEXT NOT NULL,
  geometry_type TEXT NOT NULL CHECK(geometry_type IN ('POLYGON', 'LINE', 'POINT')),
  geometry_json TEXT NOT NULL,
  elevation REAL NOT NULL DEFAULT 0,
  height REAL NOT NULL DEFAULT 0 CHECK(height >= 0),
  is_demo INTEGER NOT NULL DEFAULT 0 CHECK(is_demo IN (0, 1)),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
  CHECK((edge_from_id IS NULL AND edge_to_id IS NULL) OR
        (edge_from_id IS NOT NULL AND edge_to_id IS NOT NULL AND edge_from_id < edge_to_id)),
  FOREIGN KEY(floor_id, building_id) REFERENCES floors(id, building_id) ON DELETE CASCADE,
  FOREIGN KEY(edge_from_id, edge_to_id)
    REFERENCES navigation_edges(from_location_id, to_location_id) ON DELETE RESTRICT
);
CREATE INDEX IF NOT EXISTS idx_map_features_floor_active
  ON map_features(floor_id, is_active, feature_type);
CREATE INDEX IF NOT EXISTS idx_map_features_classroom
  ON map_features(classroom_id);
CREATE TRIGGER IF NOT EXISTS validate_map_feature_room_location
BEFORE INSERT ON map_features
WHEN NEW.classroom_id IS NOT NULL AND NOT EXISTS (
  SELECT 1 FROM classrooms c
  WHERE c.id = NEW.classroom_id AND c.floor_id = NEW.floor_id AND c.building_id = NEW.building_id
)
BEGIN
  SELECT RAISE(ABORT, 'map_room_location_mismatch');
END;
CREATE TRIGGER IF NOT EXISTS validate_map_feature_room_location_update
BEFORE UPDATE ON map_features
WHEN NEW.classroom_id IS NOT NULL AND NOT EXISTS (
  SELECT 1 FROM classrooms c
  WHERE c.id = NEW.classroom_id AND c.floor_id = NEW.floor_id AND c.building_id = NEW.building_id
)
BEGIN
  SELECT RAISE(ABORT, 'map_room_location_mismatch');
END;
CREATE TRIGGER IF NOT EXISTS validate_map_feature_nav_location
BEFORE INSERT ON map_features
WHEN NEW.navigation_location_id IS NOT NULL AND NOT EXISTS (
  SELECT 1 FROM navigation_locations n
  WHERE n.id = NEW.navigation_location_id AND n.floor_id = NEW.floor_id AND n.building_id = NEW.building_id
)
BEGIN
  SELECT RAISE(ABORT, 'map_nav_location_mismatch');
END;
CREATE TRIGGER IF NOT EXISTS validate_map_feature_nav_location_update
BEFORE UPDATE ON map_features
WHEN NEW.navigation_location_id IS NOT NULL AND NOT EXISTS (
  SELECT 1 FROM navigation_locations n
  WHERE n.id = NEW.navigation_location_id AND n.floor_id = NEW.floor_id AND n.building_id = NEW.building_id
)
BEGIN
  SELECT RAISE(ABORT, 'map_nav_location_mismatch');
END;
