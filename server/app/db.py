import sqlite3

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    handle TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    disabled INTEGER NOT NULL DEFAULT 0,
    created INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS invites (
    code TEXT PRIMARY KEY,
    created INTEGER NOT NULL,
    expires INTEGER NOT NULL,
    used_by INTEGER REFERENCES users(id),
    used_at INTEGER
);
CREATE TABLE IF NOT EXISTS device_tokens (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    token_hash TEXT NOT NULL UNIQUE,
    label TEXT NOT NULL DEFAULT '',
    created INTEGER NOT NULL,
    last_used INTEGER
);
CREATE TABLE IF NOT EXISTS web_sessions (
    id_hash TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id),
    created INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS notes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sender_id INTEGER NOT NULL REFERENCES users(id),
    created INTEGER NOT NULL,
    bitmap BLOB NOT NULL,
    sender_deleted INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS deliveries (
    note_id INTEGER NOT NULL REFERENCES notes(id),
    recipient_id INTEGER NOT NULL REFERENCES users(id),
    read_at INTEGER,
    deleted INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (note_id, recipient_id)
);
CREATE TABLE IF NOT EXISTS login_failures (
    username TEXT NOT NULL,
    ip TEXT NOT NULL,
    at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_deliveries_recipient ON deliveries(recipient_id);
CREATE INDEX IF NOT EXISTS idx_notes_sender ON notes(sender_id, created);
CREATE INDEX IF NOT EXISTS idx_login_failures ON login_failures(username, at);
"""


def connect(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL")
    return conn


def init_db(conn: sqlite3.Connection) -> None:
    conn.executescript(SCHEMA)
    conn.commit()
