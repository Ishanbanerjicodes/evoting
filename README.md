# CivicChain — Blockchain-Based E-Voting System

A full-stack, Windows-first e-voting platform that pairs a fast, auditable
MySQL backend with an Ethereum smart contract, so every vote is both easy
to manage and impossible to quietly alter.

Built as a B.Tech final year project (Computer Science & Engineering),
designed to also stand on its own as a GitHub portfolio piece.

---

## Why this architecture

Most "blockchain voting" student projects are either (a) pure smart
contract demos with no real backend, or (b) a normal CRUD app that
mentions blockchain in the README but never actually uses it. CivicChain
does neither:

- **MySQL** is the system of record for accounts, sessions, OTPs, and a
  fast, queryable audit log — things a real election office needs.
- **The Ethereum smart contract** is the system of record for the votes
  themselves. Every vote transaction is signed by the *voter's own wallet*
  via MetaMask — the backend server never sees or holds a private key, and
  cannot forge a vote on anyone's behalf.
- Double voting is blocked **twice**, independently: by the contract's own
  `hasVoted` mapping on-chain, and by a `UNIQUE(election_id, user_id)`
  constraint in MySQL. Either one alone would be a defensible design;
  having both is defense in depth.

---

## Tech stack

| Layer | Technology |
|---|---|
|Backend | C++17, custom lightweight Winsock HTTP router, MySQL 8.0 (Connector/C), OpenSSL (SHA-256), CMake,    MinGW-w64 (C++17)
| Blockchain | Solidity 0.8.20, Hardhat, Ethers.js v6, MetaMask |
| Frontend | HTML5, CSS3 (custom design system), Vanilla JavaScript, Bootstrap 5 |
| Database | MySQL 8.0 |
| OS target | Windows 10/11 only |

> **Note on the backend HTTP/JSON libraries:** instead of vendoring the
> full cpp-httplib (~20k lines) and nlohmann/json (~25k lines) headers,
> this project ships two small, purpose-built, dependency-free headers
> (`backend/lib/simple_http.hpp` and `backend/lib/simple_json.hpp`) that
> implement exactly the HTTP routing and JSON handling this API needs,
> directly on top of Winsock2. This keeps the "zero setup" build genuinely
> zero-setup — no vcpkg, no Conan, no multi-hundred-KB downloads — while
> staying 100% Windows/MinGW native (no pthread, no POSIX sockets).

---

## Folder structure

```
evoting/
├── backend/         C++17 REST API server (CMake + MinGW-w64)
│   ├── src/         Controllers, services, main.cpp
│   ├── include/     Headers
│   ├── lib/         Vendored-free simple_http.hpp / simple_json.hpp
│   └── config.json  Server + database configuration
├── blockchain/       Solidity contract + Hardhat tooling
│   ├── contracts/    EVoting.sol
│   ├── scripts/      deploy.js (auto-generates frontend contract config)
│   └── test/         Hardhat/Chai test suite
├── frontend/          Static HTML/CSS/JS client (Bootstrap 5)
│   ├── css/           Design system (variables, base, components, animations)
│   └── js/             API client, wallet integration, per-page logic
├── database/
│   └── schema.sql     Full normalized schema + seed admin account
└── docs/               Install guide, API reference, diagrams
```

---

## Quick start (Windows 10/11)

**Prerequisites** (install these first — see `docs/INSTALL.md` for detailed steps):

- Node.js LTS
- MySQL 8.0
- OpenSSL Win64
- MinGW-w64 with C++17 support *(older MinGW GCC 6.x is not supported because the backend uses C++17 features such as `std::optional`)*
- CMake
- Visual Studio Code (recommended)


### 1. Database

```powershell
mysql -u root -p < database\schema.sql
```

### 2. Backend

```powershell
cd backend
mkdir build
cd build
cmake ..
cmake --build .
```

Edit `backend\config.json` if your MySQL root password isn't `root`, then run:

```powershell
.\evoting_server.exe
```

The API is now live at `http://127.0.0.1:8080/api`.

### 3. Blockchain

```powershell
cd blockchain
npm install
npx hardhat node
```

In a **second terminal** (keep the node running in the first):

```powershell
cd blockchain
npx hardhat run scripts/deploy.js --network localhost
```

This automatically writes `frontend/js/contract-config.js` with the
deployed contract address and ABI — no manual copy-pasting.

### 4. Frontend

Just open `frontend/index.html` in your browser (or serve the folder with
any static file server / VS Code Live Server extension).

Import the Hardhat "Account #0" private key (printed by `npx hardhat node`)
into MetaMask to act as the election admin's wallet, and any other printed
account to act as a voter.

**Default admin login:** `admin@evoting.local` / `Admin@123`

### 5. MetaMask + Local Hardhat Setup

**One-time setup:**
1. Run `npx hardhat node` (or `start.bat`) — this prints 20 test accounts with private keys.
2. In MetaMask, add a network: RPC URL `http://127.0.0.1:8545`, Chain ID `1337`, currency `ETH`.
3. In MetaMask → account icon → "Add account or hardware wallet" → "Import account" → paste a private key from step 1.
4. Switch MetaMask to this imported account + the Hardhat network. Should show 10000 ETH.
5. Click "Connect Wallet" on the site to link this account.

**Every time the Hardhat node restarts** (chain resets to genesis, old MetaMask account balance goes stale):
- Easiest fix: import a *fresh, unused* private key from the new node output (repeat steps 3–5 above).
- Alternative: MetaMask → Settings → Advanced → "Clear activity and nonce data" on the existing account, then switch networks away and back to force a balance refresh.

**If MetaMask shows a red "Review alert" instead of a normal confirm button** when voting: click it, acknowledge the warning (it's flagging `http://` as insecure — expected and harmless for local dev), then proceed.

---

## Default ports

| Service | Port |
|---|---|
| Backend API | `8080` |
| Hardhat local blockchain | `8545` |
| MySQL | `3306` |

---

## Documentation

- [`docs/INSTALL.md`](docs/INSTALL.md) — detailed step-by-step Windows setup
- [`docs/API.md`](docs/API.md) — full REST API reference
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — system architecture, ER diagram, flowcharts

---

## Security notes (for the project report)

- Passwords are never stored in plaintext — only `SHA256(password + salt)`,
  with a unique random salt per user.
- Session tokens are opaque 256-bit random values (Windows CSPRNG via
  `BCryptGenRandom`), stored server-side with an expiry, not JWTs — this
  keeps the "instantly revocable on logout" property that stateless JWTs
  give up.
- All SQL queries use parameterized `?` placeholders with proper escaping —
  no string-concatenated SQL anywhere in the codebase.
- The smart contract's `vote()` function has no way to be called on a
  voter's behalf — `msg.sender` is always the connected wallet, enforced
  by the EVM itself, not just application logic.

## License

MIT — see individual file headers. Built for educational purposes.
