/* Planet Chomp sprite textures, drawn at startup (sprites.c) */
#ifndef SPRITES_H
#define SPRITES_H
/* chomper: tex_chomper[powered][dir][mouth]; dir 0 right 1 up 2 left 3 down */
extern int tex_chomper[2][4][3];
enum { GT_BLAZE, GT_PETAL, GT_FROST, GT_EMBER, GT_FRIGHT, GT_FLASH, GT_EYES, GT_COUNT };
extern int tex_ghost[GT_COUNT];
extern int tex_key;
int sprites_init(void);
#endif
