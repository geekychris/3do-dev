/* 3DO port: Void Trader's triangles as cels (tri_cels.c) */
#ifndef TRI_CELS_H
#define TRI_CELS_H
int   tc_init(void);
void  tc_begin(void *base_ccb);       /* list starts with this cel (the backdrop) */
/* a filled triangle, corners in display 16.16 */
void  tc_tri(long ax, long ay, long bx, long by, long cx, long cy, unsigned short rgb15);
void *tc_list(void);
#endif
