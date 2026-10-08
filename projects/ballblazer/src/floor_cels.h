/* 3DO port: Ballblazer's floor as cels (floor_cels.c) */
#ifndef FLOOR_CELS_H
#define FLOOR_CELS_H
int   fc_init(unsigned short even_rgb15, unsigned short odd_rgb15);
void  fc_begin(void);
/* one band: texel 0's top-left corner (x16, y16, display 16.16), its step
 * per texel at the top edge (hdx20, 12.20), the band's left-edge shift and
 * height (vdx16, vdy16) and how much the texel step grows by the bottom
 * edge (hddx20) */
void  fc_band(long x16, long y16, long hdx20, long vdx16, long vdy16, long hddx20, int texels);
void *fc_list(void);
#endif
