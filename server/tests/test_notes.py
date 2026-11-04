import pytest

from app import notes
from app.constants import NOTE_BYTES
from app.errors import ServiceError

BM = bytes(NOTE_BYTES)


def mk(conn, handle, disabled=0):
    cur = conn.execute(
        "INSERT INTO users (username, handle, password_hash, disabled, created) VALUES (?, ?, 'x', ?, 0)",
        (handle, handle, disabled),
    )
    conn.commit()
    return cur.lastrowid


def count(conn, table):
    return conn.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]


def test_send_creates_one_note_and_n_deliveries(conn):
    a, b, c = mk(conn, "alice"), mk(conn, "bob"), mk(conn, "carol")
    nid = notes.send_note(conn, a, ["bob", "carol"], BM, now=100)
    assert count(conn, "notes") == 1
    assert count(conn, "deliveries") == 2
    assert isinstance(nid, int)


def test_recipient_sees_only_self(conn):
    a, b, c = mk(conn, "alice"), mk(conn, "bob"), mk(conn, "carol")
    nid = notes.send_note(conn, a, ["bob", "carol"], BM, now=100)
    r = notes.get_note(conn, b, nid)
    assert r["recipients"] == ["bob"]
    assert r["role"] == "recipient"
    assert r["sender"] == "alice"
    assert r["bitmap"] == BM
    s = notes.get_note(conn, a, nid)
    assert sorted(s["recipients"]) == ["bob", "carol"]
    assert s["role"] == "sender"


@pytest.mark.parametrize(
    "handles,bitmap",
    [
        (["nobody"], BM),
        (["bob", "BOB"], BM),
        (["alice"], BM),
        ([f"u{i:02d}x" for i in range(11)], BM),
        ([], BM),
        (["bob"], bytes(2401)),
    ],
)
def test_rejected_send_stores_nothing(conn, handles, bitmap):
    a = mk(conn, "alice")
    mk(conn, "bob")
    for i in range(11):
        mk(conn, f"u{i:02d}x")
    with pytest.raises(ServiceError):
        notes.send_note(conn, a, handles, bitmap, now=100)
    assert count(conn, "notes") == 0
    assert count(conn, "deliveries") == 0


def test_reject_reasons(conn):
    a = mk(conn, "alice")
    mk(conn, "bob")
    cases = [
        ([], "no recipients"),
        (["zed"], "unknown handle: zed"),
        (["bob", "Bob"], "duplicate recipient"),
        (["ALICE"], "cannot send to yourself"),
        ([f"h{i}" for i in range(11)], "too many recipients"),
    ]
    for hs, reason in cases:
        with pytest.raises(ServiceError) as e:
            notes.send_note(conn, a, hs, BM, now=1)
        assert e.value.reason == reason
    with pytest.raises(ServiceError) as e:
        notes.send_note(conn, a, ["bob"], b"x", now=1)
    assert e.value.reason == "bad sketch"


def test_handles_case_insensitive(conn):
    a, b = mk(conn, "alice"), mk(conn, "bob")
    nid = notes.send_note(conn, a, ["BOB"], BM, now=1)
    assert notes.get_note(conn, b, nid)["recipients"] == ["bob"]


def test_rate_limit_31st_send_in_an_hour_rejected_and_ok_after_hour(conn):
    a = mk(conn, "alice")
    mk(conn, "bob")
    for _ in range(30):
        notes.send_note(conn, a, ["bob"], BM, now=10000)
    with pytest.raises(ServiceError) as e:
        notes.send_note(conn, a, ["bob"], BM, now=10001)
    assert e.value.reason == "rate limit" and e.value.status == 429
    assert count(conn, "notes") == 30
    notes.send_note(conn, a, ["bob"], BM, now=10000 + 3601)
    assert count(conn, "notes") == 31


def test_get_note_by_stranger_same_error_as_missing(conn):
    a, b, c = mk(conn, "alice"), mk(conn, "bob"), mk(conn, "carol")
    nid = notes.send_note(conn, a, ["bob"], BM, now=1)
    for fn in (notes.get_note, notes.delete_note):
        errs = []
        for uid, n in ((c, nid), (c, 9999)):
            with pytest.raises(ServiceError) as e:
                fn(conn, uid, n)
            errs.append((e.value.reason, e.value.status))
        assert errs[0] == errs[1] == ("no such note", 404)
    # stranger delete must not have touched anything
    assert notes.get_note(conn, b, nid)["id"] == nid
    assert notes.get_note(conn, a, nid)["id"] == nid


def test_deleted_copy_is_no_such_note(conn):
    a, b = mk(conn, "alice"), mk(conn, "bob")
    nid = notes.send_note(conn, a, ["bob"], BM, now=1)
    notes.delete_note(conn, b, nid)
    with pytest.raises(ServiceError) as e:
        notes.get_note(conn, b, nid)
    assert (e.value.reason, e.value.status) == ("no such note", 404)
    with pytest.raises(ServiceError):
        notes.delete_note(conn, b, nid)


def test_get_marks_read_only_for_recipient_and_only_when_asked(conn):
    a, b = mk(conn, "alice"), mk(conn, "bob")
    nid = notes.send_note(conn, a, ["bob"], BM, now=1)

    def read_at():
        return conn.execute("SELECT read_at FROM deliveries WHERE note_id = ?", (nid,)).fetchone()[0]

    notes.get_note(conn, a, nid)
    assert read_at() is None
    notes.get_note(conn, b, nid, mark_read=False)
    assert read_at() is None
    assert notes.inbox(conn, b, 0, 10)[0]["read"] is False
    notes.get_note(conn, b, nid)
    assert read_at() is not None
    assert notes.inbox(conn, b, 0, 10)[0]["read"] is True


def test_delete_is_per_copy(conn):
    a, b = mk(conn, "alice"), mk(conn, "bob")
    n1 = notes.send_note(conn, a, ["bob"], BM, now=1)
    notes.delete_note(conn, b, n1)
    assert notes.inbox(conn, b, 0, 10) == []
    assert [x["id"] for x in notes.sent(conn, a, 0, 10)] == [n1]
    n2 = notes.send_note(conn, a, ["bob"], BM, now=2)
    notes.delete_note(conn, a, n2)
    assert [x["id"] for x in notes.sent(conn, a, 0, 10)] == [n1]
    assert [x["id"] for x in notes.inbox(conn, b, 0, 10)] == [n2]
    assert notes.get_note(conn, b, n2)["id"] == n2


def test_inbox_hides_deleted_and_paginates_newest_first(conn):
    a, b = mk(conn, "alice"), mk(conn, "bob")
    ids = [notes.send_note(conn, a, ["bob"], BM, now=100 + i) for i in range(5)]
    same = notes.send_note(conn, a, ["bob"], BM, now=104)  # same created as ids[4]
    notes.delete_note(conn, b, ids[2])
    box = notes.inbox(conn, b, 0, 50)
    assert [x["id"] for x in box] == [same, ids[4], ids[3], ids[1], ids[0]]
    assert set(box[0]) == {"id", "created", "sender", "read"}
    assert box[0]["sender"] == "alice"
    assert [x["id"] for x in notes.inbox(conn, b, 1, 2)] == [ids[4], ids[3]]
    s = notes.sent(conn, a, 0, 2)
    assert set(s[0]) == {"id", "created", "recipients"}
    assert s[0]["recipients"] == ["bob"]


def test_limit_clamped_to_50(conn):
    a, b = mk(conn, "alice"), mk(conn, "bob")
    conn.executemany(
        "INSERT INTO notes (sender_id, created, bitmap) VALUES (?, ?, ?)", [(a, i, BM) for i in range(60)]
    )
    conn.execute("INSERT INTO deliveries (note_id, recipient_id) SELECT id, ? FROM notes", (b,))
    conn.commit()
    assert len(notes.inbox(conn, b, 0, 1000)) == 50
    assert len(notes.sent(conn, a, 0, 1000)) == 50


def test_disabled_recipient_cannot_be_addressed_and_leaves_directory(conn):
    a = mk(conn, "alice")
    mk(conn, "bobby", disabled=1)
    with pytest.raises(ServiceError) as e:
        notes.send_note(conn, a, ["bobby"], BM, now=1)
    assert e.value.reason == "unknown handle: bobby"
    assert notes.directory(conn, "bo", 0, 10) == []
    assert count(conn, "notes") == 0


def test_directory_prefix_is_literal(conn):
    mk(conn, "a_b")
    mk(conn, "axb")
    assert notes.directory(conn, "a_", 0, 10) == ["a_b"]
    assert notes.directory(conn, "%", 0, 10) == []
    assert notes.directory(conn, "A_B", 0, 10) == ["a_b"]
    assert notes.directory(conn, "", 0, 10) == ["a_b", "axb"]
    assert notes.directory(conn, "a", 1, 10) == ["axb"]


def test_rate_limit_check_and_insert_is_race_free(tmp_path):
    import threading

    from app import db

    path = str(tmp_path / "race.db")
    setup = db.connect(path)
    db.init_db(setup)
    a, _ = mk(setup, "alice"), mk(setup, "bob")
    for _ in range(29):
        notes.send_note(setup, a, ["bob"], BM, now=10000)
    results = []
    barrier = threading.Barrier(6)

    def worker():
        c = db.connect(path)
        try:
            barrier.wait()
            notes.send_note(c, a, ["bob"], BM, now=10001)
            results.append("ok")
        except ServiceError as e:
            results.append(e.reason)
        finally:
            c.close()

    threads = [threading.Thread(target=worker) for _ in range(6)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert sorted(results) == ["ok"] + ["rate limit"] * 5
    assert count(setup, "notes") == 30
    setup.close()
