# API Reference

Base URL: `http://127.0.0.1:8080/api`

All request/response bodies are JSON. Authenticated routes require an
`Authorization: Bearer <token>` header, where `<token>` is returned by
`POST /login`.

Every response follows the same envelope:

```json
{
  "success": true,
  "message": "Human-readable description",
  "data": { }
}
```

On error, `success` is `false` and an appropriate HTTP status code is set
(400/401/403/404/409/422/500).

---

## Auth

### `POST /register`
Create a new voter account.

**Body:**
```json
{
  "fullName": "Aarav Sharma",
  "email": "aarav@example.com",
  "voterIdNumber": "ABC1234567",
  "password": "Str0ng!Pass",
  "confirmPassword": "Str0ng!Pass"
}
```
**Response `201`:** `{ "userId": 5 }`

> The OTP is generated, stored in `otp_codes`, and emailed to the address
> the user registered with via SMTP — it is never included in the API
> response. See `docs/INSTALL.md` for SMTP environment variable setup.

### `POST /verify-otp`
```json
{ "userId": 5, "otpCode": "042917" }
```

### `POST /login`
```json
{ "email": "aarav@example.com", "password": "Str0ng!Pass" }
```
**Response `200`:**
```json
{
  "token": "9f2a...64charhex",
  "user": { "userId": 5, "fullName": "Aarav Sharma", "email": "...", "role": "voter", "walletAddress": "" }
}
```

### `POST /logout` *(auth required)*
Invalidates the current session token.

### `GET /profile` *(auth required)*
Returns the logged-in user's profile.

### `POST /link-wallet` *(auth required)*
```json
{ "walletAddress": "0xAbC123..." }
```

---

## Elections

### `GET /elections`
Public. Returns all elections, ordered active → upcoming → ended.

### `GET /elections/:id`
Public. Returns one election including its candidates.

### `POST /elections` *(admin only)*
```json
{
  "title": "Student Council Election 2026",
  "description": "Annual council election",
  "startTime": "2026-08-10 09:00:00",
  "endTime": "2026-08-12 18:00:00",
  "contractAddress": "0x..."
}
```

### `PUT /elections/:id` *(admin only)*
```json
{ "status": "active", "contractAddress": "0x..." }
```

### `DELETE /elections/:id` *(admin only)*

---

## Candidates

### `GET /candidates?electionId=1`
Public.

### `POST /candidates` *(admin only)*
```json
{
  "electionId": 1,
  "fullName": "Priya Verma",
  "partyName": "Unity Front",
  "bio": "Second-year CSE representative",
  "onChainIndex": 1
}
```
`onChainIndex` **must** match this candidate's index in the deployed
smart contract's candidate array (the order `addCandidate()` was called
in `scripts/deploy.js`, starting at 0).

### `PUT /candidates/:id` *(admin only)*
### `DELETE /candidates/:id` *(admin only)*

---

## Voting

### `POST /vote` *(auth required)*
Called by the frontend **after** the on-chain transaction has been
confirmed by MetaMask — this endpoint records the result for the audit
trail and fast MySQL-backed reads.

```json
{
  "electionId": 1,
  "candidateId": 3,
  "voterWallet": "0xAbC123...",
  "txHash": "0xdeadbeef...",
  "blockNumber": 42
}
```

Rejected with `409 Conflict` if this user has already voted in this
election, or if this `txHash` was already recorded.

### `GET /results?electionId=1`
Public. Returns vote counts and percentages per candidate.

### `GET /vote/check?electionId=1` *(auth required)*
```json
{ "hasVoted": true, "txHash": "0x..." }
```

---

## Admin

All routes below require `Authorization: Bearer <token>` for a user with
`role: "admin"`.

### `GET /admin/stats`
```json
{ "totalVoters": 42, "verifiedVoters": 38, "totalElections": 3, "activeElections": 1, "totalVotes": 29 }
```

### `GET /admin/users`
Lists every registered account.

### `PUT /admin/users/:id/status`
```json
{ "isActive": false }
```

### `GET /admin/audit-logs`
Returns the 200 most recent audit log entries (logins, votes, admin
actions), newest first.

---

## Health check

### `GET /api/health`
No auth required — used to verify the server is reachable.
