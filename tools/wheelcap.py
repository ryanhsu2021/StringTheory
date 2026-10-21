# -*- coding: utf-8 -*-
"""Raise the window, start the transport, grab playing frames, then prove that
the mouse wheel trims a parameter strip.

usage: wheelcap.py out_prefix
"""
import sys, time, ctypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

user32 = ctypes.WinDLL('user32', use_last_error=True)
prefix = sys.argv[1] if len(sys.argv) > 1 else r'..\shots\w'

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
W, H, _ = wincap.capture(hwnd)
SX, SY = W / 800.0, H / 1248.0


class PT(ctypes.Structure):
    _fields_ = [('x', ctypes.c_long), ('y', ctypes.c_long)]


def to_screen(lx, ly):
    p = PT(int(lx * SX), int(ly * SY))
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    return p


def click(lx, ly):
    p = to_screen(lx, ly)
    user32.SetCursorPos(p.x, p.y)
    time.sleep(0.10)
    user32.mouse_event(2, 0, 0, 0, 0)
    time.sleep(0.05)
    user32.mouse_event(4, 0, 0, 0, 0)
    time.sleep(0.20)


def wheel(lx, ly, notches):
    """MOUSEEVENTF_WHEEL at logical position; 120 units = one notch"""
    p = to_screen(lx, ly)
    user32.SetCursorPos(p.x, p.y)
    time.sleep(0.12)
    for _ in range(abs(notches)):
        user32.mouse_event(0x0800, 0, 0, 120 if notches > 0 else -120, 0)
        time.sleep(0.12)


def save(tag):
    w, h, bg = wincap.capture(hwnd)
    out = '%s_%s.png' % (prefix, tag)
    wincap.write_png(out, w, h, bg)
    print('saved', out, '%dx%d' % (w, h))


user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
user32.SetForegroundWindow(hwnd)
time.sleep(0.40)

click(600, 430)                      # stage -> keyboard focus
user32.keybd_event(0x20, 0, 0, 0)
time.sleep(0.06)
user32.keybd_event(0x20, 0, 2, 0)
time.sleep(0.60)

# stage should now be animating; if not, hit PLAY
a = wincap.capture(hwnd)[2]
time.sleep(0.40)
if a == wincap.capture(hwnd)[2]:
    click(135, 855)                  # PLAY
    time.sleep(0.60)

save('a')
time.sleep(1.3)
save('b')
time.sleep(1.6)
save('c')

# --- wheel over TENSION: 12 notches down should reach ~0 % ---------------
wheel(700, 855, -14)
time.sleep(0.9)
save('slack')

# --- wheel over the RATE strip of the first voice card ------------------
wheel(90, 1038, 6)
time.sleep(0.9)
save('rate')
print('done')
