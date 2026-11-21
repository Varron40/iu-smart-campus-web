# Vercel test deployment

## Deploy the frontend

1. Import this repository in Vercel.
2. Keep the project root at the repository root.
3. Set `VITE_API_BASE_URL` to the public HTTPS URL of the C++ backend (for example, `https://your-service.onrender.com`; do not append `/api`).
4. Deploy or redeploy after setting the variable.

The root `vercel.json` builds `frontend` and publishes `frontend/dist`.

## Deploy the backend

Vercel does not run this C++17 server as a standard backend process. For a free test deployment on Render:

1. Create a **Web Service** from this GitHub repository and use the `main` branch.
2. Select **Docker** as the language. Leave the root directory and Docker context at the repository root; set the Dockerfile path to `backend/Dockerfile`.
3. Select the **Free** instance plan and set the health-check path to `/api/health`.
4. Add `IU_ADMIN_STUDENT_ID`, `IU_ADMIN_NAME`, `IU_ADMIN_EMAIL`, and `IU_ADMIN_PASSWORD` as environment variables. Use a valid email and a strong password that satisfies the application's registration password requirements.
5. Create the service and wait for its first deploy. Render supplies `PORT`; the server binds to that port on `0.0.0.0`.
6. Copy the service's public HTTPS URL into Vercel's `VITE_API_BASE_URL`, then redeploy the Vercel frontend.

The admin account is created on first startup if all four admin variables are valid and its identifiers are not already in use. Do not put these credentials in Vercel or commit them to the repository.

## Free-plan limitations

Render's free web services sleep after 15 minutes without traffic and can take about a minute to wake. Their filesystem is ephemeral, so the SQLite database—including registered test accounts, admin account, and saved map edits—is lost whenever the service sleeps, restarts, or redeploys. This setup is for temporary feedback/testing, not durable data or production use. Persistent local disks require a paid service; keeping data between sleeps on a free plan requires migrating the backend from SQLite to a free database service.

The API base URL is public and contains no secret. Keep the admin password in Render's environment-variable settings and share the public site URL only with testers.
