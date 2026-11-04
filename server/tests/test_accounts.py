import time

import pytest

from app import accounts
from app.constants import LOGIN_FAIL_LIMIT, LOGIN_FAIL_WINDOW
from app.errors import ServiceError


def mk(conn, name="alice", handle="alice", pw="password1"):
    return accounts.signup(conn, accounts.create_invite(conn), name, pw, handle)


def reason(fn, *a, **k):
    with pytest.raises(ServiceError) as e:
        fn(*a, **k)
    return e.value


def test_signup_consumes_invite(conn):
    code = accounts.create_invite(conn)
    accounts.signup(conn, code, "alice", "password1", "alice")
    assert reason(accounts.signup, conn, code, "bob", "password1", "bob").reason == "bad invite"
    assert reason(accounts.signup, conn, "nope", "bob", "password1", "bob").reason == "bad invite"
    assert len(accounts.list_invites(conn)) == 1


def test_expired_invite_rejected(conn):
    code = accounts.create_invite(conn, expires=int(time.time()) - 10)
    assert reason(accounts.signup, conn, code, "bob", "password1", "bob").reason == "bad invite"


def test_handle_lowercased_and_validated(conn):
    uid = accounts.signup(conn, accounts.create_invite(conn), "bob", "password1", "Bob_1")
    assert conn.execute("SELECT handle FROM users WHERE id=?", (uid,)).fetchone()["handle"] == "bob_1"
    for bad in ("ab", "a-b", "a" * 17):
        code = accounts.create_invite(conn)
        assert reason(accounts.signup, conn, code, "x" + bad, "password1", bad).reason == "invalid handle"


def test_duplicates_and_short_password(conn):
    mk(conn)
    c = accounts.create_invite(conn)
    assert reason(accounts.signup, conn, c, "alice", "password1", "other").reason == "username taken"
    assert reason(accounts.signup, conn, c, "other", "password1", "ALICE").reason == "handle taken"
    assert reason(accounts.signup, conn, c, "other", "short", "other").reason == "password too short"
    accounts.signup(conn, c, "other", "password1", "other")  # invite not burned by failures


def test_password_not_stored_plain(conn):
    uid = mk(conn)
    h = conn.execute("SELECT password_hash FROM users WHERE id=?", (uid,)).fetchone()[0]
    assert h.startswith("$argon2") and "password1" not in h


def test_login_wrong_password_is_bad_login(conn):
    uid = mk(conn)
    assert accounts.verify_login(conn, "alice", "password1", "1.1.1.1") == uid
    assert reason(accounts.verify_login, conn, "alice", "wrongpass", "1.1.1.1").reason == "bad login"
    assert reason(accounts.verify_login, conn, "ghost", "wrongpass", "1.1.1.1").reason == "bad login"


def test_login_rate_limited_after_5_failures_then_ok_after_window(conn):
    uid = mk(conn)
    t = 1000
    for _ in range(LOGIN_FAIL_LIMIT):
        assert reason(accounts.verify_login, conn, "alice", "bad-pass-x", "1.1.1.1", now=t).reason == "bad login"
    e = reason(accounts.verify_login, conn, "alice", "password1", "1.1.1.1", now=t + 1)
    assert (e.reason, e.status) == ("too many attempts", 429)
    # other ip unaffected
    assert accounts.verify_login(conn, "alice", "password1", "2.2.2.2", now=t + 1) == uid
    assert accounts.verify_login(conn, "alice", "password1", "1.1.1.1", now=t + LOGIN_FAIL_WINDOW + 1) == uid


def test_disabled_login(conn):
    mk(conn)
    accounts.set_user_disabled(conn, "alice", True)
    assert reason(accounts.verify_login, conn, "alice", "password1", "1.1.1.1").reason == "account disabled"


def test_token_roundtrip_and_hash_only(conn):
    uid = mk(conn)
    tok = accounts.issue_device_token(conn, uid, "palm")
    assert accounts.user_for_token(conn, tok)["id"] == uid
    assert conn.execute("SELECT last_used FROM device_tokens").fetchone()[0] is not None
    dump = "".join(str(tuple(r)) for r in conn.execute("SELECT * FROM device_tokens"))
    assert tok not in dump
    assert accounts.user_for_token(conn, "garbage") is None
    assert [d["label"] for d in accounts.list_devices(conn, uid)] == ["palm"]


def test_revoked_token_rejected(conn):
    uid = mk(conn)
    tok = accounts.issue_device_token(conn, uid, "palm")
    accounts.revoke_device(conn, uid, accounts.list_devices(conn, uid)[0]["id"])
    assert accounts.user_for_token(conn, tok) is None


def test_revoke_device_scoped_to_user(conn):
    a = mk(conn)
    b = mk(conn, "bob", "bob")
    tok = accounts.issue_device_token(conn, a, "palm")
    accounts.revoke_device(conn, b, accounts.list_devices(conn, a)[0]["id"])
    assert accounts.user_for_token(conn, tok) is not None


def test_disabled_user_token_and_session_rejected(conn):
    uid = mk(conn)
    tok = accounts.issue_device_token(conn, uid, "palm")
    sid = accounts.create_web_session(conn, uid)
    assert accounts.user_for_session(conn, sid)["id"] == uid
    accounts.set_user_disabled(conn, "alice", True)
    assert accounts.user_for_token(conn, tok) is None
    assert accounts.user_for_session(conn, sid) is None
    accounts.set_user_disabled(conn, "alice", False)
    assert accounts.user_for_token(conn, tok) is not None


def test_session_delete_and_hash_only(conn):
    uid = mk(conn)
    sid = accounts.create_web_session(conn, uid)
    assert sid not in str(tuple(conn.execute("SELECT * FROM web_sessions").fetchone()))
    accounts.delete_web_session(conn, sid)
    assert accounts.user_for_session(conn, sid) is None


def test_revoke_all_devices(conn):
    uid = mk(conn)
    t1 = accounts.issue_device_token(conn, uid, "a")
    t2 = accounts.issue_device_token(conn, uid, "b")
    accounts.revoke_all_devices(conn, uid)
    assert accounts.user_for_token(conn, t1) is None and accounts.user_for_token(conn, t2) is None
    assert accounts.list_devices(conn, uid) == []


def test_set_user_disabled_unknown_user_raises(conn):
    with pytest.raises(ServiceError) as e:
        accounts.set_user_disabled(conn, "ghost", True)
    assert e.value.status == 404


def test_web_session_expires_after_ttl(conn):
    from app.constants import WEB_SESSION_TTL

    uid = mk(conn)
    sid = accounts.create_web_session(conn, uid)
    now = int(time.time())
    assert accounts.user_for_session(conn, sid, now=now + WEB_SESSION_TTL - 60) is not None
    assert accounts.user_for_session(conn, sid, now=now + WEB_SESSION_TTL + 60) is None


def test_handle_trailing_newline_rejected(conn):
    code = accounts.create_invite(conn)
    assert reason(accounts.signup, conn, code, "alice", "password1", "alice\n").reason == "invalid handle"
    from app.constants import HANDLE_RE
    assert HANDLE_RE.fullmatch("alice\n") is None and HANDLE_RE.fullmatch("alice")


def test_username_validation(conn):
    for bad in ("", "   ", "a b", "a|b", "a\nb", "a\tb", "a\x00b", "café", "x" * 33):
        code = accounts.create_invite(conn)
        assert reason(accounts.signup, conn, code, bad, "password1", "someone").reason == "invalid username", repr(bad)
    uid = accounts.signup(conn, accounts.create_invite(conn), "  Pad\n", "password1", "padded")
    assert conn.execute("SELECT username FROM users WHERE id=?", (uid,)).fetchone()["username"] == "pad"
    accounts.signup(conn, accounts.create_invite(conn), "x" * 32, "password1", "maxlen")


def test_password_validation(conn):
    for bad in ("pass\nword1", "passéword", "pass\x00word", "p" * 64, "pass\tword"):
        code = accounts.create_invite(conn)
        assert reason(accounts.signup, conn, code, "u1", bad, "someone").reason in ("invalid password", "password too short"), repr(bad)
    accounts.signup(conn, accounts.create_invite(conn), "u2", "p" * 63, "maxpw")
    accounts.signup(conn, accounts.create_invite(conn), "u3", "pass word!", "spacepw")
