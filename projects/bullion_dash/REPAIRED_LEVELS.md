# Bullion Dash: the repaired levels

As released, 149 of the 150 levels could not be won (only level 24 could):
gold the player can't get to, or no way up to row 0 once the gold is in,
by the game's own movement rules (player.c: he climbs only from a ladder
cell, can't drop from a bar, and a dug hole grows back on him). The levels
in `level.c` are now repaired with the smallest edits that make each one
winnable, keeping its layout; the levels as released are in
`levels_original.txt` (the editor's text format), the repaired ones in
`levels_repaired.txt`.

`tools/repair_levels.py` made them. Its route search (`tools/bdlevels.py`)
follows player.c, with the guards left out and no digging; a level is
winnable when all the gold can be collected in some order, each from the
last, and row 0 reached afterwards with the hidden ladders shown. While a
level isn't, the cheapest fix at the first place the route breaks is kept:
a ladder in one column (new or extending one; through space, a brick, a
bar's cell, or a gold or guard in its way, which moves to the nearest
place it can lie: 1-2 cells), or two such ladders when they cost less
than one; for the way out, a hidden ladder up to row 1 where it only
crosses space (it appears with the last gold, as the original exits do).
The last resort, moving a gold the player can't reach, was never needed.

`tools/check_levels.py` plays every level of `level.c` to the exit with
the game's own player.c and level.c built for the host
(`tools/hostcheck.c`), frame by frame, along the route search's moves:
150 of 150 are won (on the released levels: only level 24).

The guards are not part of this: the guard speed problem in the ports'
ORIGINAL_BUGS.md is unchanged.

Cells are (column, row), row 0 at the top; `.` space, `#` brick, `@`
solid, `H` ladder, `X` hidden ladder, `-` bar, `$` gold, `P` the start,
`E` a guard.

150 of 150 levels winnable; 1042 cells changed in all (6.9 a level).

## Level 1: winnable, 9 cells changed

- ladder at column 6, rows 13-14
- ladder at column 11, rows 6-6
- ladder at column 12, rows 7-7
- ladder at column 23, rows 5-5
- ladder at column 9, rows 3-5
- exit: hidden ladder at column 26, rows 1-1
- cells: (26,1) .->X, (9,3) .->H, (9,4) .->H, (9,5) .->H, (23,5) .->H, (11,6) .->H, (12,7) .->H, (6,13) .->H, (6,14) .->H

## Level 2: winnable, 1 cell changed

- exit: hidden ladder at column 2, rows 1-1
- cells: (2,1) .->X

## Level 3: winnable, 2 cells changed

- ladder at column 2, rows 1-1
- ladder at column 25, rows 2-2
- cells: (2,1) .->H, (25,2) .->H

## Level 4: winnable, 9 cells changed

- ladder at column 24, rows 6-7
- ladder at column 16, rows 11-14
- ladder at column 9, rows 11-11
- exit: ladder at column 24, rows 4-5 (1 through brick)
- cells: (24,4) #->H, (24,5) .->H, (24,6) .->H, (24,7) .->H, (9,11) .->H, (16,11) .->H, (16,12) .->H, (16,13) .->H, (16,14) .->H

## Level 5: winnable, 21 cells changed

- ladder at column 6, rows 6-14
- ladder at column 21, rows 3-14 (2 through brick)
- cells: (21,3) .->H, (21,4) .->H, (21,5) .->H, (6,6) .->H, (21,6) .->H, (6,7) .->H, (21,7) .->H, (6,8) .->H, (21,8) #->H, (6,9) .->H, (21,9) .->H, (6,10) .->H, (21,10) .->H, (6,11) .->H, (21,11) #->H, (6,12) .->H, (21,12) .->H, (6,13) .->H, (21,13) .->H, (6,14) .->H, (21,14) .->H

## Level 6: winnable, 1 cell changed

- ladder at column 19, rows 3-3
- cells: (19,3) .->H

## Level 7: winnable, 7 cells changed

- ladder at column 20, rows 12-14
- ladder at column 21, rows 11-11
- ladder at column 20, rows 6-7
- ladder at column 4, rows 7-7
- cells: (20,6) .->H, (4,7) .->H, (20,7) .->H, (21,11) .->H, (20,12) .->H, (20,13) .->H, (20,14) .->H

## Level 8: winnable, 2 cells changed

- ladder at column 8, rows 10-10
- ladder at column 17, rows 12-12
- cells: (8,10) .->H, (17,12) .->H

## Level 9: winnable, 6 cells changed

- ladder at column 3, rows 13-14
- ladder at column 3, rows 5-12 (1 through brick)
- ladder at column 10, rows 4-4
- exit: ladder at column 25, rows 7-7
- cells: (10,4) .->H, (3,5) #->H, (25,7) .->H, (3,12) .->H, (3,13) .->H, (3,14) .->H

## Level 10: winnable, 6 cells changed

- ladder at column 12, rows 3-6
- exit: hidden ladder at column 11, rows 1-2
- cells: (11,1) .->X, (11,2) .->X, (12,3) .->H, (12,4) .->H, (12,5) .->H, (12,6) .->H

## Level 11: winnable, 7 cells changed

- ladder at column 13, rows 11-14
- exit: ladder at column 23, rows 14-14; ladder at column 24, rows 4-10 (2 through brick)
- cells: (24,4) #->H, (24,10) #->H, (13,11) .->H, (13,12) .->H, (13,13) .->H, (13,14) .->H, (23,14) .->H

## Level 12: winnable, 6 cells changed

- ladder at column 3, rows 11-14
- ladder at column 5, rows 10-10
- ladder at column 6, rows 3-3
- cells: (6,3) .->H, (5,10) .->H, (3,11) .->H, (3,12) .->H, (3,13) .->H, (3,14) .->H

## Level 13: winnable, 5 cells changed

- ladder at column 4, rows 14-14; ladder at column 4, rows 3-6 (1 through brick)
- cells: (4,3) #->H, (4,4) .->H, (4,5) .->H, (4,6) .->H, (4,14) .->H

## Level 14: winnable, 3 cells changed

- ladder at column 26, rows 0-0
- ladder at column 17, rows 9-10
- cells: (26,0) .->H, (17,9) .->H, (17,10) .->H

## Level 15: winnable, 3 cells changed

- ladder at column 10, rows 14-14 (1 through brick)
- ladder at column 14, rows 14-14
- ladder at column 24, rows 0-0
- cells: (24,0) .->H, (10,14) #->H, (14,14) .->H

## Level 16: winnable, 1 cell changed

- ladder at column 18, rows 3-3
- cells: (18,3) .->H

## Level 17: winnable, 4 cells changed

- ladder at column 26, rows 0-0
- ladder at column 24, rows 1-1
- ladder at column 16, rows 3-4
- cells: (26,0) .->H, (24,1) .->H, (16,3) .->H, (16,4) .->H

## Level 18: winnable, 3 cells changed

- ladder at column 2, rows 14-14
- ladder at column 4, rows 6-6
- ladder at column 19, rows 13-13
- cells: (4,6) .->H, (19,13) .->H, (2,14) .->H

## Level 19: winnable, 8 cells changed

- ladder at column 10, rows 12-14
- ladder at column 11, rows 6-6
- ladder at column 17, rows 6-6
- ladder at column 11, rows 3-5
- cells: (11,3) .->H, (11,4) .->H, (11,5) .->H, (11,6) .->H, (17,6) .->H, (10,12) .->H, (10,13) .->H, (10,14) .->H

## Level 20: winnable, 8 cells changed

- ladder at column 6, rows 9-14
- ladder at column 16, rows 13-13
- exit: ladder at column 22, rows 14-14
- cells: (6,9) .->H, (6,10) .->H, (6,11) .->H, (6,12) .->H, (6,13) .->H, (16,13) .->H, (6,14) .->H, (22,14) .->H

## Level 21: winnable, 7 cells changed

- ladder at column 6, rows 14-14
- ladder at column 20, rows 6-8
- ladder at column 17, rows 5-5
- ladder at column 7, rows 4-5
- cells: (7,4) .->H, (7,5) .->H, (17,5) .->H, (20,6) .->H, (20,7) .->H, (20,8) .->H, (6,14) .->H

## Level 22: winnable, 9 cells changed

- ladder at column 9, rows 14-14
- ladder at column 1, rows 6-14
- ladder at column 9, rows 3-5
- exit: ladder at column 20, rows 14-14
- cells: (9,3) .->H, (9,4) .->H, (9,5) .->H, (1,6) .->H, (1,7) .->H, (1,13) .->H, (1,14) .->H, (9,14) .->H, (20,14) .->H

## Level 23: winnable, 14 cells changed

- ladder at column 25, rows 5-5 (1 through brick)
- ladder at column 22, rows 4-4
- ladder at column 8, rows 3-14
- cells: (8,3) .->H, (8,4) .->H, (22,4) .->H, (8,5) .->H, (25,5) #->H, (8,6) .->H, (8,7) .->H, (8,8) .->H, (8,9) .->H, (8,10) .->H, (8,11) .->H, (8,12) .->H, (8,13) .->H, (8,14) .->H

## Level 24: winnable, 0 cells changed

- unchanged

## Level 25: winnable, 1 cell changed

- ladder at column 18, rows 7-7
- cells: (18,7) .->H

## Level 26: winnable, 5 cells changed

- ladder at column 16, rows 3-6 (1 through brick)
- exit: ladder at column 22, rows 2-2
- cells: (22,2) -->H, (16,3) #->H, (16,4) .->H, (16,5) .->H, (16,6) .->H

## Level 27: winnable, 5 cells changed

- ladder at column 14, rows 9-9
- ladder at column 8, rows 8-11
- cells: (8,8) .->H, (8,9) .->H, (14,9) .->H, (8,10) .->H, (8,11) -->H

## Level 28: winnable, 10 cells changed

- ladder at column 1, rows 14-14
- ladder at column 7, rows 10-10; ladder at column 16, rows 9-9
- ladder at column 15, rows 3-8 (1 through brick)
- ladder at column 20, rows 2-2
- cells: (20,2) .->H, (15,3) #->H, (15,4) .->H, (15,5) .->H, (15,6) .->H, (15,7) .->H, (15,8) .->H, (16,9) .->H, (7,10) .->H, (1,14) .->H

## Level 29: winnable, 8 cells changed

- ladder at column 24, rows 10-10
- ladder at column 6, rows 13-14
- ladder at column 17, rows 9-10
- ladder at column 7, rows 4-6
- cells: (7,4) .->H, (7,5) .->H, (7,6) .->H, (17,9) .->H, (17,10) .->H, (24,10) .->H, (6,13) .->H, (6,14) .->H

## Level 30: winnable, 5 cells changed

- ladder at column 14, rows 12-12
- ladder at column 6, rows 5-11 (1 through brick)
- cells: (6,5) #->H, (6,6) .->H, (6,10) .->H, (6,11) .->H, (14,12) .->H

## Level 31: winnable, 10 cells changed

- ladder at column 12, rows 9-10
- ladder at column 25, rows 5-7
- ladder at column 6, rows 9-12
- exit: ladder at column 22, rows 4-4
- cells: (22,4) -->H, (25,5) .->H, (25,6) .->H, (25,7) .->H, (6,9) .->H, (12,9) .->H, (6,10) .->H, (12,10) .->H, (6,11) .->H, (6,12) .->H

## Level 32: winnable, 10 cells changed

- ladder at column 5, rows 14-14
- ladder at column 9, rows 13-13
- ladder at column 8, rows 12-12
- ladder at column 18, rows 13-14
- ladder at column 17, rows 12-12
- ladder at column 24, rows 12-12
- ladder at column 22, rows 4-4
- ladder at column 17, rows 3-3
- exit: ladder at column 8, rows 14-14 (1 through brick)
- cells: (17,3) .->H, (22,4) .->H, (8,12) .->H, (17,12) .->H, (24,12) .->H, (9,13) .->H, (18,13) .->H, (5,14) .->H, (8,14) #->H, (18,14) .->H

## Level 33: winnable, 8 cells changed

- ladder at column 17, rows 13-14
- ladder at column 12, rows 3-5
- ladder at column 13, rows 2-2
- ladder at column 8, rows 2-2; ladder at column 7, rows 1-1
- cells: (7,1) .->H, (8,2) .->H, (13,2) .->H, (12,3) .->H, (12,4) .->H, (12,5) -->H, (17,13) .->H, (17,14) .->H

## Level 34: winnable, 9 cells changed

- ladder at column 11, rows 4-6
- ladder at column 7, rows 3-3
- ladder at column 24, rows 10-14
- cells: (7,3) .->H, (11,4) .->H, (11,5) .->H, (11,6) .->H, (24,10) .->H, (24,11) .->H, (24,12) .->H, (24,13) .->H, (24,14) .->H

## Level 35: winnable, 2 cells changed

- ladder at column 26, rows 0-0
- ladder at column 21, rows 4-4
- cells: (26,0) .->H, (21,4) .->H

## Level 36: winnable, 7 cells changed

- ladder at column 9, rows 14-14
- ladder at column 15, rows 13-13
- ladder at column 7, rows 10-10
- ladder at column 9, rows 3-6
- cells: (9,3) .->H, (9,4) .->H, (9,5) .->H, (9,6) .->H, (7,10) .->H, (15,13) .->H, (9,14) .->H

## Level 37: winnable, 7 cells changed

- ladder at column 14, rows 12-12 (1 through brick)
- ladder at column 15, rows 7-7
- ladder at column 8, rows 4-7
- exit: ladder at column 22, rows 5-5
- cells: (8,4) .->H, (8,5) .->H, (22,5) -->H, (8,6) .->H, (8,7) .->H, (15,7) .->H, (14,12) #->H

## Level 38: winnable, 12 cells changed

- ladder at column 20, rows 11-11
- ladder at column 20, rows 7-10
- ladder at column 9, rows 9-14
- exit: hidden ladder at column 19, rows 1-1
- cells: (19,1) .->X, (20,7) .->H, (20,8) .->H, (9,9) .->H, (20,9) .->H, (9,10) .->H, (20,10) .->H, (9,11) .->H, (20,11) -->H, (9,12) .->H, (9,13) .->H, (9,14) .->H

## Level 39: winnable, 11 cells changed

- ladder at column 10, rows 14-14
- ladder at column 18, rows 14-14 (1 through brick)
- ladder at column 10, rows 9-13
- ladder at column 13, rows 7-8 (1 through brick)
- ladder at column 13, rows 6-6
- ladder at column 19, rows 8-8
- cells: (13,6) .->H, (13,7) .->H, (13,8) #->H, (19,8) .->H, (10,9) .->H, (10,10) .->H, (10,11) .->H, (10,12) .->H, (10,13) .->H, (10,14) .->H, (18,14) #->H

## Level 40: winnable, 8 cells changed

- ladder at column 18, rows 14-14
- ladder at column 21, rows 13-13
- ladder at column 17, rows 8-8; ladder at column 14, rows 4-8
- cells: (14,4) .->H, (14,5) .->H, (14,6) .->H, (14,7) .->H, (14,8) .->H, (17,8) .->H, (21,13) .->H, (18,14) .->H

## Level 41: winnable, 1 cell changed

- ladder at column 16, rows 5-5
- cells: (16,5) .->H

## Level 42: winnable, 5 cells changed

- ladder at column 4, rows 13-14
- ladder at column 21, rows 11-11
- ladder at column 23, rows 11-11
- exit: hidden ladder at column 20, rows 1-1
- cells: (20,1) .->X, (21,11) .->H, (23,11) .->H, (4,13) .->H, (4,14) .->H

## Level 43: winnable, 3 cells changed

- ladder at column 8, rows 14-14
- ladder at column 25, rows 0-0
- ladder at column 21, rows 3-3
- cells: (25,0) .->H, (21,3) .->H, (8,14) .->H

## Level 44: winnable, 13 cells changed

- ladder at column 19, rows 11-11
- ladder at column 16, rows 10-12 (gold moved aside)
- ladder at column 15, rows 4-9 (2 through brick, a guard moved aside)
- exit: ladder at column 19, rows 4-4
- cells: (15,4) #->H, (19,4) .->H, (15,5) .->H, (15,6) #->H, (15,7) .->H, (15,8) .->H, (14,9) .->E, (15,9) E->H, (16,10) .->H, (16,11) .->H, (19,11) .->H, (16,12) $->H, (17,12) .->$

## Level 45: winnable, 13 cells changed

- ladder at column 16, rows 14-14
- ladder at column 20, rows 13-13
- ladder at column 26, rows 0-0
- ladder at column 1, rows 13-14
- ladder at column 1, rows 4-7
- ladder at column 13, rows 3-10 (a guard moved aside)
- ladder at column 18, rows 2-2
- cells: (26,0) .->H, (13,2) .->E, (18,2) .->H, (13,3) E->H, (1,4) .->H, (1,5) .->H, (1,6) .->H, (1,7) .->H, (13,10) .->H, (1,13) .->H, (20,13) .->H, (1,14) .->H, (16,14) .->H

## Level 46: winnable, 7 cells changed

- ladder at column 27, rows 10-11
- ladder at column 18, rows 4-4 (1 through brick)
- ladder at column 12, rows 6-6
- ladder at column 1, rows 8-10 (1 through brick)
- cells: (18,4) #->H, (12,6) .->H, (1,8) #->H, (1,9) .->H, (1,10) .->H, (27,10) .->H, (27,11) .->H

## Level 47: winnable, 13 cells changed

- ladder at column 14, rows 14-14
- ladder at column 16, rows 11-13
- ladder at column 14, rows 6-6
- ladder at column 19, rows 11-14 (1 through brick, gold moved aside)
- ladder at column 22, rows 10-10
- exit: ladder at column 21, rows 11-11 (1 through brick); hidden ladder at column 14, rows 1-1
- cells: (14,1) .->X, (14,6) .->H, (22,10) .->H, (16,11) .->H, (19,11) .->H, (21,11) #->H, (16,12) .->H, (19,12) #->H, (16,13) .->H, (19,13) .->H, (14,14) .->H, (18,14) .->$, (19,14) $->H

## Level 48: winnable, 6 cells changed

- ladder at column 26, rows 0-0
- ladder at column 24, rows 3-4
- ladder at column 18, rows 5-5
- ladder at column 16, rows 3-4
- cells: (26,0) .->H, (16,3) .->H, (24,3) .->H, (16,4) .->H, (24,4) .->H, (18,5) .->H

## Level 49: winnable, 4 cells changed

- ladder at column 11, rows 14-14
- ladder at column 22, rows 14-14
- ladder at column 16, rows 9-9
- exit: ladder at column 20, rows 2-2
- cells: (20,2) .->H, (16,9) .->H, (11,14) .->H, (22,14) .->H

## Level 50: winnable, 9 cells changed

- ladder at column 16, rows 13-14
- ladder at column 15, rows 12-12
- ladder at column 15, rows 10-11
- ladder at column 14, rows 8-9
- ladder at column 18, rows 12-12 (1 through brick)
- exit: ladder at column 22, rows 9-9
- cells: (14,8) .->H, (14,9) .->H, (22,9) .->H, (15,10) .->H, (15,11) .->H, (15,12) .->H, (18,12) #->H, (16,13) .->H, (16,14) .->H

## Level 51: winnable, 7 cells changed

- ladder at column 1, rows 14-14
- ladder at column 6, rows 14-14
- ladder at column 22, rows 14-14
- ladder at column 10, rows 12-13
- ladder at column 8, rows 6-7
- cells: (8,6) .->H, (8,7) .->H, (10,12) .->H, (10,13) .->H, (1,14) .->H, (6,14) .->H, (22,14) .->H

## Level 52: winnable, 5 cells changed

- ladder at column 4, rows 9-9 (1 through brick)
- ladder at column 18, rows 7-7
- ladder at column 17, rows 6-6
- ladder at column 13, rows 4-5
- cells: (13,4) .->H, (13,5) .->H, (17,6) .->H, (18,7) .->H, (4,9) #->H

## Level 53: winnable, 5 cells changed

- ladder at column 9, rows 12-12
- ladder at column 9, rows 11-11
- ladder at column 23, rows 7-9
- cells: (23,7) .->H, (23,8) .->H, (23,9) .->H, (9,11) .->H, (9,12) .->H

## Level 54: winnable, 5 cells changed

- ladder at column 20, rows 7-7
- ladder at column 11, rows 3-5 (1 through brick)
- exit: ladder at column 23, rows 5-5
- cells: (11,3) .->H, (11,4) #->H, (11,5) .->H, (23,5) .->H, (20,7) .->H

## Level 55: winnable, 7 cells changed

- ladder at column 4, rows 14-14
- ladder at column 17, rows 14-14
- ladder at column 11, rows 7-7
- ladder at column 11, rows 9-9
- ladder at column 7, rows 5-7
- cells: (7,5) .->H, (7,6) .->H, (7,7) .->H, (11,7) .->H, (11,9) .->H, (4,14) .->H, (17,14) .->H

## Level 56: winnable, 7 cells changed

- ladder at column 20, rows 8-8
- ladder at column 17, rows 11-11
- ladder at column 24, rows 0-0
- ladder at column 21, rows 4-4
- ladder at column 13, rows 9-11
- cells: (24,0) .->H, (21,4) .->H, (20,8) .->H, (13,9) .->H, (13,10) .->H, (13,11) .->H, (17,11) .->H

## Level 57: winnable, 2 cells changed

- ladder at column 6, rows 11-11
- ladder at column 6, rows 4-4
- cells: (6,4) .->H, (6,11) .->H

## Level 58: winnable, 4 cells changed

- ladder at column 2, rows 10-10
- ladder at column 25, rows 14-14
- ladder at column 9, rows 11-11 (1 through brick)
- ladder at column 21, rows 3-3 (1 through brick)
- cells: (21,3) #->H, (2,10) .->H, (9,11) #->H, (25,14) .->H

## Level 59: winnable, 6 cells changed

- ladder at column 16, rows 3-8
- cells: (16,3) .->H, (16,4) .->H, (16,5) .->H, (16,6) .->H, (16,7) .->H, (16,8) -->H

## Level 60: winnable, 6 cells changed

- ladder at column 1, rows 14-14 (1 through brick)
- ladder at column 13, rows 11-14
- exit: ladder at column 9, rows 7-7
- cells: (9,7) .->H, (13,11) .->H, (13,12) .->H, (13,13) .->H, (1,14) #->H, (13,14) .->H

## Level 61: winnable, 6 cells changed

- ladder at column 7, rows 14-14
- ladder at column 15, rows 13-13
- ladder at column 18, rows 5-5
- ladder at column 23, rows 7-7
- exit: ladder at column 18, rows 14-14; ladder at column 25, rows 2-2
- cells: (25,2) .->H, (18,5) .->H, (23,7) -->H, (15,13) .->H, (7,14) .->H, (18,14) .->H

## Level 62: winnable, 14 cells changed

- ladder at column 5, rows 12-14
- ladder at column 5, rows 11-11
- ladder at column 13, rows 12-14 (1 through brick)
- ladder at column 25, rows 6-7
- ladder at column 20, rows 4-5
- ladder at column 14, rows 4-6
- cells: (14,4) .->H, (20,4) .->H, (14,5) .->H, (20,5) .->H, (14,6) .->H, (25,6) .->H, (25,7) .->H, (5,11) .->H, (5,12) .->H, (13,12) .->H, (5,13) .->H, (13,13) #->H, (5,14) .->H, (13,14) .->H

## Level 63: winnable, 21 cells changed

- ladder at column 6, rows 12-12
- ladder at column 21, rows 5-8
- ladder at column 5, rows 4-11
- ladder at column 9, rows 4-11
- cells: (5,4) .->H, (9,4) .->H, (5,5) .->H, (9,5) .->H, (21,5) .->H, (5,6) .->H, (9,6) .->H, (21,6) .->H, (5,7) .->H, (9,7) .->H, (21,7) .->H, (5,8) .->H, (9,8) .->H, (21,8) .->H, (5,9) .->H, (9,9) .->H, (5,10) .->H, (9,10) .->H, (5,11) .->H, (9,11) .->H, (6,12) -->H

## Level 64: winnable, 6 cells changed

- ladder at column 20, rows 8-14
- ladder at column 9, rows 4-6
- cells: (9,4) .->H, (9,5) .->H, (9,6) .->H, (20,8) .->H, (20,9) .->H, (20,14) .->H

## Level 65: winnable, 12 cells changed

- ladder at column 1, rows 14-14
- ladder at column 17, rows 14-14
- ladder at column 3, rows 12-13
- ladder at column 19, rows 8-9
- ladder at column 19, rows 3-7
- exit: ladder at column 12, rows 14-14 (1 through brick)
- cells: (19,3) .->H, (19,4) .->H, (19,5) .->H, (19,6) .->H, (19,7) .->H, (19,8) .->H, (19,9) .->H, (3,12) .->H, (3,13) .->H, (1,14) .->H, (12,14) #->H, (17,14) .->H

## Level 66: winnable, 3 cells changed

- ladder at column 17, rows 14-14 (1 through brick)
- ladder at column 18, rows 7-7
- ladder at column 21, rows 4-4
- cells: (21,4) .->H, (18,7) .->H, (17,14) #->H

## Level 67: winnable, 5 cells changed

- ladder at column 25, rows 1-1
- ladder at column 14, rows 6-7 (1 through brick)
- ladder at column 13, rows 4-5
- cells: (25,1) .->H, (13,4) .->H, (13,5) .->H, (14,6) .->H, (14,7) #->H

## Level 68: winnable, 3 cells changed

- ladder at column 14, rows 6-11 (1 through brick)
- ladder at column 24, rows 0-0
- cells: (24,0) .->H, (14,6) #->H, (14,11) .->H

## Level 69: winnable, 5 cells changed

- ladder at column 13, rows 14-14
- ladder at column 8, rows 2-2
- ladder at column 21, rows 3-4
- exit: hidden ladder at column 7, rows 1-1
- cells: (7,1) .->X, (8,2) .->H, (21,3) .->H, (21,4) .->H, (13,14) .->H

## Level 70: winnable, 3 cells changed

- ladder at column 9, rows 11-11
- exit: ladder at column 15, rows 3-4
- cells: (15,3) .->H, (15,4) .->H, (9,11) .->H

## Level 71: winnable, 3 cells changed

- ladder at column 24, rows 5-5
- ladder at column 18, rows 6-6
- ladder at column 22, rows 4-4
- cells: (22,4) .->H, (24,5) .->H, (18,6) .->H

## Level 72: winnable, 8 cells changed

- ladder at column 5, rows 14-14
- ladder at column 10, rows 13-13
- ladder at column 11, rows 12-12
- ladder at column 19, rows 4-4
- ladder at column 9, rows 12-12; ladder at column 8, rows 9-11
- cells: (19,4) .->H, (8,9) -->H, (8,10) .->H, (8,11) .->H, (9,12) .->H, (11,12) .->H, (10,13) .->H, (5,14) .->H

## Level 73: winnable, 11 cells changed

- ladder at column 9, rows 9-14
- ladder at column 15, rows 4-4
- ladder at column 22, rows 8-8
- ladder at column 12, rows 3-4
- exit: ladder at column 26, rows 14-14
- cells: (12,3) .->H, (12,4) .->H, (15,4) .->H, (22,8) .->H, (9,9) .->H, (9,10) .->H, (9,11) .->H, (9,12) .->H, (9,13) .->H, (9,14) .->H, (26,14) .->H

## Level 74: winnable, 11 cells changed

- ladder at column 4, rows 11-14 (1 through brick)
- ladder at column 10, rows 12-12
- ladder at column 11, rows 11-11
- ladder at column 0, rows 7-10
- ladder at column 12, rows 6-6
- cells: (12,6) .->H, (0,7) .->H, (0,8) .->H, (0,9) .->H, (0,10) .->H, (4,11) #->H, (11,11) .->H, (4,12) .->H, (10,12) .->H, (4,13) .->H, (4,14) .->H

## Level 75: winnable, 5 cells changed

- ladder at column 24, rows 8-8
- ladder at column 17, rows 2-2
- ladder at column 1, rows 12-14
- cells: (17,2) .->H, (24,8) .->H, (1,12) .->H, (1,13) .->H, (1,14) .->H

## Level 76: winnable, 8 cells changed

- ladder at column 21, rows 10-10
- ladder at column 19, rows 5-6
- ladder at column 2, rows 11-14
- exit: ladder at column 22, rows 4-4
- cells: (22,4) .->H, (19,5) .->H, (19,6) .->H, (21,10) .->H, (2,11) .->H, (2,12) .->H, (2,13) .->H, (2,14) .->H

## Level 77: winnable, 9 cells changed

- ladder at column 12, rows 14-14 (a guard moved aside)
- ladder at column 15, rows 7-7
- ladder at column 7, rows 9-14
- cells: (15,7) .->H, (7,9) .->H, (7,10) .->H, (7,11) .->H, (7,12) .->H, (7,13) .->H, (12,13) .->E, (7,14) .->H, (12,14) E->H

## Level 78: winnable, 5 cells changed

- ladder at column 5, rows 7-7
- ladder at column 6, rows 5-6 (1 through brick)
- ladder at column 14, rows 4-4
- exit: ladder at column 13, rows 5-5 (1 through brick)
- cells: (14,4) .->H, (6,5) .->H, (13,5) #->H, (6,6) #->H, (5,7) .->H

## Level 79: winnable, 5 cells changed

- ladder at column 5, rows 14-14
- ladder at column 17, rows 10-10
- ladder at column 17, rows 9-9
- ladder at column 11, rows 7-8
- cells: (11,7) .->H, (11,8) .->H, (17,9) .->H, (17,10) .->H, (5,14) .->H

## Level 80: winnable, 6 cells changed

- ladder at column 21, rows 12-12
- ladder at column 5, rows 9-10
- ladder at column 22, rows 10-11
- ladder at column 22, rows 5-5
- cells: (22,5) .->H, (5,9) .->H, (5,10) .->H, (22,10) .->H, (22,11) .->H, (21,12) .->H

## Level 81: winnable, 7 cells changed

- ladder at column 15, rows 12-14
- ladder at column 12, rows 9-11
- ladder at column 9, rows 11-11
- cells: (12,9) X->H, (12,10) .->H, (9,11) .->H, (12,11) .->H, (15,12) .->H, (15,13) .->H, (15,14) .->H

## Level 82: winnable, 9 cells changed

- ladder at column 4, rows 14-14
- ladder at column 10, rows 13-13
- ladder at column 11, rows 12-12
- ladder at column 10, rows 2-2
- ladder at column 18, rows 3-5
- exit: ladder at column 20, rows 13-14
- cells: (10,2) .->H, (18,3) .->H, (18,4) .->H, (18,5) .->H, (11,12) .->H, (10,13) .->H, (20,13) .->H, (4,14) .->H, (20,14) .->H

## Level 83: winnable, 13 cells changed

- ladder at column 7, rows 14-14 (1 through brick)
- ladder at column 6, rows 11-13
- ladder at column 5, rows 9-10
- ladder at column 24, rows 8-8
- ladder at column 22, rows 11-14
- exit: ladder at column 10, rows 14-14 (1 through brick); ladder at column 22, rows 3-3
- cells: (22,3) -->H, (24,8) .->H, (5,9) .->H, (5,10) .->H, (6,11) .->H, (22,11) .->H, (6,12) .->H, (22,12) .->H, (6,13) .->H, (22,13) .->H, (7,14) #->H, (10,14) #->H, (22,14) .->H

## Level 84: winnable, 6 cells changed

- ladder at column 10, rows 13-14
- ladder at column 14, rows 4-4
- ladder at column 1, rows 8-8
- exit: ladder at column 27, rows 5-6
- cells: (14,4) X->H, (27,5) .->H, (27,6) .->H, (1,8) .->H, (10,13) .->H, (10,14) .->H

## Level 85: winnable, 8 cells changed

- ladder at column 19, rows 10-10
- ladder at column 6, rows 9-12
- ladder at column 6, rows 3-5
- cells: (6,3) .->H, (6,4) .->H, (6,5) .->H, (6,9) .->H, (6,10) .->H, (19,10) -->H, (6,11) .->H, (6,12) .->H

## Level 86: winnable, 2 cells changed

- ladder at column 15, rows 8-8
- ladder at column 22, rows 13-13
- cells: (15,8) .->H, (22,13) .->H

## Level 87: winnable, 14 cells changed

- ladder at column 11, rows 14-14
- ladder at column 3, rows 13-14
- ladder at column 0, rows 4-8
- ladder at column 5, rows 3-3
- ladder at column 14, rows 5-5
- ladder at column 15, rows 4-4
- ladder at column 17, rows 12-13
- exit: ladder at column 24, rows 10-10
- cells: (5,3) .->H, (0,4) .->H, (15,4) .->H, (0,5) .->H, (14,5) -->H, (0,6) .->H, (0,7) .->H, (0,8) .->H, (24,10) .->H, (17,12) .->H, (3,13) .->H, (17,13) .->H, (3,14) .->H, (11,14) .->H

## Level 88: winnable, 4 cells changed

- ladder at column 18, rows 14-14
- ladder at column 24, rows 8-8
- ladder at column 11, rows 7-7
- exit: hidden ladder at column 20, rows 1-1
- cells: (20,1) .->X, (11,7) .->H, (24,8) .->H, (18,14) .->H

## Level 89: winnable, 6 cells changed

- ladder at column 17, rows 12-12
- ladder at column 10, rows 11-11
- ladder at column 9, rows 3-3
- ladder at column 12, rows 5-5
- exit: ladder at column 16, rows 9-9 (gold moved aside)
- cells: (9,3) .->H, (12,5) .->H, (16,8) .->$, (16,9) $->H, (10,11) .->H, (17,12) .->H

## Level 90: winnable, 8 cells changed

- ladder at column 18, rows 12-12
- ladder at column 1, rows 11-14
- ladder at column 8, rows 5-7 (1 through brick)
- cells: (8,5) #->H, (8,6) .->H, (8,7) .->H, (1,11) .->H, (1,12) .->H, (18,12) .->H, (1,13) .->H, (1,14) .->H

## Level 91: winnable, 3 cells changed

- ladder at column 2, rows 9-9
- ladder at column 4, rows 5-5
- ladder at column 25, rows 2-2
- cells: (25,2) .->H, (4,5) .->H, (2,9) .->H

## Level 92: winnable, 10 cells changed

- ladder at column 1, rows 14-14
- ladder at column 11, rows 10-13 (1 through brick)
- ladder at column 17, rows 7-9 (2 through brick)
- ladder at column 16, rows 5-6 (2 through brick)
- cells: (16,5) #->H, (16,6) #->H, (17,7) #->H, (17,8) #->H, (17,9) .->H, (11,10) #->H, (11,11) .->H, (11,12) .->H, (11,13) .->H, (1,14) .->H

## Level 93: winnable, 7 cells changed

- ladder at column 5, rows 14-14
- ladder at column 10, rows 12-12
- ladder at column 3, rows 3-4
- ladder at column 22, rows 12-14 (2 through brick)
- cells: (3,3) .->H, (3,4) .->H, (10,12) .->H, (22,12) .->H, (22,13) #->H, (5,14) .->H, (22,14) #->H

## Level 94: winnable, 6 cells changed

- ladder at column 5, rows 14-14
- ladder at column 6, rows 6-6
- ladder at column 24, rows 0-0
- ladder at column 17, rows 4-4
- ladder at column 21, rows 3-4
- cells: (24,0) .->H, (21,3) .->H, (17,4) .->H, (21,4) .->H, (6,6) .->H, (5,14) .->H

## Level 95: winnable, 1 cell changed

- ladder at column 12, rows 11-11
- cells: (12,11) .->H

## Level 96: winnable, 5 cells changed

- ladder at column 5, rows 5-7
- ladder at column 21, rows 3-4
- cells: (21,3) .->H, (21,4) .->H, (5,5) .->H, (5,6) .->H, (5,7) .->H

## Level 97: winnable, 15 cells changed

- ladder at column 15, rows 1-1
- ladder at column 17, rows 11-11
- ladder at column 18, rows 14-14
- ladder at column 23, rows 2-2
- ladder at column 4, rows 5-14
- exit: ladder at column 22, rows 13-13
- cells: (15,1) .->H, (23,2) .->H, (4,5) .->H, (4,6) .->H, (4,7) .->H, (4,8) .->H, (4,9) .->H, (4,10) .->H, (4,11) .->H, (17,11) .->H, (4,12) .->H, (4,13) .->H, (22,13) .->H, (4,14) .->H, (18,14) .->H

## Level 98: winnable, 15 cells changed

- ladder at column 1, rows 13-14 (1 through brick)
- ladder at column 1, rows 11-12
- ladder at column 7, rows 10-10
- ladder at column 13, rows 14-14
- ladder at column 13, rows 13-13
- ladder at column 26, rows 0-0
- ladder at column 19, rows 8-8
- ladder at column 8, rows 14-14 (1 through brick)
- ladder at column 18, rows 3-7
- cells: (26,0) .->H, (18,3) .->H, (18,4) .->H, (18,5) .->H, (18,6) .->H, (18,7) .->H, (19,8) .->H, (7,10) .->H, (1,11) .->H, (1,12) .->H, (1,13) .->H, (13,13) .->H, (1,14) #->H, (8,14) #->H, (13,14) .->H

## Level 99: winnable, 7 cells changed

- ladder at column 24, rows 14-14
- ladder at column 20, rows 5-5
- ladder at column 9, rows 8-9
- ladder at column 22, rows 3-5
- cells: (22,3) .->H, (22,4) .->H, (20,5) .->H, (22,5) .->H, (9,8) X->H, (9,9) X->H, (24,14) .->H

## Level 100: winnable, 12 cells changed

- ladder at column 17, rows 12-14
- ladder at column 15, rows 11-11 (1 through brick)
- ladder at column 9, rows 8-11
- ladder at column 12, rows 4-4
- ladder at column 22, rows 5-5
- ladder at column 12, rows 5-5 (1 through brick)
- exit: ladder at column 12, rows 6-6
- cells: (12,4) .->H, (12,5) #->H, (22,5) .->H, (12,6) .->H, (9,8) -->H, (9,9) .->H, (9,10) .->H, (9,11) .->H, (15,11) #->H, (17,12) .->H, (17,13) .->H, (17,14) .->H

## Level 101: winnable, 4 cells changed

- ladder at column 13, rows 8-8
- ladder at column 18, rows 3-5
- cells: (18,3) .->H, (18,4) .->H, (18,5) .->H, (13,8) .->H

## Level 102: winnable, 9 cells changed

- ladder at column 1, rows 14-14 (1 through brick)
- ladder at column 4, rows 13-13
- ladder at column 10, rows 10-10
- ladder at column 6, rows 9-10
- ladder at column 19, rows 7-7 (1 through brick)
- exit: ladder at column 19, rows 8-8 (gold moved aside); ladder at column 25, rows 5-5
- cells: (25,5) .->H, (19,6) .->$, (19,7) #->H, (19,8) $->H, (6,9) .->H, (6,10) .->H, (10,10) .->H, (4,13) .->H, (1,14) #->H

## Level 103: winnable, 6 cells changed

- ladder at column 3, rows 10-10
- ladder at column 8, rows 3-5
- exit: ladder at column 9, rows 12-12; hidden ladder at column 4, rows 1-1
- cells: (4,1) .->X, (8,3) .->H, (8,4) .->H, (8,5) .->H, (3,10) .->H, (9,12) .->H

## Level 104: winnable, 10 cells changed

- ladder at column 2, rows 14-14
- ladder at column 23, rows 6-6
- ladder at column 7, rows 4-10
- exit: ladder at column 5, rows 14-14 (1 through brick)
- cells: (7,4) .->H, (7,5) .->H, (7,6) .->H, (23,6) .->H, (7,7) .->H, (7,8) .->H, (7,9) .->H, (7,10) .->H, (2,14) .->H, (5,14) #->H

## Level 105: winnable, 8 cells changed

- ladder at column 6, rows 13-14
- ladder at column 10, rows 12-12 (1 through brick)
- ladder at column 12, rows 3-3
- ladder at column 15, rows 2-2
- ladder at column 21, rows 8-8
- ladder at column 20, rows 12-12
- exit: ladder at column 22, rows 7-7
- cells: (15,2) .->H, (12,3) X->H, (22,7) .->H, (21,8) .->H, (10,12) #->H, (20,12) -->H, (6,13) .->H, (6,14) .->H

## Level 106: winnable, 4 cells changed

- ladder at column 23, rows 9-9
- ladder at column 19, rows 3-4
- exit: hidden ladder at column 4, rows 1-1
- cells: (4,1) .->X, (19,3) .->H, (19,4) .->H, (23,9) .->H

## Level 107: winnable, 4 cells changed

- ladder at column 8, rows 11-11
- ladder at column 9, rows 10-10
- ladder at column 13, rows 7-8
- cells: (13,7) .->H, (13,8) -->H, (9,10) .->H, (8,11) .->H

## Level 108: winnable, 4 cells changed

- ladder at column 9, rows 7-7
- ladder at column 12, rows 3-3
- ladder at column 15, rows 11-12
- cells: (12,3) -->H, (9,7) .->H, (15,11) .->H, (15,12) .->H

## Level 109: winnable, 2 cells changed

- ladder at column 20, rows 3-3
- ladder at column 4, rows 7-7
- cells: (20,3) .->H, (4,7) .->H

## Level 110: winnable, 12 cells changed

- ladder at column 24, rows 8-8
- ladder at column 24, rows 6-6
- ladder at column 20, rows 6-6
- ladder at column 19, rows 3-5
- ladder at column 16, rows 2-2
- ladder at column 8, rows 5-12
- cells: (16,2) .->H, (19,3) .->H, (19,4) .->H, (8,5) .->H, (19,5) .->H, (8,6) .->H, (20,6) .->H, (24,6) .->H, (24,8) .->H, (8,10) -->H, (8,11) .->H, (8,12) .->H

## Level 111: winnable, 8 cells changed

- ladder at column 8, rows 12-12
- ladder at column 9, rows 5-7 (2 through brick)
- ladder at column 13, rows 3-6
- cells: (13,3) .->H, (13,4) .->H, (9,5) #->H, (13,5) .->H, (9,6) #->H, (13,6) .->H, (9,7) .->H, (8,12) .->H

## Level 112: winnable, 10 cells changed

- ladder at column 6, rows 7-8
- ladder at column 23, rows 5-6
- ladder at column 19, rows 4-4
- ladder at column 19, rows 3-3 (1 through brick)
- ladder at column 15, rows 10-11 (1 through brick)
- exit: hidden ladder at column 10, rows 1-2
- cells: (10,1) .->X, (10,2) .->X, (19,3) #->H, (19,4) .->H, (23,5) .->H, (23,6) .->H, (6,7) .->H, (6,8) .->H, (15,10) #->H, (15,11) .->H

## Level 113: winnable, 10 cells changed

- ladder at column 4, rows 14-14
- ladder at column 7, rows 13-13
- ladder at column 11, rows 9-9
- ladder at column 16, rows 10-10
- ladder at column 16, rows 14-14
- ladder at column 13, rows 5-8 (1 through brick)
- exit: ladder at column 5, rows 14-14 (1 through brick)
- cells: (13,5) .->H, (13,6) -->H, (13,7) #->H, (13,8) .->H, (11,9) .->H, (16,10) .->H, (7,13) .->H, (4,14) .->H, (5,14) #->H, (16,14) .->H

## Level 114: winnable, 6 cells changed

- ladder at column 12, rows 11-11
- ladder at column 7, rows 9-9
- ladder at column 11, rows 3-4
- ladder at column 20, rows 11-12
- cells: (11,3) .->H, (11,4) .->H, (7,9) .->H, (12,11) .->H, (20,11) .->H, (20,12) -->H

## Level 115: winnable, 12 cells changed

- ladder at column 3, rows 14-14
- ladder at column 3, rows 6-6
- ladder at column 20, rows 10-10
- ladder at column 7, rows 5-6
- ladder at column 22, rows 3-3
- ladder at column 7, rows 3-4
- ladder at column 13, rows 13-14 (2 through brick)
- exit: ladder at column 11, rows 13-13 (gold moved aside)
- cells: (7,3) .->H, (22,3) .->H, (7,4) .->H, (7,5) .->H, (3,6) .->H, (7,6) .->H, (20,10) .->H, (11,12) .->$, (11,13) $->H, (13,13) #->H, (3,14) .->H, (13,14) #->H

## Level 116: winnable, 9 cells changed

- ladder at column 21, rows 5-7
- ladder at column 18, rows 3-4 (1 through brick)
- ladder at column 5, rows 4-7
- cells: (18,3) #->H, (5,4) .->H, (18,4) .->H, (5,5) .->H, (21,5) .->H, (5,6) .->H, (21,6) .->H, (5,7) .->H, (21,7) .->H

## Level 117: winnable, 7 cells changed

- ladder at column 25, rows 13-14
- ladder at column 24, rows 11-12 (1 through brick)
- ladder at column 21, rows 10-10
- ladder at column 17, rows 2-2
- exit: hidden ladder at column 10, rows 1-1
- cells: (10,1) .->X, (17,2) .->H, (21,10) .->H, (24,11) #->H, (24,12) .->H, (25,13) .->H, (25,14) .->H

## Level 118: winnable, 3 cells changed

- ladder at column 19, rows 11-11
- ladder at column 19, rows 9-9 (1 through brick)
- exit: ladder at column 21, rows 3-3 (1 through brick)
- cells: (21,3) #->H, (19,9) #->H, (19,11) .->H

## Level 119: winnable, 12 cells changed

- ladder at column 5, rows 13-14
- ladder at column 15, rows 13-13
- ladder at column 19, rows 7-8 (1 through brick)
- ladder at column 18, rows 6-6
- ladder at column 24, rows 3-5
- ladder at column 15, rows 3-3
- exit: ladder at column 22, rows 11-11; ladder at column 22, rows 2-2
- cells: (22,2) .->H, (15,3) .->H, (24,3) .->H, (24,4) .->H, (24,5) .->H, (18,6) .->H, (19,7) .->H, (19,8) #->H, (22,11) .->H, (5,13) .->H, (15,13) .->H, (5,14) .->H

## Level 120: winnable, 5 cells changed

- ladder at column 5, rows 14-14
- ladder at column 5, rows 7-7
- ladder at column 11, rows 6-6
- ladder at column 3, rows 3-4
- cells: (3,3) .->H, (3,4) .->H, (11,6) .->H, (5,7) .->H, (5,14) .->H

## Level 121: winnable, 4 cells changed

- ladder at column 11, rows 9-9
- ladder at column 17, rows 14-14
- ladder at column 8, rows 10-10 (1 through brick)
- exit: ladder at column 20, rows 7-7
- cells: (20,7) .->H, (11,9) .->H, (8,10) #->H, (17,14) .->H

## Level 122: winnable, 18 cells changed

- ladder at column 1, rows 13-14 (1 through brick)
- ladder at column 5, rows 10-10 (1 through brick)
- ladder at column 7, rows 13-13
- ladder at column 11, rows 10-10
- ladder at column 11, rows 9-9
- ladder at column 16, rows 14-14
- ladder at column 15, rows 5-5
- ladder at column 14, rows 8-8
- ladder at column 4, rows 4-9
- exit: ladder at column 27, rows 5-7
- cells: (4,4) .->H, (4,5) .->H, (15,5) -->H, (27,5) .->H, (4,6) .->H, (27,6) .->H, (4,7) .->H, (27,7) .->H, (4,8) .->H, (14,8) .->H, (4,9) .->H, (11,9) .->H, (5,10) #->H, (11,10) .->H, (1,13) #->H, (7,13) .->H, (1,14) .->H, (16,14) .->H

## Level 123: winnable, 8 cells changed

- ladder at column 17, rows 7-7
- ladder at column 7, rows 8-9
- ladder at column 10, rows 3-3
- ladder at column 11, rows 4-4 (1 through brick)
- exit: ladder at column 11, rows 5-5 (gold moved aside); ladder at column 18, rows 3-3
- cells: (10,3) .->H, (11,3) .->$, (18,3) .->H, (11,4) #->H, (11,5) $->H, (17,7) .->H, (7,8) .->H, (7,9) .->H

## Level 124: winnable, 13 cells changed

- ladder at column 1, rows 14-14 (gold moved aside)
- ladder at column 8, rows 13-13
- ladder at column 8, rows 4-4
- ladder at column 9, rows 3-3
- ladder at column 18, rows 14-14
- ladder at column 22, rows 6-9
- exit: ladder at column 8, rows 14-14 (gold moved aside); ladder at column 25, rows 6-6
- cells: (9,3) .->H, (8,4) .->H, (22,6) .->H, (25,6) .->H, (22,7) .->H, (22,8) .->H, (22,9) .->H, (1,13) .->$, (7,13) .->$, (8,13) .->H, (1,14) $->H, (8,14) $->H, (18,14) .->H

## Level 125: winnable, 3 cells changed

- ladder at column 10, rows 7-7
- ladder at column 6, rows 3-4
- cells: (6,3) .->H, (6,4) -->H, (10,7) .->H

## Level 126: winnable, 6 cells changed

- ladder at column 12, rows 14-14
- ladder at column 16, rows 10-10
- ladder at column 8, rows 7-7
- ladder at column 17, rows 9-9
- ladder at column 18, rows 4-4
- exit: ladder at column 23, rows 9-9
- cells: (18,4) .->H, (8,7) .->H, (17,9) .->H, (23,9) .->H, (16,10) .->H, (12,14) .->H

## Level 127: winnable, 8 cells changed

- ladder at column 4, rows 11-14
- ladder at column 16, rows 5-5
- ladder at column 6, rows 3-5
- cells: (6,3) .->H, (6,4) .->H, (6,5) .->H, (16,5) .->H, (4,11) .->H, (4,12) .->H, (4,13) .->H, (4,14) .->H

## Level 128: winnable, 7 cells changed

- ladder at column 8, rows 14-14
- ladder at column 20, rows 9-9 (1 through brick)
- ladder at column 17, rows 8-8 (1 through brick)
- ladder at column 10, rows 5-8
- cells: (10,5) .->H, (10,6) .->H, (10,7) .->H, (10,8) .->H, (17,8) #->H, (20,9) #->H, (8,14) .->H

## Level 129: winnable, 7 cells changed

- ladder at column 24, rows 11-11 (1 through brick)
- ladder at column 25, rows 10-10
- ladder at column 21, rows 5-5
- ladder at column 19, rows 4-4
- ladder at column 11, rows 7-7
- exit: ladder at column 11, rows 8-8 (gold moved aside)
- cells: (19,4) .->H, (21,5) .->H, (11,6) .->$, (11,7) .->H, (11,8) $->H, (25,10) .->H, (24,11) #->H

## Level 130: winnable, 5 cells changed

- ladder at column 9, rows 14-14
- ladder at column 10, rows 10-10
- ladder at column 9, rows 9-9
- ladder at column 17, rows 14-14
- ladder at column 23, rows 0-0
- cells: (23,0) .->H, (9,9) .->H, (10,10) .->H, (9,14) .->H, (17,14) .->H

## Level 131: winnable, 3 cells changed

- ladder at column 9, rows 14-14
- ladder at column 17, rows 13-13
- exit: ladder at column 16, rows 14-14 (1 through brick)
- cells: (17,13) .->H, (9,14) .->H, (16,14) #->H

## Level 132: winnable, 4 cells changed

- ladder at column 16, rows 7-7
- ladder at column 9, rows 6-6
- ladder at column 5, rows 7-7
- ladder at column 4, rows 2-2
- cells: (4,2) .->H, (9,6) .->H, (5,7) -->H, (16,7) .->H

## Level 133: winnable, 6 cells changed

- ladder at column 12, rows 4-4
- ladder at column 15, rows 4-4
- ladder at column 1, rows 9-10
- ladder at column 19, rows 6-7 (1 through brick)
- cells: (12,4) .->H, (15,4) .->H, (19,6) .->H, (19,7) #->H, (1,9) .->H, (1,10) .->H

## Level 134: winnable, 15 cells changed

- ladder at column 15, rows 10-10
- ladder at column 16, rows 8-9
- ladder at column 17, rows 7-7
- ladder at column 1, rows 11-14
- ladder at column 4, rows 3-7
- ladder at column 15, rows 3-3
- exit: ladder at column 18, rows 14-14
- cells: (4,3) .->H, (15,3) .->H, (4,4) .->H, (4,5) .->H, (4,6) .->H, (4,7) .->H, (17,7) .->H, (16,8) .->H, (16,9) .->H, (15,10) .->H, (1,11) X->H, (1,12) X->H, (1,13) X->H, (1,14) X->H, (18,14) .->H

## Level 135: winnable, 1 cell changed

- exit: hidden ladder at column 16, rows 1-1
- cells: (16,1) .->X

## Level 136: winnable, 5 cells changed

- ladder at column 6, rows 13-14
- ladder at column 8, rows 6-6
- ladder at column 9, rows 5-5
- ladder at column 21, rows 3-3
- cells: (21,3) .->H, (9,5) .->H, (8,6) .->H, (6,13) .->H, (6,14) .->H

## Level 137: winnable, 6 cells changed

- ladder at column 5, rows 14-14
- ladder at column 7, rows 7-7
- ladder at column 18, rows 11-11
- ladder at column 19, rows 10-10
- ladder at column 26, rows 0-0
- ladder at column 18, rows 4-4
- cells: (26,0) .->H, (18,4) .->H, (7,7) .->H, (19,10) .->H, (18,11) .->H, (5,14) .->H

## Level 138: winnable, 4 cells changed

- ladder at column 23, rows 9-9 (1 through brick)
- ladder at column 21, rows 8-8
- ladder at column 1, rows 10-11
- cells: (21,8) .->H, (23,9) #->H, (1,10) .->H, (1,11) .->H

## Level 139: winnable, 4 cells changed

- ladder at column 2, rows 14-14
- ladder at column 15, rows 4-4
- ladder at column 9, rows 3-4
- cells: (9,3) .->H, (9,4) .->H, (15,4) .->H, (2,14) .->H

## Level 140: winnable, 5 cells changed

- ladder at column 20, rows 9-9
- ladder at column 7, rows 10-10
- ladder at column 4, rows 6-6
- ladder at column 12, rows 6-6
- ladder at column 18, rows 6-6 (1 through brick)
- cells: (4,6) .->H, (12,6) .->H, (18,6) #->H, (20,9) .->H, (7,10) -->H

## Level 141: winnable, 5 cells changed

- ladder at column 21, rows 4-4
- ladder at column 20, rows 11-14
- cells: (21,4) -->H, (20,11) .->H, (20,12) .->H, (20,13) .->H, (20,14) .->H

## Level 142: winnable, 10 cells changed

- ladder at column 3, rows 10-10
- ladder at column 8, rows 14-14
- ladder at column 14, rows 7-7
- ladder at column 14, rows 9-9
- ladder at column 7, rows 3-7
- exit: ladder at column 22, rows 3-3
- cells: (7,3) .->H, (22,3) .->H, (7,4) .->H, (7,5) .->H, (7,6) .->H, (7,7) -->H, (14,7) .->H, (14,9) .->H, (3,10) .->H, (8,14) .->H

## Level 143: winnable, 5 cells changed

- ladder at column 6, rows 14-14
- ladder at column 22, rows 13-13
- ladder at column 19, rows 8-8
- ladder at column 1, rows 6-6
- ladder at column 25, rows 8-8
- cells: (1,6) .->H, (19,8) X->H, (25,8) .->H, (22,13) -->H, (6,14) .->H

## Level 144: winnable, 4 cells changed

- ladder at column 11, rows 14-14
- ladder at column 27, rows 14-14
- ladder at column 18, rows 7-7 (1 through brick)
- ladder at column 15, rows 6-6
- cells: (15,6) .->H, (18,7) #->H, (11,14) .->H, (27,14) .->H

## Level 145: winnable, 10 cells changed

- ladder at column 17, rows 12-14
- ladder at column 14, rows 7-9
- ladder at column 6, rows 8-9
- ladder at column 6, rows 3-3
- exit: ladder at column 22, rows 8-8
- cells: (6,3) .->H, (14,7) .->H, (6,8) .->H, (14,8) .->H, (22,8) .->H, (6,9) .->H, (14,9) .->H, (17,12) .->H, (17,13) .->H, (17,14) .->H

## Level 146: winnable, 10 cells changed

- ladder at column 4, rows 14-14; ladder at column 7, rows 13-13
- ladder at column 10, rows 7-9
- ladder at column 9, rows 3-6
- exit: ladder at column 26, rows 14-14
- cells: (9,3) .->H, (9,4) .->H, (9,5) .->H, (9,6) .->H, (10,7) .->H, (10,8) .->H, (10,9) .->H, (7,13) .->H, (4,14) .->H, (26,14) .->H

## Level 147: winnable, 3 cells changed

- ladder at column 21, rows 3-4
- exit: ladder at column 23, rows 4-4
- cells: (21,3) .->H, (21,4) .->H, (23,4) .->H

## Level 148: winnable, 3 cells changed

- ladder at column 2, rows 10-10
- ladder at column 0, rows 4-5
- cells: (0,4) .->H, (0,5) .->H, (2,10) .->H

## Level 149: winnable, 13 cells changed

- ladder at column 20, rows 9-9
- ladder at column 19, rows 8-8
- ladder at column 17, rows 11-11
- ladder at column 16, rows 6-6
- ladder at column 16, rows 5-5 (1 through brick)
- ladder at column 17, rows 4-4
- ladder at column 8, rows 4-6
- ladder at column 0, rows 6-6
- ladder at column 11, rows 11-12 (1 through brick)
- exit: hidden ladder at column 0, rows 1-1
- cells: (0,1) .->X, (8,4) .->H, (17,4) .->H, (8,5) .->H, (16,5) #->H, (0,6) .->H, (8,6) .->H, (16,6) .->H, (19,8) .->H, (20,9) .->H, (11,11) .->H, (17,11) .->H, (11,12) #->H

## Level 150: winnable, 7 cells changed

- ladder at column 12, rows 13-14
- ladder at column 26, rows 0-0
- ladder at column 1, rows 12-14
- ladder at column 18, rows 5-5
- cells: (26,0) .->H, (18,5) .->H, (1,12) .->H, (1,13) .->H, (12,13) .->H, (1,14) .->H, (12,14) .->H

