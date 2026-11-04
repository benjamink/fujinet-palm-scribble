# Scribble

Scribble is an invite-only server for sending 160x120 black-and-white sketch
notes between Palm users, like email you draw with the stylus. Someone with
a Palm (a Visor, a Palm III...) and a FujiNet draws a sketch, picks up to ten
recipients from a directory, and sends it. Recipients read it on their Palm or
in a browser. A sketch is 2400 raw bytes (160*120/8, 1 bit per pixel).

```
 Palm app                FujiNet                     Scribble server (Docker)
 palm/apps/scribble      (fujinet-pc, ESP32 or       server/  (FastAPI + SQLite)
 draw, send, read  --->  FujiNet hardware)  --HTTPS-> /palm/v1  plain-text API
                         does the TLS and HTTP            |
                                                          +-- /api + React UI (server/web)
 Browser                                                  |   sign up, view, delete, devices
 sign up, view notes, manage devices  ------HTTPS---------+
                                                          +-- scribble-admin (inside the
 Owner                                                        container): invites, disable
```

- The **Palm app** never speaks TLS itself. Every request goes through
  fujinet-lib's network calls to FujiNet, which makes the HTTP(S) request.
- The **server** has two front doors on one SQLite database: a plain-text
  protocol for the Palm (`/palm/v1`, documented below) and a JSON API
  with a session cookie for the React web UI. The web UI can sign up, view,
  delete and manage devices, but cannot draw or send.
- The **owner** creates invite codes and can disable users with
  `scribble-admin` inside the container (see [deploy/README.md](../deploy/README.md)).
- Design: [docs/superpowers/specs/2026-11-03-scribble-design.md](superpowers/specs/2026-11-03-scribble-design.md).

## Verification status

Checked on 2026-11-03 at the head of the `scribble` branch, from the repo
root, on a Linux host. **Only the server, the web UI and the host-side Palm
unit tests were exercised. Nothing was run on a Palm device or in the
emulator.**

| Check | Command | Result |
|---|---|---|
| Server tests | `cd server && .venv/bin/pytest -W error` | 90 passed |
| Web UI tests | `cd server/web && npm test` | 9 files, 23 tests passed |
| Web UI build | `cd server/web && npm run build` | typecheck and build OK |
| Palm host unit tests | `make -C palm/apps/scribble test` | test_sketch 81 checks, test_proto passed, test_netutil passed, test_selection 106 checks, test_canvas 51 checks, 0 failures |
| Palm build (toolchain container) | `docker/palm-build.sh make -C palm/apps/scribble` | `scribble.prc` built from clean, 0 warnings (the `.prc` is not committed) |
| Server-only checklist lines | `BASE=http://127.0.0.1:PORT ADMIN="docker exec scribble-e2e scribble-admin" server/scripts/e2e_checklist.sh` against a fresh `scribble:latest` container | 66 passed, 0 failed (run twice on the same database) |

The e2e run used `docker run -d --name scribble-e2e -p 127.0.0.1:PORT:8000
-e SCRIBBLE_SECURE_COOKIES=0 -v scribble-e2e-vol:/data scribble:latest`, with the
volume's owner set to UID 10001 first. The script covers the lines marked
**server** in the [manual checklist](#manual-test-checklist): signups with
invites, login, a note with 0x00 and 0x0A bytes sent to two recipients,
recipient isolation, byte-identical round trip over the Palm API, read
marking, per-copy deletes, a stranger and a missing note giving the same
answer, wrong-size bodies and 11 recipients rejected with nothing stored, the
30-per-hour limit, device revoke, and disabling a user. The Palm halves of
those lines are still manual.

Not verified: see [Known limitations](#known-limitations--unverified).

## Quick start

### Run the server

With Docker (the same image as production, see [deploy/README.md](../deploy/README.md)):

```sh
docker build -t scribble:latest server/
docker volume create scribble-data
docker run --rm -v scribble-data:/data alpine chown 10001:10001 /data
docker run -d --name scribble -p 127.0.0.1:8000:8000 \
    -e SCRIBBLE_SECURE_COOKIES=0 -v scribble-data:/data scribble:latest
docker exec scribble scribble-admin invite create       # prints an invite code
```

`SCRIBBLE_SECURE_COOKIES=0` is only for testing over plain `http://`.

Or with a virtualenv, no Docker:

```sh
cd server
python -m venv .venv && .venv/bin/pip install -e '.[dev]'
export SCRIBBLE_DB=/tmp/scribble.db SCRIBBLE_SECURE_COOKIES=0
.venv/bin/uvicorn --factory app.main:create_app --reload --port 8000
.venv/bin/scribble-admin invite create                  # in another shell, same env
```

### Sign up

Open `http://127.0.0.1:8000/` (Docker image only: the built UI is served from
`/app/web`). The venv run above serves the API only: `SCRIBBLE_STATIC` defaults to
`/app/web`, which does not exist outside the image, so no UI is served and `/` returns 404. For the web UI with a venv
run, either use the dev server below or run `npm run build` in `server/web` and
set `SCRIBBLE_STATIC=$PWD/server/web/dist`. Choose Sign up, and enter
the invite code, a username, a password (8 characters or more) and a handle
(3-16 characters: a-z, 0-9, underscore). The handle is what other people see
and cannot be changed. Account sign-up is on the web only; the Palm app logs
in to an existing account.

### Web UI dev server

With the API running on port 8000 (the venv run above):

```sh
cd server/web
npm install            # first time
npm run dev            # Vite on http://localhost:5173, proxies /api to 127.0.0.1:8000
```

### Tests

```sh
cd server && .venv/bin/pytest -W error
cd server/web && npm test
make -C palm/apps/scribble test        # host-side C unit tests, no Palm toolchain needed
```

## Building and installing the Palm app

Build in the toolchain container (needs `../fujinet-lib-palmos`, see the
[main README](../README.md#prerequisites)):

```sh
docker/palm-build.sh make -C palm/apps/scribble      # palm/apps/scribble/scribble.prc
```

Install it the usual way, then press HotSync (Visor/USB cradle; add `-s PORT`
for a serial cradle, see the main README):

```sh
tools/install-apps.sh scribble
```

Start fujinet-pc (or use real FujiNet hardware) and the link as described in
the main README. Then, on the Palm, open **Scribble**. The first run shows
**Setup**:

- **Server:** the server address, e.g. `https://scribble.example.com`.
  `https://` is added if you leave out the scheme.
- **Username** and **Password:** your account. The password shows as you type
  and is never saved; only the server address, the device token and your
  handle are kept on the Palm.

Tap **Login**. On success you land in the Inbox.

## Using the Palm app

- **Inbox / Sent.** The list shows 8 notes a page; `<` and `>` page, **Refresh** reloads, **Sent** / **Inbox** toggles the list, **Setup**
  goes back to the login form, **New** starts a sketch. Inbox rows show the
  sender and when it was sent, with `*` marking unread notes. Sent rows show who
  you sent it to. The first line of the form (under the title) reads
  "Inbox, page N" or "Sent, page N" with **times UTC** at its right edge: all
  times shown are UTC, not converted to your timezone.
- **Viewer.** Tap a row to see the sketch with sender and time. **Delete**
  removes your copy (after a confirmation), **Reply** opens Compose addressed
  to the sender, **Back** returns. Opening an inbox note marks it read.
- **Compose.** Draw in the 160x120 box with the stylus. **Clear** wipes it,
  **Undo** removes the last stroke, **To...** opens the Picker, **Send** sends,
  **Back** leaves (asking before discarding a sketch).
- **Picker.** Type a handle prefix in **Find** and tap **Search** to list the
  directory. Tap names to tick up to 10. **Clear** unticks all, **Done** returns
  to Compose, which shows the chosen recipients.
- **Send.** Sends the sketch to the ticked recipients in one request. A
  failure (unknown handle, rate limit, network) shows a message and keeps
  the sketch so you can fix it and send again.
- **Log out.** In Setup, **Log out** forgets the token on the Palm (the
  device stays listed in the web UI until you revoke it there).
- **Session expiry.** If the token is revoked (in the web UI, "log out all
  devices"), or your account is disabled, the next request is refused, the
  Palm shows "Session expired - log in again" and goes back to Setup. If this
  happens while composing, the sketch is kept and you return to it after
  logging in again.

## Limits and behaviour

- **Sketch:** 160x120, black and white, exactly 2400 bytes; anything else is
  rejected.
- **Recipients:** 1 to 10 per note, no duplicates, not yourself. Handles are
  case-insensitive. Disabled users are not in the directory and cannot be
  sent to.
- **Rate limit:** 30 sends per hour per sender, counted per note (not per
  recipient). Over the limit, the Palm API answers HTTP 200 with body
  `err rate limit` (like every non-auth Palm error); the Palm shows `rate limit`.
- **BCC semantics:** each recipient sees only themselves as the recipient, on
  the web and in any API. The sender's Sent list shows everyone.
- **Deleted copies:** deleting is per copy. The sender deleting leaves every
  recipient's copy; a recipient deleting leaves the sender's and the other
  recipients' copies.
- **Soft delete and retention:** a deleted note only disappears for the user
  who deleted it. Its bitmap stays in SQLite (there is no purge job in v1), so
  deleting is not erasure. Erasing needs a manual database edit.
- **Account field rules (signup):** username is stripped of surrounding
  whitespace, then 1 to 32 characters of printable ASCII with no spaces,
  control characters, `|` or newlines (stored lowercase). Password is 8 to 63
  printable ASCII characters (spaces allowed). The limits match the Palm
  fields (32 and 63), which are Latin-1, not UTF-8, so non-ASCII is refused.
  The web login also strips the username. Handles are `[a-z0-9_]{3,16}` and
  must match in full (a trailing newline is rejected).
- **Handles are immutable.** Usernames and handles cannot be renamed.
- **Use https.** The Palm sends your password at login and a bearer token on
  every request. Over plain `http://` these cross the network in the clear.
  Plain http is only for a local test or a private network you trust; Setup
  shows a (non-blocking) warning when the server address starts with `http://`.
- **Token storage:** the Palm keeps its settings and bearer token in the
  unsaved preferences database (`PrefSetAppPreferences` with `saved` false),
  so the token is not copied to the desktop by HotSync. Consequence: a hard
  reset or restore from a HotSync backup means logging in again.
- **Failed logins:** after 5 failures in 15 minutes for an account or address,
  further logins get `err too many attempts`.

## Known limitations / UNVERIFIED

- **No password change in v1.** There is no way to change or reset a
  password. An admin can disable a user, but there is no reset; use a new
  account with a new invite.
- **Rate-limit replies at nginx.** With `deploy/nginx.conf`, the Palm login
  endpoint answers a rate-limited request with HTTP 200 and the body
  `err too many attempts`. The web endpoints (`/api/login`, `/api/signup`)
  still get nginx's HTTP 429, which the browser UI shows as a generic error.

- **The Palm app has NOT been run on a device or in the emulator.** The
  forms, the stylus drawing, the real serial transfer, and the serial POST of
  about 2.4 KB are unverified. What was tested: the host-side C unit tests, a
  zero-warning build, and (in the test described at the end of "FujiNet HTTP
  call order") the real `fnapp.c` and fujinet-lib code run on a host against a
  live fujinet-pc and a local server.
- **Whether FujiNet accepts your TLS certificate is unverified.** See "FujiNet
  TLS check" in [deploy/README.md](../deploy/README.md).
- **The browser UI has not been visually checked.** Its tests pass and it
  builds, but nobody has looked at it in a browser.
- **No idempotency key on send.** If the reply to a send is lost (timeout),
  retrying can deliver the note twice.
- **Web sessions last 30 days** (`WEB_SESSION_TTL`), or until logout, "log out
  all devices" or the account being disabled. Expired session rows are not purged.
- **Web lists page 20 at a time.** The inbox and sent lists show 20 notes, with
  paging controls.
- **No purge job** for deleted notes (see Limits).
- The server has no sign-up from the Palm; accounts are created in the browser.

## Manual test checklist

Run against a server you control, using the invite flow. Legend: **server**
means it needs only a server and a browser or curl (the script
`server/scripts/e2e_checklist.sh` automates the **server** halves, except
line 3 (web while a Palm session is open, only checked indirectly) and the
browser half of line 8 (the script checks the JSON API, not the rendered UI);
see Verification status); **Palm** means it needs a Palm or emulator with FujiNet and is **MANUAL
and UNVERIFIED**. Boxes are unticked on purpose: tick them when you have done it.

1. [ ] **server** Create two invites with `scribble-admin invite create`; sign
   up two accounts (A, B) on the web. A third (C) is needed for the
   two-recipient lines.
2. [ ] **Palm** Install Scribble, open Setup, enter the server address and A's
   username and password, tap Login and arrive in the Inbox. **server** the
   same login with curl (`POST /palm/v1/login`) returns `ok`, a token and the
   handle.
3. [ ] **server** Log in on the web as B in a browser at the same time as the
   Palm session.
4. [ ] **Palm** Draw a sketch, choose B with To..., Send. **server** the same
   send via curl returns `ok` and an id.
5. [ ] **Palm** Send a second sketch to B and C together. **server** same with
   a comma-separated list.
6. [ ] **server** B sees the note unread in the web inbox and in the Palm
   inbox listing; opening it marks it read. **Palm** B's Palm shows it unread,
   then read after viewing.
7. [ ] **server** Fetch the note over the Palm API and compare bytes with
   what was sent: identical. **Palm** the sketch looks as drawn.
8. [ ] **server** BCC: B's web view of the two-recipient note lists only B; C's
   lists only C; A's Sent and web view list both.
9. [ ] **Palm** B taps Reply: Compose opens addressed to A; send; A receives it.
10. [ ] **server** B deletes their copy: B can no longer open it, A's and C's
    copies are untouched. A deletes theirs: C still has the note. **Palm**
    the same with Delete in the Viewer.
11. [ ] **server** Strangers: a user who is not a sender or recipient gets the
    same answer for a real note id as for a missing one.
12. [ ] **server** Revoke a device in the web UI (Devices): the Palm token
    stops working (401 `err auth`). **Palm** the next Refresh shows "Session
    expired - log in again" and returns to Setup.
13. [ ] **server** Disable a user (`scribble-admin user disable NAME`): their
    web session and Palm token are refused and a new login says
    `err account disabled`. **Palm** the Palm returns to Setup. Re-enable with
    `user enable`.
14. [ ] **server** 11 recipients: `err too many recipients`. A body that is not
    2400 bytes: `err bad sketch`, and nothing is stored. **Palm** the Picker
    stops at 10 selected ("Selected: n/10"), and a server error shows its
    reason.
15. [ ] **server** 30 sends within an hour work, the 31st gives `err rate
    limit`. **Palm** the Palm shows the reason and keeps the sketch.
16. [ ] **Palm** Unreachable server (wrong address, or server stopped) and a wrong
    password both show an error; after a send failure the sketch is still in
    Compose and can be sent again.
17. [ ] **Palm** Log out in Setup, then log in again; the web Devices list shows the
    Palm device(s).

## Palm protocol

All responses are `text/plain` (except a fetched note, which is
`application/octet-stream`). Every request except login carries
`Authorization: Bearer <token>`. A missing, invalid or revoked token, or a
disabled user, gives HTTP 401 with body `err auth\n`. Any other failure (including a malformed request, `err bad request`) is
HTTP 200 whose first line is `err <short reason>`. Success begins with `ok`.

| Method and path | Request body | Response |
|---|---|---|
| `POST /palm/v1/login` | `username\npassword\n` | `ok\n<token>\n<handle>\n` |
| `GET /palm/v1/inbox?offset=0&limit=20` | none | `ok\n` + one `id\|unix\|sender\|unread` line per note |
| `GET /palm/v1/sent?offset=0&limit=20` | none | `ok\n` + one `id\|unix\|h1,h2` line per note |
| `GET /palm/v1/directory?prefix=&offset=0&limit=50` | none | `ok\n` + one handle per line |
| `GET /palm/v1/notes/{id}` | none | exactly 2400 raw bytes, or `err no such note\n`; marks read for a recipient |
| `POST /palm/v1/send` | `h1,h2\n` + 2400 raw bytes | `ok\n<id>\n` |
| `DELETE /palm/v1/notes/{id}` | none | `ok\n` |

Line formats:

- Inbox: `id|unix|sender|unread`, where `unix` is the send time in Unix
  seconds and `unread` is `1` or `0`. Example: `1|1730000000|alice|1`.
- Sent: `id|unix|h1,h2`, recipients sorted by handle. Example:
  `2|1730000100|bob,carol`.
- Login request: `username\npassword\n`; response `ok\n<token>\n<handle>\n`,
  e.g. `ok\nq3Zx...\nalice\n`. Failures: `err bad login`,
  `err account disabled`, `err too many attempts`.
- Send: the first line is a comma-separated list of recipient handles
  (case-insensitive, at most 10, no duplicates, not yourself); everything
  after the first `\n` must be exactly 2400 bytes. Errors include
  `err bad sketch`, `err unknown handle` (malformed handle list),
  `err unknown handle: bob`, `err no recipients`, `err too many recipients`,
  `err duplicate recipient`, `err cannot send to yourself`, `err rate limit`.

## FujiNet HTTP call order (Palm)

The Palm client makes every request through `fnapp_http_request()` in
`palm/apps/common/fnapp.c`. It uses fujinet-lib-palmos calls in this order:

1. `network_open(spec, mode, OPEN_TRANS_NONE)`. Use `OPEN_MODE_HTTP_GET_H`
   (12) for GET, `OPEN_MODE_HTTP_POST` (13) for POST and
   `OPEN_MODE_HTTP_DELETE_H` (9) for DELETE. Plain `OPEN_MODE_HTTP_GET` (4)
   also works for a GET without a header, but it URL-encodes the path and
   runs the firmware's filename resolve step.
2. If there is a header: `network_http_start_add_headers` (channel mode
   `SET_HEADERS`), then `network_write` the line
   `"Authorization: Bearer <token>"`, then `network_http_end_add_headers`
   (back to `BODY`).
3. POST only: `network_http_post_bin(spec, body, len)`. This switches to
   `POST_SET_DATA`, writes the body and switches back to `BODY`. The body
   may contain any byte, including 0x00 and 0x0A. Don't use
   `network_http_post`, because it uses `strlen`.
4. `network_read` until it returns 0.
5. `network_close`, on every path.

How the firmware handles this (fujinet-firmware
`lib/network-protocol/HTTP.cpp`):

- **The request goes out late.** Opening the channel only sets up the
  client. Headers are stored with `client->set_header` while in
  `SET_HEADERS` (`write_file_handle_set_header`, line 629). POST
  bytes are appended to `postData` while in `POST_SET_DATA` (line 673). The
  request itself, `http_transaction()` (line 729), runs on the first STATUS or READ in
  `BODY` mode (lines 430-436 and 505). `network_read` sends STATUS first.
- **Write everything before the first read.** Every header and the whole
  body must be written before the first `network_read` or
  `network_status`. Headers and body can come in either order, since both
  are only stored until the request runs (read from the source; only
  headers-then-body has been tested). fnapp sends the headers first.
- **Header format.** Use exactly one space after the colon, and no CR or LF
  at the end. The firmware takes the value from `pos + 2` after the colon
  (lines 649 and 652), so `"Authorization:Bearer x"` would lose a byte.
- **Large bodies.** The bus layer (`bus/palmos/fujinet-bus-palmos.c`
  `network_bus_write`) splits writes into 512-byte FujiBus frames. Sending a
  2404-byte send body this way has been tested.
- **Non-2xx replies are still readable.** A 401 sets the channel error to
  `INVALID_USERNAME_OR_PASSWORD` (`fserror_to_error`, line 355), but STATUS
  still reports the body's bytes as available (line 446).
  `network_read_nb` (fujinet-lib `common/network_read_nb.c` line 18) only
  stops when the error is set *and* nothing is available. So the client
  reads `err auth\n` like any other body, then gets 0.
- **`network_close` can return an error after a 401.** `close_file_handle`
  (line 545) maps the 401 to an error again, so the close fails even though
  the channel did close (read from the source; not checked in the test). fnapp ignores the close result. Callers decide what
  happened from the body (`ok` / `err ...`) alone.

Tested on 2026-11-03: the real `fnapp.c`, fujinet-lib-palmos `common/` and
`bus/palmos/fujinet-bus-palmos.c` were built on the host and run over
FujiBus/TCP to a live fujinet-pc (`../fujinet-firmware/build/dist/fujinet`,
bus-over-IP), which reached a local `scribble:latest`. These all behaved as
described above:

- login (POST, no header)
- GET directory with the header
- POST send with the header and a 2404-byte body containing 0x00 and 0x0A
  (the server replied `ok\n1\n`, and the recipient fetched exactly the same
  2400 bytes)
- bad token on GET and POST (HTTP 401, body `err auth\n` read)
- DELETE with the header

None of this has been run on a Palm device or in the emulator yet.
