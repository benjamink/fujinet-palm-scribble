import sqlite3

import pytest

from app import db


def test_init_db_is_idempotent(conn):
    db.init_db(conn)
    db.init_db(conn)


def test_foreign_keys_enforced(conn):
    with pytest.raises(sqlite3.IntegrityError):
        conn.execute("INSERT INTO deliveries (note_id, recipient_id) VALUES (999, 999)")
