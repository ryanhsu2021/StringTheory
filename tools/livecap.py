# -*- coding: utf-8 -*-
"""Raise the window, make sure it is actually RUNNING, then grab a few frames.

usage: livecap.py out_prefix [d1 d2 d3 ...]

The window is raised to topmost first (it can sit behind Chrome), the stage is
clicked so the editor owns the keyboard, then space toggles transport. If two
captures 400 ms apart are byte-identical the model is not moving, so we click
the PLAY button and try again -- up to three times.
"""
import sys, time, ctypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

user32 = ctypes.WinDLL('user32', use_last_error=True)

prefix = sys.argv[1] if len(sys.argv) > 1 else r'..\shots\live'
delays = [float(x) for x in sys.argv[2:]] or [1.2, 1.0, 1.4]

wins = []
for _ in range(40):
    wins = wincap.find_windows('String Theory')
    if wins:
        break
    time.sleep(0.3)
if not wins:
    print('WINDOW_NOT_FOUND')
    sys.exit(1)

hwnd = wins[0][0]


class PT(ctypes.Structure):
    _fields_ = [('x', ctypes.c_long), ('y', ctypes.c_long)]


def click(ix, iy):
    """click at CAPTURE pixel coordinates (the bitmap is the client area 1:1)"""
    p = PT(int(ix), int(iy))
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    user32.SetCursorPos(p.x, p.y)
    time.sleep(0.10)
    user32.mouse_event(2, 0, 0, 0, 0)
    time.sleep(0.05)
    user32.mouse_event(4, 0, 0, 0, 0)
    time.sleep(0.25)


def frame():
    return wincap.capture(hwnd)[2]


def is_moving():
    a = frame()
    time.sleep(0.40)
    return frame() != a


user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
user32.SetForegroundWindow(hwnd)
time.sleep(0.40)

click(600, 430)                       # stage: gives the editor keyboard focus
user32.keybd_event(0x20, 0, 0, 0)
time.sleep(0.06)
user32.keybd_event(0x20, 0, 2, 0)
time.sleep(0.50)

tries = 0
while not is_moving() and tries < 3:
    tries += 1
    click(130, 838)                   # PLAY button
    time.sleep(0.60)

if not is_moving():
    print('NOT_RUNNING')
    sys.exit(2)

print('running (retries=%d)' % tries)

for i, d in enumerate(delays):
    time.sleep(d)
    w, h, bgra = wincap.capture(hwnd)
    out = '%s%d.png' % (prefix, i + 1)
    wincap.write_png(out, w, h, bgra)
    print('saved', out, '%dx%d' % (w, h))
