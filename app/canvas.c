/* palm/apps/scribble/canvas.c -- see canvas.h. No libc. */
#include "canvas.h"

int cv_inside(int sx, int sy)
{
    return sx >= 0 && sx < SK_W && sy >= CV_TOP && sy < CV_TOP + SK_H;
}

void cv_pen_reset(CvPen *p)
{
    p->active = 0;
    p->lx = 0;
    p->ly = 0;
}

int cv_pen_down(CvPen *p, int sx, int sy, CvSeg *seg)
{
    if (!cv_inside(sx, sy)) {
        p->active = 0;
        return 0;
    }
    p->active = 1;
    p->lx = sx;
    p->ly = sy - CV_TOP;
    seg->x0 = seg->x1 = p->lx;
    seg->y0 = seg->y1 = p->ly;
    return 1;
}

int cv_pen_move(CvPen *p, int sx, int sy, CvSeg *seg)
{
    int x = sx, y = sy - CV_TOP;

    if (!p->active || (x == p->lx && y == p->ly)) return 0;
    seg->x0 = p->lx;
    seg->y0 = p->ly;
    seg->x1 = x;
    seg->y1 = y;
    p->lx = x;
    p->ly = y;
    return 1;
}

int cv_pen_up(CvPen *p, int sx, int sy, CvSeg *seg)
{
    int r = cv_pen_move(p, sx, sy, seg);

    p->active = 0;
    return r;
}

void cv_apply(Sketch *s, const CvSeg *seg)
{
    sk_line(s->cur, seg->x0, seg->y0, seg->x1, seg->y1, 1);
}

int cv_blank(const sk_u8 *bits)
{
    int i;

    for (i = 0; i < SK_BYTES; i++)
        if (bits[i]) return 0;
    return 1;
}
