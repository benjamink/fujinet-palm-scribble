/* palm/apps/scribble/net.h
 *
 * Scribble server calls over FujiNet HTTP (fnapp_http_request), one
 * blocking request per call; see docs/scribble.md "Palm protocol" and
 * "FujiNet HTTP call order (Palm)". String building and reply parsing live
 * in the host-tested netutil.c; this file only moves bytes.
 *
 * `server` is the base URL the user typed ("https://host", a trailing '/'
 * is ignored). `token` is the bearer token from net_login.
 *
 * Every function returns errNone on success or one of the NET_ERR_* codes
 * below; net_last_reason() then describes the failure (the server's text
 * from an "err <reason>" reply, or a short client-side reason). Include
 * after PalmOS.h (fnapp.h's header order).
 */
#ifndef NET_H
#define NET_H

#define NET_ERR_SERVER (appErrorClass | 1) /* "err <reason>", or a malformed reply */
#define NET_ERR_AUTH   (appErrorClass | 2) /* "err auth": token is no longer valid */
#define NET_ERR_IO     (appErrorClass | 3) /* no connection / FujiNet failure */
#define NET_ERR_ARG    (appErrorClass | 4) /* bad server address, prefix, ... */
#define NET_ERR_MEM    (appErrorClass | 5) /* out of memory */

#define NET_LIST_CAP 4096u /* largest list / directory reply read */

/* POST /palm/v1/login. On success copies the token (tcap bytes) and handle
 * (hcap bytes) out. The request body holding the password is zeroed before
 * returning, on every path. */
Err net_login(const char *server, const char *user, const char *pass,
              char *token, int tcap, char *handle, int hcap);

/* GET /palm/v1/inbox (sent == 0) or /sent, with ?offset=&limit=. On
 * success *h is a new handle (caller frees it with MemHandleFree) holding
 * ONLY the rows after the "ok" line, NUL terminated and writable, ready for
 * proto_parse_inbox / proto_parse_sent. On failure *h is 0. */
Err net_list(const char *server, const char *token, int sent, int offset, int limit,
             MemHandle *h);

/* GET /palm/v1/notes/<id> into bitmap (SK_BYTES = 2400 bytes). bitmap is
 * written only on success. Marks the note read on the server. */
Err net_fetch_note(const char *server, const char *token, unsigned long id,
                   unsigned char *bitmap);

/* DELETE /palm/v1/notes/<id>. */
Err net_delete(const char *server, const char *token, unsigned long id);

/* GET /palm/v1/directory (prefix of [a-z0-9_], may be "" or NULL). *h as
 * net_list, for proto_parse_handles. */
Err net_directory(const char *server, const char *token, const char *prefix,
                  int offset, int limit, MemHandle *h);

/* POST /palm/v1/send with "handles_csv\n" + the 2400 byte bitmap. On
 * success stores the new note id in *newId (if non-NULL). */
Err net_send(const char *server, const char *token, const char *handles_csv,
             const unsigned char *bitmap, unsigned long *newId);

/* Reason for the last failure ("" after a success). Never NULL. */
const char *net_last_reason(void);

#endif
