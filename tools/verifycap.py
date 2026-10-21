# -*- coding: utf-8 -*-
"""Deterministic verification pass.

The PLAY button is clicked directly (no keyboard focus needed), TENSION is set
with the wheel (SendMessage so the wheel lands on the right window) and reset
with a double-click, then one playing frame is saved per setting.
"""
import sys, time, ctypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

u = ctypes.WinDLL('user32', use_last_error=True)
WM_MOUSEWHEEL = 0x020A

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
S = W / 800.0


class PT(ctypes.Structure):
    _fields_ = [('x', ctypes.c_long), ('y', ctypes.c_long)]


def screen(lx, ly):
    p = PT(int(lx * S), int(ly * S))
    u.ClientToScreen(hwnd, ctypes.byref(p))
    return p


def click(lx, ly, times=1):
    p = screen(lx, ly)
    u.SetCursorPos(p.x, p.y)
    time.sleep(0.10)
    for _ in range(times):
        u.mouse_event(2, 0, 0, 0, 0)
        time.sleep(0.04)
        u.mouse_event(4, 0, 0, 0, 0)
        time.sleep(0.06)
    time.sleep(0.25)


def wheel(lx, ly, notches):
    p = screen(lx, ly)
    u.SetCursorPos(p.x, p.y)
    time.sleep(0.15)
    delta = 120 if notches > 0 else -120
    wp = (delta & 0xFFFF) << 16
    for _ in range(abs(notches)):
        u.SendMessageW(hwnd, WM_MOUSEWHEEL, ctypes.c_void_p(wp),
                       ctypes.c_void_p(((p.y & 0xFFFF) << 16) | (p.x & 0xFFFF)))
        time.sleep(0.06)
    time.sleep(0.30)


def moving():
    a = wincap.capture(hwnd)[2]
    time.sleep(0.35)
    return a != wincap.capture(hwnd)[2]


def save(tag):
    w, h, bg = wincap.capture(hwnd)
    out = r'..\shots\v2_%s.png' % tag
    wincap.write_png(out, w, h, bg)
    print('saved', out)


u.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
u.SetForegroundWindow(hwnd)
time.sleep(0.40)

for _ in range(3):
    if moving():
        break
    click(131, 855)                      # PLAY
    time.sleep(0.60)
print('playing:', moving())

# --- 100 %: hard right -------------------------------------------------------
wheel(700, 855, 30)
time.sleep(1.8)
save('100')
time.sleep(0.9)
save('100b')

# --- 50 %: double-click the strip (centre detent) ---------------------------
click(700, 855, 2)
time.sleep(1.9)
save('50')
time.sleep(0.9)
save('50b')

# --- 0 %: hard left ---------------------------------------------------------
wheel(700, 855, -30)
time.sleep(1.9)
save('0')
time.sleep(0.9)
save('0b')

# --- wheel on the RATE strip of card V1: 1/8 -> 1/16 -----------------------
wheel(90, 1038, 4)
time.sleep(0.8)
save('rate')

click(700, 855, 2)                       # back to the 50 % default
print('done')
