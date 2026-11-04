/* palm/apps/scribble/selection.c -- see selection.h. No libc. */
#include "selection.h"

static int hchar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static int seq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int slen(const char *s)
{
    int n = 0;

    while (s[n]) n++;
    return n;
}

int sel_handle_ok(const char *h)
{
    int i;

    if (!h) return 0;
    for (i = 0; h[i]; i++) {
        if (i >= SEL_HCAP - 1 || !hchar(h[i])) return 0;
    }
    return i >= 1;
}

void sel_clear(Selection *s)
{
    int i, j;

    for (i = 0; i < SEL_MAX; i++)
        for (j = 0; j < SEL_HCAP; j++) s->h[i][j] = 0;
    s->n = 0;
}

int sel_count(const Selection *s)
{
    return s->n;
}

static int find(const Selection *s, const char *h)
{
    int i;

    if (!h) return -1;
    for (i = 0; i < s->n; i++)
        if (seq(s->h[i], h)) return i;
    return -1;
}

int sel_contains(const Selection *s, const char *h)
{
    return find(s, h) >= 0;
}

int sel_add(Selection *s, const char *h)
{
    int i;

    if (!sel_handle_ok(h)) return SEL_BAD;
    if (find(s, h) >= 0) return SEL_PRESENT;
    if (s->n >= SEL_MAX) return SEL_FULL;
    for (i = 0; h[i]; i++) s->h[s->n][i] = h[i];
    s->h[s->n][i] = 0;
    s->n++;
    return SEL_ADDED;
}

int sel_toggle(Selection *s, const char *h)
{
    int k = find(s, h), i, j;

    if (k < 0) return sel_add(s, h);
    for (i = k; i + 1 < s->n; i++)
        for (j = 0; j < SEL_HCAP; j++) s->h[i][j] = s->h[i + 1][j];
    s->n--;
    for (j = 0; j < SEL_HCAP; j++) s->h[s->n][j] = 0;
    return SEL_REMOVED;
}

int sel_join(const Selection *s, const char *sep, char *out, int cap)
{
    int i, j, k = 0;
    int sl = sep ? slen(sep) : 0;

    if (!out || cap <= 0) return -1;
    for (i = 0; i < s->n; i++) {
        if (i > 0) {
            if (k + sl >= cap) goto toolong;
            for (j = 0; j < sl; j++) out[k++] = sep[j];
        }
        for (j = 0; s->h[i][j]; j++) {
            if (k + 1 >= cap) goto toolong;
            out[k++] = s->h[i][j];
        }
    }
    out[k] = 0;
    return k;
toolong:
    out[0] = 0;
    return -1;
}

int sel_prefix(const char *in, char *out, int cap)
{
    int k = 0;
    const char *p = in ? in : "";

    if (!out || cap <= 0) return -1;
    for (; *p; p++) {
        char c = *p;

        if ((unsigned char)c <= ' ') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (!hchar(c) || k >= SEL_HCAP - 1 || k + 1 >= cap) {
            out[0] = 0;
            return -1;
        }
        out[k++] = c;
    }
    out[k] = 0;
    return k;
}

int sel_drop(char (*rows)[SEL_HCAP], int n, const char *h)
{
    int i, j, k = 0;

    if (!h || !h[0]) return n;
    for (i = 0; i < n; i++) {
        if (seq(rows[i], h)) continue;
        if (k != i)
            for (j = 0; j < SEL_HCAP; j++) rows[k][j] = rows[i][j];
        k++;
    }
    return k;
}
