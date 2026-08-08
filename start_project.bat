@echo off
setlocal enabledelayedexpansion
title CivicChain E-Voting - Startup

echo ==================================================================
echo   CivicChain Blockchain E-Voting System - one-click startup
echo ==================================================================
echo.

REM ----------------------------------------------------------------------
REM  This script must be run from the project root (the folder that
REM  contains backend\, blockchain\, frontend\, database\).
REM ----------------------------------------------------------------------
set "ROOT=%~dp0"
cd /d "%ROOT%"

REM ======================================================================
REM  0) Check prerequisites
REM ======================================================================
echo [0/6] Checking prerequisites...

where node >nul 2>nul
if errorlevel 1 (
    echo   [FATAL] Node.js was not found on PATH. Install it from https://nodejs.org
    goto :fail
)
where npm >nul 2>nul
if errorlevel 1 (
    echo   [FATAL] npm was not found on PATH. Reinstall Node.js.
    goto :fail
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo   [FATAL] cmake was not found on PATH. See docs\INSTALL.md section 1.4/1.5.
    goto :fail
)
where g++ >nul 2>nul
if errorlevel 1 (
    echo   [WARN] g++ was not found on PATH ^(needed only if the backend isn't built yet^).
)

if not exist "backend\config.json" (
    echo   [FATAL] backend\config.json not found. Are you running this from the project root?
    goto :fail
)

if not exist "backend\.env" (
    echo   [WARN] backend\.env not found. OTP emails will NOT be sent until you copy
    echo          backend\.env.example to backend\.env and fill in your SMTP details.
    echo          Registration will still work - the verification code will be shown
    echo          on-screen instead of emailed. See docs\INSTALL.md section 3.
)

echo   OK.
echo.

REM ======================================================================
REM  1) Database reminder (cannot be verified/created from this script
REM     without your MySQL root password, so this is a check, not a step)
REM ======================================================================
echo [1/6] Checking MySQL is reachable...
where mysql >nul 2>nul
if errorlevel 1 (
    echo   [WARN] mysql.exe not found on PATH - skipping connectivity check.
    echo          Make sure the MySQL80 service is running and that you have
    echo          already imported database\schema.sql ^(see docs\INSTALL.md section 2^).
) else (
    mysql -u root -proot -e "SELECT 1;" >nul 2>nul
    if errorlevel 1 (
        echo   [WARN] Could not connect to MySQL as root/root. If your password is
        echo          different, that's fine - just make sure backend\config.json
        echo          has the correct password and the MySQL80 service is running.
    ) else (
        echo   OK.
    )
)
echo.

REM ======================================================================
REM  2) Blockchain: install deps, start Hardhat node, compile+deploy
REM ======================================================================
echo [2/6] Setting up the blockchain ^(Hardhat^)...
cd /d "%ROOT%blockchain"

if not exist "node_modules" (
    echo   Installing blockchain dependencies ^(npm install^)...
    call npm install
    if errorlevel 1 (
        echo   [FATAL] npm install failed in blockchain\.
        goto :fail
    )
)

echo   Starting local Hardhat blockchain node in a new window...
start "CivicChain - Hardhat Node" cmd /k "cd /d "%ROOT%blockchain" && npx hardhat node"

echo   Waiting for the Hardhat node to accept connections on port 8545...
set "HARDHAT_READY=0"
for /l %%i in (1,1,30) do (
    if "!HARDHAT_READY!"=="0" (
        powershell -NoProfile -Command "try { (New-Object Net.Sockets.TcpClient).Connect('127.0.0.1',8545); exit 0 } catch { exit 1 }" >nul 2>nul
        if not errorlevel 1 (
            set "HARDHAT_READY=1"
        ) else (
            timeout /t 1 /nobreak >nul
        )
    )
)
if "%HARDHAT_READY%"=="0" (
    echo   [FATAL] Hardhat node did not come up on port 8545 after 30 seconds.
    goto :fail
)
echo   Hardhat node is up.

echo   Deploying the EVoting smart contract...
call npx hardhat run scripts\deploy.js --network localhost
if errorlevel 1 (
    echo   [FATAL] Contract deployment failed. Check the Hardhat node window for details.
    goto :fail
)
echo   Contract deployed and frontend\js\contract-config.js updated.
echo.

REM ======================================================================
REM  3) Backend: build if needed, then start
REM ======================================================================
echo [3/6] Setting up the backend ^(C++^)...
cd /d "%ROOT%backend"

if not exist "build" mkdir build
cd build

if not exist "evoting_server.exe" (
    REM If a Debug\evoting_server.exe already exists here, a previous run
    REM configured CMake with a different (Visual Studio) generator - see
    REM the -G "MinGW Makefiles" note below for why that happens and why
    REM we now force it explicitly.
    if exist "Debug\evoting_server.exe" (
        echo   Found an existing build at backend\build\Debug\evoting_server.exe
        echo   ^(built with a multi-config generator^) - using it as-is.
        copy /y "Debug\evoting_server.exe" . >nul
    ) else (
        echo   No existing build found - configuring and building ^(this can take a
        echo   few minutes the first time^)...
        REM IMPORTANT: force the MinGW Makefiles generator explicitly.
        REM Plain "cmake .." picks CMake's *default* generator, which on a
        REM machine that also has Visual Studio installed is Visual Studio
        REM (a multi-config generator) - NOT MinGW - even with g++ on PATH.
        REM That silently builds evoting_server.exe into build\Debug\
        REM instead of build\, which is exactly the "wrong runtime folder"
        REM problem (config.json/.env copied next to the .exe would then
        REM be copied to the wrong place, and this script's `evoting_server.exe`
        REM launch line below would fail to find it at all). Forcing the
        REM generator here removes that ambiguity for good.
        cmake .. -G "MinGW Makefiles"
        if errorlevel 1 (
            echo   [FATAL] cmake configure failed. See the messages above - you may need
            echo           -DMYSQL_ROOT_DIR / -DOPENSSL_ROOT_DIR ^(see docs\INSTALL.md^).
            goto :fail
        )
        cmake --build .
        if errorlevel 1 (
            echo   [FATAL] Build failed. See the compiler errors above.
            goto :fail
        )
    )
) else (
    echo   Existing build found at backend\build\evoting_server.exe - skipping rebuild.
    echo   ^(Delete backend\build\ if you want a clean rebuild.^)
)

REM Make sure the latest config.json and .env are next to the .exe even if
REM the build was skipped or CMake was configured before .env existed.
copy /y "%ROOT%backend\config.json" . >nul
if exist "%ROOT%backend\.env" copy /y "%ROOT%backend\.env" . >nul

echo   Starting the backend server in a new window...
start "CivicChain - Backend Server" cmd /k "cd /d "%ROOT%backend\build" && evoting_server.exe"

echo   Waiting for the backend to accept connections on port 8080...
set "BACKEND_READY=0"
for /l %%i in (1,1,30) do (
    if "!BACKEND_READY!"=="0" (
        powershell -NoProfile -Command "try { (New-Object Net.Sockets.TcpClient).Connect('127.0.0.1',8080); exit 0 } catch { exit 1 }" >nul 2>nul
        if not errorlevel 1 (
            set "BACKEND_READY=1"
        ) else (
            timeout /t 1 /nobreak >nul
        )
    )
)
if "%BACKEND_READY%"=="0" (
    echo   [WARN] Backend did not respond on port 8080 after 30 seconds. Check the
    echo          "CivicChain - Backend Server" window for a MySQL connection error.
) else (
    echo   Backend is up.
)
echo.

REM ======================================================================
REM  4) Frontend: serve over HTTP (not file://) so fetch()/CORS behave
REM     exactly like a normal deployment, then open the browser
REM ======================================================================
echo [4/6] Starting the frontend server...
cd /d "%ROOT%"

start "CivicChain - Frontend Server" cmd /k "cd /d "%ROOT%frontend" && npx --yes serve -l 5500 ."

echo   Waiting for the frontend server on port 5500...
set "FRONTEND_READY=0"
for /l %%i in (1,1,30) do (
    if "!FRONTEND_READY!"=="0" (
        powershell -NoProfile -Command "try { (New-Object Net.Sockets.TcpClient).Connect('127.0.0.1',5500); exit 0 } catch { exit 1 }" >nul 2>nul
        if not errorlevel 1 (
            set "FRONTEND_READY=1"
        ) else (
            timeout /t 1 /nobreak >nul
        )
    )
)
echo.

echo [5/6] Opening the app in your default browser...
start "" "http://127.0.0.1:5500/index.html"
echo.

echo [6/6] All set.
echo ==================================================================
echo   Hardhat node   : http://127.0.0.1:8545   ^(window: "CivicChain - Hardhat Node"^)
echo   Backend API    : http://127.0.0.1:8080/api   ^(window: "CivicChain - Backend Server"^)
echo   Frontend       : http://127.0.0.1:5500   ^(window: "CivicChain - Frontend Server"^)
echo   Default admin  : admin@evoting.local / Admin@123
echo.
echo   Three new windows were opened for the node, backend, and frontend -
echo   close them (or Ctrl+C inside each) to stop the corresponding service.
echo   This window can be closed safely; the other three keep running.
echo ==================================================================
goto :eof

:fail
echo.
echo Startup stopped due to the error above. Fix it and re-run start_project.bat.
exit /b 1
