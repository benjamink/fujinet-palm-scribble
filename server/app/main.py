from pathlib import Path

from fastapi import FastAPI, Request, Response
from fastapi.responses import FileResponse

from . import db, palm_api, web_api
from .config import Settings, get_settings


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or get_settings()
    app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
    app.state.settings = settings
    init = db.connect(settings.db_path)
    try:
        db.init_db(init)
    finally:
        init.close()
    palm_api.register_handlers(app)
    app.include_router(palm_api.router)
    web_api.register_handlers(app)
    app.include_router(web_api.router)
    _mount_web(app, Path(settings.static_dir))
    return app


def _mount_web(app: FastAPI, root: Path) -> None:
    """Serve the built SPA. Registered last so /api and /palm routes win; unknown
    paths under those prefixes stay 404, anything else falls back to index.html."""
    if not (root / "index.html").is_file():
        return
    root = root.resolve()

    @app.api_route("/{path:path}", methods=["GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"],
                   include_in_schema=False)
    def spa(path: str, request: Request):
        first = path.split("/", 1)[0]
        if request.method not in ("GET", "HEAD") or first in ("api", "palm"):
            return Response("not found", status_code=404, media_type="text/plain")
        if path:
            try:
                candidate = (root / path).resolve()
                if candidate.is_file() and candidate.is_relative_to(root):
                    return FileResponse(candidate)
            except (ValueError, OSError):
                return Response("not found", status_code=404, media_type="text/plain")
            if first == "assets":
                return Response("not found", status_code=404, media_type="text/plain")
        return FileResponse(root / "index.html", media_type="text/html")
