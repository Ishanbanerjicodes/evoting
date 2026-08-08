-- ============================================================================
--  Blockchain-Based E-Voting System — Database Schema
--  Engine   : MySQL 8.0+
--  Charset  : utf8mb4
--  Author   : Ishan
--
--  Run this file once to create the database and all tables:
--      mysql -u root -p < schema.sql
-- ============================================================================

DROP DATABASE IF EXISTS evoting_db;
CREATE DATABASE evoting_db
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE evoting_db;

-- ============================================================================
-- TABLE: users
--  Stores both Admin and Voter accounts. Role-based access is driven by
--  the `role` column. Passwords are never stored in plaintext — only the
--  SHA-256 hash (with a per-user salt) is persisted.
-- ============================================================================
CREATE TABLE users (
    user_id             INT AUTO_INCREMENT PRIMARY KEY,
    full_name           VARCHAR(120)        NOT NULL,
    email               VARCHAR(150)        NOT NULL UNIQUE,
    voter_id_number     VARCHAR(50)         NOT NULL UNIQUE COMMENT 'Government issued voter ID / national ID',
    password_hash       CHAR(64)            NOT NULL COMMENT 'SHA-256 hex digest of salted password',
    password_salt       CHAR(32)            NOT NULL COMMENT 'Random hex salt, unique per user',
    role                ENUM('admin','voter') NOT NULL DEFAULT 'voter',
    wallet_address       VARCHAR(42)         NULL COMMENT 'Linked MetaMask/Ethereum address (0x...)',
    is_verified         TINYINT(1)          NOT NULL DEFAULT 0 COMMENT 'Set to 1 after OTP email verification',
    is_active           TINYINT(1)          NOT NULL DEFAULT 1 COMMENT 'Admin can disable an account',
    created_at          TIMESTAMP           NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          TIMESTAMP           NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    INDEX idx_users_email (email),
    INDEX idx_users_role (role),
    INDEX idx_users_voter_id (voter_id_number)
) ENGINE = InnoDB;

-- ============================================================================
-- TABLE: otp_codes
--  One-time-passcodes used for email verification during registration and
--  for step-up verification before a vote is cast.
-- ============================================================================
CREATE TABLE otp_codes (
    otp_id          INT AUTO_INCREMENT PRIMARY KEY,
    user_id         INT             NOT NULL,
    otp_code        CHAR(6)         NOT NULL,
    purpose         ENUM('registration','login','vote_confirmation','password_reset') NOT NULL,
    is_used         TINYINT(1)      NOT NULL DEFAULT 0,
    expires_at      TIMESTAMP       NOT NULL,
    created_at      TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT fk_otp_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    INDEX idx_otp_user_purpose (user_id, purpose),
    INDEX idx_otp_expiry (expires_at)
) ENGINE = InnoDB;

-- ============================================================================
-- TABLE: sessions
--  Server-side session/token table backing the REST API's bearer-token
--  authentication. Tokens are opaque random strings; expiry is enforced
--  at the application layer as well as here.
-- ============================================================================
CREATE TABLE sessions (
    session_id      INT AUTO_INCREMENT PRIMARY KEY,
    user_id         INT             NOT NULL,
    session_token   CHAR(64)        NOT NULL UNIQUE COMMENT 'Random hex token returned to client',
    ip_address      VARCHAR(45)     NULL,
    user_agent      VARCHAR(255)    NULL,
    expires_at      TIMESTAMP       NOT NULL,
    created_at      TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT fk_session_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    INDEX idx_session_token (session_token),
    INDEX idx_session_expiry (expires_at)
) ENGINE = InnoDB;

-- ============================================================================
-- TABLE: elections
--  An election is a named voting event with a defined time window.
--  `contract_address` links the election to its on-chain counterpart
--  deployed via Hardhat, so results can be verified independently on the
--  blockchain in addition to the MySQL copy.
-- ============================================================================
CREATE TABLE elections (
    election_id         INT AUTO_INCREMENT PRIMARY KEY,
    title                VARCHAR(150)    NOT NULL,
    description          TEXT            NULL,
    contract_address     VARCHAR(42)     NULL COMMENT 'Deployed smart contract address for this election',
    start_time           DATETIME        NOT NULL,
    end_time             DATETIME        NOT NULL,
    status               ENUM('draft','upcoming','active','ended','cancelled') NOT NULL DEFAULT 'draft',
    created_by           INT             NOT NULL COMMENT 'Admin user_id who created the election',
    created_at           TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at           TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    CONSTRAINT fk_election_creator FOREIGN KEY (created_by) REFERENCES users(user_id),
    CONSTRAINT chk_election_dates CHECK (end_time > start_time),
    INDEX idx_election_status (status),
    INDEX idx_election_dates (start_time, end_time)
) ENGINE = InnoDB;

-- ============================================================================
-- TABLE: candidates
--  Candidates are always scoped to a single election.
-- ============================================================================
CREATE TABLE candidates (
    candidate_id     INT AUTO_INCREMENT PRIMARY KEY,
    election_id      INT             NOT NULL,
    full_name        VARCHAR(120)    NOT NULL,
    party_name       VARCHAR(120)    NULL,
    symbol_url       VARCHAR(255)    NULL COMMENT 'Path/URL to party symbol or candidate photo',
    bio              TEXT            NULL,
    on_chain_index   INT             NOT NULL COMMENT 'Index of this candidate inside the smart contract array',
    vote_count_cache INT             NOT NULL DEFAULT 0 COMMENT 'Denormalized count refreshed after each vote, for fast reads',
    created_at       TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT fk_candidate_election FOREIGN KEY (election_id) REFERENCES elections(election_id) ON DELETE CASCADE,
    UNIQUE KEY uq_election_chain_index (election_id, on_chain_index),
    INDEX idx_candidate_election (election_id)
) ENGINE = InnoDB;

-- ============================================================================
-- TABLE: votes
--  One row per cast vote. The unique key on (election_id, user_id)
--  physically prevents double voting at the database layer, in addition
--  to the smart contract's own `hasVoted` mapping on-chain.
--  `tx_hash` and `block_number` prove the vote's on-chain anchor.
-- ============================================================================
CREATE TABLE votes (
    vote_id          INT AUTO_INCREMENT PRIMARY KEY,
    election_id      INT             NOT NULL,
    candidate_id     INT             NOT NULL,
    user_id          INT             NOT NULL,
    voter_wallet     VARCHAR(42)     NOT NULL COMMENT 'MetaMask address that signed the vote transaction',
    tx_hash          VARCHAR(66)     NOT NULL UNIQUE COMMENT 'Ethereum transaction hash of the on-chain vote',
    block_number     BIGINT          NULL,
    vote_hash        CHAR(64)        NOT NULL COMMENT 'SHA-256 fingerprint of vote payload, for audit integrity',
    cast_at          TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT fk_vote_election  FOREIGN KEY (election_id)  REFERENCES elections(election_id)  ON DELETE CASCADE,
    CONSTRAINT fk_vote_candidate FOREIGN KEY (candidate_id) REFERENCES candidates(candidate_id) ON DELETE CASCADE,
    CONSTRAINT fk_vote_user      FOREIGN KEY (user_id)      REFERENCES users(user_id),
    UNIQUE KEY uq_one_vote_per_election (election_id, user_id) COMMENT 'DB-level double-vote prevention',
    INDEX idx_vote_election (election_id),
    INDEX idx_vote_candidate (candidate_id)
) ENGINE = InnoDB;

-- ============================================================================
-- TABLE: audit_logs
--  Immutable-style append-only log of security-relevant actions, used for
--  post-election audits and to demonstrate traceability in the report/demo.
-- ============================================================================
CREATE TABLE audit_logs (
    log_id           BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id          INT             NULL COMMENT 'NULL for unauthenticated/system events',
    action           VARCHAR(80)     NOT NULL COMMENT 'e.g. LOGIN_SUCCESS, VOTE_CAST, ELECTION_CREATED',
    details          TEXT            NULL,
    ip_address       VARCHAR(45)     NULL,
    created_at       TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT fk_audit_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE SET NULL,
    INDEX idx_audit_action (action),
    INDEX idx_audit_created (created_at)
) ENGINE = InnoDB;

-- ============================================================================
-- SEED DATA
--  A default admin account so the system is usable immediately after setup.
--
--      Login email    : admin@evoting.local
--      Login password : Admin@123   (CHANGE THIS immediately after first login)
--
--  Hashing scheme (must match backend/src/crypto.cpp -> hashPassword()):
--      password_hash = SHA256( password + password_salt )
--  The hash below was pre-computed for password "Admin@123" and the salt
--  on this row, so login works immediately with zero extra setup.
-- ============================================================================
INSERT INTO users (full_name, email, voter_id_number, password_hash, password_salt, role, is_verified, is_active)
VALUES (
    'System Administrator',
    'admin@evoting.local',
    'ADMIN000001',
    'ead7547d8ad1f94cc6f30bb687b6788da9353d7a01f9d18d74eeba52ad04acd9',
    '5f3a9c1e8b2d4f60a7c3e9d1b6f80215',
    'admin',
    1,
    1
);

-- ============================================================================
-- VIEW: election_results
--  Convenience view used by the /results endpoint — joins candidates with
--  their cached vote counts and computes vote share percentage.
-- ============================================================================
CREATE VIEW election_results AS
SELECT
    c.election_id,
    c.candidate_id,
    c.full_name,
    c.party_name,
    c.vote_count_cache,
    ROUND(
        c.vote_count_cache * 100.0 /
        NULLIF((SELECT SUM(c2.vote_count_cache) FROM candidates c2 WHERE c2.election_id = c.election_id), 0),
        2
    ) AS vote_percentage
FROM candidates c;

-- ============================================================================
-- TRIGGER: after a vote is inserted, keep candidates.vote_count_cache in
-- sync so read-heavy endpoints (results, dashboards) don't need COUNT(*)
-- ============================================================================
DELIMITER $$

CREATE TRIGGER trg_after_vote_insert
AFTER INSERT ON votes
FOR EACH ROW
BEGIN
    UPDATE candidates
    SET vote_count_cache = vote_count_cache + 1
    WHERE candidate_id = NEW.candidate_id;
END$$

DELIMITER ;

-- ============================================================================
-- End of schema.sql
-- ============================================================================
