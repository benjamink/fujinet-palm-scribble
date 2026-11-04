# Scribble: graphical notes between Palm users

Working name "Scribble" (my call; rename freely).

## Intent

Let people with Palm handhelds sketch a note with the stylus and send it,
like email, to one or more other people. The Palm reaches the internet
through FujiNet (HTTPS GET/POST over the serial link). A server the owner
runs on the internet stores and routes the notes and also has a web UI that
shows notes as images. The point is a practical, small, social use for the
hardware: a way to send "here's what I drew" to a friend.

## Goals

- Sign up (invite only), log in, pick recipients from a directory, sketch,
  send, and read a received sketch, all from the Palm.
- A web UI to sign up, log in, and view inbox and sent notes as images
  with metadata (from handle, date, recipients).
- Send one note to several people at once; each recipient sees only their
  own name as the recipient.
- Easy for the owner to deploy: one Docker image, a systemd unit, behind
  nginx.

## Non-goals (v1)

- Drawing or sending from the web UI. The web is for sign-up, viewing,
  deleting, and managing devices.
- Push notifications. The Palm checks for new notes when the user asks.
- Colour or grayscale sketches, text messages, attachments, replies in
  threads, groups or mailing lists, email or federation with other systems.
- An admin web UI. The owner uses a command-line tool inside the container.
- Open sign-up. Every account needs an invite code.

## Anti-goals

- Nobody besides sender and a note's recipients may ever be able to see the
  note, including via the directory or the API.
- A recipient must never learn who else received the note.
- A user must never be able to read another account's inbox by guessing
  IDs.
- The Palm side must not need floating point, large downloads, or more
  than a few KB of working memory per request.

## People and constraints

- Users are a small circle of friends and retro-computing hobbyists.
- The Palm is a Handspring Visor Deluxe class device, Palm OS 3.1, 160x160
  screen, slow serial link, small heap. Existing apps in `palm/apps/` show
  the toolchain and conventions to follow (see "Palm app").
- The owner runs the server and picks the domain; HTTPS is terminated by
  nginx with a normal certificate (e.g. Let's Encrypt).
  **Check during the build** that FujiNet's TLS validation accepts that
  certificate.

## Decided with the owner

- Stack: Python FastAPI, React for the web UI, SQLite.
- Packaged as a Docker image, started by a systemd unit, running behind
  nginx.
- Accounts: username and password to log in, plus a separate public handle
  shown in the directory.
- Directory used on the Palm to choose recipients.
- Multiple recipients, delivered BCC-style (each recipient sees only
  themselves).
- The Palm app has a message list and a screen to create and send.
- Invite codes control sign-up.

## My calls (not yet confirmed; change any of these)

**Note format.** A sketch is a fixed 160x120 black and white bitmap. On the
wire it is 2400 raw bytes: 20 bytes per row, 120 rows, most significant bit
first, 1 = black. This is Palm's native 1-bit layout, so the Palm can draw
it directly. The server stores these bytes and renders a PNG for the web UI
on request. Chosen so the compose screen has room for buttons under the
canvas and so one note is small enough for the serial link.

**Handles.** 3 to 16 characters, lowercase letters, digits and underscore,
unique, set at sign-up and not changeable in v1. Usernames are separate and
private.

**Limits.** At most 10 recipients per note, 30 notes sent per account per
hour, and the sender may not be a recipient. Notes are kept until a person
deletes their own copy (the sender's copy and each recipient's copy are
separate).

**Directory.** Any logged-in user can search all handles by prefix. It
returns handles only. There is no opt-out in v1.

**Invite codes.** Single use, random, optionally expiring. The owner creates
and lists them with an admin command in the container, for example
`docker exec scribble scribble-admin invite create`. The same tool can
disable an account.

**Where the code lives.** A `server/` folder (API, web UI, Dockerfile) and a
`deploy/` folder (systemd unit, example nginx config, deployment notes) in
this repo, and the Palm app at `palm/apps/scribble/`. Move it to its own
repo if you prefer.

## How it behaves

### Accounts and login

- Sign-up (web only): invite code, username, password, handle.
- Web login uses a session cookie.
- The Palm logs in once, typing username and password in the app. The
  server answers with a long random device token and the handle. The Palm
  stores only the token and sends it on every request. The server stores
  only a hash of each token. In the web UI a user can list their devices
  and revoke any of them. Changing the password revokes nothing by itself
  but the web offers "log out all devices".
- Passwords are hashed with a modern algorithm (argon2 or bcrypt).
  Failed logins are rate limited.

### Sending

1. Sender picks 1 to 10 handles and draws a sketch.
2. The Palm sends one POST containing the handle list and the 2400-byte
   bitmap.
3. The server records the note once, plus one delivery row per recipient,
   plus the sender's own copy for their "sent" list. The sender's sent view
   shows all recipients. A recipient's view shows the sender and only that
   recipient.
4. Unknown, disabled, or duplicate handles reject the whole send with a
   clear error line the Palm can show.

### Receiving

- Inbox: newest first, showing sender handle, date and an unread marker.
- Opening a note downloads its bitmap, marks it read, and shows it.
- A recipient can delete their copy. The Palm and the web UI can both do
  this.

### Web UI

Pages: sign-up with invite code, login, inbox, sent, a single note view
(image, from, date, "to"), devices, and log out. It is a React single page
app served by the same container. Notes appear as PNGs.

## Interface between the Palm and the server

All requests are HTTPS. After login every request carries
`Authorization: Bearer <device token>`. Responses are plain text, as the
News app uses, because the Palm has no JSON-friendly parsing for lists.
Exact paths and wording are for the builder, but the shape is:

| Purpose | Request | Response |
|---|---|---|
| Log in | POST with `username`, `password` lines | token line, handle line, or an error line |
| Inbox | GET, with a limit and offset | one line per note: id, time, sender, read flag |
| Sent | GET, same | one line per note: id, time, recipient handles |
| Directory | GET with a prefix, limit and offset | one handle per line |
| Fetch a note | GET by id | exactly 2400 bytes (or an error) |
| Send | POST: first line is comma-separated handles, then the 2400 bytes | `ok` plus new id, or an error line |
| Delete | DELETE by id | `ok` |

Errors are a first line beginning `err ` followed by a short reason the Palm
can display. The web UI uses its own JSON endpoints and sessions; the
two surfaces share the same database and rules. The Palm interface
should be versioned in its path so it can change later.

## Palm app (`palm/apps/scribble`)

Follows the other network apps: fujinet-lib HTTP through
`../common/fnapp.c`, forms and event loop like `../news/`, calls
`fuji_palmos_close()` on exit, 160x160 mono. The app has:

- **Setup form**: server address and login (username, password). Shown the
  first time and from a menu.
- **Inbox**: a list of notes, a Refresh button, Sent toggle, and a New
  button. Selecting a note opens the viewer.
- **Viewer**: draws the 160x120 bitmap, shows sender and date, with Delete,
  Reply (opens compose with the sender already chosen) and Back.
- **Compose**: a 160x120 drawing area that follows the stylus, Clear and
  Undo (of the last stroke, a guess at one level), a To... button and Send.
  The strokes must end up in a 1-bit buffer in exactly the wire layout, not
  just on the screen.
- **Recipient picker**: the directory with a prefix search, checkmarks for
  multiple selections, and a count of 10 at most.

Drawing in the buffer and sending/receiving should be written so the
bitmap handling can be unit tested on the host, like `core/` and
`htmlclean`.

## What is left to the builder

Database schema, API paths and wording, token and invite formats, how the
web app is styled, the exact Palm layouts, how Undo is stored, the
migration approach, and how the dev setup runs the server locally against
fujinet-pc.

## Delivery

Build order: server and API with tests, then the web UI, then the Palm
viewer and inbox, then the Palm compose and picker. Each step should work
on its own against the previous one.

Verification: automated tests for the server (accounts, invites, BCC
privacy, authorization, limits), host tests for the bitmap code, and a
documented manual run of the Palm app against a local server using
fujinet-pc and the emulator (`docs/emulator-testing.md`). The deployment
notes should be tested by building the image and starting it the way the
systemd unit does.
