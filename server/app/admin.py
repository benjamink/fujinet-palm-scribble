import argparse
import sys
import time
from datetime import datetime, timezone

from . import accounts, db
from .config import get_settings
from .errors import ServiceError


def _positive_int(value: str) -> int:
    try:
        n = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError(f"not an integer: {value!r}")
    if n < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return n


def _iso(ts: int) -> str:
    return datetime.fromtimestamp(ts, timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="scribble-admin")
    groups = parser.add_subparsers(dest="group", required=True)

    invite = groups.add_parser("invite", help="manage invites").add_subparsers(dest="action", required=True)
    create = invite.add_parser("create", help="create an invite and print its code")
    create.add_argument("--expires-days", type=_positive_int, metavar="N", help="expire after N days (default: never)")
    invite.add_parser("list", help="list invites")

    user = groups.add_parser("user", help="manage users").add_subparsers(dest="action", required=True)
    user.add_parser("list", help="list users")
    for name in ("disable", "enable"):
        user.add_parser(name, help=f"{name} a user").add_argument("username")
    return parser


def _invite_list(conn) -> None:
    now = int(time.time())
    names = {r["id"]: r["username"] for r in conn.execute("SELECT id, username FROM users")}
    for inv in accounts.list_invites(conn):
        used = "unused" if inv["used_by"] is None else f"used by {names.get(inv['used_by'], '?')}"
        expires = "never" if not inv["expires"] else _iso(inv["expires"])
        expired = " expired" if inv["expires"] and inv["expires"] <= now and inv["used_by"] is None else ""
        print(f"{inv['code']}  {used}  expires {expires}{expired}")


def _user_list(conn) -> None:
    for u in conn.execute("SELECT * FROM users ORDER BY id"):
        state = "disabled" if u["disabled"] else "active"
        print(f"{u['username']}  {u['handle']}  {state}  {_iso(u['created'])[:10]}")


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    conn = db.connect(get_settings().db_path)
    try:
        db.init_db(conn)
        if args.group == "invite" and args.action == "create":
            expires = int(time.time()) + args.expires_days * 86400 if args.expires_days else None
            print(accounts.create_invite(conn, expires))
        elif args.group == "invite":
            _invite_list(conn)
        elif args.action == "list":
            _user_list(conn)
        else:
            accounts.set_user_disabled(conn, args.username, args.action == "disable")
    except ServiceError as e:
        print(f"error: {e.reason}: {getattr(args, 'username', '')}".rstrip(": "), file=sys.stderr)
        return 1
    finally:
        conn.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
