from fastapi import APIRouter, Depends, Request, Response

from . import accounts, notes
from .constants import HANDLE_RE
from .deps import get_conn
from .errors import ServiceError

router = APIRouter(prefix="/palm/v1")

MAX_HANDLES_LINE = 400


def text(body: str, status: int = 200) -> Response:
    return Response(body, status_code=status, media_type="text/plain")


def err(reason: str, status: int = 200) -> Response:
    return text(f"err {reason}\n", status)


def current_user(request: Request, conn=Depends(get_conn)):
    header = request.headers.get("authorization", "")
    scheme, _, token = header.partition(" ")
    user = accounts.user_for_token(conn, token.strip()) if scheme.lower() == "bearer" and token.strip() else None
    if user is None:
        raise AuthError()
    return user


class AuthError(Exception):
    pass


async def _auth_error_handler(request, exc):
    return err("auth", 401)


async def _service_error_handler(request, exc: ServiceError):
    return err(exc.reason)


def register_handlers(app) -> None:
    app.add_exception_handler(AuthError, _auth_error_handler)
    app.add_exception_handler(ServiceError, _service_error_handler)


@router.post("/login")
async def login(request: Request, conn=Depends(get_conn)):
    parts = (await request.body()).decode("utf-8", "replace").split("\n")
    username = parts[0].strip() if parts else ""
    password = parts[1].rstrip("\r") if len(parts) > 1 else ""
    ip = request.client.host if request.client else "unknown"
    user_id = accounts.verify_login(conn, username, password, ip)
    token = accounts.issue_device_token(conn, user_id, "palm")
    handle = conn.execute("SELECT handle FROM users WHERE id = ?", (user_id,)).fetchone()["handle"]
    return text(f"ok\n{token}\n{handle}\n")


@router.get("/inbox")
def inbox(offset: int = 0, limit: int = 20, user=Depends(current_user), conn=Depends(get_conn)):
    rows = notes.inbox(conn, user["id"], offset, limit)
    return text("ok\n" + "".join(f"{r['id']}|{r['created']}|{r['sender']}|{0 if r['read'] else 1}\n" for r in rows))


@router.get("/sent")
def sent(offset: int = 0, limit: int = 20, user=Depends(current_user), conn=Depends(get_conn)):
    rows = notes.sent(conn, user["id"], offset, limit)
    return text("ok\n" + "".join(f"{r['id']}|{r['created']}|{','.join(r['recipients'])}\n" for r in rows))


@router.get("/directory")
def directory(prefix: str = "", offset: int = 0, limit: int = 50, user=Depends(current_user), conn=Depends(get_conn)):
    return text("ok\n" + "".join(f"{h}\n" for h in notes.directory(conn, prefix, offset, limit)))


@router.get("/notes/{note_id}")
def fetch_note(note_id: int, user=Depends(current_user), conn=Depends(get_conn)):
    note = notes.get_note(conn, user["id"], note_id)
    return Response(note["bitmap"], media_type="application/octet-stream")


@router.post("/send")
async def send(request: Request, user=Depends(current_user), conn=Depends(get_conn)):
    body = await request.body()
    line, sep, bitmap = body.partition(b"\n")
    if not sep:
        raise ServiceError("bad sketch")
    if len(line) > MAX_HANDLES_LINE:
        raise ServiceError("unknown handle")
    handles = [h.strip() for h in line.decode("ascii", "replace").lower().split(",")]
    if not all(HANDLE_RE.fullmatch(h) for h in handles):
        raise ServiceError("unknown handle")
    note_id = notes.send_note(conn, user["id"], handles, bitmap)
    return text(f"ok\n{note_id}\n")


@router.delete("/notes/{note_id}")
def remove_note(note_id: int, user=Depends(current_user), conn=Depends(get_conn)):
    notes.delete_note(conn, user["id"], note_id)
    return text("ok\n")
