cmake_minimum_required(VERSION 3.15)
project(evoting_backend CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# backend/lib holds our own dependency-free simple_http.hpp and
# simple_json.hpp (no vcpkg, no Conan, no large third-party downloads) —
# that is the "almost zero setup" goal. backend/include holds the
# project's own controllers/services.
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/lib)

# ---------------------------------------------------------------------------
# Windows-first: statically link the MinGW runtime so the produced .exe
# runs on a machine that only has the MinGW libs used to build it, and so
# nothing depends on Linux-style shared libs that don't exist on Windows.
# ---------------------------------------------------------------------------
if (WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0601)
    if (MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static -lwsock32 -lws2_32")
    endif()
endif()

# ---------------------------------------------------------------------------
# MySQL detection
#   1) Honour -DMYSQL_ROOT_DIR=... if the user passes it explicitly.
#   2) Otherwise probe the default Windows install location.
#   3) Never probe /usr/include or /usr/lib — this project is Windows-only.
# ---------------------------------------------------------------------------
if (NOT MYSQL_ROOT_DIR)
    if (WIN32)
        set(MYSQL_ROOT_DIR "C:/Program Files/MySQL/MySQL Server 8.0")
    endif()
endif()

find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h
    PATHS "${MYSQL_ROOT_DIR}/include"
    NO_DEFAULT_PATH
)

find_library(MYSQL_LIBRARY
    NAMES libmysql mysqlclient
    PATHS "${MYSQL_ROOT_DIR}/lib"
    NO_DEFAULT_PATH
)

if (NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(WARNING
        "MySQL not found at '${MYSQL_ROOT_DIR}'. "
        "If you installed MySQL somewhere else, re-run CMake with:\n"
        "    cmake .. -DMYSQL_ROOT_DIR=\"C:/path/to/MySQL/MySQL Server 8.0\"\n"
    )
else()
    message(STATUS "Found MySQL include dir: ${MYSQL_INCLUDE_DIR}")
    message(STATUS "Found MySQL library:     ${MYSQL_LIBRARY}")
endif()

# ---------------------------------------------------------------------------
# OpenSSL detection
#   1) Honour -DOPENSSL_ROOT_DIR=... if passed explicitly.
#   2) Otherwise probe the default "OpenSSL-Win64" install location used by
#      the Shining Light / Win64 OpenSSL installer, which is what every
#      Windows setup guide for this stack points people to.
# ---------------------------------------------------------------------------
if (NOT OPENSSL_ROOT_DIR)
    if (WIN32)
        set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")
    endif()
endif()

find_package(OpenSSL QUIET)

if (NOT OPENSSL_FOUND)
    # Manual fallback probe in case find_package's default module doesn't
    # pick up the Win64 installer layout. We need BOTH libssl (TLS, used
    # by the SMTP email client for OTP delivery) and libcrypto (SHA-256
    # password hashing) here — find_package's normal path already links
    # both automatically, this fallback just mirrors that.
    find_path(OPENSSL_INCLUDE_DIR
        NAMES openssl/sha.h
        PATHS "${OPENSSL_ROOT_DIR}/include"
        NO_DEFAULT_PATH
    )
    find_library(OPENSSL_CRYPTO_LIBRARY
        NAMES libcrypto crypto libcrypto.lib
        PATHS "${OPENSSL_ROOT_DIR}/lib" "${OPENSSL_ROOT_DIR}/lib/VC/x64/MT"
        NO_DEFAULT_PATH
    )
    find_library(OPENSSL_SSL_LIBRARY
        NAMES libssl ssl libssl.lib
        PATHS "${OPENSSL_ROOT_DIR}/lib" "${OPENSSL_ROOT_DIR}/lib/VC/x64/MT"
        NO_DEFAULT_PATH
    )
    if (OPENSSL_INCLUDE_DIR AND OPENSSL_CRYPTO_LIBRARY AND OPENSSL_SSL_LIBRARY)
        set(OPENSSL_FOUND TRUE)
        set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
    endif()
endif()

if (NOT OPENSSL_FOUND)
    message(WARNING
        "OpenSSL not found at '${OPENSSL_ROOT_DIR}'. "
        "If you installed OpenSSL somewhere else, re-run CMake with:\n"
        "    cmake .. -DOPENSSL_ROOT_DIR=\"C:/path/to/OpenSSL-Win64\"\n"
    )
else()
    message(STATUS "Found OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "Found OpenSSL libraries:   ${OPENSSL_LIBRARIES}")
endif()

include_directories(${MYSQL_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE EVOTING_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(evoting_server ${EVOTING_SOURCES})

target_link_libraries(evoting_server
    ${MYSQL_LIBRARY}
    ${OPENSSL_LIBRARIES}
)

if (WIN32)
    # ws2_32: Winsock (used by the SMTP client's raw TCP connection).
    # crypt32: Windows certificate store (used by the TLS/SSL handshake).
    # bcrypt:  Windows CNG API - crypto.cpp calls BCryptGenRandom() for
    #          secure random number generation. crypto.cpp also has a
    #          `#pragma comment(lib, "bcrypt.lib")`, but that pragma is an
    #          MSVC-only auto-link trick that MinGW's GCC silently ignores,
    #          so it must be linked explicitly here too or you'll get
    #          "undefined reference to `BCryptGenRandom'" at link time.
    target_link_libraries(evoting_server ws2_32 crypt32 bcrypt)
endif()

# ---------------------------------------------------------------------------
# Copy config.json next to the built .exe automatically, so `cmake --build .`
# followed directly by `.\evoting_server.exe` just works — no manual copy
# step required, matching the project's "almost zero setup" goal.
# ---------------------------------------------------------------------------
add_custom_command(TARGET evoting_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json" "$<TARGET_FILE_DIR:evoting_server>"
    COMMENT "Copying config.json next to evoting_server.exe"
)

# ---------------------------------------------------------------------------
# Copy backend/.env (SMTP/DB secrets, if the developer created one from
# .env.example) next to the .exe too, using copy_if_different so it's a
# no-op — and doesn't fail the build — when .env doesn't exist yet.
# ---------------------------------------------------------------------------
if (EXISTS "${CMAKE_SOURCE_DIR}/.env")
    add_custom_command(TARGET evoting_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/.env" "$<TARGET_FILE_DIR:evoting_server>"
        COMMENT "Copying .env next to evoting_server.exe"
    )
endif()

# ---------------------------------------------------------------------------
# Copy the MySQL client DLL next to the .exe on Windows so the binary can
# be double-clicked / run without editing PATH manually.
# ---------------------------------------------------------------------------
if (WIN32)
    find_file(MYSQL_DLL
        NAMES libmysql.dll
        PATHS "${MYSQL_ROOT_DIR}/lib"
        NO_DEFAULT_PATH
    )
    if (MYSQL_DLL)
        add_custom_command(TARGET evoting_server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MYSQL_DLL}" "$<TARGET_FILE_DIR:evoting_server>"
            COMMENT "Copying libmysql.dll next to evoting_server.exe"
        )
    endif()
endif()

message(STATUS "==================================================")
message(STATUS " evoting_backend configured successfully")
message(STATUS " Build with: cmake --build .")
message(STATUS " Run with:   .\\evoting_server.exe   (from build dir)")
message(STATUS "==================================================")
