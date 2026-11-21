import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import {
  findNavigationRoute, getClassrooms, getMapBuildings, getMapFloorFeatures, getMapFloors,
  getMapRoomDetails, saveFloorMapMetadata, saveMapFeature, setMapFeatureActive,
  type Classroom, type MapFeature, type MapFloor, type MapFloorData, type MapRoomDetails,
  type MapPoint, type RouteResponse
} from "./services/api";

type Props = { isAdmin: boolean };
type FeatureDraft = {
  id?: number; key: string; label: string; type: MapFeature["type"];
  geometryType: MapFeature["geometryType"]; points: string;
  classroomId: string; navigationLocationId: string; edgeFromId: string; edgeToId: string;
  elevation: string; height: string; thickness: string; isDemo: boolean;
};
type ViewBox = { x: number; y: number; width: number; height: number };

function messageOf(error: unknown, fallback: string) {
  return error instanceof Error ? error.message : fallback;
}

function edgeKey(from: number, to: number) {
  return from < to ? `${from}:${to}` : `${to}:${from}`;
}

function featureEdgeKey(feature: MapFeature): string | null {
  return feature.edgeFromId == null || feature.edgeToId == null
    ? null : edgeKey(feature.edgeFromId, feature.edgeToId);
}

function isMapPointArray(value: unknown): value is MapPoint[] {
  return Array.isArray(value) && value.every(point =>
    point && typeof point.x === "number" && typeof point.y === "number");
}

function draftBounds(draft: FeatureDraft) {
  try {
    const points: unknown = JSON.parse(draft.points);
    if (!isMapPointArray(points) || !points.length) return null;
    const xs = points.map(point => point.x);
    const ys = points.map(point => point.y);
    const x = Math.min(...xs);
    const y = Math.min(...ys);
    return { x, y, width: Math.max(...xs) - x, height: Math.max(...ys) - y };
  } catch {
    return null;
  }
}

function setDraftBounds(draft: FeatureDraft, field: "x" | "y" | "width" | "height", value: number): FeatureDraft {
  const bounds = draftBounds(draft);
  if (!bounds) return draft;
  const next = { ...bounds, [field]: value };
  if (next.width < 0) next.width = 0;
  if (next.height < 0) next.height = 0;
  return {
    ...draft,
    points: JSON.stringify([
      { x: next.x, y: next.y },
      { x: next.x + next.width, y: next.y },
      { x: next.x + next.width, y: next.y + next.height },
      { x: next.x, y: next.y + next.height }
    ], null, 2)
  };
}

function draftLine(draft: FeatureDraft) {
  try {
    const points: unknown = JSON.parse(draft.points);
    if (!isMapPointArray(points) || points.length < 2) return null;
    return { start: points[0], end: points[points.length - 1] };
  } catch {
    return null;
  }
}

function setDraftLine(draft: FeatureDraft, point: "start" | "end", axis: "x" | "y", value: number): FeatureDraft {
  const line = draftLine(draft);
  if (!line) return draft;
  const start = point === "start" ? { ...line.start, [axis]: value } : line.start;
  const end = point === "end" ? { ...line.end, [axis]: value } : line.end;
  return { ...draft, points: JSON.stringify([start, end], null, 2) };
}

function draftFromFeature(feature?: MapFeature): FeatureDraft {
  return {
    id: feature?.id, key: feature?.key ?? "", label: feature?.label ?? "",
    type: feature?.type ?? "OTHER", geometryType: feature?.geometryType ?? "POLYGON",
    points: JSON.stringify(feature?.points ?? [], null, 2),
    classroomId: feature?.classroomId == null ? "" : String(feature.classroomId),
    navigationLocationId: feature?.navigationLocationId == null ? "" : String(feature.navigationLocationId),
    edgeFromId: feature?.edgeFromId == null ? "" : String(feature.edgeFromId),
    edgeToId: feature?.edgeToId == null ? "" : String(feature.edgeToId),
    elevation: String(feature?.elevation ?? 0), height: String(feature?.height ?? 0),
    thickness: String(feature?.thickness ?? 0),
    isDemo: feature?.isDemo ?? true
  };
}

export function CampusMapPage({ isAdmin }: Props) {
  const [buildings, setBuildings] = useState<Array<{ id: number; name: string; code: string }>>([]);
  const [buildingId, setBuildingId] = useState(0);
  const [floors, setFloors] = useState<MapFloor[]>([]);
  const [floorId, setFloorId] = useState(0);
  const [data, setData] = useState<MapFloorData | null>(null);
  const [rooms, setRooms] = useState<Classroom[]>([]);
  const [roomDetails, setRoomDetails] = useState<MapRoomDetails | null>(null);
  const [selectedFeatureId, setSelectedFeatureId] = useState<number | null>(null);
  const [selectedRoomId, setSelectedRoomId] = useState<number | null>(null);
  const [viewMode, setViewMode] = useState<"2D" | "3D">("2D");
  const [fromId, setFromId] = useState<number | "">("");
  const [toId, setToId] = useState<number | "">("");
  const [route, setRoute] = useState<RouteResponse | null>(null);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [metadataForm, setMetadataForm] = useState({
    width: "100", height: "70", unitLabel: "local units", metersPerUnit: "",
    elevation: "0", floorHeight: "3", verificationStatus: "UNVERIFIED" as "DEMO" | "UNVERIFIED" | "VERIFIED",
    sourceReference: ""
  });
  const [featureDraft, setFeatureDraft] = useState<FeatureDraft | null>(null);

  useEffect(() => {
    let active = true;
    getMapBuildings().then(result => {
      if (!active) return;
      setBuildings(result.buildings);
      const defaultBuilding = result.buildings.find(building => building.code === "B") ?? result.buildings[0];
      setBuildingId(defaultBuilding?.id ?? 0);
    }).catch(caught => {
      if (active) setError(messageOf(caught, "Unable to load campus buildings."));
    });
    return () => { active = false; };
  }, []);

  useEffect(() => {
    let active = true;
    if (!buildingId) { setFloors([]); setFloorId(0); return; }
    setLoading(true);
    getMapFloors(buildingId).then(result => {
      if (!active) return;
      setFloors(result.floors);
      const defaultFloor = result.floors.find(floor => floor.label === "0" && floor.hasMap) ?? result.floors[0];
      setFloorId(current => result.floors.some(item => item.id === current) ? current : defaultFloor?.id ?? 0);
    }).catch(caught => {
      if (active) setError(messageOf(caught, "Unable to load building floors."));
    }).finally(() => { if (active) setLoading(false); });
    return () => { active = false; };
  }, [buildingId]);

  useEffect(() => {
    let active = true;
    if (!floorId) { setData(null); setRooms([]); return; }
    setLoading(true);
    setError("");
    Promise.all([getMapFloorFeatures(floorId, isAdmin), getClassrooms({ floorId })]).then(([mapData, roomData]) => {
      if (!active) return;
      setData(mapData);
      setRooms(roomData.rooms);
      setSelectedFeatureId(null);
      setSelectedRoomId(null);
      setRoomDetails(null);
      setRoute(null);
      setFeatureDraft(null);
      setNotice("");
    }).catch(caught => {
      if (active) setError(messageOf(caught, "Unable to load floor map data."));
    }).finally(() => { if (active) setLoading(false); });
    return () => { active = false; };
  }, [floorId, isAdmin]);

  useEffect(() => {
    if (!data) return;
    const floor = data.floor;
    setMetadataForm({
      width: String(floor.width ?? 100), height: String(floor.height ?? 70),
      unitLabel: floor.unitLabel || "local units", metersPerUnit: floor.metersPerUnit == null ? "" : String(floor.metersPerUnit),
      elevation: String(floor.elevation ?? 0), floorHeight: String(floor.floorHeight ?? 3),
      verificationStatus: floor.verificationStatus === "DEMO" ? "DEMO"
        : floor.verificationStatus === "VERIFIED" ? "VERIFIED" : "UNVERIFIED",
      sourceReference: floor.sourceReference
    });
  }, [data]);

  useEffect(() => {
    if (!selectedRoomId) { setRoomDetails(null); return; }
    let active = true;
    getMapRoomDetails(selectedRoomId).then(result => { if (active) setRoomDetails(result.room); })
      .catch(caught => { if (active) setError(messageOf(caught, "Unable to load room details.")); });
    return () => { active = false; };
  }, [selectedRoomId]);

  const locations = useMemo(() => (data?.navigationLocations ?? [])
    .filter(item => item.isActive && item.coordinates), [data]);
  const selectedFeature = data?.features.find(item => item.id === selectedFeatureId) ?? null;
  const mappedRoomIds = useMemo(() => new Set((data?.features ?? [])
    .filter(feature => feature.isActive && feature.type === "ROOM" && feature.classroomId != null)
    .map(feature => feature.classroomId as number)), [data]);
  const unmappedRooms = rooms.filter(room => !mappedRoomIds.has(room.id));
  const routeEdges = useMemo(() => new Set((route?.edges ?? [])
    .map(edge => edgeKey(edge.fromId, edge.toId))), [route]);
  const mappedRouteEdgeCount = new Set((data?.features ?? [])
    .filter(feature => feature.isActive)
    .map(featureEdgeKey)
    .filter((key): key is string => key !== null && routeEdges.has(key))).size;

  const selectFeature = useCallback((feature: MapFeature) => {
    setSelectedFeatureId(feature.id);
    setSelectedRoomId(feature.classroomId ?? null);
    setFeatureDraft(isAdmin ? draftFromFeature(feature) : null);
  }, [isAdmin]);

  async function submitRoute(event: React.FormEvent) {
    event.preventDefault();
    if (fromId === "" || toId === "") return;
    setBusy(true);
    setError("");
    setRoute(null);
    try { setRoute(await findNavigationRoute(fromId, toId)); }
    catch (caught) { setError(messageOf(caught, "No route is available for those locations.")); }
    finally { setBusy(false); }
  }

  async function saveMetadata(event: React.FormEvent) {
    event.preventDefault();
    if (!data) return;
    setBusy(true); setError(""); setNotice("");
    try {
      await saveFloorMapMetadata({
        buildingId: data.floor.buildingId, width: Number(metadataForm.width), height: Number(metadataForm.height),
        coordinateSystem: "LOCAL_FLOOR_X_RIGHT_Y_DOWN", unitLabel: metadataForm.unitLabel,
        metersPerUnit: metadataForm.metersPerUnit.trim() ? Number(metadataForm.metersPerUnit) : null,
        elevation: Number(metadataForm.elevation), floorHeight: Number(metadataForm.floorHeight),
        verificationStatus: metadataForm.verificationStatus, sourceReference: metadataForm.sourceReference
      }, data.floor.id);
      const refreshed = await getMapFloorFeatures(data.floor.id, isAdmin);
      setData(refreshed);
      setNotice("Floor map metadata saved.");
    } catch (caught) { setError(messageOf(caught, "Unable to save floor map metadata.")); }
    finally { setBusy(false); }
  }

  async function saveFeatureDraft(event: React.FormEvent) {
    event.preventDefault();
    if (!data || !featureDraft) return;
    setBusy(true); setError(""); setNotice("");
    try {
      const parsedPoints: unknown = JSON.parse(featureDraft.points);
      if (!isMapPointArray(parsedPoints)) {
        throw new Error("Geometry must be a JSON array of {\"x\": number, \"y\": number} points.");
      }
      await saveMapFeature({
        key: featureDraft.key, label: featureDraft.label, type: featureDraft.type,
        geometryType: featureDraft.geometryType, points: parsedPoints,
        classroomId: featureDraft.classroomId ? Number(featureDraft.classroomId) : null,
        navigationLocationId: featureDraft.navigationLocationId ? Number(featureDraft.navigationLocationId) : null,
        edgeFromId: featureDraft.edgeFromId ? Number(featureDraft.edgeFromId) : null,
        edgeToId: featureDraft.edgeToId ? Number(featureDraft.edgeToId) : null,
        elevation: Number(featureDraft.elevation), height: Number(featureDraft.height),
        thickness: Number(featureDraft.thickness), isDemo: featureDraft.isDemo
      }, data.floor.id, featureDraft.id);
      setData(await getMapFloorFeatures(data.floor.id, isAdmin));
      setNotice("Map feature saved.");
    } catch (caught) { setError(messageOf(caught, "Unable to save map feature.")); }
    finally { setBusy(false); }
  }

  async function changeFeatureActive(feature: MapFeature) {
    setBusy(true); setError(""); setNotice("");
    try {
      await setMapFeatureActive(feature.id, !feature.isActive);
      setData(await getMapFloorFeatures(feature.floorId, isAdmin));
      setNotice(feature.isActive ? "Map feature deactivated." : "Map feature reactivated.");
    } catch (caught) { setError(messageOf(caught, "Unable to update map feature status.")); }
    finally { setBusy(false); }
  }

  const simpleBounds = featureDraft?.geometryType === "POLYGON" ? draftBounds(featureDraft) : null;
  const simpleLine = featureDraft?.geometryType === "LINE" ? draftLine(featureDraft) : null;

  return <section className="management-page campus-map-page">
    <div className="management-heading">
      <div><span className="eyebrow">MODULE 6 · INTERACTIVE CAMPUS MAP</span><h2>Campus map</h2>
        <p>Explore floor features, room information, and backend-computed routes. Geometry is not assumed to be surveyed.</p></div>
    </div>
    {error && <p className="error-message" role="alert">{error}</p>}
    {notice && <p className="success-message" role="status">{notice}</p>}
    <div className="map-selectors">
      <label>Building<select value={buildingId} onChange={event => setBuildingId(Number(event.target.value))}>
        {buildings.map(building => <option key={building.id} value={building.id}>{building.name}</option>)}
      </select></label>
      <label>Floor<select value={floorId} onChange={event => setFloorId(Number(event.target.value))}>
        {floors.map(floor => <option key={floor.id} value={floor.id}>{floor.label}</option>)}
      </select></label>
      <div className="map-mode-toggle" role="group" aria-label="Map view">
        <button className={viewMode === "2D" ? "selected" : ""} onClick={() => setViewMode("2D")}>2D plan</button>
        <button className={viewMode === "3D" ? "selected" : ""} onClick={() => setViewMode("3D")}>3D view</button>
      </div>
    </div>
    {loading ? <div className="empty-panel">Loading map data…</div> : !data ? <div className="empty-panel">No campus floors are available.</div> : <>
      <div className={`map-trust-banner ${data.floor.verificationStatus.toLowerCase()}`}>
        <b>{data.floor.verificationStatus === "NO_MAP" ? "No floor plan available" : `${data.floor.verificationStatus} floor data`}</b>
        <span>{data.floor.verificationStatus === "VERIFIED"
          ? `Source: ${data.floor.sourceReference}`
          : `${data.floor.sourceReference ? `Source: ${data.floor.sourceReference}. ` : ""}This geometry is illustrative or unverified. Do not use it as an official campus plan or real-world direction.`}</span>
        {data.floor.hasMap && <small>Coordinates: {data.floor.coordinateSystem} · {data.floor.unitLabel}
          {data.floor.metersPerUnit == null ? " · scale unknown" : ` · ${data.floor.metersPerUnit} m per unit`}</small>}
      </div>
      {data.floor.hasMap ? <div className="map-workspace">
        <div className="map-visual-panel">
          {viewMode === "2D"
                ? <Map2D data={data} routeEdges={routeEdges} selectedFeatureId={selectedFeatureId} isAdmin={isAdmin}
                    onSelect={selectFeature} onGeometryChange={(featureId, points) => {
                      const feature = data.features.find(item => item.id === featureId);
                      if (feature) setFeatureDraft({ ...draftFromFeature(feature), points: JSON.stringify(points, null, 2) });
                      setData(current => current ? {
                        ...current,
                        features: current.features.map(item => item.id === featureId ? { ...item, points } : item)
                      } : current);
                    }} onCreateFeature={(type, geometryType, points) => {
                      const label = type === "WALL" ? "New wall"
                        : type === "ENTRANCE" ? "New door or entrance"
                        : type === "ROOM" ? "New room"
                        : "New map feature";
                      const draft: FeatureDraft = {
                        key: `map-${type.toLowerCase()}-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
                        label,
                        type,
                        geometryType,
                        points: JSON.stringify(points, null, 2),
                        classroomId: "",
                        navigationLocationId: "",
                        edgeFromId: "",
                        edgeToId: "",
                        elevation: "0",
                        height: type === "WALL" ? "8" : "1",
                        thickness: type === "WALL" ? "3" : "0",
                        isDemo: false
                      };
                      if (type === "ROOM") {
                        setFeatureDraft(draft);
                        setNotice("Room drawn. Link a classroom, then press Save feature.");
                        return;
                      }
                      setBusy(true);
                      setError("");
                      saveMapFeature({
                        key: draft.key,
                        label: draft.label,
                        type: draft.type,
                        geometryType: draft.geometryType,
                        points,
                        elevation: 0,
                        height: Number(draft.height),
                        thickness: Number(draft.thickness),
                        isDemo: false
                      }, data.floor.id).then(async () => {
                        setData(await getMapFloorFeatures(data.floor.id, isAdmin));
                        setNotice(type === "ENTRANCE" ? "Door or entrance saved." : "Wall saved.");
                      }).catch(caught => {
                        setError(messageOf(caught, "Unable to save the drawn map feature."));
                      }).finally(() => setBusy(false));
                    }} />
            : <Map3D data={data} routeEdges={routeEdges} selectedFeatureId={selectedFeatureId} onSelect={selectFeature} />}
          <div className="map-legend"><span><i className="legend-room"/> Room</span><span><i className="legend-corridor"/> Corridor/wall</span><span><i className="legend-door"/> Door</span><span><i className="legend-route"/> Backend route</span></div>
        </div>
        <aside className="map-side-panel">
          {locations.length >= 2 ? <form className="map-route-form" onSubmit={submitRoute}>
            <h3>Find a route</h3>
            <label>From<select required value={fromId} onChange={event => setFromId(event.target.value === "" ? "" : Number(event.target.value))}>
              <option value="">Choose start</option>{locations.map(location => <option key={location.id} value={location.id}>{location.label}{location.isDemo ? " · DEMO" : ""}</option>)}
            </select></label>
            <label>To<select required value={toId} onChange={event => setToId(event.target.value === "" ? "" : Number(event.target.value))}>
              <option value="">Choose destination</option>{locations.map(location => <option key={location.id} value={location.id}>{location.label}{location.isDemo ? " · DEMO" : ""}</option>)}
            </select></label>
            <button className="primary-button" disabled={busy || fromId === "" || toId === ""}>{busy ? "Finding route…" : "Find route"}</button>
          </form> : <div className="map-route-form"><h3>Find a route</h3>
            <p className="demo-label">Route navigation will be available after walkable paths and entrances are confirmed for this floor.</p>
          </div>}
          {route && <div className="map-route-result">
            <b>{route.totalCost ?? route.distance} {route.costUnit ?? "distance units"}</b>
            <div className="route-steps">{(route.locations ?? []).map((location, index) =>
              <span key={location.id}>{index > 0 && <i>→</i>}{location.label}</span>)}</div>
            {route.warnings?.map(warning => <small className="demo-label" key={warning}>{warning}</small>)}
            {route.edges?.length ? <small>{mappedRouteEdgeCount === route.edges.length
              ? "All route segments have matching floor geometry."
              : "Some route segments have no mapped geometry; directions remain available as text."}</small> : null}
          </div>}
          {roomDetails ? <div className="map-room-details">
            <span className="eyebrow">ROOM DETAILS</span><h3>{roomDetails.roomName} · {roomDetails.roomCode}</h3>
            <p>{roomDetails.buildingName} · {roomDetails.floorLabel} · {roomDetails.roomType}</p>
            <div className="record-meta"><span>Capacity</span><b>{roomDetails.capacityKnown ? roomDetails.capacity : "Not verified"}</b></div>
            {roomDetails.seatAvailabilityAvailable ? <div className="room-section-list">
              <b>Active section seat summaries</b>{roomDetails.sections.map(section => <div key={section.id}>
                <span>{section.courseCode} · {section.sectionCode} · {section.term}</span>
                <small>{section.enrolledCount} enrolled · {section.remainingSeats} seats remaining</small>
              </div>)}
            </div> : <p className="demo-label">No active assigned sections; room-wide occupancy is not available.</p>}
          </div> : <div className="map-selection-hint">Select a mapped room or an unmapped room below to see its details.</div>}
          <div className="unmapped-rooms"><h3>Rooms without map position · {unmappedRooms.length}</h3>
            {unmappedRooms.length ? unmappedRooms.map(room => <button key={room.id}
              className={selectedRoomId === room.id ? "selected" : ""}
              onClick={() => { setSelectedRoomId(room.id); setSelectedFeatureId(null); setFeatureDraft(null); }}>
              <b>{room.roomCode} · {room.roomName}</b><span>Not placed on this floor plan</span>
            </button>) : <p>All active rooms on this floor have mapped shapes.</p>}
          </div>
        </aside>
      </div> : <div className="empty-panel">This floor has no mapped geometry yet. An administrator can add floor metadata and map features.</div>}
      {isAdmin && <div className="map-admin-editors">
        <details><summary>Administrator · floor metadata</summary>
          <form className="management-form" onSubmit={saveMetadata}>
            <label>Width<input required type="number" min="0.01" step="any" value={metadataForm.width} onChange={event => setMetadataForm({ ...metadataForm, width: event.target.value })}/></label>
            <label>Height<input required type="number" min="0.01" step="any" value={metadataForm.height} onChange={event => setMetadataForm({ ...metadataForm, height: event.target.value })}/></label>
            <label>Coordinate system<input value="LOCAL_FLOOR_X_RIGHT_Y_DOWN" readOnly/></label>
            <label>Unit label<input required maxLength={32} value={metadataForm.unitLabel} onChange={event => setMetadataForm({ ...metadataForm, unitLabel: event.target.value })}/></label>
            <label>Meters per unit (optional)<input type="number" min="0.000001" step="any" value={metadataForm.metersPerUnit} onChange={event => setMetadataForm({ ...metadataForm, metersPerUnit: event.target.value })}/></label>
            <label>Elevation<input type="number" step="any" value={metadataForm.elevation} onChange={event => setMetadataForm({ ...metadataForm, elevation: event.target.value })}/></label>
            <label>Floor height<input required type="number" min="0.01" step="any" value={metadataForm.floorHeight} onChange={event => setMetadataForm({ ...metadataForm, floorHeight: event.target.value })}/></label>
            <label>Verification status<select value={metadataForm.verificationStatus} onChange={event => setMetadataForm({ ...metadataForm, verificationStatus: event.target.value as typeof metadataForm.verificationStatus })}>
              <option value="DEMO">DEMO</option><option value="UNVERIFIED">UNVERIFIED</option><option value="VERIFIED">VERIFIED</option>
            </select></label>
            <label className="wide-field">Source reference<input maxLength={500} value={metadataForm.sourceReference} onChange={event => setMetadataForm({ ...metadataForm, sourceReference: event.target.value })}/></label>
            <p className="wide-field demo-label">VERIFIED is rejected while active synthetic features or navigation locations remain. Never cite a source you have not checked.</p>
            <button className="primary-button wide-field" disabled={busy}>Save floor metadata</button>
          </form>
        </details>
        <details><summary>Administrator · map feature editor</summary>
          <button className="secondary-button" onClick={() => { setSelectedFeatureId(null); setFeatureDraft(draftFromFeature()); }}>Add feature</button>
          <label className="map-feature-picker">Edit existing feature<select value={selectedFeatureId ?? ""}
            onChange={event => {
              const feature = data.features.find(item => item.id === Number(event.target.value));
              setSelectedFeatureId(feature?.id ?? null);
              setFeatureDraft(feature ? draftFromFeature(feature) : null);
            }}>
            <option value="">Choose a feature</option>{data.features.map(feature =>
              <option key={feature.id} value={feature.id}>{feature.label} · {feature.isActive ? "active" : "inactive"}</option>)}
          </select></label>
          {featureDraft && <form className="management-form map-feature-form" onSubmit={saveFeatureDraft}>
            <h3>{featureDraft.id ? `Edit feature #${featureDraft.id}` : "Add a feature"}</h3>
            <label>Stable key<input required maxLength={100} value={featureDraft.key} onChange={event => setFeatureDraft({ ...featureDraft, key: event.target.value })}/></label>
            <label>Label<input required maxLength={200} value={featureDraft.label} onChange={event => setFeatureDraft({ ...featureDraft, label: event.target.value })}/></label>
            <label>Feature type<select value={featureDraft.type} onChange={event => setFeatureDraft({ ...featureDraft, type: event.target.value as MapFeature["type"] })}>
              {["ROOM","CORRIDOR","STAIRS","ELEVATOR","ENTRANCE","RAMP","WALL","OTHER"].map(type => <option key={type}>{type}</option>)}
            </select></label>
            <label>Geometry type<select value={featureDraft.geometryType} onChange={event => setFeatureDraft({ ...featureDraft, geometryType: event.target.value as MapFeature["geometryType"] })}>
              {["POLYGON","LINE","POINT"].map(type => <option key={type}>{type}</option>)}
            </select></label>
            {simpleBounds && <div className="map-simple-size-editor wide-field">
              <b>Simple size and position</b>
              <div className="map-simple-grid">
                <label>X<input type="number" step="any" value={Number(simpleBounds.x.toFixed(2))} onChange={event => setFeatureDraft(setDraftBounds(featureDraft, "x", Number(event.target.value)))} /></label>
                <label>Y<input type="number" step="any" value={Number(simpleBounds.y.toFixed(2))} onChange={event => setFeatureDraft(setDraftBounds(featureDraft, "y", Number(event.target.value)))} /></label>
                <label>Width<input type="number" min="0" step="any" value={Number(simpleBounds.width.toFixed(2))} onChange={event => setFeatureDraft(setDraftBounds(featureDraft, "width", Number(event.target.value)))} /></label>
                <label>Height/length<input type="number" min="0" step="any" value={Number(simpleBounds.height.toFixed(2))} onChange={event => setFeatureDraft(setDraftBounds(featureDraft, "height", Number(event.target.value)))} /></label>
              </div>
            </div>}
            {simpleLine && <div className="map-simple-size-editor wide-field">
              <b>Simple wall or line endpoints</b>
              <div className="map-simple-grid">
                <label>Start X<input type="number" step="any" value={Number(simpleLine.start.x.toFixed(2))} onChange={event => setFeatureDraft(setDraftLine(featureDraft, "start", "x", Number(event.target.value)))} /></label>
                <label>Start Y<input type="number" step="any" value={Number(simpleLine.start.y.toFixed(2))} onChange={event => setFeatureDraft(setDraftLine(featureDraft, "start", "y", Number(event.target.value)))} /></label>
                <label>End X<input type="number" step="any" value={Number(simpleLine.end.x.toFixed(2))} onChange={event => setFeatureDraft(setDraftLine(featureDraft, "end", "x", Number(event.target.value)))} /></label>
                <label>End Y<input type="number" step="any" value={Number(simpleLine.end.y.toFixed(2))} onChange={event => setFeatureDraft(setDraftLine(featureDraft, "end", "y", Number(event.target.value)))} /></label>
              </div>
            </div>}
            <label className="wide-field">Geometry points (JSON)<textarea required rows={6} value={featureDraft.points} onChange={event => setFeatureDraft({ ...featureDraft, points: event.target.value })}/></label>
            <label>Classroom ID (optional)<input type="number" min="1" value={featureDraft.classroomId} onChange={event => setFeatureDraft({ ...featureDraft, classroomId: event.target.value })}/></label>
            <label>Navigation location ID (optional)<input type="number" min="0" value={featureDraft.navigationLocationId} onChange={event => setFeatureDraft({ ...featureDraft, navigationLocationId: event.target.value })}/></label>
            <label>Graph edge from ID<input type="number" min="0" value={featureDraft.edgeFromId} onChange={event => setFeatureDraft({ ...featureDraft, edgeFromId: event.target.value })}/></label>
            <label>Graph edge to ID<input type="number" min="0" value={featureDraft.edgeToId} onChange={event => setFeatureDraft({ ...featureDraft, edgeToId: event.target.value })}/></label>
            <label>Elevation<input type="number" step="any" value={featureDraft.elevation} onChange={event => setFeatureDraft({ ...featureDraft, elevation: event.target.value })}/></label>
            <label>{featureDraft.type === "WALL" ? "Wall height" : "Extrusion height"}<input type="number" min="0" step="any" value={featureDraft.height} onChange={event => setFeatureDraft({ ...featureDraft, height: event.target.value })}/></label>
            {featureDraft.type === "WALL" && <label>Wall thickness<input type="number" min="0" step="any" value={featureDraft.thickness} onChange={event => setFeatureDraft({ ...featureDraft, thickness: event.target.value })}/></label>}
            <label className="checkbox-filter"><input type="checkbox" checked={featureDraft.isDemo} onChange={event => setFeatureDraft({ ...featureDraft, isDemo: event.target.checked })}/> Synthetic/demo geometry</label>
            <div className="form-actions"><button type="button" className="secondary-button" onClick={() => setFeatureDraft(null)}>Cancel</button>
              {featureDraft.id && selectedFeature && <button type="button" className="secondary-button" disabled={busy} onClick={() => void changeFeatureActive(selectedFeature)}>{selectedFeature.isActive ? "Deactivate feature" : "Reactivate feature"}</button>}
              <button className="primary-button" disabled={busy}>Save feature</button>
            </div>
          </form>}
        </details>
      </div>}
    </>}
  </section>;
}

function DoorSymbol({ points }: { points: MapPoint[] }) {
  if (points.length < 2) return null;
  const start = points[0];
  const end = points[points.length - 1];
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  const length = Math.hypot(dx, dy);
  if (!length) return null;
  const perpendicular = { x: -dy / length, y: dx / length };
  const hinge = Math.abs(dx) >= Math.abs(dy) ? start : start;
  const leafEnd = {
    x: hinge.x + perpendicular.x * length,
    y: hinge.y + perpendicular.y * length
  };
  const oppositeLeafEnd = {
    x: end.x + perpendicular.x * length,
    y: end.y + perpendicular.y * length
  };
  return <g className="map-door-symbol" pointerEvents="none">
    <line x1={start.x} y1={start.y} x2={end.x} y2={end.y} className="map-door-opening"/>
    <line x1={hinge.x} y1={hinge.y} x2={leafEnd.x} y2={leafEnd.y} className="map-door-leaf"/>
    <path d={`M ${end.x} ${end.y} A ${length} ${length} 0 0 1 ${oppositeLeafEnd.x} ${oppositeLeafEnd.y}`}
      className="map-door-swing"/>
  </g>;
}

function Map2D({ data, routeEdges, selectedFeatureId, isAdmin, onSelect, onGeometryChange, onCreateFeature }: {
  data: MapFloorData; routeEdges: Set<string>; selectedFeatureId: number | null; isAdmin: boolean;
  onSelect: (feature: MapFeature) => void; onGeometryChange: (featureId: number, points: MapPoint[]) => void;
  onCreateFeature: (type: MapFeature["type"], geometryType: MapFeature["geometryType"], points: MapPoint[]) => void;
}) {
  const floor = data.floor;
  const isCsGroundFloor = floor.sourceReference.startsWith("User-provided CS Building Floor 0 plan");
  const width = floor.width ?? 100;
  const height = floor.height ?? 70;
  const [viewBox, setViewBox] = useState<ViewBox>({ x: 0, y: 0, width, height });
  const dragStart = useRef<{ x: number; y: number; originX: number; originY: number } | null>(null);
  const captureTarget = useRef<SVGElement | null>(null);
  const geometryDrag = useRef<{
    featureId: number; mode: "move" | "vertex"; pointIndex?: number;
    start: MapPoint; originalPoints: MapPoint[];
  } | null>(null);
  const [drawTool, setDrawTool] = useState<"SELECT" | "ROOM" | "WALL" | "DOOR">("SELECT");
  const drawingStart = useRef<MapPoint | null>(null);
  const [drawingPoints, setDrawingPoints] = useState<MapPoint[]>([]);
  const drawingPointsRef = useRef<MapPoint[]>([]);
  const drawingGestureActive = useRef(false);
  useEffect(() => setViewBox({ x: 0, y: 0, width, height }), [width, height, floor.id]);

  function zoom(factor: number, x = viewBox.x + viewBox.width / 2, y = viewBox.y + viewBox.height / 2) {
    const nextWidth = Math.max(width / 8, Math.min(width * 3, viewBox.width * factor));
    const nextHeight = Math.max(height / 8, Math.min(height * 3, viewBox.height * factor));
    const ratio = nextWidth / viewBox.width;
    setViewBox({ x: x - (x - viewBox.x) * ratio, y: y - (y - viewBox.y) * ratio, width: nextWidth, height: nextHeight });
  }

  function pointerToMapPoint(event: React.PointerEvent<SVGSVGElement | SVGElement>): MapPoint {
    const svg = event.currentTarget instanceof SVGSVGElement
      ? event.currentTarget
      : event.currentTarget.ownerSVGElement;
    if (!svg) return { x: 0, y: 0 };
    const bounds = svg.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(width, viewBox.x + ((event.clientX - bounds.left) / bounds.width) * viewBox.width)),
      y: Math.max(0, Math.min(height, viewBox.y + ((event.clientY - bounds.top) / bounds.height) * viewBox.height))
    };
  }

  function beginGeometryDrag(
    event: React.PointerEvent<SVGElement>,
    feature: MapFeature,
    mode: "move" | "vertex",
    pointIndex?: number
  ) {
    if (!isAdmin) return;
    event.stopPropagation();
    const point = pointerToMapPoint(event);
    geometryDrag.current = {
      featureId: feature.id, mode, pointIndex, start: point,
      originalPoints: feature.points.map(item => ({ ...item }))
    };
    dragStart.current = null;
    captureTarget.current = event.currentTarget;
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function onWheel(event: React.WheelEvent<SVGSVGElement>) {
    event.preventDefault();
    const bounds = event.currentTarget.getBoundingClientRect();
    const x = viewBox.x + ((event.clientX - bounds.left) / bounds.width) * viewBox.width;
    const y = viewBox.y + ((event.clientY - bounds.top) / bounds.height) * viewBox.height;
    zoom(event.deltaY > 0 ? 1.15 : 0.87, x, y);
  }

  function onPointerMove(event: React.PointerEvent<SVGSVGElement>) {
    if (drawingStart.current) {
      const start = drawingStart.current;
      const end = pointerToMapPoint(event);
      if (drawTool === "ROOM") {
        const points = [
          { x: Math.min(start.x, end.x), y: Math.min(start.y, end.y) },
          { x: Math.max(start.x, end.x), y: Math.min(start.y, end.y) },
          { x: Math.max(start.x, end.x), y: Math.max(start.y, end.y) },
          { x: Math.min(start.x, end.x), y: Math.max(start.y, end.y) }
        ];
        drawingPointsRef.current = points;
        setDrawingPoints(points);
      } else {
        const points = [start, end];
        drawingPointsRef.current = points;
        setDrawingPoints(points);
      }
      return;
    }
    if (geometryDrag.current) {
      const drag = geometryDrag.current;
      const current = pointerToMapPoint(event);
      if (drag.mode === "move") {
        const dx = current.x - drag.start.x;
        const dy = current.y - drag.start.y;
        const xs = drag.originalPoints.map(point => point.x + dx);
        const ys = drag.originalPoints.map(point => point.y + dy);
        const deltaX = xs.some(x => x < 0) ? -Math.min(...xs) : xs.some(x => x > width) ? width - Math.max(...xs) : 0;
        const deltaY = ys.some(y => y < 0) ? -Math.min(...ys) : ys.some(y => y > height) ? height - Math.max(...ys) : 0;
        onGeometryChange(drag.featureId, drag.originalPoints.map(point => ({
          x: Math.max(0, Math.min(width, point.x + dx + deltaX)),
          y: Math.max(0, Math.min(height, point.y + dy + deltaY))
        })));
      } else if (drag.pointIndex != null) {
        const next = drag.originalPoints.map(point => ({ ...point }));
        next[drag.pointIndex] = current;
        onGeometryChange(drag.featureId, next);
      }
      return;
    }
    if (!dragStart.current) return;
    const bounds = event.currentTarget.getBoundingClientRect();
    const dx = (event.clientX - dragStart.current.x) * viewBox.width / bounds.width;
    const dy = (event.clientY - dragStart.current.y) * viewBox.height / bounds.height;
    setViewBox({ ...viewBox, x: dragStart.current.originX - dx, y: dragStart.current.originY - dy });
  }

  return <div className={`map-canvas map-2d${isCsGroundFloor ? " cs-ground-plan" : ""}`}>
    <div className="map-zoom-controls"><button aria-label="Zoom in" onClick={() => zoom(0.8)}>+</button>
      <button aria-label="Zoom out" onClick={() => zoom(1.25)}>−</button>
      <button aria-label="Reset map view" onClick={() => setViewBox({ x: 0, y: 0, width, height })}>Reset</button></div>
    {isAdmin && <div className="map-draw-tools" aria-label="Map drawing tools">
      <button className={drawTool === "SELECT" ? "selected" : ""} onClick={() => setDrawTool("SELECT")}>Select / move</button>
      <button className={drawTool === "ROOM" ? "selected" : ""} onClick={() => setDrawTool("ROOM")}>Draw room</button>
      <button className={drawTool === "WALL" ? "selected" : ""} onClick={() => setDrawTool("WALL")}>Draw wall</button>
      <button className={drawTool === "DOOR" ? "selected" : ""} onClick={() => setDrawTool("DOOR")}>Draw door / opening</button>
    </div>}
    <svg viewBox={`${viewBox.x} ${viewBox.y} ${viewBox.width} ${viewBox.height}`} role="img"
      aria-label={`${floor.label} floor plan in ${floor.unitLabel}`}
      onWheel={onWheel} onPointerDown={event => {
        if (isAdmin && drawTool !== "SELECT") {
          if (drawingGestureActive.current) return;
          drawingGestureActive.current = true;
          const start = pointerToMapPoint(event);
          drawingStart.current = start;
          drawingPointsRef.current = [start];
          setDrawingPoints([start]);
          geometryDrag.current = null;
          dragStart.current = null;
          const target = event.target instanceof SVGElement ? event.target : event.currentTarget;
          captureTarget.current = target;
          target.setPointerCapture(event.pointerId);
          return;
        }
        if (event.button !== 0) return;
        dragStart.current = { x: event.clientX, y: event.clientY, originX: viewBox.x, originY: viewBox.y };
        const target = event.target instanceof SVGElement ? event.target : event.currentTarget;
        captureTarget.current = target;
        target.setPointerCapture(event.pointerId);
      }} onPointerMove={onPointerMove}
      onPointerUp={event => {
        if (drawingStart.current) {
          const start = drawingStart.current;
          const end = pointerToMapPoint(event);
          let finalPoints: MapPoint[];
          if (drawTool === "ROOM") {
            finalPoints = [
              { x: Math.min(start.x, end.x), y: Math.min(start.y, end.y) },
              { x: Math.max(start.x, end.x), y: Math.min(start.y, end.y) },
              { x: Math.max(start.x, end.x), y: Math.max(start.y, end.y) },
              { x: Math.min(start.x, end.x), y: Math.max(start.y, end.y) }
            ];
          } else {
            finalPoints = [start, end];
          }
          const size = finalPoints.length === 4
            ? Math.abs(finalPoints[1].x - finalPoints[0].x) * Math.abs(finalPoints[2].y - finalPoints[1].y)
            : Math.hypot(end.x - start.x, end.y - start.y);
          if (size >= 1) {
            const type: MapFeature["type"] = drawTool === "ROOM" ? "ROOM"
              : drawTool === "WALL" ? "WALL" : "ENTRANCE";
            const geometryType: MapFeature["geometryType"] = drawTool === "ROOM" ? "POLYGON" : "LINE";
            onCreateFeature(type, geometryType, finalPoints);
          }
          drawingPointsRef.current = [];
          drawingStart.current = null;
          setDrawingPoints([]);
          setDrawTool("SELECT");
        }
        drawingGestureActive.current = false;
        dragStart.current = null;
        geometryDrag.current = null;
        const target = captureTarget.current;
        if (target?.hasPointerCapture(event.pointerId)) target.releasePointerCapture(event.pointerId);
        captureTarget.current = null;
      }}
      onPointerCancel={() => {
        dragStart.current = null; captureTarget.current = null;
        drawingPointsRef.current = [];
        drawingGestureActive.current = false;
        drawingStart.current = null; setDrawingPoints([]);
      }}>
      <rect x="0" y="0" width={width} height={height} className="map-floor-background"/>
      <defs><marker id="map-arrowhead" markerWidth="7" markerHeight="7" refX="6" refY="3.5"
        orient="auto" markerUnits="strokeWidth"><path d="M0,0 L7,3.5 L0,7 z" className="map-arrowhead"/></marker></defs>
      {data.features.filter(feature => feature.isActive).map(feature => {
        const points = feature.points.map(point => `${point.x},${point.y}`).join(" ");
        const center = feature.points.reduce((sum, point) => ({
          x: sum.x + point.x / feature.points.length, y: sum.y + point.y / feature.points.length
        }), { x: 0, y: 0 });
        const selected = feature.id === selectedFeatureId;
        const route = feature.edgeFromId != null && feature.edgeToId != null &&
          routeEdges.has(edgeKey(feature.edgeFromId, feature.edgeToId));
        const select = (event: React.MouseEvent | React.KeyboardEvent) => { event.stopPropagation(); onSelect(feature); };
        const keySelect = (event: React.KeyboardEvent) => { if (event.key === "Enter" || event.key === " ") select(event); };
        const common = {
          className: `map-feature map-feature-${feature.type.toLowerCase()}${isCsGroundFloor ? " map-feature-hotspot" : ""}${selected ? " selected" : ""}${route ? " map-route-edge" : ""}${feature.isDemo ? " demo" : ""}`,
          style: feature.type === "WALL" && feature.thickness > 0 ? { strokeWidth: feature.thickness } : undefined,
          onClick: select, onKeyDown: keySelect, role: "button" as const, tabIndex: 0,
          "aria-label": `${feature.label}${feature.isDemo ? ", demo geometry" : ""}`
        };
        const labelY = feature.geometryType === "LINE"
          ? center.y > height * 0.7 ? Math.min(height - 8, center.y + 48) : center.y - 12
          : center.y;
        const label = feature.type === "WALL" ? null
          : <text className="map-feature-label" x={center.x} y={labelY} textAnchor="middle" pointerEvents="none">{feature.label}</text>;
        const withArrow = feature.type === "ENTRANCE" ? { markerEnd: "url(#map-arrowhead)" } : {};
        if (feature.geometryType === "POLYGON")
          return <g key={feature.id}><polygon {...common} points={points}
            onPointerDown={event => beginGeometryDrag(event, feature, "move")}/>{label}</g>;
        if (feature.geometryType === "LINE")
          return <g key={feature.id}>
            {feature.type === "ENTRANCE"
              ? <><polyline {...common} points={points} onPointerDown={event => beginGeometryDrag(event, feature, "move")}/>
                <DoorSymbol points={feature.points}/></>
              : <polyline {...common} {...withArrow} points={points}
                  onPointerDown={event => beginGeometryDrag(event, feature, "move")}/>}
            {label}
          </g>;
        const point = feature.points[0];
        if (!point) return null;
        return <g key={feature.id}><circle {...common} cx={point.x} cy={point.y} r={Math.max(width, height) * 0.012}/>{label}</g>;
      })}
      {isAdmin && data.features.filter(feature => feature.isActive && feature.id === selectedFeatureId)
        .flatMap(feature => feature.points.map((point, index) => (
          <circle key={`${feature.id}-${index}`}
            className="map-edit-handle"
            cx={point.x} cy={point.y}
            r={Math.max(width, height) * (feature.geometryType === "LINE" ? 0.016 : 0.011)}
            role="button" aria-label={`Drag ${feature.label} point ${index + 1}`}
            onPointerDown={event => beginGeometryDrag(event, feature, "vertex", index)}
          />
        )))}
      {drawingPoints.length > 0 && (drawTool === "ROOM"
        ? <polygon className="map-drawing-preview" points={drawingPoints.map(point => `${point.x},${point.y}`).join(" ")}/>
        : <polyline className="map-drawing-preview" points={drawingPoints.map(point => `${point.x},${point.y}`).join(" ")}/>
      )}
      {data.navigationLocations.map(location => {
        const coordinates = location.coordinates;
        if (!coordinates) return null;
        return <g key={location.id} className={`map-nav-point${location.isDemo ? " demo" : ""}`}
          aria-label={`${location.label}${location.isDemo ? " demo" : ""}`}>
          <circle cx={coordinates.x} cy={coordinates.y} r={Math.max(width, height) * 0.009}/>
          <title>{location.label}{location.isDemo ? " · DEMO" : ""}</title>
        </g>;
      })}
    </svg>
    <small>Drag to pan · scroll or use +/− to zoom</small>
  </div>;
}

function Map3D({ data, routeEdges, selectedFeatureId, onSelect }: {
  data: MapFloorData; routeEdges: Set<string>; selectedFeatureId: number | null; onSelect: (feature: MapFeature) => void;
}) {
  const host = useRef<HTMLDivElement>(null);
  const [unavailable, setUnavailable] = useState("");
  const isCsGroundFloor = data.floor.sourceReference.startsWith("User-provided CS Building Floor 0 plan");
  useEffect(() => {
    const container = host.current;
    if (!container) return;
    let renderer: THREE.WebGLRenderer;
    let frame = 0;
    let controls: OrbitControls | null = null;
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    } catch {
      setUnavailable("WebGL is unavailable in this browser. Use the 2D map instead.");
      return;
    }
    setUnavailable("");
    if (isCsGroundFloor) renderer.shadowMap.enabled = true;
    const width = data.floor.width ?? 100;
    const height = data.floor.height ?? 70;
    const elevation = data.floor.elevation ?? 0;
    const sceneScale = Math.max(width, height);
    const wallHeight = sceneScale * (isCsGroundFloor ? 0.1 : 0.06);
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(isCsGroundFloor ? "#edf3f5" : "#f6f4f8");
    const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 10000);
    camera.position.set(width * 0.95, elevation + sceneScale * 0.85 + wallHeight * 0.15, height * 1.1);
    const ambient = new THREE.HemisphereLight("#ffffff", isCsGroundFloor ? "#a9bbc4" : "#d0c0dc", 2);
    scene.add(ambient);
    const light = new THREE.DirectionalLight(isCsGroundFloor ? "#fffaf0" : "#ffffff", 2.6);
    light.position.set(width * 0.4, elevation + sceneScale * 1.2, height * 0.45);
    scene.add(light);
    if (isCsGroundFloor) {
      light.castShadow = true;
      light.shadow.mapSize.set(1024, 1024);
      light.shadow.camera.left = -width;
      light.shadow.camera.right = width;
      light.shadow.camera.top = height;
      light.shadow.camera.bottom = -height;
    }
    const baseThickness = Math.max(sceneScale * 0.012, 0.35);
    const base = new THREE.Mesh(
      new THREE.BoxGeometry(width, baseThickness, height),
      new THREE.MeshStandardMaterial({
        color: isCsGroundFloor ? "#dbe5e8" : "#e9e6ed",
        roughness: 0.88,
        metalness: 0.02
      })
    );
    base.position.set(width / 2, elevation - baseThickness / 2, height / 2);
    base.receiveShadow = isCsGroundFloor;
    scene.add(base);
    const pickTargets: THREE.Object3D[] = [];

    const addLabel = (feature: MapFeature) => {
      if (feature.type === "WALL" || !feature.points.length) return;
      const center = feature.points.reduce((sum, point) => ({
        x: sum.x + point.x / feature.points.length,
        y: sum.y + point.y / feature.points.length
      }), { x: 0, y: 0 });
      const canvas = document.createElement("canvas");
      const context = canvas.getContext("2d");
      if (!context) return;
      const fontSize = 34;
      context.font = `600 ${fontSize}px Manrope, Arial, sans-serif`;
      canvas.width = Math.ceil(context.measureText(feature.label).width + 36);
      canvas.height = 64;
      const labelContext = canvas.getContext("2d");
      if (!labelContext) return;
      labelContext.font = `600 ${fontSize}px Manrope, Arial, sans-serif`;
      labelContext.fillStyle = "rgba(248, 252, 252, 0.98)";
      labelContext.fillRect(0, 0, canvas.width, canvas.height);
      labelContext.strokeStyle = "rgba(38, 59, 73, 0.42)";
      labelContext.strokeRect(0.5, 0.5, canvas.width - 1, canvas.height - 1);
      labelContext.fillStyle = "#263b49";
      labelContext.textAlign = "center";
      labelContext.textBaseline = "middle";
      labelContext.fillText(feature.label, canvas.width / 2, canvas.height / 2);
      const texture = new THREE.CanvasTexture(canvas);
      texture.colorSpace = THREE.SRGBColorSpace;
      const material = new THREE.SpriteMaterial({ map: texture, transparent: true, depthTest: false });
      const sprite = new THREE.Sprite(material);
      const scalePerPixel = isCsGroundFloor ? 0.28 : sceneScale * 0.00035;
      sprite.scale.set(canvas.width * scalePerPixel, canvas.height * scalePerPixel, 1);
      sprite.position.set(center.x, elevation + wallHeight + sceneScale * 0.02, center.y);
      sprite.renderOrder = 10;
      scene.add(sprite);
      if (feature.classroomId != null) {
        sprite.userData.featureId = feature.id;
        pickTargets.push(sprite);
      }
    };

    for (const feature of data.features.filter(item => item.isActive)) {
      const selectedRoom = feature.classroomId != null && feature.id === selectedFeatureId;
      const color = selectedRoom ? "#f2bb4b"
        : feature.type === "WALL" ? (isCsGroundFloor ? "#526875" : "#695c70")
        : feature.type === "ROOM" || (isCsGroundFloor && feature.label.startsWith("Lecture hall")) ? "#a9c3d5"
        : isCsGroundFloor && /bathroom/i.test(feature.label) ? "#b8ddd5"
        : isCsGroundFloor && (feature.type === "STAIRS" || feature.type === "ELEVATOR") ? "#bcc8cb"
        : isCsGroundFloor && feature.label.startsWith("Lobby") ? "#edf3ef" : "#c4d4dc";
      const material = new THREE.MeshStandardMaterial({
        color, roughness: 0.8, side: THREE.DoubleSide,
        transparent: isCsGroundFloor && feature.label.startsWith("Lobby"),
        opacity: isCsGroundFloor && feature.label.startsWith("Lobby") ? 0.85 : 1,
        depthWrite: !(isCsGroundFloor && feature.label.startsWith("Lobby"))
      });
      let object: THREE.Object3D | null = null;
      if (feature.geometryType === "POLYGON" && feature.points.length >= 3) {
        const shape = new THREE.Shape();
        feature.points.forEach((point, index) => {
          if (index === 0) shape.moveTo(point.x, -point.y);
          else shape.lineTo(point.x, -point.y);
        });
        const geometry = new THREE.ExtrudeGeometry(shape, {
          depth: Math.max(feature.height, isCsGroundFloor ? 1.4 : 1), bevelEnabled: false
        });
        geometry.rotateX(-Math.PI / 2);
        geometry.translate(0, elevation + feature.elevation, 0);
        object = new THREE.Mesh(geometry, material);
      } else if (feature.type === "WALL" && feature.geometryType === "LINE" && feature.points.length >= 2) {
        const wall = new THREE.Group();
        const thickness = feature.thickness > 0
          ? feature.thickness
          : sceneScale * 0.008;
        const segmentHeight = feature.height > 0 ? feature.height : wallHeight;
        const openingSegments = data.features.filter(item =>
          item.isActive && item.type === "ENTRANCE" && item.geometryType === "LINE" &&
          item.points.length >= 2
        ).map(item => ({ start: item.points[0], end: item.points[item.points.length - 1] }));

        const addWallSegment = (previous: MapPoint, current: MapPoint) => {
          const dx = current.x - previous.x;
          const dz = current.y - previous.y;
          const length = Math.hypot(dx, dz);
          if (!length) return;
          const segment = new THREE.Mesh(
            new THREE.BoxGeometry(length, segmentHeight, thickness),
            new THREE.MeshStandardMaterial({ color, roughness: 0.85 })
          );
          segment.position.set(
            (previous.x + current.x) / 2,
            elevation + segmentHeight / 2,
            (previous.y + current.y) / 2
          );
          segment.rotation.y = -Math.atan2(dz, dx);
          segment.userData.featureId = feature.id;
          wall.add(segment);
        };

        for (let index = 1; index < feature.points.length; index += 1) {
          const previous = feature.points[index - 1];
          const current = feature.points[index];
          const dx = current.x - previous.x;
          const dz = current.y - previous.y;
          const length = Math.hypot(dx, dz);
          if (!length) continue;
          const openings: Array<[number, number]> = [];
          for (const opening of openingSegments) {
            const openingDx = opening.end.x - opening.start.x;
            const openingDz = opening.end.y - opening.start.y;
            if (!openingDx && !openingDz) continue;
            const startOffset = ((opening.start.x - previous.x) * dx + (opening.start.y - previous.y) * dz) / (length * length);
            const endOffset = ((opening.end.x - previous.x) * dx + (opening.end.y - previous.y) * dz) / (length * length);
            const low = Math.max(0, Math.min(startOffset, endOffset) - 0.01);
            const high = Math.min(1, Math.max(startOffset, endOffset) + 0.01);
            if (high > low) openings.push([low, high]);
          }
          openings.sort((left, right) => left[0] - right[0]);
          let cursor = 0;
          for (const [low, high] of openings) {
            if (low > cursor) {
              addWallSegment(
                { x: previous.x + dx * cursor, y: previous.y + dz * cursor },
                { x: previous.x + dx * low, y: previous.y + dz * low }
              );
            }
            cursor = Math.max(cursor, high);
          }
          if (cursor < 1) {
            addWallSegment(
              { x: previous.x + dx * cursor, y: previous.y + dz * cursor },
              current
            );
          }
        }
        object = wall;
      } else if (feature.geometryType === "LINE" && feature.points.length >= 2) {
        const points = feature.points.map(point => new THREE.Vector3(point.x, elevation + feature.elevation + 0.2, point.y));
        const geometry = new THREE.BufferGeometry().setFromPoints(points);
        const lineMaterial = new THREE.LineBasicMaterial({
          color: feature.edgeFromId != null && feature.edgeToId != null &&
            routeEdges.has(edgeKey(feature.edgeFromId, feature.edgeToId)) ? "#e5a900" : "#765489",
          linewidth: 3
        });
        object = new THREE.Line(geometry, lineMaterial);
      } else if (feature.geometryType === "POINT" && feature.points.length) {
        const point = feature.points[0];
        const geometry = new THREE.SphereGeometry(Math.max(width, height) * 0.012, 16, 12);
        object = new THREE.Mesh(geometry, material);
        object.position.set(point.x, elevation + feature.elevation + 0.2, point.y);
      }
      if (object) {
        object.userData.featureId = feature.id;
        if (isCsGroundFloor) {
          object.traverse?.(child => {
            if (child instanceof THREE.Mesh) {
              child.castShadow = true;
              child.receiveShadow = true;
            }
          });
          if (object instanceof THREE.Mesh) {
            object.castShadow = true;
            object.receiveShadow = true;
          }
        }
        scene.add(object);
        if (feature.classroomId != null && !(object instanceof THREE.Group)) pickTargets.push(object);
      }
      addLabel(feature);
    }
    const rendererSize = () => {
      const bounds = container.getBoundingClientRect();
      if (!bounds.width || !bounds.height) return;
      renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
      renderer.setSize(bounds.width, bounds.height, false);
      camera.aspect = bounds.width / bounds.height;
      camera.updateProjectionMatrix();
    };
    rendererSize();
    container.appendChild(renderer.domElement);
    controls = new OrbitControls(camera, renderer.domElement);
    controls.target.set(width / 2, elevation + wallHeight * 0.18, height / 2);
    controls.enableDamping = true;
    const resizeObserver = new ResizeObserver(rendererSize);
    resizeObserver.observe(container);
    const animate = () => {
      frame = requestAnimationFrame(animate);
      controls?.update();
      renderer.render(scene, camera);
    };
    animate();
    const raycaster = new THREE.Raycaster();
    const mouse = new THREE.Vector2();
    const onClick = (event: MouseEvent) => {
      const bounds = renderer.domElement.getBoundingClientRect();
      mouse.x = ((event.clientX - bounds.left) / bounds.width) * 2 - 1;
      mouse.y = -((event.clientY - bounds.top) / bounds.height) * 2 + 1;
      raycaster.setFromCamera(mouse, camera);
      const hit = raycaster.intersectObjects(pickTargets, false)[0];
      const feature = data.features.find(item => item.id === hit?.object.userData.featureId);
      if (feature) onSelect(feature);
    };
    renderer.domElement.addEventListener("click", onClick);
    return () => {
      cancelAnimationFrame(frame);
      resizeObserver.disconnect();
      renderer.domElement.removeEventListener("click", onClick);
      controls?.dispose();
      scene.traverse(object => {
        if (object instanceof THREE.Mesh || object instanceof THREE.Line || object instanceof THREE.Sprite) {
          if (object instanceof THREE.Mesh || object instanceof THREE.Line) object.geometry.dispose();
          const materials = Array.isArray(object.material) ? object.material : [object.material];
          materials.forEach(material => {
            if (material instanceof THREE.MeshBasicMaterial || material instanceof THREE.SpriteMaterial)
              material.map?.dispose();
            material.dispose();
          });
        }
      });
      renderer.dispose();
      renderer.domElement.remove();
    };
  }, [data, routeEdges, selectedFeatureId, onSelect]);

  return <div className={`map-canvas map-3d${isCsGroundFloor ? " cs-ground-plan" : ""}`}>
    <div className="map-3d-host" ref={host}/>
    {unavailable && <div className="empty-panel" role="alert">{unavailable}</div>}
    <small>{isCsGroundFloor ? "Walls and floating labels improve visibility; vertical height is illustrative, not measured. · " : ""}
      Drag to orbit · scroll to zoom · right-drag to pan</small>
  </div>;
}
