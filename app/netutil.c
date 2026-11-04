/* palm/apps/scribble/netutil.c -- see netutil.h. Portable C89, no libc. */
#include "netutil.h"
#include "proto.h"

/* ------------------------------------------------------------------ */
/* Bounded string building                                             */
/* ------------------------------------------------------------------ */

/* A tiny append buffer: every append is bounded by cap; once something
 * does not fit, bad is set and the result is rejected as a whole. */
typedef struct {
    char *out;
    int cap;
    int len;
    int bad;
} Buf;

static void b_init(Buf *b, char *out, int cap)
{
    b->out = out;
    b->cap = cap;
    b->len = 0;
    b->bad = (out == 0 || cap <= 0);
    if (!b->bad) {
        out[0] = 0;
    }
}

static void b_putn(Buf *b, const char *s, int n)
{
    int i;

    if (b->bad) {
        return;
    }
    if (n < 0 || n >= b->cap - b->len) { /* keep room for the NUL */
        b->bad = 1;
        return;
    }
    for (i = 0; i < n; i++) {
        b->out[b->len++] = s[i];
    }
    b->out[b->len] = 0;
}

/* strlen, or -1 for anything absurdly long (treated as "does not fit"). */
static int s_len(const char *s)
{
    int n = 0;

    while (s[n]) {
        n++;
        if (n > 30000) {
            return -1;
        }
    }
    return n;
}

static void b_puts(Buf *b, const char *s)
{
    b_putn(b, s, s_len(s));
}

static void b_putul(Buf *b, unsigned long v)
{
    char tmp[11];
    int i = (int)sizeof tmp;

    do {
        tmp[--i] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    } while (v != 0UL && i > 0);
    b_putn(b, tmp + i, (int)sizeof tmp - i);
}

/* Returns the final length, or -1 (with out emptied) on any failure. */
static int b_done(Buf *b)
{
    if (b->bad) {
        if (b->out && b->cap > 0) {
            b->out[0] = 0;
        }
        return -1;
    }
    return b->len;
}

static int starts_with(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++) {
            return 0;
        }
    }
    return 1;
}

static int handle_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/* ------------------------------------------------------------------ */
/* Server / spec                                                       */
/* ------------------------------------------------------------------ */

int nu_server_ok(const char *server)
{
    const char *p;
    const char *host;

    if (!server) {
        return 0;
    }
    if (starts_with(server, "https://")) {
        host = server + 8;
    } else if (starts_with(server, "http://")) {
        host = server + 7;
    } else {
        return 0;
    }
    for (p = server; *p; p++) {
        unsigned char c = (unsigned char)*p;

        if (c <= ' ' || c >= 0x7f || c == '?' || c == '#') {
            return 0;
        }
    }
    return *host != 0 && *host != '/'; /* at least one host character */
}

int nu_server_cleartext(const char *server)
{
    return nu_server_ok(server) && starts_with(server, "http://");
}

/* Appends "N:" + server without its trailing '/'. */
static void b_server(Buf *b, const char *server)
{
    int n;

    if (!nu_server_ok(server)) {
        b->bad = 1;
        return;
    }
    n = s_len(server);
    while (n > 0 && server[n - 1] == '/') {
        n--;
    }
    b_puts(b, "N:");
    b_putn(b, server, n);
}

int nu_spec(char *out, int cap, const char *server, const char *path)
{
    Buf b;

    b_init(&b, out, cap);
    if (!path || path[0] != '/') {
        b.bad = 1;
    }
    b_server(&b, server);
    if (!b.bad) {
        b_puts(&b, path);
    }
    return b_done(&b);
}

static int range_ok(int offset, int limit)
{
    return offset >= 0 && limit >= 1 && limit <= NU_LIMIT_MAX;
}

static void b_page(Buf *b, int offset, int limit)
{
    b_puts(b, "?offset=");
    b_putul(b, (unsigned long)offset);
    b_puts(b, "&limit=");
    b_putul(b, (unsigned long)limit);
}

int nu_spec_list(char *out, int cap, const char *server, int sent, int offset, int limit)
{
    Buf b;

    b_init(&b, out, cap);
    if (!range_ok(offset, limit)) {
        b.bad = 1;
    }
    b_server(&b, server);
    b_puts(&b, sent ? "/palm/v1/sent" : "/palm/v1/inbox");
    b_page(&b, offset, limit);
    return b_done(&b);
}

int nu_spec_directory(char *out, int cap, const char *server, const char *prefix,
                      int offset, int limit)
{
    Buf b;
    int i;

    b_init(&b, out, cap);
    if (!prefix) {
        prefix = "";
    }
    for (i = 0; prefix[i]; i++) {
        if (i >= NU_PREFIX_MAX || !handle_char(prefix[i])) {
            b.bad = 1;
            break;
        }
    }
    if (!range_ok(offset, limit)) {
        b.bad = 1;
    }
    b_server(&b, server);
    b_puts(&b, "/palm/v1/directory");
    b_page(&b, offset, limit);
    if (prefix[0]) {
        b_puts(&b, "&prefix=");
        b_puts(&b, prefix);
    }
    return b_done(&b);
}

int nu_spec_note(char *out, int cap, const char *server, unsigned long id)
{
    Buf b;

    b_init(&b, out, cap);
    b_server(&b, server);
    b_puts(&b, "/palm/v1/notes/");
    b_putul(&b, id);
    return b_done(&b);
}

/* 1 if s is non-empty printable non-space ASCII. */
static int token_ok(const char *s)
{
    if (!s || !*s) {
        return 0;
    }
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (c <= ' ' || c >= 0x7f) {
            return 0;
        }
    }
    return 1;
}

int nu_auth_header(char *out, int cap, const char *token)
{
    Buf b;

    b_init(&b, out, cap);
    if (!token_ok(token)) {
        b.bad = 1;
    } else {
        b_puts(&b, "Authorization: Bearer ");
        b_puts(&b, token);
    }
    return b_done(&b);
}

/* ------------------------------------------------------------------ */
/* Replies                                                             */
/* ------------------------------------------------------------------ */

static void set_reason(char *reason, int cap, const char *text)
{
    int i;

    if (!reason || cap <= 0) {
        return;
    }
    for (i = 0; i < cap - 1 && text[i]; i++) {
        reason[i] = text[i];
    }
    reason[i] = 0;
}

int nu_reply(const char *body, unsigned long len, const char **rest,
             char *reason, int reason_cap)
{
    char r[64];
    const char *after = 0;

    if (!body || len == 0UL) {
        set_reason(reason, reason_cap, "no reply");
        return NU_ERR;
    }
    if (proto_status(body, &after, r, (int)sizeof r)) {
        if (rest) {
            *rest = after;
        }
        return NU_OK;
    }
    set_reason(reason, reason_cap, r);
    if (r[0] == 'a' && r[1] == 'u' && r[2] == 't' && r[3] == 'h' && r[4] == 0) {
        return NU_AUTH;
    }
    return NU_ERR;
}

int nu_note_reply(const char *body, unsigned long len, char *reason, int reason_cap)
{
    int k;

    if (body && len == (unsigned long)NU_NOTE_BYTES) {
        return NU_OK;
    }
    k = nu_reply(body, len, 0, reason, reason_cap);
    if (k == NU_OK) { /* "ok" text is not a bitmap */
        set_reason(reason, reason_cap, "bad reply");
        return NU_ERR;
    }
    return k;
}

/* Copies the line at *p (ended by '\n', "\r\n" or NUL) into dst if it has
 * 1..cap-1 chars and advances *p past the line end. Returns the length, or
 * -1 if the line is empty or too long. */
static int take_line(const char **p, char *dst, int cap)
{
    const char *s = *p;
    int n = 0;
    int i;

    while (s[n] && s[n] != '\n' && s[n] != '\r') {
        n++;
        if (n >= cap) {
            return -1;
        }
    }
    if (n == 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        dst[i] = s[i];
    }
    dst[n] = 0;
    s += n;
    if (*s == '\r') {
        s++;
    }
    if (*s == '\n') {
        s++;
    }
    *p = s;
    return n;
}

static int handle_ok(const char *h)
{
    int n = 0;

    for (; *h; h++, n++) {
        if (!handle_char(*h)) {
            return 0;
        }
    }
    return n >= 1 && n <= 16;
}

static void copy_str(char *dst, const char *src)
{
    while ((*dst++ = *src++) != 0) {
    }
}

int nu_login_reply(const char *body, unsigned long len, char *token, int tcap,
                   char *handle, int hcap, char *reason, int reason_cap)
{
    const char *rest = 0;
    char t[64];
    char h[20];
    int k = nu_reply(body, len, &rest, reason, reason_cap);

    if (k != NU_OK) {
        return k;
    }
    if (!token || !handle || tcap <= 1 || hcap <= 1 ||
        take_line(&rest, t, (int)sizeof t) < 0 || !token_ok(t) ||
        take_line(&rest, h, (int)sizeof h) < 0 || !handle_ok(h) ||
        s_len(t) >= tcap || s_len(h) >= hcap) {
        set_reason(reason, reason_cap, "bad reply");
        return NU_ERR;
    }
    copy_str(token, t);
    copy_str(handle, h);
    return NU_OK;
}

int nu_send_reply(const char *body, unsigned long len, unsigned long *id,
                  char *reason, int reason_cap)
{
    const char *rest = 0;
    char line[12];
    unsigned long v = 0;
    int k = nu_reply(body, len, &rest, reason, reason_cap);
    int i;

    if (k != NU_OK) {
        return k;
    }
    if (take_line(&rest, line, (int)sizeof line) < 0) {
        goto bad;
    }
    for (i = 0; line[i]; i++) {
        unsigned long d;

        if (line[i] < '0' || line[i] > '9') {
            goto bad;
        }
        d = (unsigned long)(line[i] - '0');
        if (v > (0xFFFFFFFFUL - d) / 10UL) {
            goto bad;
        }
        v = v * 10UL + d;
    }
    if (v == 0UL) {
        goto bad;
    }
    if (id) {
        *id = v;
    }
    return NU_OK;

bad:
    set_reason(reason, reason_cap, "bad reply");
    return NU_ERR;
}
