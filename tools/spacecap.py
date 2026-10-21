# -*- coding: utf-8 -*-
"""空格触发播放并截图（这台机器窗口高于屏幕，绝对坐标点击会被系统夹边，改用键盘）"""
import sys, time, ctypes
from ctypes import wintypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

user32 = ctypes.WinDLL('user32', use_last_error=True)

keyword = sys.argv[1] if len(sys.argv) > 1 else 'String Theory'
out = sys.argv[2] if len(sys.argv) > 2 else 'shot.png'
wait = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0

wins = wincap.find_windows(keyword)
if not wins:
    print('WINDOW_NOT_FOUND')
    sys.exit(1)

hwnd, title, cls, rect = wins[0]
wincap.focus(hwnd)
time.sleep(0.3)

user32.keybd_event(0x20, 0, 0, 0)   # space down
time.sleep(0.05)
user32.keybd_event(0x20, 0, 2, 0)   # space up
time.sleep(wait)

w, h, bgra = wincap.capture(hwnd)
wincap.write_png(out, w, h, bgra)
print('saved %s %dx%d' % (out, w, h))
