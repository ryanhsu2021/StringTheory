# -*- coding: utf-8 -*-
"""Focus, start the transport, then sweep TENSION with the wheel and grab one
frame at 0 / 50 / 100 so the swings can be compared side by side.

Wheel is injected with SendMessage(WM_MOUSEWHEEL) because the OS-level
mouse_event wheel goes to the foreground input queue, which a background
script does not own.
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
S = W / 800.0                      # the capture bitmap is the client area at 1.5x


class PT(ctypes.Structure):
    _fields_ = [('x', ctypes.c_long), ('y', ctypes.c_long)]


def screen(lx, ly):
    p = PT(int(lx * S), int(ly * S))
    u.ClientToScreen(hwnd, ctypes.byref(p))
    return p


def click(lx, ly):
    p = screen(lx, ly)
    u.SetCursorPos(p.x, p.y)
    time.sleep(0.10)
    u.mouse_event(2, 0, 0, 0, 0)
    time.sleep(0.05)
    u.mouse_event(4, 0, 0, 0, 0)
    time.sleep(0.20)


def wheel(lx, ly, notches):
    p = screen(lx, ly)
    u.SetCursorPos(p.x, p.y)
    time.sleep(0.15)
    delta = 120 if notches > 0 else -120
    wp = (delta & 0xFFFF) << 16          # signed 16-bit high word
    for _ in range(abs(notches)):
        u.SendMessageW(hwnd, WM_MOUSEWHEEL, ctypes.c_void_p(wp),
                       ctypes.c_void_p(((p.y & 0xFFFF) << 16) | (p.x & 0xFFFF)))
        time.sleep(0.07)
    time.sleep(0.25)


def save(tag):
    w, h, bg = wincap.capture(hwnd)
    out = r'..\shots\tn_%s.png' % tag
    wincap.write_png(out, w, h, bg)
    print('saved', out)


u.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
u.SetForegroundWindow(hwnd)
time.sleep(0.40)

click(600, 430)                    # stage -> keyboard focus
u.keybd_event(0x20, 0, 0, 0)
time.sleep(0.06)
u.keybd_event(0x20, 0, 2, 0)
time.sleep(0.70)

wheel(700, 855, 30)                # hard right = 100 %
time.sleep(2.2)
save('100')
time.sleep(1.1)
save('100b')

wheel(700, 855, -15)               # back to ~50 %
time.sleep(2.0)
save('50')

wheel(700, 855, -30)               # hard left = 0 %
time.sleep(2.2)
save('0')
time.sleep(1.2)
save('0b')

wheel(90, 1038, 4)                 # RATE strip on card V1: 1/8 -> 1/16
time.sleep(1.0)
save('rate')
wheel(700, 855, 15)                # restore ~50 %
print('done')
