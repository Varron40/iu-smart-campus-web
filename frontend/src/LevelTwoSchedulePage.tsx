import { useEffect, useMemo, useState } from "react";
import { getMySchedule, getSections, type AcademicSection } from "./services/api";

const groups = ["G1", "G2", "G3", "G4", "G5", "G6", "G7", "G8", "G9", "G10", "G12", "G13", "G14", "G16"];
const dayOrder = ["Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"];

export function LevelTwoSchedulePage({ isAdmin }: { isAdmin: boolean }) {
  const [sections, setSections] = useState<AcademicSection[]>([]);
  const [mySchedule, setMySchedule] = useState<Awaited<ReturnType<typeof getMySchedule>> | null>(null);
  const [group, setGroup] = useState("");
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let active = true;
    const timetable = getSections();
    const personal = isAdmin ? Promise.resolve(null) : getMySchedule();
    Promise.all([timetable, personal])
      .then(([result, schedule]) => {
        if (active) {
          setSections(result.sections.filter(section => groups.includes(section.sectionCode)));
          setMySchedule(schedule);
        }
      })
      .catch(caught => {
        if (active) setError(caught instanceof Error ? caught.message : "Unable to load your schedule.");
      })
      .finally(() => {
        if (active) setLoading(false);
      });
    return () => { active = false; };
  }, [isAdmin]);

  const meetings = useMemo(() => sections
    .filter(section => !group || section.sectionCode === group)
    .flatMap(section => section.meetings.map(meeting => ({ section, meeting })))
    .sort((left, right) => {
      const dayDifference = dayOrder.indexOf(left.meeting.day) - dayOrder.indexOf(right.meeting.day);
      if (dayDifference) return dayDifference;
      const timeDifference = left.meeting.startTime.localeCompare(right.meeting.startTime);
      if (timeDifference) return timeDifference;
      return left.section.sectionCode.localeCompare(right.section.sectionCode);
    }), [sections, group]);

  return <section className="management-page">
    <div className="management-heading">
      <div>
        <span className="eyebrow">{isAdmin ? "ACADEMIC TIMETABLE" : "PERSONAL TIMETABLE"}</span>
        <h2>My Schedule</h2>
        <p>{isAdmin ? "Review the shared Level 2 timetable and room assignments." : "Your enrolled courses and selected lab meetings, alongside the full Level 2 timetable."}</p>
      </div>
    </div>
    {!isAdmin && <section className="my-schedule-panel">
      <h3 className="feature-subheading">My enrolled schedule</h3>
      {mySchedule?.schedule.length ? <div className="schedule-table-wrap"><table className="schedule-table">
        <thead><tr><th>Day</th><th>Time</th><th>Course</th><th>Activity</th><th>Location</th></tr></thead>
        <tbody>{mySchedule.schedule.flatMap(item => item.meetings.map(meeting => ({ item, meeting })))
          .sort((left, right) => dayOrder.indexOf(left.meeting.day) - dayOrder.indexOf(right.meeting.day) ||
            left.meeting.startTime.localeCompare(right.meeting.startTime))
          .map(({ item, meeting }, index) => <tr key={`${item.enrollmentId}-${meeting.id}-${index}`}>
            <td>{meeting.day}</td><td>{meeting.startTime}–{meeting.endTime}</td>
            <td><span className="record-code">{item.courseCode} · {item.sectionCode}</span><span className="schedule-course-name">{item.courseName}</span></td>
            <td>{meeting.activityType}</td><td>{meeting.venueLabel || meeting.classroomCode || "Location not specified"}</td>
          </tr>)}</tbody>
      </table></div> : <div className="empty-panel">No enrolled courses yet. Enroll in an open section to build your schedule.</div>}
    </section>}
    <h3 className="feature-subheading">Shared timetable · Computer Science Level 2</h3>
    <div className="filter-bar">
      <select aria-label="Filter timetable by group" value={group} onChange={event => setGroup(event.target.value)}>
        <option value="">All groups</option>{groups.map(item => <option key={item}>{item}</option>)}
      </select>
    </div>
    <div className="schedule-source-note">Filter by group to see the shared weekly timetable. Students who enroll in a course with multiple lab groups see only their chosen lab in My enrolled schedule.</div>
    {error && <div className="error-message">{error}</div>}
    {loading ? <div className="empty-panel">Loading timetable…</div> : meetings.length ? <div className="schedule-table-wrap">
      <table className="schedule-table">
        <thead><tr>
          <th scope="col">Group</th>
          <th scope="col">Day</th>
          <th scope="col">Time</th>
          <th scope="col">Course</th>
          <th scope="col">Activity</th>
          <th scope="col">Venue label</th>
          <th scope="col">Instructor / TA</th>
        </tr></thead>
        <tbody>{meetings.map(({ section, meeting }) => <tr key={meeting.id}>
          <td>{section.sectionCode}</td>
          <td>{meeting.day}</td>
          <td>{meeting.startTime}–{meeting.endTime}</td>
          <td><span className="record-code">{section.courseCode}</span><span className="schedule-course-name">{section.courseName}</span></td>
          <td>{meeting.activityType}</td>
          <td>{meeting.venueLabel || meeting.classroomCode || "Location not specified"}</td>
          <td>{meeting.instructor || "Not specified"}</td>
        </tr>)}</tbody>
      </table>
    </div> : <div className="empty-panel">No timetable meetings match this group.</div>}
  </section>;
}
