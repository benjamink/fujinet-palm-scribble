/* palm/apps/scribble/sketch.c -- see sketch.h for the contract. */
#include "sketch.h"

void sk_clear(Sketch *s)
{
    int i;
    for (i = 0; i < SK_BYTES; i++) {
        s->cur[i] = 0;
        s->undo[i] = 0;
    }
    s->can_undo = 0;
}

void sk_set(sk_u8 *buf, int x, int y, int black)
{
    if (x < 0 || x >= SK_W || y < 0 || y >= SK_H) return;
    if (black)
        buf[y * SK_ROW + (x >> 3)] |= (sk_u8)(0x80 >> (x & 7));
    else
        buf[y * SK_ROW + (x >> 3)] &= (sk_u8)~(0x80 >> (x & 7));
}

int sk_get(const sk_u8 *buf, int x, int y)
{
    if (x < 0 || x >= SK_W || y < 0 || y >= SK_H) return 0;
    return (buf[y * SK_ROW + (x >> 3)] & (0x80 >> (x & 7))) ? 1 : 0;
}

void sk_line(sk_u8 *buf, int x0, int y0, int x1, int y1, int black)
{
    long dx, dy, sy, err, e2, x, y;
    int t;

    /* Trivial reject: both endpoints beyond the same edge. */
    if ((x0 < 0 && x1 < 0) || (x0 >= SK_W && x1 >= SK_W) ||
        (y0 < 0 && y1 < 0) || (y0 >= SK_H && y1 >= SK_H))
        return;

    /* Canonical direction so the pixel set is order independent. */
    if (x1 < x0 || (x1 == x0 && y1 < y0)) {
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }

    dx = (long)x1 - x0;
    dy = (long)y1 - y0;
    sy = 1;
    if (dy < 0) { dy = -dy; sy = -1; }
    err = dx - dy;
    x = x0;
    y = y0;
    for (;;) {
        if (x >= 0 && x < SK_W && y >= 0 && y < SK_H)
            sk_set(buf, (int)x, (int)y, black);
        if (x == x1 && y == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x++; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

void sk_begin_stroke(Sketch *s)
{
    int i;
    for (i = 0; i < SK_BYTES; i++) s->undo[i] = s->cur[i];
    s->can_undo = 1;
}

int sk_undo(Sketch *s)
{
    int i;
    if (!s->can_undo) return 0;
    for (i = 0; i < SK_BYTES; i++) s->cur[i] = s->undo[i];
    s->can_undo = 0;
    return 1;
}

int sk_runs(const sk_u8 *buf, int y, int *starts, int *lens, int max)
{
    int x, n = 0, start = -1;

    if (y < 0 || y >= SK_H || max <= 0) return 0;
    for (x = 0; x <= SK_W; x++) {
        int on = (x < SK_W) && sk_get(buf, x, y);
        if (on) {
            if (start < 0) start = x;
        } else if (start >= 0) {
            starts[n] = start;
            lens[n] = x - start;
            n++;
            start = -1;
            if (n >= max) break;
        }
    }
    return n;
}
