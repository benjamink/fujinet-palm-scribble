# Deploying Scribble

Scribble runs as one Docker container behind nginx, managed by systemd.
The container listens on `127.0.0.1:8000` only; nginx terminates TLS.

**Security note (forwarded headers).** The container runs uvicorn with
`--proxy-headers --forwarded-allow-ips="*"` so rate limiting sees the real
client IP from nginx's `X-Forwarded-For`. This is safe only because
`scribble.service` publishes the port on `127.0.0.1` alone. Never publish it on
a public address, or clients could spoof their IP. The nginx config likewise
*overwrites* `X-Forwarded-For` with `$remote_addr` rather than appending.

## Install

1. Build the image (from the repo root):

       docker build -t scribble:latest server/

   Podman hosts (`docker` is a podman shim, no `docker.service`): the unit
   works unchanged, but image names must be fully qualified. Either pull
   `docker.io/<user>/<image>:<tag>` and use that name in `ExecStart`, or add
   `unqualified-search-registries = ["docker.io"]` to
   `/etc/containers/registries.conf`.

2. Create the data directory, owned by the container user (UID/GID 10001):

       sudo install -d -o 10001 -g 10001 -m 750 /var/lib/scribble

3. Create the env file:

       sudo install -d /etc/scribble
       echo 'SCRIBBLE_SECURE_COOKIES=1' | sudo tee /etc/scribble/scribble.env
       sudo chmod 640 /etc/scribble/scribble.env

   `SCRIBBLE_SECURE_COOKIES=1` is the default; only set `0` when testing over
   plain HTTP. Other variables: `SCRIBBLE_DB` (default `/data/scribble.db`),
   `SCRIBBLE_STATIC` (default `/app/web`).

4. Install and start the unit:

       sudo cp deploy/scribble.service /etc/systemd/system/
       sudo systemctl daemon-reload
       sudo systemctl enable --now scribble

5. Certificate, then nginx. The config refers to the certificate files, so
   `nginx -t` fails until they exist; obtain the certificate first, while nothing
   else is using port 80:

       sudo certbot certonly --standalone -d your.host

   Then copy `deploy/nginx.conf` into nginx's `conf.d/`, replace
   `scribble.example.com` (server names and certificate paths), check with
   `sudo nginx -t`, and reload nginx (`sudo systemctl reload nginx`). The config
   redirects HTTP to HTTPS, limits bodies to 16k and rate-limits
   `/palm/v1/login`, `/api/login`, `/api/signup`. (Syntax-checked with
   nginx 1.28 using a throwaway self-signed certificate.)

## First invite and user management

    docker exec scribble scribble-admin invite create [--expires-days N]
    docker exec scribble scribble-admin invite list
    docker exec scribble scribble-admin user list
    docker exec scribble scribble-admin user disable USERNAME   # and: enable

A disabled user's tokens and sessions stop working immediately. Give the invite
code to the new user; they sign up on the website. The Palm app only logs in
to an existing account; it cannot sign up.

## Upgrading

    git pull
    docker build -t scribble:latest server/
    sudo systemctl restart scribble

The schema is created and checked at startup; data in `/var/lib/scribble` is kept.

## Backup

Data is a SQLite database (WAL mode) in `/var/lib/scribble`. Take an online
backup with SQLite's backup API, which is safe while the server runs and includes
recent writes still in the WAL file:

    sqlite3 /var/lib/scribble/scribble.db ".backup '/path/to/scribble-$(date +%F).db'"

If the host has no `sqlite3`, use the container's Python (the file lands in the
data directory; move it off the host afterwards):

    docker exec scribble python -c "import sqlite3; s=sqlite3.connect('/data/scribble.db'); d=sqlite3.connect('/data/backup.db'); s.backup(d); d.close()"

Or stop the unit (`systemctl stop scribble`) and copy the whole directory,
including any `-wal` and `-shm` files. Never copy only the `.db` file while the
server runs.

## Retention caveat

Deleting a note is a soft delete: it disappears for users, but its bitmap stays
in SQLite. Tell users this if it matters; true erasure needs a manual database edit.

## FujiNet TLS check (UNVERIFIED)

Not verified by the implementer (no FujiNet device or fujinet-pc was available).
The owner must check, from a real FujiNet device or fujinet-pc, that an HTTPS
GET to `https://YOUR_HOST/palm/v1/directory` returns `err auth` (HTTP 401). That
proves FujiNet accepts the server certificate (Let's Encrypt chain) and the Palm
app can reach the server.

If the request fails with a TLS or connection error rather than `err auth`:
check the clock on the device, that nginx serves the full chain (`fullchain.pem`,
not `cert.pem`), and that FujiNet's firmware trusts the Let's Encrypt root.
If it does not, options are a certificate from a CA the firmware trusts, or
serving plain HTTP on a private network (the Palm API sends tokens in the clear
then; do not expose that to the internet).
