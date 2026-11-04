/* palm/apps/scribble/selection.h
 *
 * The recipient selection for Scribble's Compose / Picker forms: up to
 * SEL_MAX handles, toggled from the directory list, joined into the
 * comma separated list that POST /palm/v1/send takes (docs/scribble.md),
 * plus the directory prefix check and a "drop my own handle" row filter.
 *
 * Portable ANSI C89 with no platform headers and no libc, like proto.c,
 * so it builds on the host (test_selection.c) and with m68k-palmos-gcc
 * (16-bit int). Handles are 1..16 chars of [a-z0-9_]; nothing else is
 * ever stored, so a joined list can go straight into a request.
 */
#ifndef SELECTION_H
#define SELECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#define SEL_MAX 10                 /* server limit: at most 10 recipients */
#define SEL_HCAP 17                /* 16 chars + NUL */
#define SEL_CSV_CAP (SEL_MAX * SEL_HCAP) /* 10*16 + 9 commas + NUL = 170 */

/* sel_add / sel_toggle results */
#define SEL_ADDED 1
#define SEL_REMOVED 0
#define SEL_PRESENT 2   /* sel_add: already selected, unchanged */
#define SEL_FULL (-1)   /* already SEL_MAX selected, unchanged */
#define SEL_BAD (-2)    /* not a valid handle, unchanged */

typedef struct {
    char h[SEL_MAX][SEL_HCAP]; /* h[0..n-1], in the order they were added */
    int n;
} Selection;

/* 1 if h is 1..16 chars of [a-z0-9_], else 0 (NULL gives 0). */
int sel_handle_ok(const char *h);

void sel_clear(Selection *s);
int sel_count(const Selection *s);
int sel_contains(const Selection *s, const char *h);

/* Adds h: SEL_ADDED, SEL_PRESENT, SEL_FULL or SEL_BAD. */
int sel_add(Selection *s, const char *h);

/* Removes h if selected (SEL_REMOVED, order of the rest kept), else adds
 * it (SEL_ADDED, SEL_FULL or SEL_BAD). */
int sel_toggle(Selection *s, const char *h);

/* Writes the handles joined by sep (e.g. "," for the request, ", " for
 * display) plus a NUL; returns the length. Returns -1 (writing "" when
 * out and cap > 0 allow) if it does not fit in cap bytes. */
int sel_join(const Selection *s, const char *sep, char *out, int cap);

/* Normalizes a typed directory prefix: whitespace and control characters
 * are dropped, A-Z lower-cased, a-z 0-9 _ kept. Returns the length, or -1
 * (out = "") if any other character is present, the result is longer than
 * 16 chars, or it does not fit in cap bytes. NULL in reads as "". */
int sel_prefix(const char *in, char *out, int cap);

/* Removes every row equal to h from rows[0..n-1] (order kept) and returns
 * the new count. An empty or NULL h removes nothing. */
int sel_drop(char (*rows)[SEL_HCAP], int n, const char *h);

#ifdef __cplusplus
}
#endif

#endif
