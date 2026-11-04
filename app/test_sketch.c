/* Host test for the Scribble sketch buffer. cc -std=c89 -Wall -Wextra */
#include <stdio.h>
#include <string.h>
#include "sketch.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

/* Sketch surrounded by canaries to detect out-of-bounds writes. */
typedef struct { sk_u8 pre[64]; Sketch s; sk_u8 post[64]; } Guarded;
static Guarded g;

static void gclear(void)
{
    memset(&g, 0xA5, sizeof g);
    memset(g.s.cur, 0, SK_BYTES);
    memset(g.s.undo, 0, SK_BYTES);
    g.s.can_undo = 0;
}
static int canaries_ok(void)
{
    int i;
    for (i = 0; i < 64; i++)
        if (g.pre[i] != 0xA5 || g.post[i] != 0xA5) return 0;
    return 1;
}
static int count_black(const sk_u8 *b)
{
    int x, y, n = 0;
    for (y = 0; y < SK_H; y++)
        for (x = 0; x < SK_W; x++) n += sk_get(b, x, y);
    return n;
}

static void test_bits(void)
{
    gclear();
    sk_set(g.s.cur, 0, 0, 1);
    CHECK(g.s.cur[0] == 0x80);
    sk_set(g.s.cur, 7, 0, 1);
    CHECK(g.s.cur[0] == 0x81);
    sk_set(g.s.cur, 8, 0, 1);
    CHECK(g.s.cur[1] == 0x80);
    sk_set(g.s.cur, 159, 119, 1);
    CHECK(g.s.cur[2399] == 0x01);
    CHECK(sk_get(g.s.cur, 159, 119) == 1);
    CHECK(sk_get(g.s.cur, 0, 0) == 1);
    CHECK(sk_get(g.s.cur, 1, 0) == 0);
    sk_set(g.s.cur, 0, 0, 0);
    CHECK(g.s.cur[0] == 0x01);
    CHECK(canaries_ok());
}

static void test_oob(void)
{
    sk_u8 ref[SK_BYTES];
    gclear();
    sk_set(g.s.cur, 5, 5, 1);
    memcpy(ref, g.s.cur, SK_BYTES);
    sk_set(g.s.cur, -1, 0, 1);
    sk_set(g.s.cur, 160, 0, 1);
    sk_set(g.s.cur, 0, -1, 1);
    sk_set(g.s.cur, 0, 120, 1);
    sk_set(g.s.cur, 5, 5, 1);
    sk_set(g.s.cur, 1000, 1000, 1);
    sk_set(g.s.cur, -1000, -1000, 1);
    CHECK(memcmp(ref, g.s.cur, SK_BYTES) == 0);
    CHECK(sk_get(g.s.cur, -1, 0) == 0);
    CHECK(sk_get(g.s.cur, 160, 0) == 0);
    CHECK(sk_get(g.s.cur, 0, -1) == 0);
    CHECK(sk_get(g.s.cur, 0, 120) == 0);
    CHECK(canaries_ok());
}

static void test_lines(void)
{
    int st[8], ln[8], n, i, y;
    gclear();
    sk_line(g.s.cur, 0, 5, 9, 5, 1);
    n = sk_runs(g.s.cur, 5, st, ln, 8);
    CHECK(n == 1 && st[0] == 0 && ln[0] == 10);
    CHECK(count_black(g.s.cur) == 10);

    gclear();
    sk_line(g.s.cur, 30, 10, 30, 19, 1);
    CHECK(count_black(g.s.cur) == 10);
    for (y = 10; y <= 19; y++) {
        n = sk_runs(g.s.cur, y, st, ln, 8);
        CHECK(n == 1 && st[0] == 30 && ln[0] == 1);
    }
    CHECK(sk_runs(g.s.cur, 9, st, ln, 8) == 0);

    gclear();
    sk_line(g.s.cur, 3, 4, 53, 34, 1);
    CHECK(sk_get(g.s.cur, 3, 4) == 1);
    CHECK(sk_get(g.s.cur, 53, 34) == 1);
    CHECK(count_black(g.s.cur) == 51); /* max(50,30)+1 */

    gclear();
    sk_line(g.s.cur, 10, 10, 20, 20, 1);
    CHECK(count_black(g.s.cur) == 11);
    for (i = 10; i <= 20; i++) CHECK(sk_get(g.s.cur, i, i) == 1);

    {
        sk_u8 a[SK_BYTES];
        gclear();
        sk_line(g.s.cur, 3, 4, 53, 34, 1);
        memcpy(a, g.s.cur, SK_BYTES);
        gclear();
        sk_line(g.s.cur, 53, 34, 3, 4, 1);
        CHECK(memcmp(a, g.s.cur, SK_BYTES) == 0);
    }

    /* erase */
    gclear();
    sk_line(g.s.cur, 0, 0, 20, 0, 1);
    sk_line(g.s.cur, 5, 0, 9, 0, 0);
    n = sk_runs(g.s.cur, 0, st, ln, 8);
    CHECK(n == 2 && st[0] == 0 && ln[0] == 5 && st[1] == 10 && ln[1] == 11);
    CHECK(canaries_ok());
}

static void test_clip(void)
{
    int st[8], ln[8], n;
    gclear();
    sk_line(g.s.cur, -50, -50, 300, 200, 1);
    CHECK(canaries_ok());
    CHECK(sk_get(g.s.cur, 100, 57) == 1);    /* y = -50 + 5(x+50)/7 */
    CHECK(count_black(g.s.cur) > 100);

    gclear();
    sk_line(g.s.cur, -1000, 60, 1000, 60, 1);
    CHECK(canaries_ok());
    n = sk_runs(g.s.cur, 60, st, ln, 8);
    CHECK(n == 1 && st[0] == 0 && ln[0] == 160);
    CHECK(count_black(g.s.cur) == 160);

    gclear();
    sk_line(g.s.cur, -400, -300, -200, -100, 1);
    sk_line(g.s.cur, 500, 500, 900, 700, 1);
    sk_line(g.s.cur, -300, 900, 900, -900, 1);
    CHECK(canaries_ok());

    gclear();
    sk_line(g.s.cur, 155, 60, 400, 60, 1);
    n = sk_runs(g.s.cur, 60, st, ln, 8);
    CHECK(n == 1 && st[0] == 155 && ln[0] == 5);
    CHECK(canaries_ok());
}

static void test_undo(void)
{
    sk_u8 before[SK_BYTES];
    gclear();
    sk_line(g.s.cur, 0, 0, 50, 50, 1);
    memcpy(before, g.s.cur, SK_BYTES);
    CHECK(sk_undo(&g.s) == 0);
    sk_begin_stroke(&g.s);
    CHECK(g.s.can_undo == 1);
    sk_line(g.s.cur, 0, 100, 150, 20, 1);
    CHECK(memcmp(before, g.s.cur, SK_BYTES) != 0);
    CHECK(sk_undo(&g.s) == 1);
    CHECK(memcmp(before, g.s.cur, SK_BYTES) == 0);
    CHECK(sk_undo(&g.s) == 0);
    CHECK(memcmp(before, g.s.cur, SK_BYTES) == 0);
    CHECK(canaries_ok());
}

static void test_clear(void)
{
    int i, ok = 1;
    gclear();
    memset(g.s.cur, 0xFF, SK_BYTES);
    sk_begin_stroke(&g.s);
    sk_clear(&g.s);
    for (i = 0; i < SK_BYTES; i++) if (g.s.cur[i] != 0) ok = 0;
    CHECK(ok);
    /* documented: clear drops undo state */
    CHECK(g.s.can_undo == 0);
    CHECK(sk_undo(&g.s) == 0);
    for (i = 0; i < SK_BYTES; i++) if (g.s.cur[i] != 0) ok = 0;
    CHECK(ok);
    CHECK(canaries_ok());
}

static void test_runs(void)
{
    int st[8], ln[8], n, x;
    gclear();
    sk_line(g.s.cur, 0, 3, 2, 3, 1);       /* touches x=0 */
    sk_line(g.s.cur, 10, 3, 10, 3, 1);
    sk_line(g.s.cur, 14, 3, 20, 3, 1);     /* spans a byte boundary */
    sk_line(g.s.cur, 155, 3, 159, 3, 1);   /* ends at x=159 */
    n = sk_runs(g.s.cur, 3, st, ln, 8);
    CHECK(n == 4);
    CHECK(st[0] == 0 && ln[0] == 3);
    CHECK(st[1] == 10 && ln[1] == 1);
    CHECK(st[2] == 14 && ln[2] == 7);
    CHECK(st[3] == 155 && ln[3] == 5);
    /* truncation */
    n = sk_runs(g.s.cur, 3, st, ln, 2);
    CHECK(n == 2 && st[1] == 10 && ln[1] == 1);
    CHECK(sk_runs(g.s.cur, 3, st, ln, 0) == 0);
    /* empty and full */
    CHECK(sk_runs(g.s.cur, 4, st, ln, 8) == 0);
    for (x = 0; x < SK_W; x++) sk_set(g.s.cur, x, 7, 1);
    n = sk_runs(g.s.cur, 7, st, ln, 8);
    CHECK(n == 1 && st[0] == 0 && ln[0] == 160);
    /* y out of range */
    CHECK(sk_runs(g.s.cur, -1, st, ln, 8) == 0);
    CHECK(sk_runs(g.s.cur, 120, st, ln, 8) == 0);
    CHECK(canaries_ok());
}

int main(void)
{
    test_bits();
    test_oob();
    test_lines();
    test_clip();
    test_undo();
    test_clear();
    test_runs();
    printf("test_sketch: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
