/* palm/apps/scribble/proto.h
 *
 * Wire-protocol helpers for the Scribble Palm client (see docs/scribble.md,
 * "Palm protocol"): reply status parsing, inbox/sent/directory list
 * parsing, request body builders and a UTC time formatter.
 *
 * Portable ANSI C89, no platform headers and no libc (no string.h, stdio.h
 * or stdlib.h), so it builds on the host for tests and with the m68k Palm
 * compiler. On the Palm target int is 16 bits, so ids and timestamps are
 * unsigned long. Wire numbers are parsed overflow-safely: a number that does
 * not fit in 32 bits makes its line malformed (the line is skipped).
 * No floating point.
 *
 * Parsers MUTATE the caller's buffer (they write NULs over '\n', '\r' and
 * '|'; commas are left alone), so callers must pass a writable buffer.
 * proto_status only reads, and its const char *rest points into that buffer.
 * They tolerate "\r\n" line endings and a missing final newline, skip
 * malformed lines (wrong field count, non-numeric or overflowing numbers,
 * empty or over-long fields), stop after max entries and return the count.
 */
#ifndef PROTO_H
#define PROTO_H

#include "sketch.h" /* sk_u8 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned long id, created; /* note id; send time, Unix seconds */
    char sender[20];           /* handle, 1..16 chars */
    int unread;                /* 1 or 0 */
} InboxRow;

typedef struct {
    unsigned long id, created;
    char to[176];              /* comma separated recipient handles */
} SentRow;

/*
 * Classifies a reply. Returns 1 and sets *rest (if non-NULL) to the text
 * after the first line when that line is exactly "ok" (terminated by '\n',
 * "\r\n" or the end of text). Returns 0 with reason (if non-NULL and
 * reason_cap > 0) set to the text after "err " up to the end of that line
 * (CR/LF excluded), truncated to reason_cap-1 chars and NUL terminated, for
 * an "err " line. Returns 0 with reason "bad reply" for anything else
 * ("okay", "ok ", "err" with no space, empty or NULL text). *rest is
 * untouched on a 0 return.
 */
int proto_status(const char *text, const char **rest, char *reason, int reason_cap);

/* Lines "id|unix|sender|unread"; unread must be 0 or 1; sender 1..16 chars
 * with no '|'. Returns rows stored (<= max). */
int proto_parse_inbox(char *text, InboxRow *rows, int max);

/* Lines "id|unix|h1,h2,..."; recipients field must be non-empty. The server
 * allows 10 handles of 16 chars (169 chars), which always fits. A row is
 * never dropped for a long list: if the field exceeds 175 chars (garbage
 * only) it is cut at the last comma that fits, so no half handle shows.
 * Returns rows stored (<= max). */
int proto_parse_sent(char *text, SentRow *rows, int max);

/* One handle per line; accepts 1..16 chars of [a-z0-9_], skips other lines.
 * Each is copied NUL terminated into out[i]. Returns count (<= max). */
int proto_parse_handles(char *text, char (*out)[17], int max);

/*
 * Writes "user\npass\n" to out and returns its length (excluding the NUL,
 * which is added when there is room). Returns -1 if it does not fit in cap
 * bytes, or if user or pass is NULL, empty or contains '\n'.
 */
int proto_build_login(char *out, int cap, const char *user, const char *pass);

/*
 * Writes handles_csv, '\n', then the SK_BYTES bitmap bytes (copied verbatim,
 * 0x00 and 0x0A included) and returns the total length strlen+1+2400
 * (no terminator). Returns -1 if cap is too small, bitmap is NULL, or
 * handles_csv is NULL, empty, longer than 400 chars or contains '\n'.
 */
int proto_build_send(sk_u8 *out, int cap, const char *handles_csv, const sk_u8 *bitmap);

/* Writes "YYYY-MM-DD HH:MM" (UTC) plus a NUL; out holds at least 17 bytes.
 * Integer math only; valid for 1970 through at least 2100. */
void proto_format_time(unsigned long unix_secs, char *out);

#ifdef __cplusplus
}
#endif

#endif
