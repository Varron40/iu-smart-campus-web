# Module 6 — Interactive Campus Map Handoff

This note describes the implemented Module 6 campus map, its data contract, and the work required to move from the clearly marked sample to an accurate campus map. It supplements the existing Modules 3–5 handoff.

## Run and validate

From the project root in PowerShell:

```powershell
cd frontend
npm install
npm run build
```

In another terminal:

```powershell
cd backend
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\iu_smart_campus_api.exe
```

The backend initializes map metadata/features after campus and navigation schemas, seeds the existing demo floor only when it has no map metadata, and refreshes the navigation graph after assigning the sample nodes coordinates. Existing account and campus records are not deleted.

## User experience

- The authenticated sidebar links to **Interactive map**. Users can select a building and floor, view the white-background CS ground-floor plan with interactive B 0.1/B 0.2 room hotspots, or use the light-themed Three.js 3D floor model with raised walls, floating labels, and orbit controls.
- Unsupported WebGL displays a message while the 2D map remains available.
- Active classrooms with no `ROOM` feature are listed separately as unplaced; their locations are never guessed. Selecting a mapped or unplaced room loads its normal classroom data and aggregate seat counts for active assigned sections. Student identities are not returned; room-wide occupancy remains unavailable.
- Route requests use `POST /api/navigation/route`; the frontend does not calculate alternatives. A route segment is drawn only where its returned graph edge has matching map feature geometry. Text directions and backend warnings remain available when geometry is missing.
- The administrator map editor can edit floor metadata, add/edit geometry features, deactivate/reactivate features, and associate room, navigation node, or graph-edge IDs. Geometry is entered as a JSON list of `{ "x": number, "y": number }` points.

## Provenance and coordinate rules

The user supplied a diagram identified as the CS Building ground floor. The 2D view uses a white-background rendering that preserves the diagram's layout and labels; its visible room footprints are also traced into local image coordinates (873 × 522) and linked to existing B 0.1 and B 0.2 classroom records. The 3D view uses those traces for raised walls and labeled floor areas; wall height is an illustration, not a measurement. The plan is marked `UNVERIFIED`: it has no measured scale or surveyed dimensions, and its exterior location is not established by the campus pin. Routing is intentionally not seeded for this floor until walkable paths and entrances can be confirmed. The separate `DEMO` / “Demo Floor” remains an illustrative map with synthetic navigation-edge lines.

The required coordinate system is `LOCAL_FLOOR_X_RIGHT_Y_DOWN`: x increases rightward and y downward within one floor. Width, height, elevation, and floor height are metadata values. `metersPerUnit` is nullable; when it is unknown the interface reports “scale unknown” and never labels local coordinates as meters.

Before entering real map data:

1. Replace or review the user-provided traced plan against an approved floor-plan source and room-position records; record the source reference and its revision/date.
2. Verify each floor's origin, orientation, dimensions, and scale independently. Keep scale null until measured.
3. Replace/deactivate the active synthetic map features and navigation locations, then create graph nodes/edges and geometry from the same floor coordinate reference.
4. Link each room polygon to an existing classroom ID and each rendered route segment to the canonical graph edge IDs.
5. Mark the floor `VERIFIED` only after review. The API requires a source reference and rejects verification while active demo map features or navigation nodes remain.
6. Confirm accessibility classifications in person before setting graph edges to `STEP_FREE`; geometry alone does not establish accessibility.

## Storage and API

`floor_map_metadata` stores floor dimensions, coordinate system, units, optional scale, elevation, verification state, and source reference. `map_features` stores geometry and optional classroom, navigation-location, or canonical graph-edge links. Foreign keys and triggers enforce floor/building relationships; repository validation enforces coordinate bounds and geometry shape.

| Method | Route | Permission | Purpose |
|---|---|---|---|
| GET | `/api/map/buildings` | Authenticated | Active buildings with floor/map counts |
| GET | `/api/map/buildings/{buildingId}/floors` | Authenticated | Floor metadata, including `NO_MAP` floors |
| GET | `/api/map/floors/{floorId}/features` | Authenticated | Active features, floor metadata, and active navigation locations |
| GET | `/api/map/floors/{floorId}/features?includeInactive=true` | Admin | Include inactive features for restoration |
| GET | `/api/map/rooms/{classroomId}` | Authenticated | Room record and per-section seat summaries |
| PUT | `/api/admin/map/floors/{floorId}/metadata` | Admin | Create/update floor map metadata |
| POST / PUT | `/api/admin/map/floors/{floorId}/features[/{featureId}]` | Admin | Create/update a point, line, or polygon |
| PATCH | `/api/admin/map/features/{featureId}/status` | Admin | Deactivate/reactivate a feature |

Map and route reads require a bearer session. Admin writes use the same role authorization as the existing campus modules. Unknown map scale and missing room geometry are represented explicitly rather than filled with success-shaped guesses.

## Implementation and tests

- Frontend implementation: `frontend/src/CampusMapPage.tsx`; API types/calls: `frontend/src/services/api.ts`; authenticated navigation: `frontend/src/App.tsx`.
- Backend persistence and validation: `backend/src/MapRepository.cpp`; routes: `backend/src/MapRoutes.cpp`; shared types: `backend/include/models/CampusMap.h`.
- Startup wires the repository and routes from `backend/src/main.cpp`. Runtime schema is mirrored in `database/schema.sql`.
- `backend/tests/ModuleWorkflowTests.cpp` covers map seeding, demo labels, location reload, out-of-bounds rejection, active/inactive feature visibility, verification guardrails, and aggregate section seat counts.

The 3D scene is a visualization of stored local coordinates, not evidence that the underlying plan is real or accurate.
