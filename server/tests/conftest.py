import warnings

import pytest

from app import db


@pytest.fixture
def conn():
    c = db.connect(":memory:")
    db.init_db(c)
    yield c
    c.close()


@pytest.fixture
def db_path(tmp_path):
    return str(tmp_path / "scribble.db")


@pytest.fixture
def client(db_path, tmp_path):
    with warnings.catch_warnings():
        # Starlette prefers httpx2, which is not installed here; httpx works.
        warnings.filterwarnings(
            "ignore",
            message=r"Using `httpx` with `starlette\.testclient` is deprecated",
            category=DeprecationWarning,
            module=r"fastapi\.testclient",
        )
        from fastapi.testclient import TestClient

    from app.config import Settings
    from app.main import create_app

    app = create_app(Settings(db_path=db_path, secure_cookies=False, static_dir=str(tmp_path)))
    with TestClient(app) as c:
        yield c


@pytest.fixture
def make_user(db_path, client):
    from app import accounts

    def _make(handle, password="password123"):
        c = db.connect(db_path)
        try:
            cur = c.execute(
                "INSERT INTO users (username, handle, password_hash, created) VALUES (?, ?, ?, 0)",
                (handle, handle, accounts._ph.hash(password)),
            )
            c.commit()
            uid = cur.lastrowid
            return uid, accounts.issue_device_token(c, uid, "test")
        finally:
            c.close()

    return _make
