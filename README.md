# Scribble

Sketch notes between Palm users, like email you draw with the stylus. Draw a
160x120 black-and-white sketch on a Palm (Visor, Palm III, ...) with a
[FujiNet](https://fujinet.online/), pick up to ten recipients from a
directory, and send it. Recipients read it on their Palm or in a browser.
Accounts are invite-only. A BCC recipient sees only their own name.

```
 Palm app (app/)        FujiNet               Scribble server (server/)
 draw, send, read  -->  does TLS and HTTP -->  FastAPI + SQLite
                                               /palm/v1  plain-text API
 Browser           ---------------------->     /api + React UI (server/web)
 sign up, view, delete, manage devices
```

## Layout

| Path | What it is |
|---|---|
| `app/` | Palm OS client (C89, m68k): Setup, Inbox, Viewer, Compose, recipient Picker, Send. Pure helpers have host-side unit tests |
| `server/` | FastAPI + SQLite server, React web UI (`server/web`), `scribble-admin` CLI, Dockerfile, e2e script |
| `deploy/` | systemd unit, nginx config, deployment guide (Docker or podman) |
| `docs/scribble.md` | Full reference: quick start, Palm usage, protocol, limits, known limitations, manual test checklist |
| `docs/superpowers/` | Design spec and implementation plan |
| `patches/` | Change to the shared `palm/apps/common/fnapp.c/.h` in the [fujinet-palm](https://github.com/benjamink/fujinet-palm) repo that the Palm app needs |

## Quick start (server)

```sh
docker build -t scribble:latest server/
docker volume create scribble-data
docker run --rm -v scribble-data:/data alpine chown 10001:10001 /data
docker run -d --name scribble -p 127.0.0.1:8000:8000 \
    -e SCRIBBLE_SECURE_COOKIES=0 -v scribble-data:/data scribble:latest
docker exec scribble scribble-admin invite create
```

Open <http://127.0.0.1:8000/>, sign up with the invite code, then log in from
the Palm app. `SCRIBBLE_SECURE_COOKIES=0` is for testing over plain HTTP only.
See [docs/scribble.md](docs/scribble.md) for the virtualenv alternative and
[deploy/README.md](deploy/README.md) for production (systemd + nginx + TLS).

## Tests

```sh
cd server && .venv/bin/pytest -W error     # server
cd server/web && npm test                  # web UI
make -C app test                           # Palm host-side unit tests
```

## Palm app build

`app/Makefile` builds with the m68k PalmOS toolchain and expects sibling
checkouts that are **not** in this repo: `../common` (from
[fujinet-palm](https://github.com/benjamink/fujinet-palm), with the change in
`patches/` applied) and `fujinet-lib-palmos`. Set `FNLIB` and `COMMON` in the
Makefile to wherever they live. Until that is wired up for this repo, the Palm
app is easiest to build from a fujinet-palm checkout.

## Status

The server, web UI and host-side Palm tests pass. **The Palm app has not been run
on a device or emulator**, FujiNet's acceptance of the server's TLS
certificate is unconfirmed, and the web UI hasn't been looked at in a browser.
Details and a manual checklist are in [docs/scribble.md](docs/scribble.md).

Some paths in `docs/` still use the old layout (`palm/apps/scribble`); in this
repo that is `app/`.

## Limits worth knowing

No password change in v1. Deleted notes stay in the database (soft delete).
10 recipients per note, 30 sends per hour per user. See
[docs/scribble.md](docs/scribble.md#known-limitations--unverified).
