import time

from app import db
from app.constants import NOTE_BYTES

BM = bytes([0xA5]) * NOTE_BYTES


def auth(token):
    return {"Authorization": f"Bearer {token}"}


def send(client, token, handles, body=BM):
    return client.post("/palm/v1/send", content=handles.encode() + b"\n" + body, headers=auth(token))


def test_login_returns_token_and_handle(client, make_user):
    make_user("alice")
    r = client.post("/palm/v1/login", content=b"alice\npassword123\n")
    assert r.status_code == 200
    assert r.headers["content-type"].startswith("text/plain")
    lines = r.text.split("\n")
    assert lines[0] == "ok" and lines[2] == "alice" and lines[3] == ""
    assert len(lines[1]) > 20
    assert client.get("/palm/v1/inbox", headers=auth(lines[1])).text == "ok\n"


def test_login_bad_password_err_line(client, make_user):
    make_user("alice")
    r = client.post("/palm/v1/login", content=b"alice\nwrongwrong\n")
    assert r.status_code == 200
    assert r.text == "err bad login\n"


def test_missing_or_bad_token_is_401_err_auth(client):
    for headers in ({}, auth("nope"), {"Authorization": "Basic abc"}):
        r = client.get("/palm/v1/inbox", headers=headers)
        assert r.status_code == 401
        assert r.text == "err auth\n"


def test_send_then_inbox_line_format(client, make_user):
    _, ta = make_user("alice")
    _, tb = make_user("bob")
    before = int(time.time())
    r = send(client, ta, "bob")
    assert r.status_code == 200 and r.text == "ok\n1\n"
    lines = client.get("/palm/v1/inbox?offset=0&limit=20", headers=auth(tb)).text
    head, unix, sender, unread = lines.rstrip("\n").split("\n")[1].split("|")
    assert lines.startswith("ok\n") and lines.endswith("\n")
    assert (head, sender, unread) == ("1", "alice", "1")
    assert before <= int(unix) <= int(time.time())


def test_fetch_note_is_exactly_2400_bytes_and_marks_read(client, make_user):
    _, ta = make_user("alice")
    _, tb = make_user("bob")
    send(client, ta, "bob")
    r = client.get("/palm/v1/notes/1", headers=auth(tb))
    assert r.headers["content-type"] == "application/octet-stream"
    assert r.content == BM and len(r.content) == 2400
    assert client.get("/palm/v1/inbox", headers=auth(tb)).text.endswith("|alice|0\n")
    assert client.get("/palm/v1/notes/99", headers=auth(tb)).text == "err no such note\n"


def test_send_body_without_newline_or_wrong_size_errs_and_stores_nothing(client, make_user, db_path):
    _, ta = make_user("alice")
    make_user("bob")
    h = auth(ta)
    assert client.post("/palm/v1/send", content=b"bob" + BM, headers=h).text == "err bad sketch\n"
    assert client.post("/palm/v1/send", content=b"bob\n" + BM[:-1], headers=h).text == "err bad sketch\n"
    assert client.post("/palm/v1/send", content=b"bob\n" + BM + b"x", headers=h).text == "err bad sketch\n"
    assert client.post("/palm/v1/send", content=b"", headers=h).text == "err bad sketch\n"
    c = db.connect(db_path)
    assert c.execute("SELECT COUNT(*) FROM notes").fetchone()[0] == 0
    c.close()


def test_send_bad_handles(client, make_user):
    _, ta = make_user("alice")
    assert send(client, ta, "bob").text == "err unknown handle: bob\n"
    r = send(client, ta, "<script>x" * 3)
    assert r.text == "err unknown handle\n"
    assert send(client, ta, "a" * 500).text == "err unknown handle\n"


def test_sent_line_lists_all_recipients(client, make_user):
    _, ta = make_user("alice")
    make_user("bob")
    make_user("carol")
    send(client, ta, "Carol,bob")
    lines = client.get("/palm/v1/sent", headers=auth(ta)).text.split("\n")
    assert lines[0] == "ok"
    _, _, hs = lines[1].split("|")
    assert hs == "bob,carol"


def test_revoked_token_next_request_401(client, make_user, db_path):
    uid, t = make_user("alice")
    assert client.get("/palm/v1/inbox", headers=auth(t)).status_code == 200
    c = db.connect(db_path)
    c.execute("DELETE FROM device_tokens WHERE user_id = ?", (uid,))
    c.commit()
    c.close()
    r = client.get("/palm/v1/inbox", headers=auth(t))
    assert r.status_code == 401 and r.text == "err auth\n"


def test_disabled_user_401(client, make_user, db_path):
    uid, t = make_user("alice")
    c = db.connect(db_path)
    c.execute("UPDATE users SET disabled = 1 WHERE id = ?", (uid,))
    c.commit()
    c.close()
    r = client.get("/palm/v1/inbox", headers=auth(t))
    assert r.status_code == 401 and r.text == "err auth\n"


def test_directory_lines(client, make_user):
    _, t = make_user("alice")
    make_user("albert")
    make_user("bob")
    assert client.get("/palm/v1/directory?prefix=al", headers=auth(t)).text == "ok\nalbert\nalice\n"
    assert client.get("/palm/v1/directory", headers=auth(t)).text == "ok\nalbert\nalice\nbob\n"


def test_delete_ok(client, make_user):
    _, ta = make_user("alice")
    _, tb = make_user("bob")
    send(client, ta, "bob")
    assert client.delete("/palm/v1/notes/1", headers=auth(tb)).text == "ok\n"
    assert client.get("/palm/v1/inbox", headers=auth(tb)).text == "ok\n"
    assert client.delete("/palm/v1/notes/1", headers=auth(tb)).text == "err no such note\n"


def test_validation_errors_are_plain_text_err(client, make_user):
    _, tok = make_user("alice")
    r = client.get("/palm/v1/inbox?offset=abc", headers=auth(tok))
    assert r.headers["content-type"].startswith("text/plain")
    assert r.text == "err bad request\n"
    r = client.get("/palm/v1/notes/xyz", headers=auth(tok))
    assert r.text == "err bad request\n"


def test_sketch_with_0x0a_bytes_round_trips(client, make_user):
    _, ta = make_user("alice")
    _, tb = make_user("bob")
    bm = bytes([0x0A, 0x00, 0x0D, 0x0A, 0xFF, 0x0A]) * 400
    assert len(bm) == NOTE_BYTES
    r = send(client, ta, "bob", bm)
    assert r.text.startswith("ok\n")
    nid = r.text.split("\n")[1]
    got = client.get(f"/palm/v1/notes/{nid}", headers=auth(tb))
    assert got.content == bm
    assert client.get(f"/palm/v1/notes/{nid}", headers=auth(ta)).content == bm


def test_stranger_gets_same_response_as_missing_note(client, make_user):
    _, ta = make_user("alice")
    make_user("bob")
    _, tc = make_user("carol")
    nid = send(client, ta, "bob").text.split("\n")[1]
    real = client.get(f"/palm/v1/notes/{nid}", headers=auth(tc))
    missing = client.get("/palm/v1/notes/99999", headers=auth(tc))
    assert (real.status_code, real.headers["content-type"], real.content) == (
        missing.status_code, missing.headers["content-type"], missing.content)
    assert real.text == "err no such note\n"
    # a stranger's delete is also indistinguishable
    assert client.delete(f"/palm/v1/notes/{nid}", headers=auth(tc)).text == "err no such note\n"


def test_send_handle_with_embedded_newline_or_control_rejected(client, make_user):
    make_user("alice")
    make_user("bob")
    t = client.post("/palm/v1/login", content=b"alice\npassword123\n").text.split("\n")[1]
    for bad in ("bob\x00", "bo\x0bb", "bo\x1fb"):
        assert send(client, t, bad).text == "err unknown handle\n"
    assert client.get("/palm/v1/directory?prefix=bob%0a", headers=auth(t)).text == "ok\n"
