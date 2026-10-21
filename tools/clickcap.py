# -*- coding: utf-8 -*-
"""点击窗口内某点并截图（复用 DotSequencer/tools/wincap.py 的抓屏函数）"""
import sys, time, ctypes
from ctypes import wintypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

user32 = ctypes.WinDLL('user32', use_last_error=True)
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ClientToScreen.restype = wintypes.BOOL

keyword = sys.argv[1] if len(sys.argv) > 1 else 'String Theory'
out = sys.argv[2] if len(sys.argv) > 2 else 'shot.png'
cx = int(sys.argv[3]) if len(sys.argv) > 3 else -1
cy = int(sys.argv[4]) if len(sys.argv) > 4 else -1
wait = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0

wins = wincap.find_windows(keyword)
if not wins:
    print('WINDOW_NOT_FOUND')
    sys.exit(1)

hwnd, title, cls, (rx, ry, rw, rh) = wins[0]
wincap.focus(hwnd)
time.sleep(0.4)
if cx >= 0 and cy >= 0:
    pt = wintypes.POINT(int(cx), int(cy))
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    wincap.click(pt.x, pt.y, 1)
    time.sleep(wait)

w, h, bgra = wincap.capture(hwnd)
wincap.write_png(out, w, h, bgra)
print('saved %s %dx%d' % (out, w, h))
