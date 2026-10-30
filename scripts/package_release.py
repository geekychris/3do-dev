#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Package the 3DO games for download: one zip per game holding its disc
image (<id>.iso) and a ReadMe, and manifest.json, which the website's
/games/ page is made from.

  python3 scripts/package_release.py [--out dist/release] [--tag games-YYYY-MM-DD]

Uses the disc images already built in projects/<id>/build/ (./3do build);
tests and demos are left out.
"""
import argparse
import datetime
import json
import os
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "geekychris/3do-dev"
SKIP = {"demo", "hello", "paula_test", "unittest"}
TITLES = {"rj_birthday": "RJ's Birthday Bash", "arcade": "The Arcade (every game on one disc)", "ace_pilot": "Ace Pilot",
          "stakattack": "StakAttack"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "release"))
    ap.add_argument("--tag", default="games-" + datetime.date.today().isoformat())
    opt = ap.parse_args()
    if os.path.exists(opt.out):
        shutil.rmtree(opt.out)
    os.makedirs(opt.out)
    games = []
    for gid in sorted(os.listdir(os.path.join(ROOT, "projects"))):
        iso = os.path.join(ROOT, "projects", gid, "build", gid + ".iso")
        if gid in SKIP or not os.path.isfile(iso):
            continue
        title = TITLES.get(gid, gid.replace("_", " ").title())
        name = f"{gid}-3do.zip"
        with zipfile.ZipFile(os.path.join(opt.out, name), "w", zipfile.ZIP_DEFLATED) as z:
            z.write(iso, f"{gid}/{gid}.iso")
            z.writestr(f"{gid}/ReadMe.txt", "\n".join([
                title, "=" * len(title), "",
                "A disc image for the 3DO Interactive Multiplayer.", "",
                "To play: open " + gid + ".iso in a 3DO emulator (Opera, in RetroArch or on",
                "its own, with a 3DO BIOS you own). On real hardware a homebrew disc only",
                "boots on a console set up to run one.", "",
                f"Source: https://github.com/{REPO}/tree/main/projects/{gid}", "MIT licence.", ""]))
        games.append({"id": gid, "title": title, "packages": {"3do": {
            "file": name, "bytes": os.path.getsize(os.path.join(opt.out, name)),
            "url": f"https://github.com/{REPO}/releases/download/{opt.tag}/{name}"}}})
    with open(os.path.join(opt.out, "manifest.json"), "w") as fh:
        json.dump({"repo": REPO, "tag": opt.tag, "platforms": ["3do"], "games": games}, fh, indent=1)
    print(f"{len(games)} discs -> {opt.out}")


if __name__ == "__main__":
    main()
