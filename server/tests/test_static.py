import warnings

import pytest

with warnings.catch_warnings():
    warnings.simplefilter("ignore")
    from fastapi.testclient import TestClient

from app.config import Settings
from app.main import create_app


@pytest.fixture
def web(tmp_path, db_path):
    dist = tmp_path / "dist"
    (dist / "assets").mkdir(parents=True)
    (dist / "index.html").write_text("<!doctype html><title>Scribble</title>")
    (dist / "assets" / "app.js").write_text("console.log(1)")
    (tmp_path / "secret.txt").write_text("TOPSECRET")
    app = create_app(Settings(db_path=db_path, secure_cookies=False, static_dir=str(dist)))
    with TestClient(app) as c:
        yield c


def test_root_and_spa_paths_return_index(web):
    for path in ("/", "/inbox", "/some/deep/path"):
        r = web.get(path)
        assert r.status_code == 200, path
        assert "<title>Scribble</title>" in r.text
        assert r.headers["content-type"].startswith("text/html")


def test_assets_served(web):
    r = web.get("/assets/app.js")
    assert r.status_code == 200
    assert r.text == "console.log(1)"


def test_missing_asset_is_404(web):
    assert web.get("/assets/missing.js").status_code == 404


def test_unknown_api_and_palm_are_not_html(web):
    for path in ("/api/nope", "/palm/v1/nope", "/api", "/palm"):
        r = web.get(path)
        assert r.status_code == 404, path
        assert "<title>" not in r.text


@pytest.mark.parametrize("path", [
    "/..%2f..%2fsecret.txt", "/..%2fsecret.txt", "/%2e%2e/secret.txt",
    "/assets/..%2f..%2fsecret.txt", "/assets/../../secret.txt", "/..%5csecret.txt", "/x%00y",
])
def test_path_traversal_blocked(web, path):
    r = web.get(path)
    assert "TOPSECRET" not in r.text
    assert r.status_code in (200, 404)
    if r.status_code == 200:
        assert "<title>Scribble</title>" in r.text


def test_missing_static_dir_still_starts(tmp_path, db_path):
    app = create_app(Settings(db_path=db_path, secure_cookies=False, static_dir=str(tmp_path / "nope")))
    with TestClient(app) as c:
        assert c.get("/").status_code == 404
