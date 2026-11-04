/* Host test for the Scribble compose canvas helpers. cc -std=c89 -Wall -Wextra */
#include <stdio.h>
#include <string.h>
#include "canvas.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

#define SEG(g, a, b, c, d) ((g).x0 == (a) && (g).y0 == (b) && (g).x1 == (c) && (g).y1 == (d))

static void test_inside(void)
{
    CHECK(CV_TOP == 16);
    CHECK(cv_inside(0, 16));
    CHECK(cv_inside(159, 135));
    CHECK(cv_inside(80, 70));
    CHECK(!cv_inside(0, 15));
    CHECK(!cv_inside(0, 136));
    CHECK(!cv_inside(-1, 20));
    CHECK(!cv_inside(160, 20));
    CHECK(!cv_inside(10, 150));   /* button row */
}

static void test_stroke(void)
{
    CvPen p;
    CvSeg g;

    cv_pen_reset(&p);
    CHECK(!p.active);
    /* a tap: one dot in canvas coordinates */
    CHECK(cv_pen_down(&p, 10, 26, &g) == 1);
    CHECK(p.active);
    CHECK(SEG(g, 10, 10, 10, 10));
    /* move: segment from the previous point */
    CHECK(cv_pen_move(&p, 20, 36, &g) == 1);
    CHECK(SEG(g, 10, 10, 20, 20));
    /* no motion: nothing new to draw */
    CHECK(cv_pen_move(&p, 20, 36, &g) == 0);
    CHECK(cv_pen_move(&p, 25, 36, &g) == 1);
    CHECK(SEG(g, 20, 20, 25, 20));
    /* pen up at a new point draws the last bit, then ends the stroke */
    CHECK(cv_pen_up(&p, 26, 37, &g) == 1);
    CHECK(SEG(g, 25, 20, 26, 21));
    CHECK(!p.active);
    CHECK(cv_pen_move(&p, 30, 40, &g) == 0);
    CHECK(cv_pen_up(&p, 30, 40, &g) == 0);
    /* pen up where the last move was: nothing, but still ends */
    CHECK(cv_pen_down(&p, 5, 20, &g) == 1);
    CHECK(cv_pen_up(&p, 5, 20, &g) == 0);
    CHECK(!p.active);
}

static void test_outside(void)
{
    CvPen p;
    CvSeg g;

    cv_pen_reset(&p);
    /* pen down outside the canvas (header, buttons): not ours */
    CHECK(cv_pen_down(&p, 10, 5, &g) == 0);
    CHECK(!p.active);
    CHECK(cv_pen_move(&p, 10, 30, &g) == 0);
    CHECK(cv_pen_up(&p, 10, 30, &g) == 0);
    CHECK(cv_pen_down(&p, 10, 150, &g) == 0);
    CHECK(cv_pen_down(&p, 10, 136, &g) == 0);
    CHECK(!p.active);

    /* leave and re-enter: segments run through the outside point (the
     * caller clips), so the line resumes where the pen comes back in */
    CHECK(cv_pen_down(&p, 50, 130, &g) == 1);
    CHECK(cv_pen_move(&p, 50, 145, &g) == 1);
    CHECK(SEG(g, 50, 114, 50, 129));         /* y 129 is off the canvas */
    CHECK(cv_pen_move(&p, 60, 150, &g) == 1);
    CHECK(SEG(g, 50, 129, 60, 134));         /* wholly outside: clips away */
    CHECK(cv_pen_move(&p, 70, 120, &g) == 1);
    CHECK(SEG(g, 60, 134, 70, 104));         /* back in */
    CHECK(p.active);
    CHECK(cv_pen_up(&p, 70, 120, &g) == 0);
}

static void test_draw_into_sketch(void)
{
    static Sketch s;
    CvPen p;
    CvSeg g;

    sk_clear(&s);
    CHECK(cv_blank(s.cur));
    cv_pen_reset(&p);
    CHECK(cv_pen_down(&p, 0, 16, &g) == 1);
    cv_apply(&s, &g);
    CHECK(sk_get(s.cur, 0, 0) == 1);
    CHECK(!cv_blank(s.cur));
    CHECK(cv_pen_move(&p, 159, 135, &g) == 1);
    cv_apply(&s, &g);
    CHECK(sk_get(s.cur, 159, 119) == 1);
    /* a segment far outside writes nothing out of range */
    g.x0 = -500; g.y0 = -500; g.x1 = 500; g.y1 = -400;
    cv_apply(&s, &g);
    CHECK(sk_get(s.cur, 0, 0) == 1);
    sk_clear(&s);
    s.cur[SK_BYTES - 1] = 1;
    CHECK(!cv_blank(s.cur));
    s.cur[SK_BYTES - 1] = 0;
    s.cur[0] = 0x80;
    CHECK(!cv_blank(s.cur));
}

int main(void)
{
    test_inside();
    test_stroke();
    test_outside();
    test_draw_into_sketch();
    printf("test_canvas: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
