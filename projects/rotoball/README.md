# Rotoball (3DO)

A 3DO port of Rotoball, the split-screen futuristic ball sport from
[geekychris/amiga_games](https://github.com/geekychris/amiga_games)
(`rotoball/`): you and the computer each pilot a hovering skimmer over a
chequered pitch; carry the ball over the other goal line before the 90
seconds run out. The top half of the screen is the computer's view, the
bottom half yours.

![playing](../../docs/images/games/rotoball-play.png)

## Playing

| Pad | |
|---|---|
| UP or A / DOWN or B | drive forward / back |
| LEFT / RIGHT | strafe |
| A or P | start the match (kick-off screen), play again (full time) |
| X | back to the arcade menu |

## How it was ported

`src/main.c`, `pitch.c` and `sound.c` are the Amiga sources with
`AMIGA3DO` branches: the sdk/amiga layer's 32-colour screen, the pad, the
game logic at 50 steps a second (`gfx_steps`), and the chequered floor drawn
by the cel engine under the frame (`floor_cels.c`).

## Fixes (October 2026, the same as in the Amiga original)

- **Match clock**: it counted frames at an assumed 25 a second, so at the
  port's 50 steps a second the match was over in 45 seconds. It now counts
  1/50 s units (one per step here; real time on the Amiga): 90 seconds.
- **Full time**: play freezes under the result banner (the skimmers kept
  moving and goals kept counting).
- **Start / restart**: a kick-off screen at launch, A starts; after full
  time A plays again.
- **Tackles**: a tackle locks the ball to its new holder until the
  skimmers have separated, so two skimmers in contact no longer pass it
  back and forth at every cooldown expiry.
- The +Z comment in `rotoball.h` and the `-Wmisleading-indentation` lines
  are fixed as upstream.

## Testing

`./3do test rotoball`: kick-off screen (frozen), A starts, the pad moves the
skimmer (positions read from memory at the addresses the game logs), game
speed (50 steps/s), a full match that must last 90 emulated seconds, frozen
full time, A plays again, X quits.
