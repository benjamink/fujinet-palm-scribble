import time

import pytest

from app import accounts, db
from app.admin import main
from app.errors import ServiceError


@pytest.fixture
def env(db_path, monkeypatch):
    monkeypatch.setenv("SCRIBBLE_DB", db_path)
    return db_path


def run(capsys, *argv):
    code = main(list(argv))
    out = capsys.readouterr()
    return code, out.out, out.err


def open_db(path):
    c = db.connect(path)
    return c


def test_invite_create_prints_code_that_can_sign_up(env, capsys):
    code, out, err = run(capsys, "invite", "create")
    assert code == 0 and err == ""
    invite = out.strip()
    assert out == invite + "\n" and invite
    c = open_db(env)
    try:
        accounts.signup(c, invite, "Alice", "password123", "alice")
        row = c.execute("SELECT expires FROM invites WHERE code = ?", (invite,)).fetchone()
        assert row["expires"] == 0
    finally:
        c.close()


def test_invite_create_with_expiry(env, capsys):
    before = int(time.time())
    code, out, _ = run(capsys, "invite", "create", "--expires-days", "2")
    assert code == 0
    c = open_db(env)
    try:
        exp = c.execute("SELECT expires FROM invites WHERE code = ?", (out.strip(),)).fetchone()["expires"]
    finally:
        c.close()
    assert before + 2 * 86400 <= exp <= int(time.time()) + 2 * 86400


@pytest.mark.parametrize("n", ["0", "-3"])
def test_invite_create_rejects_nonpositive_days(env, capsys, n):
    with pytest.raises(SystemExit) as e:
        main(["invite", "create", "--expires-days", n])
    assert e.value.code != 0
    assert capsys.readouterr().err


def test_invite_list_shows_used_and_unused(env, capsys):
    _, used, _ = run(capsys, "invite", "create")
    _, unused, _ = run(capsys, "invite", "create", "--expires-days", "1")
    c = open_db(env)
    try:
        accounts.signup(c, used.strip(), "alice", "password123", "alice")
    finally:
        c.close()
    code, out, err = run(capsys, "invite", "list")
    assert code == 0 and err == ""
    lines = {ln.split()[0]: ln for ln in out.splitlines()}
    assert "used by alice" in lines[used.strip()]
    assert "never" in lines[used.strip()]
    assert "unused" in lines[unused.strip()]
    assert "never" not in lines[unused.strip()]
    assert "expired" not in lines[unused.strip()]


def test_expired_invite_listed_and_cannot_sign_up(env, capsys):
    c = open_db(env)
    try:
        db.init_db(c)
        old = accounts.create_invite(c, expires=int(time.time()) - 10)
        with pytest.raises(ServiceError, match="bad invite"):
            accounts.signup(c, old, "bob", "password123", "bob")
    finally:
        c.close()
    code, out, _ = run(capsys, "invite", "list")
    assert code == 0
    assert "expired" in out and "unused" in out


def test_user_list_and_disable_enable(env, capsys):
    c = open_db(env)
    try:
        db.init_db(c)
        inv = accounts.create_invite(c)
        accounts.signup(c, inv, "alice", "password123", "ally")
    finally:
        c.close()
    code, out, _ = run(capsys, "user", "list")
    assert code == 0 and "alice" in out and "ally" in out and "active" in out

    assert run(capsys, "user", "disable", "ALICE")[0] == 0
    c = open_db(env)
    try:
        with pytest.raises(ServiceError, match="account disabled"):
            accounts.verify_login(c, "alice", "password123", "1.1.1.1")
    finally:
        c.close()
    assert "disabled" in run(capsys, "user", "list")[1]

    assert run(capsys, "user", "enable", "alice")[0] == 0
    c = open_db(env)
    try:
        assert accounts.verify_login(c, "alice", "password123", "1.1.1.1")
    finally:
        c.close()
    assert "active" in run(capsys, "user", "list")[1]


@pytest.mark.parametrize("cmd", ["disable", "enable"])
def test_unknown_user_fails_with_message(env, capsys, cmd):
    code, out, err = run(capsys, "user", cmd, "nobody")
    assert code == 1 and out == ""
    assert "no such user" in err and "nobody" in err
