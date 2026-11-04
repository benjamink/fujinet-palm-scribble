import hashlib
import secrets
import sqlite3
import time

from argon2 import PasswordHasher
from argon2.exceptions import InvalidHashError, VerifyMismatchError

from .constants import HANDLE_RE, PASSWORD_RE, USERNAME_RE, LOGIN_FAIL_LIMIT, LOGIN_FAIL_WINDOW, MIN_PASSWORD, WEB_SESSION_TTL
from .errors import ServiceError

_ph = PasswordHasher()
_DUMMY_HASH = _ph.hash("scribble-dummy-password")


def _h(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


def create_invite(conn, expires: int | None = None) -> str:
    """expires is an absolute unix time; None means never (stored as 0)."""
    code = secrets.token_urlsafe(9)
    conn.execute(
        "INSERT INTO invites (code, created, expires) VALUES (?, ?, ?)",
        (code, int(time.time()), expires or 0),
    )
    conn.commit()
    return code


def list_invites(conn):
    return conn.execute("SELECT * FROM invites ORDER BY created DESC, rowid DESC").fetchall()


def signup(conn, invite: str, username: str, password: str, handle: str) -> int:
    now = int(time.time())
    inv = conn.execute("SELECT * FROM invites WHERE code = ?", (invite,)).fetchone()
    if inv is None or inv["used_by"] is not None or (inv["expires"] and inv["expires"] <= now):
        raise ServiceError("bad invite")
    handle = handle.lower()
    username = username.strip().lower()
    if not USERNAME_RE.fullmatch(username):
        raise ServiceError("invalid username")
    if not HANDLE_RE.fullmatch(handle):
        raise ServiceError("invalid handle")
    if len(password) < MIN_PASSWORD:
        raise ServiceError("password too short")
    if not PASSWORD_RE.fullmatch(password):
        raise ServiceError("invalid password")
    if conn.execute("SELECT 1 FROM users WHERE username = ?", (username,)).fetchone():
        raise ServiceError("username taken")
    if conn.execute("SELECT 1 FROM users WHERE handle = ?", (handle,)).fetchone():
        raise ServiceError("handle taken")
    try:
        cur = conn.execute(
            "INSERT INTO users (username, handle, password_hash, created) VALUES (?, ?, ?, ?)",
            (username, handle, _ph.hash(password), now),
        )
        used = conn.execute(
            "UPDATE invites SET used_by = ?, used_at = ? WHERE code = ? AND used_by IS NULL",
            (cur.lastrowid, now, invite),
        )
        if used.rowcount != 1:
            raise ServiceError("bad invite")
        conn.commit()
    except sqlite3.IntegrityError:
        conn.rollback()
        raise ServiceError("username taken")
    except ServiceError:
        conn.rollback()
        raise
    return cur.lastrowid


def verify_login(conn, username: str, password: str, ip: str, now: int | None = None) -> int:
    now = int(time.time()) if now is None else now
    username = username.lower()
    cutoff = now - LOGIN_FAIL_WINDOW
    conn.execute("DELETE FROM login_failures WHERE at <= ?", (cutoff,))
    fails = conn.execute(
        "SELECT COUNT(*) FROM login_failures WHERE username = ? AND ip = ? AND at > ?",
        (username, ip, cutoff),
    ).fetchone()[0]
    if fails >= LOGIN_FAIL_LIMIT:
        conn.commit()
        raise ServiceError("too many attempts", 429)
    user = conn.execute("SELECT * FROM users WHERE username = ?", (username,)).fetchone()
    ok = False
    try:
        _ph.verify(user["password_hash"] if user else _DUMMY_HASH, password)
        ok = user is not None
    except (VerifyMismatchError, InvalidHashError):
        pass
    if not ok:
        conn.execute("INSERT INTO login_failures (username, ip, at) VALUES (?, ?, ?)", (username, ip, now))
        conn.commit()
        raise ServiceError("bad login")
    conn.commit()
    if user["disabled"]:
        raise ServiceError("account disabled")
    return user["id"]


def issue_device_token(conn, user_id: int, label: str) -> str:
    token = secrets.token_urlsafe(32)
    conn.execute(
        "INSERT INTO device_tokens (user_id, token_hash, label, created) VALUES (?, ?, ?, ?)",
        (user_id, _h(token), label, int(time.time())),
    )
    conn.commit()
    return token


def user_for_token(conn, token: str):
    th = _h(token)
    row = conn.execute(
        "SELECT u.* FROM device_tokens d JOIN users u ON u.id = d.user_id "
        "WHERE d.token_hash = ? AND u.disabled = 0",
        (th,),
    ).fetchone()
    if row is not None:
        conn.execute("UPDATE device_tokens SET last_used = ? WHERE token_hash = ?", (int(time.time()), th))
        conn.commit()
    return row


def list_devices(conn, user_id):
    return conn.execute(
        "SELECT id, label, created, last_used FROM device_tokens WHERE user_id = ? ORDER BY id",
        (user_id,),
    ).fetchall()


def revoke_device(conn, user_id, device_id) -> None:
    conn.execute("DELETE FROM device_tokens WHERE id = ? AND user_id = ?", (device_id, user_id))
    conn.commit()


def revoke_all_devices(conn, user_id) -> None:
    conn.execute("DELETE FROM device_tokens WHERE user_id = ?", (user_id,))
    conn.commit()


def create_web_session(conn, user_id) -> str:
    sid = secrets.token_urlsafe(32)
    conn.execute(
        "INSERT INTO web_sessions (id_hash, user_id, created) VALUES (?, ?, ?)",
        (_h(sid), user_id, int(time.time())),
    )
    conn.commit()
    return sid


def user_for_session(conn, sid: str, now: int | None = None):
    now = int(time.time()) if now is None else now
    return conn.execute(
        "SELECT u.* FROM web_sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.id_hash = ? AND u.disabled = 0 AND s.created > ?",
        (_h(sid), now - WEB_SESSION_TTL),
    ).fetchone()


def delete_web_session(conn, sid) -> None:
    conn.execute("DELETE FROM web_sessions WHERE id_hash = ?", (_h(sid),))
    conn.commit()


def set_user_disabled(conn, username: str, disabled: bool) -> None:
    cur = conn.execute("UPDATE users SET disabled = ? WHERE username = ?", (1 if disabled else 0, username.lower()))
    conn.commit()
    if cur.rowcount != 1:
        raise ServiceError("no such user", 404)
