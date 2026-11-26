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

"""Global VR's races (NASCAR Team Racing, NASCAR Racing, Need for Speed Underground): each control
held in a race, a picture after each, the force feedback and the lamps from the DLL's log.

    python scripts/agent/gvr_controls.py nascar|nascar11|nfsu [wait frames]

Pictures and log in logs/gvr-<game>/ (PNG, half size).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAMES = {
    "nascar": os.path.join(ROOT, "games", "nascar-team-racing", "NASCAR", "Game", "NASCAR_GVR.exe"),
    "nascar11": os.path.join(ROOT, "games", "nascar-racing", "NASCAR", "Game", "NASCAR_GVR.exe"),
    "nfsu": os.path.join(ROOT, "games", "nfsu", "Underground", "UndergroundGVR.exe"),
}
name = sys.argv[1]
wait = int(sys.argv[2]) if len(sys.argv) > 2 else 900
OUT = os.path.join(ROOT, "logs", "gvr-" + name)
os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(OUT, "game.log")


def png(bmp):
    try:
        from PIL import Image
        im = Image.open(bmp)
        im = im.resize((im.width // 2, im.height // 2))
        im.save(bmp[:-4] + ".png")
        os.remove(bmp)
    except Exception as e:  # keep the BMP
        print("  (png: %s)" % e)


def log_size():
    try:
        return os.path.getsize(LOG)
    except OSError:
        return 0


def new_log(since):
    with open(LOG, "rb") as f:
        f.seek(since)
        return f.read().decode("utf-8", "replace").splitlines()


def show(lines, label):
    ffb = [l for l in lines if "ffb: effects" in l]
    other = [l for l in lines if re.search(r"(nascar|nfsu): (gear|buttons|button|GvrIO message|nascar:)", l) or "onGvr" in l]
    print("%-12s ffb %d changes%s" % (label, len(ffb), (": last " + ffb[-1].split("effects: ", 1)[1]) if ffb else ""))
    for l in other[:12]:
        print("    " + l.split("] ", 2)[-1][:150])


def phase(game, label, holds, frames):
    since = log_size()
    for n, v in holds:
        game.set(n, v)
    game.step(frames)
    game.shot(os.path.join(OUT, label + ".bmp"))
    for n, _ in holds:
        game.release(n)
    game.step(20)
    png(os.path.join(OUT, label + ".bmp"))
    show(new_log(since), label)


with WLAgent(LAUNCHER, GAMES[name], log=LOG, extra_args=["--set", "Display.PRESENT=DIRECT"]) as game:
    print("ready", game.game, game.width, game.height, "inputs:", ", ".join(n for n, _ in game.inputs()), flush=True)
    game.step(wait)
    game.shot(os.path.join(OUT, "00-start.bmp"))
    png(os.path.join(OUT, "00-start.bmp"))
    game.press("Start", 15)
    game.step(300)
    phase(game, "01-gas", [("Accelerator", 1)], 400)
    phase(game, "02-gas-left", [("Accelerator", 1), ("Steering", 1)], 150)
    phase(game, "03-gas-right", [("Accelerator", 1), ("Steering", -1)], 150)
    phase(game, "04-brake", [("Brake", 1)], 150)
    for button in ("View", "LookBack", "Nitrous" if name == "nfsu" else "Nos", "Music", "ShiftUp", "ShiftUp", "ShiftDown", "Ebrake"):
        try:
            phase(game, "05-" + button, [("Accelerator", 1), (button, 1)], 60)
        except Exception as e:
            print("  %s: %s" % (button, e))
    phase(game, "06-start", [("Start", 1)], 30)
    game.quit()
