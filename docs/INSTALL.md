# Installation Guide (Windows 10/11)

This guide walks through every prerequisite from a completely clean
Windows machine to a fully running CivicChain instance. Follow it in
order — later steps depend on earlier ones.

**Already done this once and just want to start the app?** Skip to
[`start_project.bat`](../start_project.bat) in the project root —
double-click it (or run it from a terminal) and it will build the
backend if needed, start the Hardhat blockchain node, deploy the
contract if it isn't deployed yet, start the backend server, serve the
frontend, and open it in your browser — all in one step. Steps 1–3
below (installing prerequisites, creating the database, and setting up
`backend\.env`) still need to be done once first.

---

## 1. Install prerequisites

### 1.1 Node.js (LTS)

Download and run the installer from [nodejs.org](https://nodejs.org) —
choose the **LTS** version. Accept all defaults. Verify:

```powershell
node --version
npm --version
```

### 1.2 MySQL 8.0

Download the **MySQL Installer for Windows** from
[dev.mysql.com/downloads/installer](https://dev.mysql.com/downloads/installer/).

- Choose the "Developer Default" or "Server only" setup type.
- When prompted, set a **root password** you'll remember (e.g. `root` for
  local development — you'll enter this in `backend/config.json` later).
- Keep the default install path: `C:\Program Files\MySQL\MySQL Server 8.0`
  — the backend's CMake script looks here automatically.
- Keep the default port `3306`.
- Finish the wizard and let it start the MySQL80 Windows service.

Verify MySQL is running:

```powershell
mysql --version
```

### 1.3 OpenSSL (Win64)

Download the **Win64 OpenSSL v3.x** installer (the full version, not
"Light") from [slproweb.com/products/Win32OpenSSL.html](https://slproweb.com/products/Win32OpenSSL.html).

- Install to the default path: `C:\Program Files\OpenSSL-Win64` — the
  backend's CMake script looks here automatically.
- When asked where to copy OpenSSL DLLs, choose **"The Windows system
  directory"** (simplest option for local development).

### 1.4 MinGW (MinGW-w64 via MSYS2)

The easiest reliable route on modern Windows is [MSYS2](https://www.msys2.org/):

1. Download and run the MSYS2 installer, accept defaults.
2. Open the **MSYS2 MinGW64** terminal (not the plain MSYS2 terminal) from
   the Start menu.
3. Run:
   ```bash
   pacman -Syu
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make
   ```
4. Add `C:\msys64\mingw64\bin` to your Windows **PATH** environment
   variable (Settings → System → About → Advanced system settings →
   Environment Variables → Path → New).
5. Open a **fresh** PowerShell/Command Prompt window and verify:
   ```powershell
   g++ --version
   cmake --version
   ```

### 1.5 CMake

If you installed CMake via MSYS2 above, you already have it. Otherwise,
download the standalone installer from [cmake.org/download](https://cmake.org/download/)
and make sure to check **"Add CMake to system PATH"** during setup.

### 1.6 VS Code

Download from [code.visualstudio.com](https://code.visualstudio.com/).
Recommended extensions (optional, for a nicer dev experience):
- C/C++ (ms-vscode.cpptools)
- Solidity (Juan Blanco)
- Live Server (for previewing the frontend)

---

## 2. Set up the database

Open PowerShell in the project's root folder:

```powershell
mysql -u root -p < database\schema.sql
```

Enter your MySQL root password when prompted. This creates the
`evoting_db` database, all tables, the `election_results` view, the vote
trigger, and a working seed admin account.

**Verify:**
```powershell
mysql -u root -p -e "USE evoting_db; SELECT email, role FROM users;"
```
You should see `admin@evoting.local | admin`.

---

## 3. Configure SMTP for OTP emails

Registration sends a real verification code by email. The backend reads
SMTP credentials **only from environment variables** — never from
`config.json`, never hardcoded, and never committed to git.

### One-time setup (no PowerShell commands needed ever again)

1. Turn on 2-Step Verification on the Google account you'll send from:
   [myaccount.google.com/security](https://myaccount.google.com/security)
2. Create an **App Password**: [myaccount.google.com/apppasswords](https://myaccount.google.com/apppasswords)
   — choose "Mail" as the app, generate it, and copy the 16-character
   password shown (spaces don't matter).
3. Copy `backend\.env.example` to `backend\.env` and fill in real values:

```
SMTP_HOST=smtp.gmail.com
SMTP_PORT=465
SMTP_USERNAME=youraddress@gmail.com
SMTP_PASSWORD=your16charapppassword
```

That's it. `backend\.env` is loaded automatically every time the server
starts (whether you run `evoting_server.exe` directly or via
`start_project.bat`), and it's listed in `.gitignore` so it's never
committed. No `$env:` / `setx` commands, no re-entering credentials in a
new terminal window.

Any other SMTP provider (Outlook, a college mail server, etc.) works the
same way — just change `SMTP_HOST` / `SMTP_PORT` to match it.

**If you skip this step:** registration still works and the account is
still created. The server can't email the code, so — instead of the
registration flow dead-ending — the API returns the verification code
directly and the frontend shows it on-screen in the "Verify your email"
modal, with a clear note that SMTP isn't configured. You'll see the same
warning in the server log. Fill in `backend\.env` and restart the server
whenever you want real emails instead.

---

## 4. Build and run the backend

```powershell
cd backend
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

The `-G "MinGW Makefiles"` flag matters if you also have Visual Studio
installed: without it, `cmake ..` silently picks the Visual Studio
generator instead of MinGW, and the .exe ends up in `build\Debug\`
instead of directly in `build\` — so `config.json`/`.env` (which get
copied next to the .exe) and this guide's `.\evoting_server.exe` command
below would be looking in the wrong folder. Forcing MinGW Makefiles
avoids that entirely.

If CMake reports it couldn't find MySQL or OpenSSL (only happens if you
installed them somewhere other than the default paths above), re-run with
explicit paths:

```powershell
cmake .. -G "MinGW Makefiles" -DMYSQL_ROOT_DIR="D:\MySQL\MySQL Server 8.0" -DOPENSSL_ROOT_DIR="D:\OpenSSL-Win64"
```

**Before running the server**, open `backend\config.json` and make sure
`database.password` matches the MySQL root password you set in step 1.2.
(Edit the copy in `backend\config.json`, not `backend\build\config.json`
— CMake copies it into the build folder automatically on every build.)

Run the server (from the `build` folder, where CMake placed both the
`.exe` and a copy of `config.json` automatically):

```powershell
.\evoting_server.exe
```

You should see:
```
Connected to MySQL database 'evoting_db' at 127.0.0.1:3306
Routes registered. Starting server on 0.0.0.0:8080
API base URL: http://127.0.0.1:8080/api
```

Leave this window open — it's your running backend server.

---

## 5. Deploy the smart contract

Open a **new** PowerShell window:

```powershell
cd blockchain
npm install
npx hardhat node
```

Leave this running too — it's your local Ethereum blockchain (Hardhat
prints 20 test accounts with private keys; you'll use these in MetaMask).

Open **another new** PowerShell window:

```powershell
cd blockchain
npx hardhat run scripts/deploy.js --network localhost
```

This compiles and deploys `EVoting.sol`, seeds three sample candidates,
opens voting, and automatically writes
`frontend\js\contract-config.js` with the deployed address and ABI.

---

## 6. Configure MetaMask

1. Install the [MetaMask](https://metamask.io/) browser extension if you
   haven't already.
2. Add a custom network:
   - Network name: `Hardhat Localhost`
   - RPC URL: `http://127.0.0.1:8545`
   - Chain ID: `31337`
   - Currency symbol: `ETH`

   (The frontend's wallet integration will actually prompt to add this
   automatically the first time you click "Connect Wallet" — you can skip
   manual setup.)
3. Import a test account: in the `npx hardhat node` terminal window,
   copy one of the printed private keys (e.g. Account #1, since Account
   #0 deployed the contract and is the on-chain admin). In MetaMask:
   Account menu → Import Account → paste the private key.

---

## 7. Open the frontend

Simply open `frontend\index.html` in your browser by double-clicking it,
or right-click → "Open with Live Server" in VS Code for auto-reload.

Register a new voter account and verify the OTP. If you configured SMTP
in step 3, the code arrives by email; if not, the app shows it directly
on-screen so you're never blocked. Then log in, connect your imported
MetaMask wallet, and vote.

To manage elections and candidates, log in with the default admin account
(`admin@evoting.local` / `Admin@123`) instead.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `bind() failed ... port already in use` | Another process is using port 8080. Close it, or edit `backend/config.json`'s `server.port`. |
| `MySQL connection failed` | Make sure the MySQL80 service is running (Services app) and `config.json`'s password is correct. |
| CMake can't find MySQL/OpenSSL | Pass `-DMYSQL_ROOT_DIR` / `-DOPENSSL_ROOT_DIR` explicitly, pointing at your actual install folder. |
| "MetaMask is not installed" | Install the browser extension and refresh the page. |
| "Smart contract not deployed yet" | Re-run `npx hardhat run scripts/deploy.js --network localhost` — make sure `npx hardhat node` is still running in another window. |
| Vote transaction fails / reverts | Make sure the connected MetaMask account hasn't already voted, and that the election's status is `active` (set this on the Admin → Elections page). |
| Registration succeeds but no OTP email arrives | Check the server log for `AUTH password` or `MAIL FROM` errors. Confirm `SMTP_HOST`/`SMTP_USERNAME`/`SMTP_PASSWORD` are set in the *same terminal* you launched `evoting_server.exe` from, and that you're using a Gmail **App Password**, not your normal account password. |
| `535` error in server log during SMTP AUTH | Wrong username/app password, or 2-Step Verification isn't enabled on the Gmail account (required for App Passwords to work). |
