/* palm/apps/scribble/proto.c - see proto.h for the contract. Portable C89,
 * no libc. */
#include "proto.h"

#define MAX_ULONG 0xFFFFFFFFUL

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Parses an all-digit field (non-empty, fits 32 bits). Returns 1 on success. */
static int parse_num(const char *s, unsigned long *v)
{
    unsigned long r = 0, d;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        d = (unsigned long)(*s - '0');
        if (r > (MAX_ULONG - d) / 10UL) return 0;
        r = r * 10UL + d;
    }
    *v = r;
    return 1;
}

/* Splits off the next line of *p: returns its start, NUL terminates it
 * (dropping a trailing CR) and advances *p. Returns NULL at end of text. */
static char *next_line(char **p)
{
    char *s = *p, *e;
    if (!s || !*s) return 0;
    for (e = s; *e && *e != '\n'; e++) {}
    if (*e) { *e = 0; *p = e + 1; } else *p = e;
    if (e > s && e[-1] == '\r') e[-1] = 0;
    return s;
}

/* Splits line in place on '|' into at most n fields; returns the number of
 * fields found (n+1 if there were more than n). */
static int split_bar(char *line, char **f, int n)
{
    int c = 0;
    for (;;) {
        if (c < n) f[c] = line;
        c++;
        while (*line && *line != '|') line++;
        if (!*line) break;
        *line++ = 0;
    }
    return c;
}

int proto_status(const char *text, const char **rest, char *reason, int reason_cap)
{
    int i;
    if (text && text[0] == 'o' && text[1] == 'k') {
        if (text[2] == '\n') { if (rest) *rest = text + 3; return 1; }
        if (text[2] == '\r' && text[3] == '\n') { if (rest) *rest = text + 4; return 1; }
        if (text[2] == 0) { if (rest) *rest = text + 2; return 1; }
    }
    if (reason && reason_cap > 0) {
        const char *src = "bad reply";
        if (text && text[0] == 'e' && text[1] == 'r' && text[2] == 'r' && text[3] == ' ')
            src = text + 4;
        for (i = 0; i < reason_cap - 1 && src[i] && src[i] != '\n' && src[i] != '\r'; i++)
            reason[i] = src[i];
        reason[i] = 0;
    }
    return 0;
}

int proto_parse_inbox(char *text, InboxRow *rows, int max)
{
    int n = 0, i, len;
    char *line, *f[4];
    unsigned long id, created;
    while (n < max && (line = next_line(&text)) != 0) {
        if (split_bar(line, f, 4) != 4) continue;
        if (!parse_num(f[0], &id) || !parse_num(f[1], &created)) continue;
        len = slen(f[2]);
        if (len < 1 || len > 16) continue;
        if (f[3][0] < '0' || f[3][0] > '1' || f[3][1]) continue;
        rows[n].id = id;
        rows[n].created = created;
        for (i = 0; i <= len; i++) rows[n].sender[i] = f[2][i];
        rows[n].unread = f[3][0] - '0';
        n++;
    }
    return n;
}

int proto_parse_sent(char *text, SentRow *rows, int max)
{
    int n = 0, i, len;
    char *line, *f[3];
    unsigned long id, created;
    while (n < max && (line = next_line(&text)) != 0) {
        if (split_bar(line, f, 3) != 3) continue;
        if (!parse_num(f[0], &id) || !parse_num(f[1], &created)) continue;
        len = slen(f[2]);
        if (len < 1) continue;
        if (len > (int)sizeof(rows[n].to) - 1) {
            /* garbage input only: cut at the last comma that fits */
            len = (int)sizeof(rows[n].to) - 1;
            for (i = len; i > 0 && f[2][i] != ','; i--) {}
            if (i > 0) len = i;
        }
        rows[n].id = id;
        rows[n].created = created;
        for (i = 0; i < len; i++) rows[n].to[i] = f[2][i];
        rows[n].to[len] = 0;
        n++;
    }
    return n;
}

int proto_parse_handles(char *text, char (*out)[17], int max)
{
    int n = 0, i;
    char *line;
    while (n < max && (line = next_line(&text)) != 0) {
        for (i = 0; line[i]; i++) {
            char c = line[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) break;
        }
        if (line[i] || i < 1 || i > 16) continue;
        for (i = 0; line[i]; i++) out[n][i] = line[i];
        out[n][i] = 0;
        n++;
    }
    return n;
}

static int has_nl(const char *s)
{
    for (; *s; s++) if (*s == '\n') return 1;
    return 0;
}

int proto_build_login(char *out, int cap, const char *user, const char *pass)
{
    int ul, pl, i, k = 0;
    if (!out || !user || !pass || !user[0] || !pass[0]) return -1;
    if (has_nl(user) || has_nl(pass)) return -1;
    ul = slen(user);
    pl = slen(pass);
    if (ul + pl + 2 > cap) return -1;
    for (i = 0; i < ul; i++) out[k++] = user[i];
    out[k++] = '\n';
    for (i = 0; i < pl; i++) out[k++] = pass[i];
    out[k++] = '\n';
    if (k < cap) out[k] = 0;
    return k;
}

int proto_build_send(sk_u8 *out, int cap, const char *handles_csv, const sk_u8 *bitmap)
{
    int hl, i, k = 0;
    if (!out || !bitmap || !handles_csv || !handles_csv[0]) return -1;
    hl = slen(handles_csv);
    if (hl > 400 || has_nl(handles_csv)) return -1;
    if (hl + 1 + SK_BYTES > cap) return -1;
    for (i = 0; i < hl; i++) out[k++] = (sk_u8)handles_csv[i];
    out[k++] = '\n';
    for (i = 0; i < SK_BYTES; i++) out[k++] = bitmap[i];
    return k;
}

static void put2(char *o, long v) { o[0] = (char)('0' + v / 10); o[1] = (char)('0' + v % 10); }

void proto_format_time(unsigned long unix_secs, char *out)
{
    long days = (long)(unix_secs / 86400UL);
    long sod = (long)(unix_secs % 86400UL);
    long z = days + 719468L;
    long era = z / 146097L;            /* z >= 0 so plain division is fine */
    long doe = z - era * 146097L;
    long yoe = (doe - doe / 1460L + doe / 36524L - doe / 146096L) / 365L;
    long y = yoe + era * 400L;
    long doy = doe - (365L * yoe + yoe / 4L - yoe / 100L);
    long mp = (5L * doy + 2L) / 153L;
    long d = doy - (153L * mp + 2L) / 5L + 1L;
    long m = mp < 10 ? mp + 3L : mp - 9L;
    if (m <= 2) y++;
    out[0] = (char)('0' + (y / 1000L) % 10);
    out[1] = (char)('0' + (y / 100L) % 10);
    out[2] = (char)('0' + (y / 10L) % 10);
    out[3] = (char)('0' + y % 10);
    out[4] = '-';
    put2(out + 5, m);
    out[7] = '-';
    put2(out + 8, d);
    out[10] = ' ';
    put2(out + 11, sod / 3600L);
    out[13] = ':';
    put2(out + 14, (sod % 3600L) / 60L);
    out[16] = 0;
}
