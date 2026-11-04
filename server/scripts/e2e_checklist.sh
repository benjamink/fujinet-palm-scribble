#!/bin/sh
# Automates the server-only lines of the Scribble manual checklist (docs/scribble.md)
# against a running server, using only curl and POSIX sh.
#
#   BASE=http://127.0.0.1:8000 ADMIN="docker exec scribble scribble-admin" ./e2e_checklist.sh
#   ./e2e_checklist.sh http://127.0.0.1:8000 "docker exec scribble scribble-admin"
#
# The server must have been started with SCRIBBLE_SECURE_COOKIES=0 when BASE is
# plain http (otherwise curl will not send the session cookie back).
# Usernames carry a timestamp, so the script can be run repeatedly against the
# same database. Tokens, passwords and invite codes are never printed.
# Exit status: 0 if every check passed, 1 otherwise.

BASE=${1:-${BASE:-}}
ADMIN=${2:-${ADMIN:-}}
if [ -z "$BASE" ] || [ -z "$ADMIN" ]; then
    echo "usage: BASE=http://host:port ADMIN='docker exec scribble scribble-admin' $0" >&2
    exit 2
fi

T=$(mktemp -d) || exit 2
trap 'rm -rf "$T"' EXIT
trap 'exit 130' INT TERM

PASSES=0
FAILS=0
pass() { PASSES=$((PASSES + 1)); echo "PASS  $1"; }
fail() { FAILS=$((FAILS + 1)); echo "FAIL  $1"; }
# check DESCRIPTION: passes when the previous command's status was 0 (use as: cmd; check "..." $?)
check() { if [ "$2" -eq 0 ]; then pass "$1"; else fail "$1"; fi; }

PW="pw-$$-e2e-secret"
RUN=$(date +%s)
SUFFIX=$(printf '%s' "$RUN" | tail -c 8)   # keeps handles within 16 chars
HDR_JSON='Content-Type: application/json'
HDR_XRW='X-Requested-With: scribble'

# --- helpers -----------------------------------------------------------------

# web METHOD PATH JARNAME [JSONBODY]: prints the HTTP status, body in $T/web.out
web() {
    _m=$1; _p=$2; _j=$3; _b=${4:-}
    if [ -n "$_b" ]; then
        curl -s -o "$T/web.out" -w '%{http_code}' -X "$_m" -H "$HDR_XRW" -H "$HDR_JSON" \
            -b "$T/$_j.jar" -c "$T/$_j.jar" --data "$_b" "$BASE$_p"
    else
        curl -s -o "$T/web.out" -w '%{http_code}' -X "$_m" -H "$HDR_XRW" \
            -b "$T/$_j.jar" -c "$T/$_j.jar" "$BASE$_p"
    fi
}

# palm METHOD PATH TOKENFILE [BODYFILE]: prints the HTTP status, body in $T/palm.out
palm() {
    _m=$1; _p=$2; _t=$3; _f=${4:-}
    if [ -n "$_f" ]; then
        curl -s -o "$T/palm.out" -w '%{http_code}' -X "$_m" -H "Authorization: Bearer $(cat "$T/$_t.tok")" \
            --data-binary "@$_f" "$BASE$_p"
    else
        curl -s -o "$T/palm.out" -w '%{http_code}' -X "$_m" -H "Authorization: Bearer $(cat "$T/$_t.tok")" \
            "$BASE$_p"
    fi
}

first_line() { head -n 1 "$T/palm.out"; }

new_user() {  # new_user NAME: invite, web signup (stores cookie jar $T/NAME.jar), Palm login (token $T/NAME.tok)
    _n=$1
    _inv=$($ADMIN invite create 2>/dev/null | tail -n 1)
    [ -n "$_inv" ] || return 1
    _u="${_n}${SUFFIX}"
    printf '%s' "$_u" > "$T/$_n.user"
    _s=$(web POST /api/signup "$_n" \
        "{\"invite\":\"$_inv\",\"username\":\"$_u\",\"password\":\"$PW\",\"handle\":\"$_u\"}")
    [ "$_s" = 200 ] || return 1
    palm_login "$_n"
}

palm_login() {  # palm_login NAME -> token file
    printf '%s\n%s\n' "$(cat "$T/$1.user")" "$PW" > "$T/login.body"
    curl -s -o "$T/login.out" -X POST --data-binary "@$T/login.body" "$BASE/palm/v1/login"
    [ "$(sed -n 1p "$T/login.out")" = ok ] || return 1
    sed -n 2p "$T/login.out" > "$T/$1.tok"
    [ -s "$T/$1.tok" ]
}

handle() { cat "$T/$1.user"; }

# A 2400-byte note that contains 0x00, 0x0A, 0x0D and 0xFF bytes up front.
{ printf '\000\n\r\377\200\n\000'; head -c 2393 /dev/urandom; } > "$T/note.bin"
[ "$(wc -c < "$T/note.bin")" -eq 2400 ] || { echo "cannot build test bitmap" >&2; exit 2; }
# The same bitmap behind a recipient line: send bodies are "h1,h2\n" + 2400 bytes.
send_body() {  # send_body OUTFILE HANDLES [BITMAPFILE]
    { printf '%s\n' "$2"; cat "${3:-$T/note.bin}"; } > "$1"
}

echo "Scribble server e2e checklist against $BASE (run $RUN)"

# --- setup: invites, signups, Palm logins --------------------------------------
new_user alice; check "invite + web signup + Palm login: sender" $?
new_user bob;   check "invite + web signup + Palm login: recipient 1" $?
new_user carol; check "invite + web signup + Palm login: recipient 2" $?
new_user dave;  check "invite + web signup + Palm login: bystander" $?
new_user rl;    check "invite + web signup + Palm login: rate-limit user" $?

[ "$(web GET /api/me alice)" = 200 ] && grep -q "$(handle alice)" "$T/web.out"
check "web session works after signup (/api/me returns the handle)" $?

# A used invite cannot be reused.
_inv=$($ADMIN invite create 2>/dev/null | tail -n 1)
web POST /api/signup x "{\"invite\":\"$_inv\",\"username\":\"dup1$SUFFIX\",\"password\":\"$PW\",\"handle\":\"dup1$SUFFIX\"}" >/dev/null
_s=$(web POST /api/signup x "{\"invite\":\"$_inv\",\"username\":\"dup2$SUFFIX\",\"password\":\"$PW\",\"handle\":\"dup2$SUFFIX\"}")
[ "$_s" != 200 ] && grep -q 'bad invite' "$T/web.out"
check "a used invite is rejected on second signup" $?

# Wrong password: Palm sees an error line, not a token.
printf '%s\nwrong-password-x\n' "$(handle alice)" > "$T/bad.body"
curl -s -o "$T/bad.out" -X POST --data-binary "@$T/bad.body" "$BASE/palm/v1/login"
[ "$(sed -n 1p "$T/bad.out")" = "err bad login" ] && [ "$(wc -l < "$T/bad.out")" -eq 1 ]
check "wrong password: Palm login returns 'err bad login' and no token" $?

# --- send to two recipients ------------------------------------------------------
B=$(handle bob); C=$(handle carol); A=$(handle alice)
send_body "$T/send2.body" "$B,$C"
_s=$(palm POST /palm/v1/send alice "$T/send2.body")
NOTE=$(sed -n 2p "$T/palm.out")
[ "$_s" = 200 ] && [ "$(first_line)" = ok ] && [ -n "$NOTE" ]
check "send a 2400-byte note (with 0x00/0x0A bytes) to two recipients" $?

# Sender's Sent lists both recipients (sorted by handle); Palm and web agree.
palm GET /palm/v1/sent alice >/dev/null
grep -q "^$NOTE|[0-9]*|$B,$C\$" "$T/palm.out"
check "sender's Palm Sent lists both recipients" $?
web GET /api/sent alice >/dev/null
grep -q "\"$B\"" "$T/web.out" && grep -q "\"$C\"" "$T/web.out"
check "sender's web Sent lists both recipients" $?

# --- unread state, then read-marking ----------------------------------------------
palm GET /palm/v1/inbox bob >/dev/null
grep -q "^$NOTE|[0-9]*|$A|1\$" "$T/palm.out"
check "recipient 1 sees the note unread in the Palm inbox" $?
palm GET /palm/v1/inbox carol >/dev/null
grep -q "^$NOTE|[0-9]*|$A|1\$" "$T/palm.out"
check "recipient 2 sees the note unread in the Palm inbox" $?

_s=$(palm GET "/palm/v1/notes/$NOTE" bob)
[ "$_s" = 200 ] && cmp -s "$T/palm.out" "$T/note.bin"
check "recipient 1 fetches the note over the Palm API: byte-identical to what was sent" $?
palm GET /palm/v1/inbox bob >/dev/null
grep -q "^$NOTE|[0-9]*|$A|0\$" "$T/palm.out"
check "fetching marks it read for recipient 1" $?
palm GET /palm/v1/inbox carol >/dev/null
grep -q "^$NOTE|[0-9]*|$A|1\$" "$T/palm.out"
check "recipient 2's copy is still unread" $?

# --- BCC privacy -----------------------------------------------------------------
_s=$(web GET "/api/notes/$NOTE" bob)
[ "$_s" = 200 ] && grep -q "\"$B\"" "$T/web.out" && ! grep -q "$C" "$T/web.out"
check "recipient 1's web view lists only themselves (not recipient 2)" $?
_s=$(web GET "/api/notes/$NOTE" carol)
[ "$_s" = 200 ] && grep -q "\"$C\"" "$T/web.out" && ! grep -q "$B" "$T/web.out"
check "recipient 2's web view lists only themselves (not recipient 1)" $?
web GET "/api/notes/$NOTE" alice >/dev/null
grep -q "\"$B\"" "$T/web.out" && grep -q "\"$C\"" "$T/web.out" && grep -q '"role":"sender"' "$T/web.out"
check "sender's web view lists both recipients" $?
_s=$(palm GET /palm/v1/inbox bob)
[ "$_s" = 200 ] && ! grep -q "$C" "$T/palm.out"
check "recipient 1's Palm inbox does not mention recipient 2" $?

palm GET "/palm/v1/notes/$NOTE" alice >/dev/null
cmp -s "$T/palm.out" "$T/note.bin"
check "sender fetches the note over the Palm API: byte-identical" $?
palm GET "/palm/v1/notes/$NOTE" carol >/dev/null
cmp -s "$T/palm.out" "$T/note.bin"
check "recipient 2 fetches the note over the Palm API: byte-identical" $?
_s=$(curl -s -o "$T/png.out" -w '%{http_code}' -b "$T/bob.jar" "$BASE/api/notes/$NOTE.png")
[ "$_s" = 200 ] && head -c 8 "$T/png.out" | od -An -c | grep -q 'P *N *G'
check "web PNG rendering of the note is a PNG" $?

# --- single recipient + reply ------------------------------------------------------
send_body "$T/reply.body" "$A"
_s=$(palm POST /palm/v1/send bob "$T/reply.body")
REPLY=$(sed -n 2p "$T/palm.out")
[ "$_s" = 200  ] && [ "$(first_line)" = ok ] && [ -n "$REPLY" ]
check "recipient 1 sends a single-recipient reply to the sender" $?
palm GET /palm/v1/inbox alice >/dev/null
grep -q "^$REPLY|[0-9]*|$B|1\$" "$T/palm.out"
check "the reply shows up unread in the sender's inbox" $?

# --- per-copy delete semantics -------------------------------------------------------
_s=$(palm DELETE "/palm/v1/notes/$NOTE" bob)
[ "$_s" = 200 ] && [ "$(first_line)" = ok ]
check "recipient 1 deletes their copy" $?
palm GET "/palm/v1/notes/$NOTE" bob >/dev/null
[ "$(first_line)" = "err no such note" ]
check "recipient 1 can no longer fetch it" $?
_s=$(palm GET /palm/v1/inbox bob)
[ "$_s" = 200 ] && ! grep -q "^$NOTE|" "$T/palm.out"
check "it is gone from recipient 1's inbox" $?
palm GET "/palm/v1/notes/$NOTE" carol >/dev/null
cmp -s "$T/palm.out" "$T/note.bin"
check "recipient 2 still has their copy after recipient 1 deleted" $?
palm GET /palm/v1/sent alice >/dev/null
grep -q "^$NOTE|" "$T/palm.out"
check "sender still has the note in Sent after a recipient deleted" $?
palm DELETE "/palm/v1/notes/$NOTE" bob >/dev/null
[ "$(first_line)" = "err no such note" ]
check "deleting an already deleted copy reports 'no such note'" $?

_s=$(palm DELETE "/palm/v1/notes/$NOTE" alice)
[ "$(first_line)" = ok ]
check "sender deletes their copy" $?
palm GET "/palm/v1/notes/$NOTE" alice >/dev/null
[ "$(first_line)" = "err no such note" ]
check "sender can no longer fetch it" $?
palm GET "/palm/v1/notes/$NOTE" carol >/dev/null
cmp -s "$T/palm.out" "$T/note.bin"
check "recipient 2 still has their copy after the sender deleted" $?
_s=$(web DELETE "/api/notes/$NOTE" carol)
[ "$_s" = 200 ]
check "recipient 2 deletes their copy over the web API" $?
palm GET "/palm/v1/notes/$NOTE" carol >/dev/null
[ "$(first_line)" = "err no such note" ]
check "recipient 2 can no longer fetch it" $?

# --- stranger vs missing note ----------------------------------------------------------
send_body "$T/s1.body" "$B"
palm POST /palm/v1/send alice "$T/s1.body" >/dev/null
LIVE=$(sed -n 2p "$T/palm.out")
_s1=$(palm GET "/palm/v1/notes/$LIVE" dave); cp "$T/palm.out" "$T/stranger.out"
_s2=$(palm GET "/palm/v1/notes/99999999" dave); cp "$T/palm.out" "$T/missing.out"
[ "$_s1" = "$_s2" ] && cmp -s "$T/stranger.out" "$T/missing.out" && [ "$(head -n 1 "$T/missing.out")" = "err no such note" ]
check "Palm: a stranger fetching a real note gets the same response as for a missing note" $?
_s1=$(web GET "/api/notes/$LIVE" dave); cp "$T/web.out" "$T/stranger.out"
_s2=$(web GET "/api/notes/99999999" dave); cp "$T/web.out" "$T/missing.out"
[ "$_s1" = 404 ] && [ "$_s1" = "$_s2" ] && cmp -s "$T/stranger.out" "$T/missing.out"
check "web: a stranger fetching a real note gets the same 404 as for a missing note" $?
_s=$(palm DELETE "/palm/v1/notes/$LIVE" dave)
[ "$(first_line)" = "err no such note" ]
check "a stranger cannot delete someone else's note" $?
palm GET "/palm/v1/notes/$LIVE" bob >/dev/null
cmp -s "$T/palm.out" "$T/note.bin"
check "...and the real recipient still has it" $?

# --- bad requests store nothing -----------------------------------------------------------
palm GET /palm/v1/sent alice >/dev/null; SENT_BEFORE=$(wc -l < "$T/palm.out")
palm GET /palm/v1/inbox bob >/dev/null; INBOX_BEFORE=$(wc -l < "$T/palm.out")
head -c 2399 "$T/note.bin" > "$T/short.bin"; send_body "$T/short.body" "$B" "$T/short.bin"
palm POST /palm/v1/send alice "$T/short.body" >/dev/null
[ "$(first_line)" = "err bad sketch" ]
check "a 2399-byte body is rejected with 'err bad sketch'" $?
{ cat "$T/note.bin"; printf 'x'; } > "$T/long.bin"; send_body "$T/long.body" "$B" "$T/long.bin"
palm POST /palm/v1/send alice "$T/long.body" >/dev/null
[ "$(first_line)" = "err bad sketch" ]
check "a 2401-byte body is rejected with 'err bad sketch'" $?
send_body "$T/zero.body" "$B" /dev/null
palm POST /palm/v1/send alice "$T/zero.body" >/dev/null
[ "$(first_line)" = "err bad sketch" ]
check "an empty bitmap is rejected with 'err bad sketch'" $?
LIST11=$(i=1; out=; while [ $i -le 11 ]; do out="$out${out:+,}usr$i"; i=$((i + 1)); done; echo "$out")
send_body "$T/eleven.body" "$LIST11"
palm POST /palm/v1/send alice "$T/eleven.body" >/dev/null
[ "$(first_line)" = "err too many recipients" ]
check "11 recipients are rejected with 'err too many recipients'" $?
send_body "$T/self.body" "$A"
palm POST /palm/v1/send alice "$T/self.body" >/dev/null
[ "$(first_line)" = "err cannot send to yourself" ]
check "sending to yourself is rejected" $?
send_body "$T/dup.body" "$B,$B"
palm POST /palm/v1/send alice "$T/dup.body" >/dev/null
[ "$(first_line)" = "err duplicate recipient" ]
check "a duplicate recipient is rejected" $?
send_body "$T/unk.body" "nx$SUFFIX"
palm POST /palm/v1/send alice "$T/unk.body" >/dev/null
[ "$(first_line)" = "err unknown handle: nx$SUFFIX" ]
check "an unknown handle is rejected by name" $?
palm GET /palm/v1/sent alice >/dev/null; SENT_AFTER=$(wc -l < "$T/palm.out")
palm GET /palm/v1/inbox bob >/dev/null; INBOX_AFTER=$(wc -l < "$T/palm.out")
[ "$SENT_BEFORE" = "$SENT_AFTER" ] && [ "$INBOX_BEFORE" = "$INBOX_AFTER" ]
check "none of the rejected sends stored anything (Sent and inbox unchanged)" $?

# --- rate limit ---------------------------------------------------------------------------------
send_body "$T/rl.body" "$B"
i=1; ok=0
while [ $i -le 30 ]; do
    palm POST /palm/v1/send rl "$T/rl.body" >/dev/null
    [ "$(first_line)" = ok ] && ok=$((ok + 1))
    i=$((i + 1))
done
[ "$ok" -eq 30 ]
check "30 sends in an hour are accepted" $?
_s=$(palm POST /palm/v1/send rl "$T/rl.body")
[ "$(first_line)" = "err rate limit" ] && [ "$_s" = 200 ]
check "the 31st send is rejected with 'err rate limit' (HTTP $_s)" $?

# --- device revoke ------------------------------------------------------------------------------
palm_login alice   # a second device for alice
web GET /api/devices alice >/dev/null
DEVS=$(grep -o '"id":[0-9]*' "$T/web.out" | sed 's/"id"://')
[ -n "$DEVS" ]
check "web lists the sender's devices" $?
[ "$(palm GET /palm/v1/inbox alice)" = 200 ]
check "the Palm token works before revocation" $?
for d in $DEVS; do web DELETE "/api/devices/$d" alice >/dev/null; done
_s=$(palm GET /palm/v1/inbox alice)
[ "$_s" = 401 ] && [ "$(first_line)" = "err auth" ]
check "after revoking the device in the web UI the Palm token gets 401 'err auth'" $?
[ "$(web GET /api/me alice)" = 200 ]
check "revoking devices leaves the web session logged in" $?
palm_login alice
[ "$(palm GET /palm/v1/inbox alice)" = 200 ]
check "logging in again on the Palm issues a working new token" $?
_s=$(web POST /api/devices/revoke-all alice)
[ "$_s" = 200 ] && [ "$(palm GET /palm/v1/inbox alice)" = 401 ]
check "revoke-all invalidates the Palm token" $?

# --- web logout ---------------------------------------------------------------------------------
[ "$(web POST /api/logout dave)" = 200 ] && [ "$(web GET /api/me dave)" = 401 ]
check "web logout ends the session (/api/me returns 401)" $?
[ "$(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -b "$T/bob.jar" --data '{}' "$BASE/api/logout")" = 403 ]
check "a mutating web request without X-Requested-With is refused (403)" $?

# --- disable user -----------------------------------------------------------------------------------
[ "$(web GET /api/me carol)" = 200 ] && [ "$(palm GET /palm/v1/inbox carol)" = 200 ]
check "before disabling, carol works on web and Palm" $?
$ADMIN user disable "$(handle carol)" >/dev/null 2>&1
check "scribble-admin user disable succeeds" $?
_w=$(web GET /api/me carol); _p=$(palm GET /palm/v1/inbox carol)
[ "$_w" = 401 ] && [ "$_p" = 401 ] && [ "$(first_line)" = "err auth" ]
check "disabled user: existing web session and Palm token are both refused" $?
printf '%s\n%s\n' "$(handle carol)" "$PW" > "$T/login.body"
curl -s -o "$T/login.out" -X POST --data-binary "@$T/login.body" "$BASE/palm/v1/login"
[ "$(sed -n 1p "$T/login.out")" = "err account disabled" ]
check "disabled user: a new Palm login says 'err account disabled'" $?
_s=$(web POST /api/login carol "{\"username\":\"$(handle carol)\",\"password\":\"$PW\"}")
[ "$_s" != 200 ]
check "disabled user: a new web login is refused (HTTP $_s)" $?
_s=$(palm GET "/palm/v1/directory?prefix=$(handle carol)" dave)
[ "$_s" = 200 ] && ! grep -q "^$(handle carol)\$" "$T/palm.out"
check "disabled user is dropped from the directory" $?
send_body "$T/tocarol.body" "$(handle carol)"
palm POST /palm/v1/send dave "$T/tocarol.body" >/dev/null
[ "$(first_line)" = "err unknown handle: $(handle carol)" ]
check "sending to a disabled user is refused" $?
$ADMIN user enable "$(handle carol)" >/dev/null 2>&1
palm_login carol
check "re-enabling the user lets them log in again" $?

echo
echo "$PASSES passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
