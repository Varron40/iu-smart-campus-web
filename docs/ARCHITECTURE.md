# Architecture

- `frontend/`: React + TypeScript browser client. Vite proxies `/api` to `localhost:8080` in development.
- `backend/`: C++17 REST API. Academic data structures and algorithms live here, not in React.
- `database/`: initial SQLite schema outline. Database connection/persistence is not implemented yet.

## Data structures
- Graph: adjacency-list representation of walkable campus nodes.
- Dijkstra: shortest path with non-negative edge weights.
- MaxHeap: custom max binary heap for reservation priority.
- HashTable: custom separate-chaining hash table for fast record lookup.

## Important
Demo vertices, edges, rooms, and seat counts are placeholders. They must not be treated as actual Innovation University data.
