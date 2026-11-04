import time

from .bitmap import validate_bitmap
from .constants import MAX_RECIPIENTS, SENDS_PER_HOUR
from .errors import ServiceError

MAX_LIMIT = 50


def _page(offset: int, limit: int) -> tuple[int, int]:
    return max(int(offset), 0), max(min(int(limit), MAX_LIMIT), 0)


def _no_such_note() -> ServiceError:
    return ServiceError("no such note", 404)


def send_note(conn, sender_id: int, handles: list[str], bitmap: bytes, now: int | None = None) -> int:
    now = int(time.time()) if now is None else now
    handles = [h.lower() for h in handles]
    if not handles:
        raise ServiceError("no recipients")
    if len(handles) > MAX_RECIPIENTS:
        raise ServiceError("too many recipients")
    if len(set(handles)) != len(handles):
        raise ServiceError("duplicate recipient")
    validate_bitmap(bitmap)
    ids = []
    for h in handles:
        row = conn.execute("SELECT id FROM users WHERE handle = ? AND disabled = 0", (h,)).fetchone()
        if row is None:
            raise ServiceError(f"unknown handle: {h}")
        if row["id"] == sender_id:
            raise ServiceError("cannot send to yourself")
        ids.append(row["id"])
    try:
        # Take the write lock before counting so concurrent sends cannot both pass the check.
        if conn.in_transaction:
            conn.commit()
        conn.execute("BEGIN IMMEDIATE")
        recent = conn.execute(
            "SELECT COUNT(*) FROM notes WHERE sender_id = ? AND created > ?", (sender_id, now - 3600)
        ).fetchone()[0]
        if recent >= SENDS_PER_HOUR:
            raise ServiceError("rate limit", 429)
        cur = conn.execute(
            "INSERT INTO notes (sender_id, created, bitmap) VALUES (?, ?, ?)", (sender_id, now, bitmap)
        )
        conn.executemany(
            "INSERT INTO deliveries (note_id, recipient_id) VALUES (?, ?)",
            [(cur.lastrowid, r) for r in ids],
        )
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    return cur.lastrowid


def inbox(conn, user_id, offset: int, limit: int) -> list[dict]:
    offset, limit = _page(offset, limit)
    rows = conn.execute(
        "SELECT n.id, n.created, u.handle AS sender, d.read_at "
        "FROM deliveries d JOIN notes n ON n.id = d.note_id JOIN users u ON u.id = n.sender_id "
        "WHERE d.recipient_id = ? AND d.deleted = 0 "
        "ORDER BY n.created DESC, n.id DESC LIMIT ? OFFSET ?",
        (user_id, limit, offset),
    ).fetchall()
    return [
        {"id": r["id"], "created": r["created"], "sender": r["sender"], "read": r["read_at"] is not None}
        for r in rows
    ]


def _recipient_handles(conn, note_id) -> list[str]:
    return [
        r["handle"]
        for r in conn.execute(
            "SELECT u.handle FROM deliveries d JOIN users u ON u.id = d.recipient_id "
            "WHERE d.note_id = ? ORDER BY u.handle",
            (note_id,),
        )
    ]


def sent(conn, user_id, offset: int, limit: int) -> list[dict]:
    offset, limit = _page(offset, limit)
    rows = conn.execute(
        "SELECT id, created FROM notes WHERE sender_id = ? AND sender_deleted = 0 "
        "ORDER BY created DESC, id DESC LIMIT ? OFFSET ?",
        (user_id, limit, offset),
    ).fetchall()
    return [
        {"id": r["id"], "created": r["created"], "recipients": _recipient_handles(conn, r["id"])}
        for r in rows
    ]


def get_note(conn, user_id, note_id: int, mark_read: bool = True) -> dict:
    n = conn.execute(
        "SELECT n.id, n.created, n.bitmap, n.sender_id, n.sender_deleted, u.handle AS sender "
        "FROM notes n JOIN users u ON u.id = n.sender_id WHERE n.id = ?",
        (note_id,),
    ).fetchone()
    if n is None:
        raise _no_such_note()
    if n["sender_id"] == user_id:
        if n["sender_deleted"]:
            raise _no_such_note()
        role, recipients = "sender", _recipient_handles(conn, note_id)
    else:
        d = conn.execute(
            "SELECT u.handle, d.deleted FROM deliveries d JOIN users u ON u.id = d.recipient_id "
            "WHERE d.note_id = ? AND d.recipient_id = ?",
            (note_id, user_id),
        ).fetchone()
        if d is None or d["deleted"]:
            raise _no_such_note()
        role, recipients = "recipient", [d["handle"]]
        if mark_read:
            conn.execute(
                "UPDATE deliveries SET read_at = ? WHERE note_id = ? AND recipient_id = ? AND read_at IS NULL",
                (int(time.time()), note_id, user_id),
            )
            conn.commit()
    return {
        "id": n["id"],
        "created": n["created"],
        "sender": n["sender"],
        "recipients": recipients,
        "bitmap": bytes(n["bitmap"]),
        "role": role,
    }


def delete_note(conn, user_id, note_id) -> None:
    cur = conn.execute(
        "UPDATE notes SET sender_deleted = 1 WHERE id = ? AND sender_id = ? AND sender_deleted = 0",
        (note_id, user_id),
    )
    if cur.rowcount == 0:
        cur = conn.execute(
            "UPDATE deliveries SET deleted = 1 WHERE note_id = ? AND recipient_id = ? AND deleted = 0",
            (note_id, user_id),
        )
    if cur.rowcount == 0:
        conn.rollback()
        raise _no_such_note()
    conn.commit()


def directory(conn, prefix: str, offset: int, limit: int) -> list[str]:
    offset, limit = _page(offset, limit)
    prefix = prefix.lower()
    rows = conn.execute(
        "SELECT handle FROM users WHERE disabled = 0 AND substr(handle, 1, ?) = ? "
        "ORDER BY handle LIMIT ? OFFSET ?",
        (len(prefix), prefix, limit, offset),
    ).fetchall()
    return [r["handle"] for r in rows]
