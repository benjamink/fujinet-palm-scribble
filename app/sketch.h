/* palm/apps/scribble/sketch.h
 *
 * 160x120 1-bit sketch buffer for Scribble: pixel set/get, clipped line
 * drawing, one-level undo, and black-run extraction per row.
 *
 * Deliberately portable ANSI C89 with NO platform headers (no PalmOS.h, no
 * libc string.h even) so it can be compiled and unit tested on any host as
 * well as with the m68k-palmos cross compiler. See test_sketch.c.
 *
 * Bitmap layout (identical to what the server stores and to Palm's native
 * 1-bit bitmaps): SK_W x SK_H pixels, 1 bit per pixel, SK_ROW (20) bytes
 * per row, SK_BYTES (2400) bytes total, MSB first, 1 = black. Pixel (x,y)
 * is bit (0x80 >> (x & 7)) of byte (y * SK_ROW + (x >> 3)).
 *
 * Coordinates are plain ints; anything outside 0..SK_W-1 / 0..SK_H-1 is
 * safely ignored (sets) or reads as white (gets). Line arithmetic uses
 * long, so it is safe with a 16-bit int.
 */
#ifndef SKETCH_H
#define SKETCH_H

#ifdef __cplusplus
extern "C" {
#endif

#define SK_W 160
#define SK_H 120
#define SK_ROW 20
#define SK_BYTES 2400

typedef unsigned char sk_u8;

typedef struct {
    sk_u8 cur[SK_BYTES];   /* the picture being edited */
    sk_u8 undo[SK_BYTES];  /* snapshot taken by sk_begin_stroke */
    int can_undo;          /* nonzero iff undo[] holds a valid snapshot */
} Sketch;

/*
 * Clears cur to all white AND discards the undo state: undo[] is zeroed and
 * can_undo is set to 0, so sk_undo() right after sk_clear() returns 0 and
 * changes nothing. (Clearing the canvas is not itself undoable; callers
 * that want that call sk_begin_stroke() before sk_clear().)
 */
void sk_clear(Sketch *s);

/* Sets pixel (x,y) black (nonzero) or white (0). Out of range is ignored. */
void sk_set(sk_u8 *buf, int x, int y, int black);

/* Returns 1 if (x,y) is black, else 0. Out of range returns 0. */
int sk_get(const sk_u8 *buf, int x, int y);

/*
 * Bresenham line from (x0,y0) to (x1,y1) inclusive, in black (nonzero) or
 * white (0). Endpoints may lie anywhere (even far outside the buffer); the
 * visible part is drawn exactly as the unclipped line would be and nothing
 * outside the buffer is ever written. The pixel set does not depend on the
 * order of the endpoints.
 */
void sk_line(sk_u8 *buf, int x0, int y0, int x1, int y1, int black);

/* Copies cur to undo and sets can_undo = 1. Call before each stroke. */
void sk_begin_stroke(Sketch *s);

/*
 * If can_undo, restores cur from undo, sets can_undo = 0 and returns 1.
 * Otherwise returns 0 and leaves cur untouched. One level only.
 */
int sk_undo(Sketch *s);

/*
 * Finds the maximal runs of black pixels on row y. Run i starts at
 * starts[i] and is lens[i] pixels long. At most max runs are stored; the
 * return value is the number stored (so it truncates at max). Returns 0
 * for y outside 0..SK_H-1 or max <= 0.
 */
int sk_runs(const sk_u8 *buf, int y, int *starts, int *lens, int max);

#ifdef __cplusplus
}
#endif

#endif
