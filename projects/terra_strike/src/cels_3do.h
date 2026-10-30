/* cels_3do.h - Terra Strike's polygons through the 3DO's cel engine (cels_3do.c) */
#ifndef CELS_3DO_H
#define CELS_3DO_H

#include <stdint.h>

void ts3_begin(int bg_pen, int w, int h);   /* a new list: the view (0,0)-(w,h) in bg_pen first */
void ts3_poly(const int16_t *xs, const int16_t *ys, int n, int col);   /* convex, game pixels */
void ts3_rect(int x0, int y0, int x1, int y1, int col);                 /* inclusive */
void ts3_end(void);                         /* the list goes under the frame (gfx_set_underlay) */
extern int32_t ts3_ncels;
/* q / z for drawing (z > 0): a reciprocal table instead of the ARM60's
 * software divide; floor(q / z) or one more, truncated toward zero */
int32_t ts3_div(int32_t q, int32_t z);

#endif
