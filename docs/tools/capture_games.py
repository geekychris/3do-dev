"""Regenerate docs/games.md and docs/images/games/ (the arcade + every game).

    ./3do selftest 2>&1 | tee /tmp/selftest.log     # tests save build/test-*.png
    .venv/bin/python docs/tools/capture_games.py [/tmp/selftest.log]

Game screenshots come from each project's test (projects/<g>/build/test-*.png,
the title screen plus one in-game shot). The arcade menu is captured here by
booting projects/arcade/build/arcade.iso. With a selftest log, the page also
lists the measured game speed and drawing rate.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tdo" / "src"))
from PIL import Image                      # noqa: E402
from tdo.core import Emulator              # noqa: E402

OUT = ROOT / "docs" / "images" / "games"
PAGE = ROOT / "docs" / "games.md"


def games():
    out = []
    for f in sorted(ROOT.glob("projects/*/arcade.txt")):
        meta = dict(l.split("=", 1) for l in f.read_text().splitlines() if "=" in l)
        meta["name"] = f.parent.name
        out.append(meta)
    return sorted(out, key=lambda m: int(m.get("order", 999)))


def save(src, dst):
    """Native 320x240 PNG (the test shots are 2x)."""
    Image.open(src).convert("RGB").resize((320, 240), Image.NEAREST).save(dst, optimize=True)


def capture_arcade():
    e = Emulator()
    e.load(str(ROOT / "projects/arcade/build/arcade.iso"))
    e.run_until_log("ARCADE: menu", 2000)
    e.step(40)
    e.screenshot(str(OUT / "arcade-menu.png"), scale=1)
    for _ in range(len(games()) - 1):      # scroll to the end to show the rest
        e.press(["DOWN"])
        e.step(6)
    e.step(10)
    e.screenshot(str(OUT / "arcade-menu-scrolled.png"), scale=1)
    e.close()


def speeds(log):
    """{game: (logic, drawing)} from a selftest log."""
    out, cur = {}, None
    for line in Path(log).read_text().splitlines():
        m = re.match(r"==> (\S+)", line)
        if m:
            cur = m.group(1)
            continue
        m = re.search(r"PASS (?:game )?speed ([\d.]+) (fps|steps/s)", line)
        if m and cur:
            out[cur] = [float(m.group(1)), float(m.group(1)) if m.group(2) == "fps" else None]
        m = re.search(r"INFO drawing ([\d.]+) fps", line)
        if m and cur in out:
            out[cur][1] = float(m.group(1))
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    capture_arcade()
    spd = speeds(sys.argv[1]) if len(sys.argv) > 1 else {}
    rows, sections = [], []
    for g in games():
        shots = sorted((ROOT / "projects" / g["name"] / "build").glob("test-*.png"))
        title = [s for s in shots if s.stem == "test-title"]
        play = [s for s in shots if s.stem not in ("test-title", "test-gameover")]
        imgs = []
        for kind, src in (("title", title[:1]), ("play", play[:1])):
            if src:
                save(src[0], OUT / f"{g['name']}-{kind}.png")
                imgs.append(f"![{g['title']} {kind}](images/games/{g['name']}-{kind}.png)")
        if g["name"] in spd:
            logic, draw = spd[g["name"]]
            rows.append(f"| [{g['title']}](#{g['name'].replace('_', '-')}) | {logic:.0f} | {draw:.0f} |")
        sections.append(f"### {g['title']}\n<a id=\"{g['name'].replace('_', '-')}\"></a>\n\n"
                        f"{g.get('desc', '')}\n\n{g.get('genre', '')} · `projects/{g['name']}` · from the {g.get('origin', '')} version\n\n"
                        + " ".join(imgs) + "\n")
    page = ["# The games", "",
            "Ports of the Amiga games to the 3DO through the sdk/amiga compatibility",
            "layer, and two Unity games rebuilt for the 3DO's cel engine (Planet Chomp",
            "and Spectral Keep).",
            "Every game is its own disc (`./3do build <game>`), and the arcade",
            "disc (`./3do build arcade`) bundles all of them behind a menu.",
            "Screenshots are from the automated tests; regenerate this page with",
            "`docs/tools/capture_games.py`.", "",
            "## The arcade", "",
            "Up/Down choose, Left/Right page, A plays. In a game, X goes back to the",
            "menu. The bar to the right of the list shows where you are.", "",
            "![arcade menu](images/games/arcade-menu.png) "
            "![arcade menu, scrolled](images/games/arcade-menu-scrolled.png)", ""]
    if rows:
        page += ["## Speed on the 3DO", "",
                 "Every game keeps its Amiga speed: logic runs at 50 steps per second",
                 "(the PAL frame rate), and the 3D games that draw fewer frames catch",
                 "up on logic steps (`gfx_steps()`) rather than slowing down.",
                 "Measured in the emulator by `./3do selftest`.", "",
                 "| Game | logic steps/s | frames drawn/s |", "|---|---|---|"] + rows + [""]
    page += ["## Games", ""] + sections
    PAGE.write_text("\n".join(page))
    print(f"wrote {PAGE} and {len(list(OUT.glob('*.png')))} images")


if __name__ == "__main__":
    main()
