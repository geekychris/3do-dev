/*
 * Amiga graphics.library subset for the 3DO compatibility layer.
 * Implemented in sdk/amiga/src/amiga_gfx.c. All drawing is clipped.
 */
#ifndef GRAPHICS_RASTPORT_H
#define GRAPHICS_RASTPORT_H

#include "amiga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef UBYTE *PLANEPTR;

/* The shims don't use these; they exist so pointers to them compile. */
struct ViewPort;
struct ColorMap;
struct Layer;
struct GelsInfo;
struct View;

struct BitMap {
	UWORD BytesPerRow;
	UWORD Rows;
	UBYTE Flags;
	UBYTE Depth;
	UWORD pad;
	PLANEPTR Planes[8];
};

struct TextAttr {
	STRPTR ta_Name;
	UWORD  ta_YSize;
	UBYTE  ta_Style;
	UBYTE  ta_Flags;
};

struct TextFont {
	UWORD tf_YSize;
	UBYTE tf_Style;
	UBYTE tf_Flags;
	UWORD tf_XSize;
	UWORD tf_Baseline;
};

struct AreaInfo {
	WORD *VctrTbl;
	WORD *VctrPtr;
	BYTE *FlagTbl;
	BYTE *FlagPtr;
	WORD  Count;
	WORD  MaxCount;
	WORD  FirstX, FirstY;
};

struct TmpRas {
	BYTE *RasPtr;
	LONG  Size;
};

struct RastPort {
	struct Layer    *Layer;
	struct BitMap   *BitMap;     /* NULL: the layer's offscreen frame */
	UWORD           *AreaPtrn;
	struct TmpRas   *TmpRas;
	struct AreaInfo *AreaInfo;
	struct GelsInfo *GelsInfo;
	UBYTE  Mask;
	BYTE   FgPen;
	BYTE   BgPen;
	BYTE   AOlPen;
	BYTE   DrawMode;
	BYTE   AreaPtSz;
	BYTE   linpatcnt;
	BYTE   dummy;
	UWORD  Flags;
	UWORD  LinePtrn;
	WORD   cp_x, cp_y;
	UBYTE  minterms[8];
	WORD   PenWidth;
	WORD   PenHeight;
	struct TextFont *Font;
	UBYTE  AlgoStyle;
	UBYTE  TxFlags;
	UWORD  TxHeight;
	UWORD  TxWidth;
	UWORD  TxBaseline;
	WORD   TxSpacing;
	APTR  *RP_User;
	/* 3DO layer private */
	UBYTE  apen, bpen;           /* full 8-bit pens (FgPen/BgPen are signed) */
	LONG   clip_x0, clip_y0, clip_x1, clip_y1;   /* LONG: ARMv3 has no halfword loads */
};

/* draw modes */
#define JAM1        0
#define JAM2        1
#define COMPLEMENT  2
#define INVERSVID   4

/* minterms */
#define ABC   0x80
#define ABNC  0x40
#define ANBC  0x20
#define ANBNC 0x10
#define NABC  0x08
#define NABNC 0x04
#define NANBC 0x02
#define NANBNC 0x01

void  InitRastPort(struct RastPort *rp);
void  SetAPen(struct RastPort *rp, ULONG pen);
void  SetBPen(struct RastPort *rp, ULONG pen);
void  SetDrMd(struct RastPort *rp, ULONG mode);
void  SetWriteMask(struct RastPort *rp, ULONG mask);
void  SetRast(struct RastPort *rp, ULONG pen);
void  Move(struct RastPort *rp, LONG x, LONG y);
void  Draw(struct RastPort *rp, LONG x, LONG y);
void  RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1);
/* renamed: the 3DO graphics.lib has its own WritePixel/ReadPixel */
#define WritePixel(rp,x,y) amiga_WritePixel(rp,x,y)
#define ReadPixel(rp,x,y)  amiga_ReadPixel(rp,x,y)
LONG  amiga_WritePixel(struct RastPort *rp, LONG x, LONG y);
LONG  amiga_ReadPixel(struct RastPort *rp, LONG x, LONG y);
void  PolyDraw(struct RastPort *rp, LONG count, const WORD *xy);
void  DrawEllipse(struct RastPort *rp, LONG cx, LONG cy, LONG rx, LONG ry);
LONG  Text(struct RastPort *rp, CONST_STRPTR str, ULONG count);
WORD  TextLength(struct RastPort *rp, CONST_STRPTR str, ULONG count);
LONG  SetFont(struct RastPort *rp, struct TextFont *font);
struct TextFont *OpenFont(struct TextAttr *ta);
void  CloseFont(struct TextFont *font);
ULONG SetSoftStyle(struct RastPort *rp, ULONG style, ULONG enable);

void  InitArea(struct AreaInfo *ai, APTR buffer, LONG maxvectors);
struct TmpRas *InitTmpRas(struct TmpRas *tr, PLANEPTR buffer, LONG size);
LONG  AreaMove(struct RastPort *rp, LONG x, LONG y);
LONG  AreaDraw(struct RastPort *rp, LONG x, LONG y);
LONG  AreaEnd(struct RastPort *rp);
LONG  AreaEllipse(struct RastPort *rp, LONG cx, LONG cy, LONG rx, LONG ry);

void  InitBitMap(struct BitMap *bm, LONG depth, LONG width, LONG height);
PLANEPTR AllocRaster(ULONG width, ULONG height);
void  FreeRaster(PLANEPTR p, ULONG width, ULONG height);
/* planar source -> pens (minterm 0xC0 copies; others treated as copy) */
void  BltBitMapRastPort(const struct BitMap *src, LONG sx, LONG sy, struct RastPort *dst,
                        LONG dx, LONG dy, LONG w, LONG h, ULONG minterm);
void  ScrollRaster(struct RastPort *rp, LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1);

void  SetRGB4(struct ViewPort *vp, LONG pen, ULONG r, ULONG g, ULONG b);
void  SetRGB32(struct ViewPort *vp, ULONG pen, ULONG r, ULONG g, ULONG b);
void  LoadRGB4(struct ViewPort *vp, const UWORD *colors, LONG count);
void  LoadRGB32(struct ViewPort *vp, const ULONG *table);
ULONG GetRGB4(struct ColorMap *cm, LONG pen);
void  WaitTOF(void);
void  WaitBlit(void);
void  OwnBlitter(void);
void  DisownBlitter(void);


#ifdef __cplusplus
}
#endif

#endif
