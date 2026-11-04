import io

from PIL import Image

from app import accounts, db
from app.constants import NOTE_BYTES

BM = bytes([0xA5]) * NOTE_BYTES
H = {"X-Requested-With": "scribble"}


def auth(token):
    return {"Authorization": f"Bearer {token}"}


def palm_send(client, token, handles):
    r = client.post("/palm/v1/send", content=handles.encode() + b"\n" + BM, headers=auth(token))
    assert r.text.startswith("ok"), r.text
    return int(r.text.split("\n")[1])


def web_login(client, username, password="password123"):
    r = client.post("/api/login", json={"username": username, "password": password}, headers=H)
    assert r.status_code == 200, r.text
    return r


def invite(db_path):
    c = db.connect(db_path)
    try:
        return accounts.create_invite(c)
    finally:
        c.close()


def test_signup_login_me(client, db_path):
    code = invite(db_path)
    r = client.post(
        "/api/signup",
        json={"invite": code, "username": "Zed", "password": "password123", "handle": "zed"},
        headers=H,
    )
    assert r.status_code == 200 and r.json() == {"handle": "zed"}
    cookie = r.headers["set-cookie"].lower()
    assert "scribble_session=" in cookie and "httponly" in cookie and "samesite=lax" in cookie
    assert "path=/" in cookie and "secure" not in cookie.replace("samesite", "")
    assert client.get("/api/me").json() == {"handle": "zed"}
    client.cookies.clear()
    assert client.get("/api/me").status_code == 401
    assert web_login(client, "zed").json() == {"handle": "zed"}
    assert client.get("/api/me").json() == {"handle": "zed"}


def test_secure_cookie_flag_when_configured(db_path, tmp_path, make_user):
    import warnings

    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        from fastapi.testclient import TestClient
    from app.config import Settings
    from app.main import create_app

    app = create_app(Settings(db_path=db_path, secure_cookies=True, static_dir=str(tmp_path)))
    with TestClient(app) as c:
        c2 = db.connect(db_path)
        c2.execute(
            "INSERT INTO users (username, handle, password_hash, created) VALUES ('a','a',?,0)",
            (accounts._ph.hash("password123"),),
        )
        c2.commit()
        c2.close()
        r = c.post("/api/login", json={"username": "a", "password": "password123"}, headers=H)
        assert "secure" in r.headers["set-cookie"].lower().replace("samesite", "")


def test_signup_used_invite_400(client, db_path):
    code = invite(db_path)
    body = {"invite": code, "username": "uone", "password": "password123", "handle": "uone"}
    assert client.post("/api/signup", json=body, headers=H).status_code == 200
    client.cookies.clear()
    r = client.post("/api/signup", json={**body, "username": "uu", "handle": "uuuu"}, headers=H)
    assert r.status_code == 400 and r.json() == {"error": "bad invite"}


def test_bad_login_error_shape(client, make_user):
    make_user("alice")
    r = client.post("/api/login", json={"username": "alice", "password": "nopenope"}, headers=H)
    assert r.status_code == 400 and r.json() == {"error": "bad login"}


def test_unauthenticated_401(client):
    for path in ("/api/inbox", "/api/sent", "/api/me", "/api/devices", "/api/notes/1", "/api/notes/1.png"):
        r = client.get(path)
        assert r.status_code == 401 and r.json() == {"error": "not logged in"}, path
    for m, path in (("post", "/api/logout"), ("delete", "/api/notes/1"), ("post", "/api/devices/revoke-all"),
                    ("delete", "/api/devices/1")):
        r = getattr(client, m)(path, headers=H)
        assert r.status_code == 401, path
    client.cookies.set("scribble_session", "bogus")
    assert client.get("/api/me").status_code == 401


def test_csrf_header_and_content_type(client, make_user):
    make_user("alice")
    body = {"username": "alice", "password": "password123"}
    r = client.post("/api/login", json=body)
    assert r.status_code == 403 and r.json() == {"error": "bad request"}
    r = client.post("/api/login", content=b'{"username":"alice","password":"password123"}',
                    headers={**H, "Content-Type": "text/plain"})
    assert r.status_code == 403 and r.json() == {"error": "bad request"}
    web_login(client, "alice")
    for m, path in (("post", "/api/logout"), ("post", "/api/devices/revoke-all"),
                    ("delete", "/api/devices/1"), ("delete", "/api/notes/1")):
        r = getattr(client, m)(path)
        assert r.status_code == 403 and r.json() == {"error": "bad request"}, path
    assert client.get("/api/me").status_code == 200  # still logged in, logout was refused


def test_invalid_input_scoped_to_api(client, make_user):
    make_user("alice")
    r = client.post("/api/login", json={"username": "alice"}, headers=H)
    assert r.status_code == 422 and r.json() == {"error": "invalid input"}
    web_login(client, "alice")
    r = client.get("/api/inbox?limit=abc")
    assert r.status_code == 422 and r.json() == {"error": "invalid input"}
    r = client.get("/palm/v1/inbox?limit=abc", headers=auth("x"))
    assert r.status_code == 401 and r.text == "err auth\n"


def test_png_for_sender_and_recipient_404_for_stranger(client, make_user):
    _, ta = make_user("alice")
    make_user("bob")
    make_user("carol")
    nid = palm_send(client, ta, "bob")
    for who, ok in (("alice", True), ("bob", True), ("carol", False)):
        client.cookies.clear()
        web_login(client, who)
        r = client.get(f"/api/notes/{nid}.png")
        if ok:
            assert r.status_code == 200 and r.headers["content-type"] == "image/png"
            assert r.headers["cache-control"] == "private, no-store"
            img = Image.open(io.BytesIO(r.content))
            assert img.format == "PNG" and img.size == (160, 120)
        else:
            assert r.status_code == 404 and r.json() == {"error": "no such note"}
            assert client.get(f"/api/notes/{nid}").json() == {"error": "no such note"}
    assert client.get("/api/notes/999.png").json() == {"error": "no such note"}
    assert client.get("/api/notes/999").status_code == 404


def test_note_json_and_read_semantics(client, make_user):
    _, ta = make_user("alice")
    _, tb = make_user("bob")
    make_user("carol")
    nid = palm_send(client, ta, "bob,carol")
    web_login(client, "alice")
    j = client.get(f"/api/notes/{nid}").json()
    assert set(j) == {"id", "created", "sender", "recipients", "role"}
    assert j["role"] == "sender" and j["sender"] == "alice" and sorted(j["recipients"]) == ["bob", "carol"]
    client.cookies.clear()
    web_login(client, "bob")
    assert client.get("/api/inbox").json()[0]["read"] is False
    j = client.get(f"/api/notes/{nid}").json()
    assert j["role"] == "recipient" and j["recipients"] == ["bob"]
    assert client.get("/api/inbox").json()[0]["read"] is True


def test_inbox_sent_delete(client, make_user):
    _, ta = make_user("alice")
    make_user("bob")
    n1 = palm_send(client, ta, "bob")
    n2 = palm_send(client, ta, "bob")
    web_login(client, "alice")
    s = client.get("/api/sent").json()
    assert {r["id"] for r in s} == {n1, n2} and set(s[0]) == {"id", "created", "recipients"}
    assert len(client.get("/api/sent?limit=1").json()) == 1
    assert len(client.get("/api/sent?offset=1&limit=5").json()) == 1
    r = client.delete(f"/api/notes/{n1}", headers=H)
    assert r.status_code == 200
    assert client.get(f"/api/notes/{n1}.png").status_code == 404
    assert client.delete(f"/api/notes/{n1}", headers=H).status_code == 404
    client.cookies.clear()
    web_login(client, "bob")
    ib = client.get("/api/inbox").json()
    assert set(ib[0]) == {"id", "created", "sender", "read"}


def test_devices_list_and_revoke_across_surfaces(client, make_user):
    make_user("alice")
    r = client.post("/palm/v1/login", content=b"alice\npassword123\n")
    ptoken = r.text.split("\n")[1]
    web_login(client, "alice")
    devs = client.get("/api/devices").json()
    assert len(devs) == 2  # make_user token + palm login
    for d in devs:
        assert set(d) == {"id", "label", "created", "last_used"}
    assert ptoken not in client.get("/api/devices").text
    palm = next(d for d in devs if d["label"] == "palm")
    assert client.get("/palm/v1/inbox", headers=auth(ptoken)).status_code == 200
    assert client.delete(f"/api/devices/{palm['id']}", headers=H).status_code == 200
    assert client.get("/palm/v1/inbox", headers=auth(ptoken)).status_code == 401
    assert len(client.get("/api/devices").json()) == 1


def test_cannot_revoke_other_users_device(client, make_user):
    make_user("alice")
    _, tb = make_user("bob")
    web_login(client, "alice")
    bob_dev = next(d for d in _devices_of(client, "bob"))
    r = client.delete(f"/api/devices/{bob_dev}", headers=H)
    assert r.status_code == 404 and r.json() == {"error": "no such device"}
    assert client.get("/palm/v1/inbox", headers=auth(tb)).status_code == 200


def _devices_of(client, handle):
    path = client.app.state.settings.db_path
    c = db.connect(path)
    try:
        return [r["id"] for r in c.execute(
            "SELECT d.id FROM device_tokens d JOIN users u ON u.id = d.user_id WHERE u.handle = ?", (handle,))]
    finally:
        c.close()


def test_revoke_all(client, make_user):
    _, t1 = make_user("alice")
    t2 = client.post("/palm/v1/login", content=b"alice\npassword123\n").text.split("\n")[1]
    web_login(client, "alice")
    assert client.post("/api/devices/revoke-all", headers=H).status_code == 200
    assert client.get("/api/devices").json() == []
    for t in (t1, t2):
        assert client.get("/palm/v1/inbox", headers=auth(t)).status_code == 401
    assert client.get("/api/me").status_code == 200  # web session unaffected


def test_disabled_user_session_refused(client, make_user, db_path):
    make_user("alice")
    web_login(client, "alice")
    assert client.get("/api/me").status_code == 200
    c = db.connect(db_path)
    accounts.set_user_disabled(c, "alice", True)
    c.close()
    r = client.get("/api/me")
    assert r.status_code == 401 and r.json() == {"error": "not logged in"}


def test_logout_deletes_session(client, make_user):
    make_user("alice")
    r = web_login(client, "alice")
    sid = client.cookies.get("scribble_session")
    assert client.post("/api/logout", headers=H).status_code == 200
    assert client.get("/api/me").status_code == 401
    # the old cookie value is dead server-side, not just cleared in the browser
    client.cookies.set("scribble_session", sid)
    assert client.get("/api/me").status_code == 401


def test_web_login_strips_username(client, make_user):
    make_user("alice")
    assert web_login(client, " alice\n").status_code == 200
