# -*- coding: utf-8 -*-
"""Capture one playing frame at TENSION 0 / 50 / 100.

The client area sits ~25 logical px below the window origin (the editor draws
its own title strip on top), so screen mapping is x*1.5 and y*1.5 + 37.
"""
import sys, time, ctypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

u = ctypes.WinDLL('user32', use_last_error=True)
WM_MOUSEWHEEL = 0x020A
DY = 37

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
    p = PT(int(lx * S), int(ly * S) + DY)
    u.ClientToScreen(hwnd, ctypes.byref(p))
    return p


def click(lx, ly, times=1):
    p = screen(lx, ly)
    u.SetCursorPos(p.x, p.y)
    time.sleep(0.10)
    for _ in range(times):
        u.mouse_event(2, 0, 0, 0, 0)
        time.sleep(0.05)
        u.mouse_event(4, 0, 0, 0, 0)
        time.sleep(0.08)
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
        time.sleep(0.14)
    time.sleep(0.30)


def moving():
    a = wincap.capture(hwnd)[2]
    time.sleep(0.35)
    return a != wincap.capture(hwnd)[2]


def save(tag):
    w, h, bg = wincap.capture(hwnd)
    out = r'..\shots\f_%s.png' % tag
    wincap.write_png(out, w, h, bg)
    print('saved', out)


u.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
u.SetForegroundWindow(hwnd)
time.sleep(0.40)

for _ in range(3):
    if moving():
        break
    click(131, 855)                     # PLAY
    time.sleep(0.70)
print('playing:', moving())

click(760, 855)                         # TENSION hard right
time.sleep(1.9)
save('100')
time.sleep(1.0)
save('100b')

click(700, 855, 2)                      # double-click = centre detent
time.sleep(2.0)
save('50')
time.sleep(1.0)
save('50b')

click(634, 855)                         # hard left
time.sleep(2.0)
save('0')
time.sleep(1.2)
save('0b')

wheel(700, 855, 6)                      # wheel: +6 notches = +12 %
time.sleep(0.6)
save('wheel')
print('done')
