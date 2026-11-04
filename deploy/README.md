# Deploying Scribble

Scribble runs as one Docker (or podman) container behind nginx, managed by systemd.
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

   Using a pre-built registry image instead? Set it in `ExecStart` (see
   "Podman hosts" below for name rules).

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

## Podman hosts

Some distros alias `docker` to podman (`podman-docker`; there is then no
`docker.service`). The unit works unchanged, with these differences, all hit
on a real Debian + podman 5 / netavark host:

- **Fully qualified image names.** Podman has no default registry, so
  `docker pull user/image:tag` fails ("short-name ... did not resolve").
  Use `docker.io/<user>/<image>:<tag>` in `ExecStart` and for pulls, or put
  `unqualified-search-registries = ["docker.io"]` in
  `/etc/containers/registries.conf`. `docker login` also needs the registry:
  `docker login docker.io`. (`touch /etc/containers/nodocker` silences the
  "Emulate Docker CLI" notice.)
- **No systemd sandboxing in the unit.** `PrivateTmp`, `ProtectHome`,
  `ProtectControlGroups` and friends give the service a private mount
  namespace. Podman then cannot create the container cgroup ("create directory
  .../libpod-*.scope/container: No such file or directory") and its network
  namespace mount is invisible to the host ("unknown FS magic" from
  `podman port`). The shipped unit therefore has none; the container is the
  sandbox. Do not add them back.
- **Stale port-forward rules.** If the service crashed or was restarted while
  broken, netavark can leave old DNAT rules for `127.0.0.1:8000` pointing at
  dead container IPs. The first matching rule wins, so `curl
  http://127.0.0.1:8000` fails with "No route to host" even though the
  container is healthy. Look with `sudo iptables -t nat -S | grep 8000`: there
  should be exactly one DNAT target for port 8000. Stop the service,
  `podman rm -f scribble`, delete the stale rules with `iptables -t nat -D` (the
  same line with `-D` instead of `-A`; leave other containers' rules alone), then start
  the service. Rebooting also clears them.
- **`route_localnet`.** Publishing on `127.0.0.1` relies on kernel DNAT of
  localhost traffic; some hosts need `net.ipv4.conf.all.route_localnet=1`.
  That setting also lets other machines on the same network segment reach
  localhost-only services on this host, so prefer limiting it to the podman
  bridge (`net.ipv4.conf.podman0.route_localnet=1`) and test whether you need
  it at all. Not confirmed necessary on the reference host.

## nginx on older distros

`http2 on;` needs nginx 1.25.1 or newer (Debian 12 has 1.22). The shipped
config uses `listen 443 ssl http2;`, which works everywhere (newer nginx only
logs a deprecation warning).

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
