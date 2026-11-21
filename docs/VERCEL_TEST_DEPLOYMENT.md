# Vercel test deployment

## Deploy the frontend

1. Import this repository in Vercel.
2. Keep the project root at the repository root.
3. Set `VITE_API_BASE_URL` to the public HTTPS URL of the C++ backend.
4. Deploy.

The root `vercel.json` builds `frontend` and publishes `frontend/dist`.

## Deploy the backend

Vercel does not run this C++17 server as a standard backend process. Deploy `backend/build/iu_smart_campus_api.exe` or build this project in a container on a persistent service such as Render, Railway, Fly.io, or Azure App Service.

The backend must:

- Listen on `0.0.0.0`.
- Use a persistent writable SQLite volume for `backend/data/iu-smart-campus.db`.
- Accept HTTPS requests from the Vercel frontend.
- Set `IU_ADMIN_STUDENT_ID`, `IU_ADMIN_NAME`, `IU_ADMIN_EMAIL`, and `IU_ADMIN_PASSWORD` for initial admin provisioning.

Configure CORS on the API for the exact Vercel origin and keep `VITE_API_BASE_URL` secret-free.

## Test account

Create accounts through registration. For an administrator, provision the initial admin through backend environment variables and restart once.
