/* graphics/gfxmacros.h macros (3DO compatibility layer) */
#ifndef AMIGA_GFXMACROS_H
#define AMIGA_GFXMACROS_H
#define SetDrPt(w,p)    ((w)->LinePtrn = (p), (w)->Flags |= 1, (w)->linpatcnt = 15)
#define SetAfPt(w,p,n)  ((w)->AreaPtrn = (p), (w)->AreaPtSz = (n))
#define SetWrMsk(w,m)   ((w)->Mask = (m))
#define SetOPen(w,c)    ((w)->AOlPen = (c))
#define BNDRYOFF(w)     ((w)->Flags &= ~8)
#define ON_DISPLAY
#define OFF_DISPLAY
#define ON_SPRITE
#define OFF_SPRITE
#endif
