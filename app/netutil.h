/* palm/apps/scribble/netutil.h
 *
 * The pure, host-testable half of the Scribble network layer (net.c is the
 * Palm half that actually calls fnapp_http_request). Everything here only
 * builds strings and interprets reply bytes: FujiNet device specs
 * ("N:" + server + path), the Authorization header line, and the
 * classification of server replies (see docs/scribble.md "Palm protocol").
 *
 * Portable ANSI C89 with no platform headers and no libc, like proto.c, so
 * it builds on the host (test_netutil.c) and with m68k-palmos-gcc, where
 * int is 16 bits. Every builder is bounded by its cap argument and returns
 * -1 (writing an empty string when cap > 0) instead of truncating.
 */
#ifndef NETUTIL_H
#define NETUTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#define NU_SPEC_CAP 160   /* device spec buffer size used by net.c */
#define NU_HDR_CAP 80     /* "Authorization: Bearer " + a 43 char token */
#define NU_PREFIX_MAX 16  /* longest directory prefix (a full handle) */
#define NU_LIMIT_MAX 50   /* largest page size the client asks for */
#define NU_NOTE_BYTES 2400

/* Reply classes. */
#define NU_OK 0
#define NU_ERR 1   /* "err <reason>", or a malformed / missing reply */
#define NU_AUTH 2  /* "err auth": the token is no longer valid */

/*
 * Returns 1 if server is a usable base URL: "http://" or "https://"
 * followed by at least one host character, only printable ASCII (no
 * spaces or control characters) and no '?' or '#'. Trailing '/' are
 * allowed (they are stripped when building a spec). Otherwise 0.
 */
int nu_server_ok(const char *server);

/*
 * Returns 1 if server is nu_server_ok and uses plain "http://" (so the
 * password and token travel unencrypted), else 0. Used to warn, not block.
 */
int nu_server_cleartext(const char *server);

/*
 * Writes "N:" + server (trailing '/' stripped) + path + NUL into out and
 * returns the length (without the NUL). path must start with '/'. Returns
 * -1 if the server is not nu_server_ok, path is NULL or does not start
 * with '/', or the result plus its NUL does not fit in cap bytes.
 */
int nu_spec(char *out, int cap, const char *server, const char *path);

/* "/palm/v1/inbox?offset=O&limit=L" (or /sent when sent != 0). offset must
 * be >= 0 and limit 1..NU_LIMIT_MAX, else -1. Otherwise as nu_spec. */
int nu_spec_list(char *out, int cap, const char *server, int sent, int offset, int limit);

/* "/palm/v1/directory?offset=O&limit=L" plus "&prefix=P" when prefix is
 * non-empty. prefix (NULL means "") may only hold [a-z0-9_] and at most
 * NU_PREFIX_MAX chars, else -1. Ranges as nu_spec_list. */
int nu_spec_directory(char *out, int cap, const char *server, const char *prefix,
                      int offset, int limit);

/* "/palm/v1/notes/<id>". As nu_spec. */
int nu_spec_note(char *out, int cap, const char *server, unsigned long id);

/*
 * Writes "Authorization: Bearer <token>" (exactly one space after the
 * colon, no CR/LF) and returns its length. Returns -1 if token is NULL,
 * empty, holds anything but printable non-space ASCII, or does not fit.
 */
int nu_auth_header(char *out, int cap, const char *token);

/*
 * Classifies a NUL-terminated text reply of len bytes. NU_OK sets *rest
 * (if non-NULL) to the text after the "ok" line. NU_AUTH for "err auth".
 * NU_ERR otherwise, with the reason copied (truncated, NUL terminated)
 * into reason (if non-NULL, reason_cap > 0): the server's text after
 * "err ", "no reply" when len is 0, or "bad reply" for anything else.
 * For NU_AUTH the reason is "auth".
 */
int nu_reply(const char *body, unsigned long len, const char **rest,
             char *reason, int reason_cap);

/*
 * Classifies a note fetch reply: exactly NU_NOTE_BYTES bytes is NU_OK (the
 * bitmap). Any other length is read as text with nu_reply; a text "ok"
 * reply (wrong for a note) becomes NU_ERR "bad reply". body must be NUL
 * terminated at body[len] (fnapp guarantees this).
 */
int nu_note_reply(const char *body, unsigned long len, char *reason, int reason_cap);

/*
 * Login reply "ok\n<token>\n<handle>\n" (CRLF tolerated, last newline
 * optional). On NU_OK copies the token (1..tcap-1 printable non-space
 * chars) and the handle (1..16 chars of [a-z0-9_], must fit hcap) out.
 * A well-formed "ok" with a missing or invalid token/handle is NU_ERR
 * "bad reply". token/handle are only written on NU_OK.
 */
int nu_login_reply(const char *body, unsigned long len, char *token, int tcap,
                   char *handle, int hcap, char *reason, int reason_cap);

/* Send reply "ok\n<id>\n": on NU_OK stores the decimal id (1..2^32-1) in
 * *id. A malformed or overflowing id is NU_ERR "bad reply". */
int nu_send_reply(const char *body, unsigned long len, unsigned long *id,
                  char *reason, int reason_cap);

#ifdef __cplusplus
}
#endif

#endif
