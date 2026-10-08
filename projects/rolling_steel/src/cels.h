/* cels.c: the only file with 3DO graphics headers */
#ifndef CELS_H
#define CELS_H

/* pixel processor words (both halves) */
#define PIXC_SHADOW 0x8F008F00UL      /* frame buffer * 4/8 */
#define PIXC_GHOST  0x0F810F81UL      /* (cel + frame buffer) / 2 */
#define PIXC_ADD    0x1F801F80UL      /* cel + frame buffer: flashes, sparks */

int   cels_init(void);
void  cels_begin(void);
int   cels_count(void);
void  cels_quad(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
                unsigned short rgb15, unsigned long pixc);
int   cels_tex(int w, int h, unsigned short *pixels);
void  cels_sprite(int t, long x, long y, long hx, long hy, unsigned long pixc);
void *cels_list(void);
int   cels_mark(void);
void *cels_segment(int from, int to);
#endif
