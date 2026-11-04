from fastapi import APIRouter, Depends, Request, Response
from fastapi.exception_handlers import request_validation_exception_handler
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from fastapi.routing import APIRoute
from pydantic import BaseModel

from . import accounts, bitmap, notes, palm_api
from .constants import WEB_SESSION_TTL
from .deps import get_conn
from .errors import ServiceError

COOKIE = "scribble_session"
MUTATING = {"POST", "DELETE", "PUT", "PATCH"}


class WebAuthError(Exception):
    pass


class BadRequest(Exception):
    pass


def json_error(reason: str, status: int) -> JSONResponse:
    return JSONResponse({"error": reason}, status_code=status)


def csrf_guard(request: Request) -> None:
    """Defence in depth on top of SameSite=Lax: every mutating request must carry the
    custom header (which forces a CORS preflight), and bodies must be JSON."""
    if request.method not in MUTATING:
        return
    if request.headers.get("x-requested-with") != "scribble":
        raise BadRequest()
    if request.method == "POST" and request.headers.get("content-length", "0") not in ("", "0"):
        if request.headers.get("content-type", "").split(";")[0].strip().lower() != "application/json":
            raise BadRequest()


class ApiRoute(APIRoute):
    """Render ServiceError as JSON for this router only (the Palm router's handler is untouched)."""

    def get_route_handler(self):
        original = super().get_route_handler()

        async def handler(request: Request):
            try:
                return await original(request)
            except ServiceError as exc:
                return json_error(exc.reason, exc.status)

        return handler


router = APIRouter(prefix="/api", dependencies=[Depends(csrf_guard)], route_class=ApiRoute)


def current_web_user(request: Request, conn=Depends(get_conn)):
    sid = request.cookies.get(COOKIE, "")
    user = accounts.user_for_session(conn, sid) if sid else None
    if user is None:
        raise WebAuthError()
    return user


def _handle(user_id: int, conn) -> str:
    return conn.execute("SELECT handle FROM users WHERE id = ?", (user_id,)).fetchone()["handle"]


def _login_response(request: Request, conn, user_id: int) -> JSONResponse:
    resp = JSONResponse({"handle": _handle(user_id, conn)})
    resp.set_cookie(
        COOKIE,
        accounts.create_web_session(conn, user_id),
        httponly=True,
        samesite="lax",
        secure=request.app.state.settings.secure_cookies,
        max_age=WEB_SESSION_TTL,
        path="/",
    )
    return resp


def _ip(request: Request) -> str:
    return request.client.host if request.client else "unknown"


class SignupBody(BaseModel):
    invite: str
    username: str
    password: str
    handle: str


class LoginBody(BaseModel):
    username: str
    password: str


@router.post("/signup")
def signup(body: SignupBody, request: Request, conn=Depends(get_conn)):
    user_id = accounts.signup(conn, body.invite, body.username, body.password, body.handle)
    return _login_response(request, conn, user_id)


@router.post("/login")
def login(body: LoginBody, request: Request, conn=Depends(get_conn)):
    user_id = accounts.verify_login(conn, body.username.strip(), body.password, _ip(request))
    return _login_response(request, conn, user_id)


@router.post("/logout")
def logout(request: Request, user=Depends(current_web_user), conn=Depends(get_conn)):
    # Requires a valid session (401 otherwise); deletes it server-side and clears the cookie.
    accounts.delete_web_session(conn, request.cookies.get(COOKIE, ""))
    resp = JSONResponse({})
    resp.delete_cookie(COOKIE, path="/", httponly=True, samesite="lax",
                       secure=request.app.state.settings.secure_cookies)
    return resp


@router.get("/me")
def me(user=Depends(current_web_user)):
    return {"handle": user["handle"]}


@router.get("/inbox")
def inbox(offset: int = 0, limit: int = 20, user=Depends(current_web_user), conn=Depends(get_conn)):
    return notes.inbox(conn, user["id"], offset, limit)


@router.get("/sent")
def sent(offset: int = 0, limit: int = 20, user=Depends(current_web_user), conn=Depends(get_conn)):
    return notes.sent(conn, user["id"], offset, limit)


# Declared before /notes/{note_id} so ".png" is matched here, not rejected as a non-integer id.
@router.get("/notes/{note_id}.png")
def note_png(note_id: int, user=Depends(current_web_user), conn=Depends(get_conn)):
    note = notes.get_note(conn, user["id"], note_id, mark_read=True)
    return Response(bitmap.to_png(note["bitmap"]), media_type="image/png",
                    headers={"Cache-Control": "private, no-store"})


@router.get("/notes/{note_id}")
def note_json(note_id: int, user=Depends(current_web_user), conn=Depends(get_conn)):
    note = notes.get_note(conn, user["id"], note_id, mark_read=True)
    return {k: note[k] for k in ("id", "created", "sender", "recipients", "role")}


@router.delete("/notes/{note_id}")
def remove_note(note_id: int, user=Depends(current_web_user), conn=Depends(get_conn)):
    notes.delete_note(conn, user["id"], note_id)
    return {}


@router.get("/devices")
def devices(user=Depends(current_web_user), conn=Depends(get_conn)):
    return [dict(r) for r in accounts.list_devices(conn, user["id"])]


# Declared before DELETE /devices/{device_id}; different method, but keep literals first.
@router.post("/devices/revoke-all")
def revoke_all(user=Depends(current_web_user), conn=Depends(get_conn)):
    accounts.revoke_all_devices(conn, user["id"])
    return {}


@router.delete("/devices/{device_id}")
def revoke_device(device_id: int, user=Depends(current_web_user), conn=Depends(get_conn)):
    if not any(d["id"] == device_id for d in accounts.list_devices(conn, user["id"])):
        raise ServiceError("no such device", 404)
    accounts.revoke_device(conn, user["id"], device_id)
    return {}


def _is_api(request: Request) -> bool:
    return request.url.path == "/api" or request.url.path.startswith("/api/")


async def _auth_handler(request, exc):
    return json_error("not logged in", 401)


async def _bad_request_handler(request, exc):
    return json_error("bad request", 403)


async def _validation_handler(request, exc: RequestValidationError):
    if _is_api(request):
        return json_error("invalid input", 422)
    if request.url.path.startswith("/palm/"):
        return palm_api.err("bad request")
    return await request_validation_exception_handler(request, exc)


def register_handlers(app) -> None:
    app.add_exception_handler(WebAuthError, _auth_handler)
    app.add_exception_handler(BadRequest, _bad_request_handler)
    app.add_exception_handler(RequestValidationError, _validation_handler)
