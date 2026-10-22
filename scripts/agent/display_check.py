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

"""What the game's window really shows for each aspect ratio and display mode, then in
WINDOW_STRETCH with the window shrunk to other sizes and shapes. For each case: the window's
client size, where the picture is (the part that is not black, from a capture of the window) and
its shape, next to the place windowsloader computed (its log). The display mode is set back to
ORIGINAL WINDOW at the end. Needs a session open on the screen: the window is captured from the
screen by this script, in physical pixels (the game may not know the screen's scale, and the
agent's own "screen" capture then reads the wrong place).

    python scripts/agent/display_check.py [day | day-nse | sr3 | fr | kod]
"""
import ctypes
import ctypes.wintypes as wt
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAMES = {
    "day": (os.path.join(ROOT, "games", "daytonausa", "Sega", "Daytona", "Daytona.exe"), 50),
    "day-nse": (os.path.join(ROOT, "games", "daytonausa-nse", "Sega", "Daytona", "Daytona.exe"), 60),
    "sr3": (os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe"), 15),
    "fr": (os.path.join(ROOT, "games", "fordracing", "FordRacing", "fordracing.exe"), 15),
    "kod": (os.path.join(ROOT, "games", "kodrive", "exe", "M-DriveR_RingWide.exe"), 85),
}
which = sys.argv[1] if len(sys.argv) > 1 else "day"
GAME, START = GAMES[which]
OUT = os.path.join(ROOT, "logs", "display-" + which)
LOG = os.path.join(OUT, "display.log")
os.makedirs(OUT, exist_ok=True)

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
ctypes.windll.shcore.SetProcessDpiAwareness(2)  # per-monitor: physical pixels everywhere


def game_window():
    """The visible top-level window of the game's process (by its executable's name)"""
    exe = os.path.basename(GAME).lower()
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        h = kernel32.OpenProcess(0x1000, False, pid.value)  # PROCESS_QUERY_LIMITED_INFORMATION
        if h:
            buf = ctypes.create_unicode_buffer(512)
            n = wt.DWORD(512)
            if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) and buf.value.lower().endswith("\\" + exe):
                r = wt.RECT()
                user32.GetClientRect(hwnd, ctypes.byref(r))
                if r.right > 100:
                    found.append(hwnd)
            kernel32.CloseHandle(h)
        return True

    user32.EnumWindows(each, 0)
    return found[0] if found else None


def picture_box(path):
    """Size of the capture, and the box of the rows and columns that are mostly not black (a
    window's 1-pixel border line does not count)"""
    import numpy as np
    from PIL import Image

    a = np.asarray(Image.open(path).convert("L")) > 24
    a[0] = False  # the top line of a resizable window is its border, not the picture
    h, w = a.shape
    rows = np.where(a.mean(axis=1) > 0.2)[0]
    cols = np.where(a.mean(axis=0) > 0.2)[0]
    if not len(rows) or not len(cols):
        return (w, h), None
    return (w, h), (int(cols[0]), int(rows[0]), int(cols[-1]) + 1, int(rows[-1]) + 1)


def last_layout():
    """windowsloader's last "display:" line: the window and the picture it computed"""
    lines = [l for l in open(LOG, encoding="utf-8", errors="replace") if "] display: " in l and "picture" in l]
    m = re.search(r"(?:window|client) (\d+)x(\d+).*picture (\d+)x(\d+) at (-?\d+),(-?\d+)", lines[-1]) if lines else None
    return tuple(int(v) for v in m.groups()) if m else None


def capture(path):
    """The game window's client area as the screen shows it"""
    from PIL import ImageGrab

    hwnd = game_window()
    if not hwnd:
        return False
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    o = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(o))
    ImageGrab.grab(bbox=(o.x, o.y, o.x + r.right, o.y + r.bottom), all_screens=True).save(path)
    return True


def awareness():
    hwnd = game_window()
    if not hwnd:
        return "?"
    ctx = user32.GetWindowDpiAwarenessContext(hwnd)
    return {0: "unaware", 1: "system aware", 2: "per-monitor aware"}.get(user32.GetAwarenessFromDpiAwarenessContext(ctx), "?")


def check(game, name):
    path = os.path.join(OUT, name + ".png")
    if not capture(path):
        print("%-34s no window" % name, flush=True)
        return
    (cw, ch), box = picture_box(path)
    lay = last_layout()
    if box:
        bw, bh = box[2] - box[0], box[3] - box[1]
        seen = "picture %4dx%-4d shape %.3f, bars top %d bottom %d left %d right %d" % (bw, bh, bw / max(bh, 1), box[1], ch - box[3], box[0],
                                                                                         cw - box[2])
    else:
        seen = "all black"
    planned = "computed %dx%d at %d,%d" % lay[2:] if lay else ""
    print("%-34s client %4dx%-4d %s | %s" % (name, cw, ch, seen, planned), flush=True)


with WLAgent(LAUNCHER, GAME, log=LOG, extra_args=("--timeout", "400")) as game:
    print("ready", game.game, game.width, game.height, "back buffer shape %.3f" % (game.width / game.height), flush=True)
    time.sleep(START)
    print("game window: %s of the screen's scale (%d dpi)" % (awareness(), user32.GetDpiForWindow(game_window() or 0)), flush=True)
    for mode in ("WINDOW", "WINDOW_STRETCH", "FULLSCREEN", "FULLSCREEN_STRETCH"):
        for aspect in ("ORIGINAL", "4:3", "16:9", "4:3_LETTERBOX"):
            game.display(aspect, mode)
            time.sleep(1.5)
            check(game, "%s-%s" % (mode, aspect.replace(":", "x")))
    # A stretched window the user made smaller, wider or taller
    hwnd = None
    for aspect in ("ORIGINAL", "4:3", "4:3_LETTERBOX"):
        game.display(aspect, "WINDOW_STRETCH")
        time.sleep(1.5)
        hwnd = hwnd or game_window()
        if not hwnd:
            print("no window of the game found", flush=True)
            break
        for w, h in ((960, 600), (800, 800), (1400, 450)):
            user32.SetWindowPos(hwnd, None, 100, 100, w, h, 0x0014)  # SWP_NOZORDER | SWP_NOACTIVATE
            time.sleep(1.5)
            check(game, "STRETCH-%s-%dx%d" % (aspect.replace(":", "x"), w, h))
    game.display("ORIGINAL", "WINDOW")
    time.sleep(1)
