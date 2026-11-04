/* palm/apps/scribble/canvas.h
 *
 * Pen tracking for Scribble's Compose canvas: the 160x120 sketch sits on
 * screen at x 0..159, y CV_TOP..CV_TOP+119. Screen points become canvas
 * points (y - CV_TOP); a stroke only starts on a pen-down inside the
 * canvas (anything else is left to the form, e.g. buttons).
 *
 * Leaving the canvas mid-stroke: the stroke stays active and keeps the
 * last point even when it is outside, and every move yields the segment
 * from the previous point to the new one. The caller draws it clipped to
 * the canvas (sk_line clips the buffer; the screen draw uses a clip
 * rectangle), so the visible line runs to the edge where the pen left and
 * picks up at the edge where it comes back, as one continuous stroke.
 *
 * Portable ANSI C89 with no platform headers (host test: test_canvas.c).
 */
#ifndef CANVAS_H
#define CANVAS_H

#include "sketch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CV_TOP 16 /* screen y of canvas row 0 */

typedef struct {
    int active;  /* a stroke is in progress */
    int lx, ly;  /* last point, canvas coordinates (may be outside) */
} CvPen;

typedef struct {
    int x0, y0, x1, y1; /* canvas coordinates, may be outside the canvas */
} CvSeg;

/* 1 if screen point (sx,sy) is on the canvas. */
int cv_inside(int sx, int sy);

void cv_pen_reset(CvPen *p);

/* Pen down at screen (sx,sy). Inside the canvas: starts a stroke, sets
 * *seg to a one-pixel dot there and returns 1 (the caller then calls
 * sk_begin_stroke and draws seg). Outside: returns 0, no stroke. */
int cv_pen_down(CvPen *p, int sx, int sy, CvSeg *seg);

/* Pen moved to screen (sx,sy). Returns 1 with *seg from the previous
 * point to this one when a stroke is active and the point changed. */
int cv_pen_move(CvPen *p, int sx, int sy, CvSeg *seg);

/* Pen up at (sx,sy): as cv_pen_move, then ends the stroke. */
int cv_pen_up(CvPen *p, int sx, int sy, CvSeg *seg);

/* Draws seg in black into s->cur (clipped by sk_line). */
void cv_apply(Sketch *s, const CvSeg *seg);

/* 1 if every pixel of the SK_BYTES bitmap is white. */
int cv_blank(const sk_u8 *bits);

#ifdef __cplusplus
}
#endif

#endif
