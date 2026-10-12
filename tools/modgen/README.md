# modgen: the games' music and voices

All the music and speech on the game discs that isn't generated inside the
game itself comes from here, so every note and sample is ours: the songs
are written in Python, the instruments are synthesised (no recorded
samples), and the voices are synthetic speech.

| Script | Writes | What it is |
|---|---|---|
| `stakattack.py` | `projects/stakattack/takeme/stakattack/stakattack.mod` | "Stack Bounce": original, bouncy puzzle tune, G major |
| `uranus.py` | `projects/uranus_lander/takeme/uranus_lander/uranus.mod` | "Ring Drift": original slow space-funk, D dorian |
| `orbital_patrol.py` | `projects/orbital_patrol/takeme/orbital_patrol/orbital_patrol.mod` | "Patrol Drive": original driving techno, A minor |
| `rj_party.py` | `projects/rj_birthday/takeme/rj_birthday/party.mod` | "Party Time": original upbeat party tune, C major |
| `rj_birthday.py` | `projects/rj_birthday/takeme/rj_birthday/birthday.mod` | "Happy Birthday to You" (Mildred and Patty Hill, public domain), our own waltz arrangement |
| `arcade_voices.py` | `projects/rj_birthday/takeme/rj_birthday/snd_arcade1..4.raw` | "Game on!", "Ready, player!", "High score!", "Level up!" spoken by Kokoro TTS (Apache-2.0), made to sound like a robot |

Ace Pilot's `blue_danube.mod` (Strauss, public domain) was made the same way
by `gen_mod.py` in the Amiga game's own repo.

## Regenerating

```sh
cd tools/modgen
python3 stakattack.py            # any song script; writes straight into the project
python3 render.py ../../projects/stakattack/takeme/stakattack/stakattack.mod /tmp/stak.wav   # listen on the Mac
python3 arcade_voices.py         # needs kokoro-server running on 127.0.0.1:8770 (KOKORO_URL)
./3do build stakattack           # (from the repo root) put it on the disc
```

`modlib.py` is the writer: `Song`, `Pattern.melody()` / `Pattern.drums()`,
`m("C-2:4 E-2:2 .:2")` for compact note lists, and the synthesised
instruments (`cycle`, `pluck`, `pad`, `kick`, `snare`, `hat`, `clap`, `mix`).
`render.py` is a preview player for the effects the songs use. It isn't a full
ProTracker player.

How the games share channels with sound effects: the effects take channels from
3 downwards, and `mt_MusicChannels` says how many the music keeps. So the songs
put the melody on channel 0 and the bass on 1, and drums and chords on 2 and 3.
Uranus Lander keeps only channel 0, so its melody works on its own.
