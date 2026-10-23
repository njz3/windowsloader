# windowsloader - runs Windows arcade games with emulated cabinet hardware
# Copyright (C) 2026 Benjamin Maurin - njz3
# License: GNU General Public License version 3 (GPL v3)
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU General Public License version 3 as published by the Free Software Foundation.
# It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
# the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
# file for the full text. The author reserves the right to change the license of this code.
#
# Code taken from MAME, linuxloader, sm2-emu, Flycast or segatools stays under the license of
# its own project; such code is marked with its origin.

"""A game from its attract mode to a race: coins (when the game takes them), Start every 3 s
through the menus (the default choices), then the accelerator held and the wheel moved; a
picture every few seconds, a contact sheet at the end and the force feedback changes seen.

    python scripts/agent/race_check.py GAME [MENUS DRIVE] [wheel]

GAME: bg4 (Battle Gear 4 Tuned), bg4eng (Battle Gear 4, 2006), d1gp (D1GP Arcade), vlimitr (Valve Limit R), chq2 (Chase H.Q. 2), showdown (DiRT Showdown) or gticlub3
(GTI Club: Supermini Festa!), copied in games/ as BILAN.md says. MENUS and DRIVE: seconds of Start presses and of driving (40 and 30).
wheel: the controllers are opened too (launcher --controllers), the force feedback goes to the
player 1 controller (vJoy on the test PC: warn its other users first).
Environment: PATTERN, the wheel's positions one per STEP seconds, -1..1, +1 the wheel fully left
(default "0,0,0.5,0.5,0,0,-0.5,-0.5"); STEP (1); BOOT, the seconds before the first coin.
The pictures and the log go to logs/race-GAME/, the sheet to logs/race-GAME/sheet.png.
"""
import os
import sys
import time

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAMES = {
    # name: path in games/, the cabinet's arguments, seconds of boot, coins taken
    "bg4": (("bg4tp", "game.exe"), ("_MTS_FULL_SCREEN_",), 30, True),
    "bg4eng": (("bg4", "game.exe"), ("_MTS_FULL_SCREEN_",), 45, True),
    "d1gp": (("d1gp", "game.exe"), (), 85, True),
    "vlimitr": (("vlimitr", "launcher.exe"), (), 20, True),
    "chq2": (("chasehq2", "game.exe"), (), 45, True),
    "showdown": (("showdown", "Showdown", "Showdown.exe"), (), 40, False),
    "gticlub3": (("gticlub3", "gti3.exe"), (), 30, False),
}

name = sys.argv[1] if len(sys.argv) > 1 else ""
if name not in GAMES:
    sys.exit("usage: race_check.py %s [MENUS DRIVE] [wheel]" % "|".join(GAMES))
path, game_args, boot, coins = GAMES[name]
numbers = [int(a) for a in sys.argv[2:] if a.isdigit()]
menus = numbers[0] if len(numbers) > 0 else 40
drive = numbers[1] if len(numbers) > 1 else 30
wheel = "wheel" in sys.argv[2:]
pattern = [float(x) for x in os.environ.get("PATTERN", "0,0,0.5,0.5,0,0,-0.5,-0.5").split(",")]
step = float(os.environ.get("STEP", "1"))
boot = int(os.environ.get("BOOT", boot))

OUT = os.path.join(ROOT, "logs", "race-" + name)
os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    if f.endswith(".bmp"):
        os.remove(os.path.join(OUT, f))
shots = []


def shot(game, label):
    p = os.path.join(OUT, "%03d-%s.bmp" % (len(shots), label))
    try:
        game.shot(p)
    except AgentError as e:  # a frame between two (a loading screen): the next picture will do
        print("no picture %s: %s" % (label, e), flush=True)
        return
    shots.append((label, p))


def tap(game, input_name, hold=0.3):
    game.set(input_name, 1)
    time.sleep(hold)
    game.release(input_name)


extra = ("--timeout", str(boot + menus + drive + 120)) + (("--controllers",) if wheel else ())
with WLAgent(LAUNCHER, os.path.join(ROOT, "games", *path), log=os.path.join(OUT, "race.log"), extra_args=extra, game_args=game_args) as game:
    time.sleep(boot)
    shot(game, "demo")
    if coins:
        for _ in range(2):
            tap(game, "Coin")
            time.sleep(2)
    shot(game, "coins")
    t0 = time.time()
    n = 0
    while time.time() - t0 < menus:
        tap(game, "Start")
        time.sleep(2.7)
        n += 1
        shot(game, "start%d" % n)
    t0 = time.time()
    k = 0
    while time.time() - t0 < drive:
        game.set("Accelerator", 1)
        game.set("Steering", pattern[k % len(pattern)])
        time.sleep(step)
        k += 1
        if k % max(1, int(3 / step)) == 0:
            shot(game, "drive%d" % k)
    game.release("all")
    events = game.ffb()
    print("force feedback: %d changes, the first %s" % (len(events), events[:5]), flush=True)

ims = []
for label, p in shots:
    im = Image.open(p).convert("RGB").resize((320, 240))
    ImageDraw.Draw(im).text((4, 4), label, fill=(255, 255, 0))
    ims.append(im)
cols = 6
sheet = Image.new("RGB", (320 * cols, 240 * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % cols) * 320, (i // cols) * 240))
sheet.save(os.path.join(OUT, "sheet.png"))
print("sheet:", os.path.join(OUT, "sheet.png"), len(ims), "pictures")
